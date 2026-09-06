/**
 * Copyright (c) 2025 Alex Potsides
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "iceudpmuxlistener.hpp"

#include "impl/iceudpmuxlistener.hpp"

namespace rtc {

IceUdpMuxListener::IceUdpMuxListener(uint16_t port, optional<string> bindAddress)
    : CheshireCat<impl::IceUdpMuxListener>(port, std::move(bindAddress)) {}

IceUdpMuxListener::IceUdpMuxListener(IceUdpMuxListenerConfiguration config,
                                     std::function<void(IceUdpMuxRequest)> callback)
    : CheshireCat<impl::IceUdpMuxListener>(std::move(config), std::move(callback)) {}

IceUdpMuxListener::~IceUdpMuxListener() {}

void IceUdpMuxListener::stop() { impl()->stop(); }

uint16_t IceUdpMuxListener::port() const { return impl()->port; }

void IceUdpMuxListener::OnUnhandledStunRequest(std::function<void(IceUdpMuxRequest)> callback) {
	impl()->unhandledStunRequestCallback = callback;
}

void IceUdpMuxListener::prepare(uint64_t requestId, Configuration config,
                                Description remoteDescription, LocalDescriptionInit localInit,
                                shared_ptr<PeerConnection> &peer) {
	impl()->prepare(requestId, std::move(config), std::move(remoteDescription),
	                std::move(localInit), peer);
}

void IceUdpMuxListener::accept(uint64_t requestId, shared_ptr<PeerConnection> peer) {
	impl()->accept(requestId, std::move(peer));
}

void IceUdpMuxListener::reject(uint64_t requestId) { impl()->reject(requestId); }

IceUdpMuxListenerStats IceUdpMuxListener::stats() const { return impl()->stats(); }

} // namespace rtc
