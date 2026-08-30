## Architecture Summary

```
/protocol/   libprotocol.a — wire format, framing, byte-order (single source of truth)
/server/     authoritative simulation: epoll/select loop, ghost AI, pellet state, scores
/client/     SDL2 rendering, interpolation between snapshots, TCP/UDP networking
/tests/      headless load tester, network conditioning script
```
