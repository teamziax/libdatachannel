/**
 * Copyright (c) 2025 Alex Potsides
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "iceudpmuxlistener.hpp"
#include "internals.hpp"
#include "peerconnection.hpp"

namespace rtc::impl {

#if !USE_NICE
namespace {
thread_local const IceUdpMuxListener *CallbackListener = nullptr;
IceUdpMuxRequest copyRequest(const juice_mux_binding_request_t &info, uint64_t id = 0) {
	return {info.local_ufrag, info.remote_ufrag, info.address, info.port, id};
}
}

void IceUdpMuxListener::UnhandledStunRequestCallback(const juice_mux_binding_request *info,
                                                     void *user_ptr) {
	auto listener = static_cast<IceUdpMuxListener *>(user_ptr);
	if (listener) {
		auto previous = std::exchange(CallbackListener, listener);
		scope_guard reset([previous] { CallbackListener = previous; });
		listener->unhandledStunRequestCallback(copyRequest(*info));
	}
}

void IceUdpMuxListener::PendingRequestCallback(const juice_mux_pending_request_t *info,
                                               void *user_ptr) {
	auto listener = static_cast<IceUdpMuxListener *>(user_ptr);
	if (!listener || listener->mStopped)
		return;
	auto previous = std::exchange(CallbackListener, listener);
	scope_guard reset([previous] { CallbackListener = previous; });

	try {
		auto metadata = copyRequest(info->binding, info->request_id);
		{
			std::lock_guard lock(listener->mRequestsMutex);
			listener->removeExpiredRequests();
			if (listener->mStopped || listener->mRequests.size() >= listener->mMaxPendingRequests) {
				juice_mux_reject_request(listener->bindAddress ? listener->bindAddress->c_str() : nullptr,
				                         listener->port, metadata.id);
				return;
			}
			listener->mRequests.emplace(metadata.id, Request{
			    metadata, std::chrono::steady_clock::now() +
			                  std::chrono::milliseconds(listener->mRequestTimeoutMs), {}, false});
		}
		if (!listener->unhandledStunRequestCallback(metadata))
			listener->reject(metadata.id);
	} catch (const std::exception &e) {
		PLOG_WARNING << "Incoming ICE request callback failed: " << e.what();
		try { listener->reject(info->request_id); } catch (...) {}
	} catch (...) {
		try { listener->reject(info->request_id); } catch (...) {}
	}
}
#endif

IceUdpMuxListener::IceUdpMuxListener(uint16_t port, optional<string> bindAddress)
    : port(port), bindAddress(std::move(bindAddress)) {
#if !USE_NICE
	if (juice_mux_listen(this->bindAddress ? this->bindAddress->c_str() : nullptr, port,
	                     UnhandledStunRequestCallback, this) < 0)
		throw std::runtime_error("Failed to register ICE UDP mux listener");
#else
	PLOG_WARNING << "ICE UDP mux is not available with libnice";
#endif
}

IceUdpMuxListener::IceUdpMuxListener(IceUdpMuxListenerConfiguration config,
                                     std::function<void(IceUdpMuxRequest)> callback)
    : port(config.port), bindAddress(std::move(config.bindAddress)),
      unhandledStunRequestCallback(std::move(callback)), mPendingMode(true),
      mMaxPendingRequests(config.maxPendingRequests ? config.maxPendingRequests : 256),
      mRequestTimeoutMs(config.requestTimeoutMs ? config.requestTimeoutMs : 5000) {
	if (!port || mMaxPendingRequests > 4096 || mRequestTimeoutMs > 30000)
		throw std::invalid_argument("Invalid ICE UDP mux listener limits");
#if !USE_NICE
	juice_mux_pending_config_t nativeConfig{mMaxPendingRequests, mRequestTimeoutMs};
	if (juice_mux_listen_pending(bindAddress ? bindAddress->c_str() : nullptr, port,
	                             &nativeConfig, PendingRequestCallback, this) < 0)
		throw std::runtime_error("Failed to register pending ICE UDP mux listener");
#else
	throw std::runtime_error("ICE UDP mux requires libjuice");
#endif
}

IceUdpMuxListener::~IceUdpMuxListener() {
	try { stop(); } catch (const std::exception &e) { PLOG_ERROR << e.what(); }
}

void IceUdpMuxListener::stop() {
#if !USE_NICE
	if (CallbackListener == this)
		throw std::logic_error("Stop the ICE UDP mux listener from outside its callback");
#endif
	std::lock_guard stopLock(mStopMutex);
	if (mStopped.exchange(true))
		return;
#if !USE_NICE
	const char *address = bindAddress ? bindAddress->c_str() : nullptr;
	int result = mPendingMode ? juice_mux_listen_pending(address, port, nullptr, nullptr, nullptr)
	                          : juice_mux_listen(address, port, nullptr, nullptr);
	if (result < 0) {
		mStopped = false;
		throw std::runtime_error("Failed to unregister ICE UDP mux listener");
	}
#endif
	std::lock_guard lock(mRequestsMutex);
	for (auto &[id, request] : mRequests)
		if (auto peer = request.peer.lock())
			peer->close();
	mRequests.clear();
}

void IceUdpMuxListener::removeExpiredRequests() {
	auto now = std::chrono::steady_clock::now();
	for (auto it = mRequests.begin(); it != mRequests.end();) {
		if (it->second.expiresAt <= now) {
			if (auto peer = it->second.peer.lock())
				peer->close();
			it = mRequests.erase(it);
		} else {
			++it;
		}
	}
}

void IceUdpMuxListener::prepare(uint64_t requestId, Configuration config,
                                Description remoteDescription, LocalDescriptionInit localInit,
                                shared_ptr<rtc::PeerConnection> &peer) {
	if (peer)
		throw std::invalid_argument("Peer output must be empty");
#if !USE_NICE
	try {
		{
			std::lock_guard lock(mRequestsMutex);
			removeExpiredRequests();
			auto it = mRequests.find(requestId);
			if (!mPendingMode || mStopped || it == mRequests.end() || it->second.prepared)
				throw std::invalid_argument("Incoming ICE request is no longer available");
			const auto &metadata = it->second.metadata;
			if (!localInit.iceUfrag || *localInit.iceUfrag != metadata.localUfrag ||
			    !localInit.icePwd || localInit.icePwd->size() < 22 || localInit.icePwd->size() > 256 ||
			    remoteDescription.iceUfrag() != metadata.remoteUfrag || !remoteDescription.icePwd() ||
			    remoteDescription.icePwd()->size() < 22 || remoteDescription.icePwd()->size() > 256 ||
			    !remoteDescription.fingerprint() || !remoteDescription.fingerprint()->isValid() ||
			    remoteDescription.type() != Description::Type::Offer ||
			    config.disableFingerprintVerification)
				throw std::invalid_argument("Incoming ICE credentials or remote fingerprint are invalid");
			if (juice_mux_verify_request(bindAddress ? bindAddress->c_str() : nullptr, port,
			                             requestId, localInit.icePwd->c_str()) < 0)
				throw std::invalid_argument("Incoming STUN authentication failed");
			it->second.prepared = true;
			config.enableIceUdpMux = true;
			config.bindAddress = bindAddress;
			config.portRangeBegin = port;
			config.portRangeEnd = port;
			config.disableAutoNegotiation = true;
			config.disableAutoGathering = true;
			peer = std::make_shared<rtc::PeerConnection>(std::move(config));
			it->second.peer = peer;
		}
		// Preserve ownership in the output even when either operation throws.
		peer->setRemoteDescription(std::move(remoteDescription));
		peer->setLocalDescription(Description::Type::Answer, std::move(localInit));
	} catch (...) {
		try { reject(requestId); } catch (...) {}
		throw;
	}
#else
	(void)requestId; (void)config; (void)remoteDescription; (void)localInit;
	throw std::runtime_error("ICE UDP mux requires libjuice");
#endif
}

void IceUdpMuxListener::accept(uint64_t requestId, shared_ptr<rtc::PeerConnection> peer) {
	std::lock_guard lock(mRequestsMutex);
	removeExpiredRequests();
	auto it = mRequests.find(requestId);
	if (mStopped || it == mRequests.end() || !peer || it->second.peer.lock() != peer ||
	    peer->state() == rtc::PeerConnection::State::Closed)
		throw std::invalid_argument("Incoming ICE request is no longer available");
	peer->gatherLocalCandidates();
	auto transport = peer->impl()->getIceTransport();
	if (!transport)
		throw std::runtime_error("Incoming peer has no ICE transport");
	transport->acceptUdpMuxRequest(bindAddress, port, requestId);
	mRequests.erase(it);
}

void IceUdpMuxListener::reject(uint64_t requestId) {
	std::lock_guard lock(mRequestsMutex);
	auto it = mRequests.find(requestId);
	if (it != mRequests.end()) {
		if (auto peer = it->second.peer.lock())
			peer->close();
		mRequests.erase(it);
	}
#if !USE_NICE
	if (mPendingMode && !mStopped)
		juice_mux_reject_request(bindAddress ? bindAddress->c_str() : nullptr, port, requestId);
#else
	(void)requestId;
#endif
}

IceUdpMuxListenerStats IceUdpMuxListener::stats() const {
#if !USE_NICE
	juice_mux_stats_t result{};
	if (mStopped || juice_mux_get_stats(bindAddress ? bindAddress->c_str() : nullptr,
	                                    port, &result) < 0)
		throw std::runtime_error("ICE UDP mux listener is stopped");
	return {result.received, result.rejected, result.notifications, result.duplicates,
	        static_cast<unsigned int>(result.agents), static_cast<unsigned int>(result.mapped_tuples),
	        static_cast<unsigned int>(result.pending)};
#else
	throw std::runtime_error("ICE UDP mux requires libjuice");
#endif
}

} // namespace rtc::impl
