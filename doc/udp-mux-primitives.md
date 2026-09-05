# Shared UDP endpoint primitives

The retained listener bind address makes shutdown remove the same endpoint that
was opened. Applications binding multiple local interfaces on the same UDP port
must not stop a different interface's listener.

`rtcGetPeerConnectionCreationAttempts()` reports calls reaching the C API's native
peer constructor, including constructor failures. This monotonic diagnostic helps
applications verify their admission filters and resource accounting: rejected
input should not allocate a peer. It does not authenticate traffic or count live
peers. Invalid configuration rejected before construction is not counted.

Persistent DTLS identity and explicit local ICE credentials use upstream APIs:
`rtcConfiguration.certificatePemFile`, `keyPemFile`, `keyPemPass`, and
`rtcSetLocalDescriptionEx` with `rtcLocalDescriptionInit`. No duplicate creation
or SDP configuration entrypoints are added. Applications can advertise a stable
endpoint certificate fingerprint before allocating individual peers; private
keys remain under application ownership and are never sent by these APIs.
