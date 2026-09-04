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
