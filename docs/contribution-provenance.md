# Contribution provenance

This maintained fork compares `nxs-dev` with the upstream mirror at `51085b8de4e6185dc019e3705c88b87933d7c3f6`.
The aggregate owned draft records the combined work; future external submissions
should separate independent fixes, acceptance APIs and lifecycle changes.

| Source | Contribution |
| --- | --- |
| Upstream [`51085b8d`](https://github.com/paullouisageneau/libdatachannel/commit/51085b8de4e6185dc019e3705c88b87933d7c3f6) | Baseline, including the accepted certificate and ICE configuration APIs. |
| Zulu `5bd65429` | Bind-address shutdown fix and construction diagnostics. Duplicate identity and ICE APIs were removed. |
| Zulu `a2cb59b3`, `ce30eeb0` | Cleanup completion and its regression tests. |
| Zulu `7ebeb805`, `a479c36a` | Earlier dependency updates, replaced by coordinated maintained-fork pins. |
| Consolidation and current changes | C/C++ asynchronous acceptance, explicit failure ownership, safe listener deletion, documentation and CI. |

## Review follow-up, 8 September 2026

Close peers outside listener locks, construct peers outside the receive callback mutex, authenticate additional source tuples for existing peers, expose asynchronous transport destruction and a scoped prepared-peer owner, and make constructor counters test-only.
