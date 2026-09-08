# Transport cleanup completion

Servers that reserve connection capacity need to know when native resources are
released. `PeerConnection::closeAsync()` returns a shared future that becomes ready
after the final ICE, DTLS and SCTP transport destruction, including references
retained outside the cleanup queue. It schedules closure without blocking the caller
or occupying a worker just to wait. It can be called from peer callbacks.

The C API is `rtcClosePeerConnectionAsync(pc, callback, ptr)`. A successful call
invokes the callback once on a native worker, outside transport locks. Keep the
callback context and caller-owned handle until notification; the callback may delete
the peer. The C++ callback overload provides the same notification semantics.

`rtcClosePeerConnectionAndWait(pc, timeoutMs)` and C++ `closeAndWait(timeout)` are
blocking conveniences over asynchronous closure. Call them from external owner
threads. The C timeout is 1..30000 ms, includes waiting for queued closure to begin,
and returns `RTC_ERR_NOT_AVAIL` if destruction is not yet complete. Timeout leaves
ownership with the caller; success still requires deleting a C handle. Existing
asynchronous close/delete operations remain available.

The focused regression stalls the teardown worker, checks timeout without releasing
capacity, holds an external transport reference past cleanup-queue completion, and
requires destruction before success. It also verifies that an asynchronous callback
can delete the peer without deadlocking. Build with `TRANSPORT_TEARDOWN_TESTS=ON`
and run `ctest -R transport-teardown`.
