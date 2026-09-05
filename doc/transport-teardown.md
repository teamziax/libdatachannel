# Bounded transport teardown

Servers that reserve a UDP endpoint or a bounded number of ICE agents need to know
when those resources are released before reusing capacity. Ordinary close/delete
remains asynchronous. `rtcClosePeerConnectionAndWait(pc, timeoutMs)` and C++
`PeerConnection::closeAndWait(timeout)` force closure and wait for final destruction
of the peer's ICE, DTLS and SCTP transports, including references retained outside
the teardown queue. Success is not merely removal of the public handle.

Call only on an external owner thread, never a native callback or teardown thread.
The C API accepts 1..30000 ms. Timeout returns `RTC_ERR_NOT_AVAIL` and leaves the
handle with its owner to retry or delete; success still requires handle deletion.
Existing asynchronous close/delete behavior is unchanged.

The deterministic regression stalls the teardown worker, proves the legacy API
returns with an ICE agent alive, tests bounded timeout, and then verifies zero
agents after completion. It separately holds a transport reference beyond queued
teardown and requires a timeout until that final reference is released. Build with
`TRANSPORT_TEARDOWN_TESTS=ON`, then run `ctest -R transport-teardown` in the build
directory. The resource assertions depend on the libjuice mux statistics extension.
