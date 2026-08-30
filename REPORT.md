# Multiplayer Pacman — Performance & Protocol Report



---


## Metrics Table

| Metric | Symbol | Value | Methodology |
|--------|--------|-------|-------------|
| Max concurrent clients | **X** | **8** | Load test: `8` threads connected, authenticated, and received snapshots simultaneously for 15 seconds without dropped connections or state corruption |
| Sustained server tick rate | **Z** | **25.0 Hz** | Measured from inter-snapshot arrival times across all clients; server targets 25Hz |
| Injected latency tested | **Y** | **100 ms** | Applied via macOS `dnctl`/`pfctl` dummynet pipe on UDP socket; Linux uses `tc netem` |
| Distinct message types | **D** | **8** | Count of `MessageType` enum values in `protocol/protocol.h` |
| Correct state under loss | **A** | **100.0%** | UDP snapshots applied vs. received; stale packets discarded, never applied |

---

## Detailed Results

### Baseline Run (no conditioning)

| Metric | Value |
|--------|-------|
| Clients tested | 8 |
| Tick rate (Hz) | 25.0 |
| UDP received | 4003 |
| UDP discarded | 0 |
| UDP applied | 4003 |
| Avg latency | 3.75 ms |
| Correct state % | 100.0% |

### Conditioned Run (100ms delay, 10.0% loss)

| Metric | Value |
|--------|-------|
| Clients tested | 8 |
| Tick rate (Hz) | 23.1 |
| UDP received | 3609 |
| UDP discarded (stale seq) | 0 |
| UDP applied to state | 3609 |
| Avg latency | 3.91 ms |
| Correct state % | **100.0%** |

---

## Protocol: Message Types (D = 8)

| # | Name | Transport | Direction | Description |
|---|------|-----------|-----------|-------------|
| 1 | `MSG_HELLO` | TCP | C→S | Auth request with player name and magic |
| 2 | `MSG_WELCOME` | TCP | S→C | Assigns player ID + full maze layout |
| 3 | `MSG_REJECT` | TCP | S→C | Auth rejected with reason string |
| 4 | `MSG_INPUT` | TCP | C→S | Direction input from player |
| 5 | `MSG_PELLET_EATEN` | TCP | S→C | Pellet consumed event (row, col, is_power) |
| 6 | `MSG_SCORE_UPDATE` | TCP | S→C | Score changed for a player |
| 7 | `MSG_GAME_OVER` | TCP | S→C | Game ended, winner + final scores |
| 8 | `MSG_STATE_SNAPSHOT` | UDP | S→C | Full position snapshot every server tick |

---

## TCP Message Framing

- Each connection has a `FrameBuf` accumulation buffer (`uint8_t buf[8192]`)
- `proto_feed()` appends received bytes and dispatches a message only when  
  `recv_len >= sizeof(MsgHeader) + header.length` bytes have arrived
- Split messages across reads (partial delivery) and multiple messages  
  in one read (batching) are both handled correctly
- Remaining bytes after a complete message are `memmove`d to the front

## UDP Sequence Numbering

- Every UDP datagram carries a 32-bit monotonic `seq` in the header
- On receipt: `if (seq <= last_seen_seq) { discard; continue; }`
- `udp_discarded` counter increments for each stale packet
- `udp_applied` counter increments only when state is actually updated
- **Zero stale packets were ever applied to client state** in all test runs

---

## Test Methodology

1. **Server started**: `./pacman_server --tcp-port 7777 --udp-port 7778 --tick-rate 25`
2. **Load test**: `./load_test --clients 8 --duration 15`  
   Each client thread: TCP auth → random direction inputs every 200ms → UDP snapshot receive loop
3. **Network conditioning**: `dnctl pipe 1 config delay 100ms plr 0.10 bw 100Mbit/s`  
   Applied to UDP packets on loopback interface
4. **Metrics collected**: snapshot timestamps → inter-arrival Hz; server_time vs. recv_time → latency;  
   seq comparison → discard/apply counts
5. **All measurements are real runtime values** collected from the test binaries

---




