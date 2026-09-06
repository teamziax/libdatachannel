# Accept incoming ICE connections

`IceUdpMuxListener` can retain a new STUN Binding request while an application
checks whether to accept the connection. This extends the existing listener used
by WebRTC Direct. It is useful when the application can reconstruct the peer's
connection settings from the request, instead of creating a peer through ordinary
signalling first.

The listener reports the request ID, local and remote ICE username fragments, and
source address. These values are untrusted. The application decides what the
username fragments mean; the transport has no token format or key management.
The original packet stays in libjuice.

## C API

Create the listener with `rtcCreateIceUdpMuxListener`. Its callback runs outside
the UDP receive lock. Copy the metadata, queue application work and return. The
callback may reject a request, but must not delete its own listener.

Once the application accepts the credentials, call
`rtcPrepareIceUdpMuxPeer(listener, requestId, config, remoteSdp, localInit, &pc)`.
This checks that the supplied username fragments match the request, requires a
valid remote DTLS fingerprint and verifies STUN integrity before constructing a
peer. The peer uses the listener's address and port. Automatic negotiation and
candidate gathering are disabled during preparation.

Preparation and acceptance are separate so bindings can install their peer
wrapper and callbacks before any packet reaches the connection:

```c
int pc = -1;
int result = rtcPrepareIceUdpMuxPeer(listener, requestId, &config,
                                  remoteSdp, &localInit, &pc);
if (result == RTC_ERR_SUCCESS) {
    rtcSetUserPointer(pc, peerContext);
    rtcSetStateChangeCallback(pc, onStateChange);
    rtcSetDataChannelCallback(pc, onDataChannel);
    result = rtcAcceptIceUdpMuxPeer(listener, requestId, pc);
}
if (result != RTC_ERR_SUCCESS) {
    rtcRejectIceUdpMuxRequest(listener, requestId);
    if (pc >= 0) {
        // The caller owns this handle even when configuration failed.
        // Close it and wait for teardown before returning reserved capacity.
        schedulePeerCleanup(pc);
    }
}
```

`rtcAcceptIceUdpMuxPeer` starts gathering and attaches the prepared peer to the
retained request. Native code processes that first request immediately; progress
does not depend on the client retransmitting it. DTLS still checks the remote
certificate fingerprint. Disabling fingerprint verification is not supported by
this incoming-connection API.

A request can be prepared once. Duplicate pending STUN requests share its
notification. Rejection, expiry and listener deletion cancel pending requests;
late acceptance fails. If a peer was already returned, the caller still owns it
and must close and delete it. Listener deletion waits for its request callbacks
to return and closes prepared peers. Accepted peers remain owned by their callers.

The default limit is 256 pending requests with a 5-second timeout. Applications
can configure up to 4096 requests and 30 seconds. Unanswered requests expire even
when application validation never completes. Applications should also bound their
executor queue and connection count.

## C++ API

Construct `IceUdpMuxListener` with `IceUdpMuxListenerConfiguration` and a callback
to use retained requests. `prepare`, `accept` and `reject` follow the same contract
as the C API. `prepare` writes a `shared_ptr<PeerConnection>` output; if subsequent
configuration throws, that output remains owned by the caller.

The existing port/address constructor and `OnUnhandledStunRequest` keep their
notification-only behaviour. They do not retain the packet for later acceptance.

## Identity, diagnostics and tests

Local certificates use the existing `certificatePemFile`, `keyPemFile` and
`keyPemPass` configuration. Explicit local ICE credentials use
`rtcLocalDescriptionInit`. A server can advertise its certificate fingerprint
before allocating peers and reuse that identity for incoming connections.

`rtcGetIceUdpMuxListenerStats` reports received datagrams, rejected traffic,
pending requests, notifications, duplicates, registered agents and mapped source
addresses. Established transport packets do not call the admission callback.
`rtcGetPeerConnectionCreationAttempts()` is a monotonic native constructor counter
for C and C++ peers, including constructor failures. It helps tests check that
failed authentication allocates no peer; it does not count live peers.

Build with `ICE_UDP_MUX_TESTS=ON` and run `ctest -R ice-udp-mux-pending`. The focused
OpenSSL/POSIX test sends the first request once, delays acceptance, and requires a
response. It also checks duplicate handling, native processing after acceptance,
authentication before construction, and caller ownership after configuration
failure. Transport cleanup is covered separately by
`TRANSPORT_TEARDOWN_TESTS=ON` and `ctest -R transport-teardown`.
