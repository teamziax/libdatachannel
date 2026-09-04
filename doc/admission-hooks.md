# Narrow admission primitives

This branch exposes existing C++ identity and local ICE configuration through C:

- `rtcCreatePeerConnectionWithIdentity(config, certificateFile, keyFile, keyPass)`
  imports a paired PEM DTLS identity without changing `rtcConfiguration` ABI.
- `rtcSetLocalDescriptionWithIce(peer, type, ufrag, password)` sets the actual ICE
  agent's local credentials before gathering. It does not just rewrite SDP.
- `rtcGetPeerConnectionCreationAttempts()` is a monotonic diagnostic counter at
  the C API construction boundary, including failed construction attempts.

The pinned owned libjuice submodule adds the raw ingress gate and rejects
oversized credentials. `IceUdpMuxListener` retains its bind address when stopping
so independent endpoints on the same port are not confused. Existing C entrypoints
and conventional offer/answer remain available.

Validation is the companion Java JNI `nativeAdmissionProbe`: imported fingerprint
matches the provisioned certificate, custom ufrags of 167/178/256 characters are
preserved, and a reconstructed offer without candidates completes ICE/DTLS/SCTP
plus both data channels using the first admitted STUN's peer-reflexive tuple.
A token-bound incorrect remote fingerprint fails at DTLS before channels open.
Invalid pre-admission input leaves the native construction counter unchanged.

No Warden cryptography/policy is implemented in libdatachannel. The companion
probe's simulated encrypted-token signer is test code, not a stock Minecraft test.

## Exact teardown completion

CI run `teamziax/NetworkCompatible/actions/runs/33828480051` reproduced an ICE
agent remaining in the mux immediately after `rtcDeletePeerConnection` returned.
The original public delete API schedules `closeTransports` work on the separate
`TearDownProcessor`; deleting the C handle does not await that work. Counting
capacity as free at that point is not a valid native resource bound.

`test/admission/teardown.cpp` is a deterministic minimal reproduction. It stalls
the teardown worker, deletes an ICE peer, and observes one agent still present.
It then tests the narrow remedy: `rtcClosePeerConnectionAndWait(pc, timeoutMs)`
forces remote closure and waits on that peer's transport teardown completion.
It returns `RTC_ERR_NOT_AVAIL` on timeout, leaving ownership with the caller, and
succeeds only after the queued transport references have been released. The test
observes zero mux agents on success; it does not replace that assertion with a
sleep. Timeout is bounded to 1–30,000 ms. Only external owner threads may call it;
never a native callback or teardown thread. The old close/delete APIs retain
their asynchronous behavior for existing consumers.

Enable `WARDEN_ADMISSION_TESTS=ON`, build `admission-teardown-test`, then run
`ctest --test-dir <build>/libdatachannel --output-on-failure -R admission-teardown`
for the nested JNI build (use `<build>` for a standalone native build).
