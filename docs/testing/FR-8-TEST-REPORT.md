# FR-8 Test Report: TCP Client Disconnect and Timeout Detection

| Field | Value |
|---|---|
| Report ID | TR-FR8-002 |
| Requirement | FR-8: TCP client disconnect and timeout detection |
| Project | ESP32 TCP-Serial Bridge |
| Firmware under test | App version `63cbb71`, ELF SHA256 `8d79dbe83...`, built `Nov 4 2026 10:08:58`, ESP-IDF v6.1 |
| Source revision | Commit `63cbb71` (`feat(network): reject clients whose keepalive cannot be configured (FR-8)`). The boot log shows no `-dirty` suffix |
| Test date | 2026-11-04 (UTC-5) |
| Tested by | Project operator |
| Report prepared by | Claude Code, from the source code, host test run and retained ESP32 serial logs |
| Previous record | [HW-VAL-008](../test-records/hw-validation-fr8.md), on firmware `78290c9-dirty` |

## 1. Summary

The firmware enables TCP keepalive on every accepted client socket, with a 5 s idle time, a 2 s interval and 3 probes. It reads each value back after setting it, and rejects any client whose keepalive can't be confirmed. A client that stays silent is aborted 11.0 s to 11.5 s after the last segment received from it, well within the 30 s limit.

On hardware, the retained serial logs confirm:

- the keepalive configuration on every accepted client;
- detection of an abrupt RST (`errno 104`);
- a healthy idle client kept for 300 s;
- two half-open detections (`errno 113`), each followed by cleanup and a new accepted client;
- 200 consecutive connect and close cycles with stable heap;
- a graceful close with 95 bytes pending, all 95 counted as expected discards and none as loss.

The 30 s detection limit for half-open connections is **not yet shown by retained evidence**. The ESP32 log doesn't record when the iptables block was applied, so it can't measure the time from block to detection. For C1, the operator observed about 9 s, which is consistent with the design analysis in section 3.4. C1 and C2 are therefore reported as INCONCLUSIVE.

**Overall FR-8 status: PARTIAL.** See section 9 for what would close the open items.

## 2. Functional Requirement

FR-8 requires the bridge to:

| ID | Requirement |
|---|---|
| FR-8.1 | Detect TCP client disconnections, both graceful (FIN) and abrupt (RST) |
| FR-8.2 | Detect half-open TCP connections, where the client disappears without sending FIN or RST |
| FR-8.3 | Detect an unresponsive client within 30 seconds |
| FR-8.4 | Release the socket and bridge resources after a disconnection |
| FR-8.5 | Accept a new client connection after a disconnection |

The bridge accepts one client at a time (FR-2), so FR-8.4 and FR-8.5 are closely linked: a session that is never detected as dead blocks every later client.

The earlier record [HW-VAL-008](../test-records/hw-validation-fr8.md) covers the Ethernet link-loss scenarios (H6, H7, H8) on older firmware. This report covers the keepalive-based detection added in `63cbb71`.

## 3. Implementation

### 3.1 Source Files and Functions

| File | Function or symbol | Role in FR-8 |
|---|---|---|
| `main/network/client_socket.h` | `CLIENT_KEEPALIVE_IDLE_S`, `CLIENT_KEEPALIVE_INTERVAL_S`, `CLIENT_KEEPALIVE_PROBES` | Keepalive parameters: 5 s, 2 s, 3 probes |
| `main/network/client_socket.c` | `_Static_assert(...)` | Fails the build if `IDLE + INTERVAL * PROBES` is 30 s or more |
| `main/network/client_socket.c` | `client_socket_configure()` | Sets `TCP_NODELAY` and the four keepalive options, then logs the effective values |
| `main/network/client_socket.c` | `apply_keepalive_option()` | Sets one option, reads it back with `getsockopt()` and compares it |
| `main/network/network.c` | `accept_client()` | Calls `client_socket_configure()`, and closes the client if it fails |
| `main/network/network.c` | `tcp_server_task()` | Listener loop. Keeps the listening socket open across sessions and link changes |
| `main/network/network.c` | `on_eth_event()` | On `ETHERNET_EVENT_DISCONNECTED`, calls `bridge_request_disconnect()` |
| `main/bridge/bridge.c` | `receive_from_client()` | Turns `recv()` results into close reasons: `0` = peer closed, hard error = socket error |
| `main/bridge/bridge.c` | `send_to_client()` | Detects send failures and a stalled client (5 s without progress) |
| `main/bridge/bridge.c` | `run_session()` | Session loop. Returns the reason the session ended |
| `main/bridge/bridge.c` | `bridge_task()` | Records the session end, closes the socket, logs the heap and releases the slot |
| `main/bridge/bridge.c` | `abort_on_close()` | Sets `SO_LINGER {1, 0}` so a stalled client is closed with a RST |
| `main/bridge/bridge.c` | `bridge_attach_client()` | Claims the single session slot (`s_session_busy`) |
| `sdkconfig.defaults` | `CONFIG_LWIP_SO_LINGER=y` | Required by `abort_on_close()`. `bridge.c` fails the build without it |

### 3.2 TCP Keepalive Configuration

`client_socket_configure()` applies these options to each accepted socket, in this order:

| Order | Level | Option | Value | Purpose |
|---|---|---|---|---|
| 1 | `IPPROTO_TCP` | `TCP_NODELAY` | 1 | Low latency. A failure is only logged as a warning |
| 2 | `SOL_SOCKET` | `SO_KEEPALIVE` | 1 | Enables keepalive probes |
| 3 | `IPPROTO_TCP` | `TCP_KEEPIDLE` | 5 s | Silence before the first probe |
| 4 | `IPPROTO_TCP` | `TCP_KEEPINTVL` | 2 s | Time between probes |
| 5 | `IPPROTO_TCP` | `TCP_KEEPCNT` | 3 | Unanswered probes before the connection is dropped |

The keepalive options are defined in the `KEEPALIVE_OPTIONS[]` table in `client_socket.c`. The detection budget is checked at compile time:

```c
// lwIP checks keepalive every 500 ms, so detection can take that much longer.
_Static_assert(CLIENT_KEEPALIVE_IDLE_S + CLIENT_KEEPALIVE_INTERVAL_S * CLIENT_KEEPALIVE_PROBES < 30,
               "FR-8: a half-open client must be detected within 30 seconds");
```

Without these options, lwIP's default keepalive idle time is 7200 s, so a half-open client would hold the only session slot for hours.

### 3.3 Socket Option Validation and Readback

For each keepalive option, `apply_keepalive_option()`:

1. calls `setsockopt()`, and fails with `Cannot set <option>: errno <n>` if it returns an error;
2. calls `getsockopt()`, and fails with `Cannot read back <option>: errno <n>` if it returns an error;
3. compares the value read back. `SO_KEEPALIVE` only has to be non-zero, because lwIP returns its internal flag bit (`SOF_KEEPALIVE`, `0x08`), not `1`. The other options must match exactly. A mismatch fails with `<option> reads back <x> after it was set to <y>`.

The first failure stops the configuration, and `client_socket_configure()` returns `ESP_FAIL`. `accept_client()` then logs the following message and closes the socket before it reaches the bridge:

```text
E NETWORK: TCP client <ip>:<port> rejected: keepalive could not be configured
```

On success, the values that were **read back**, not the requested ones, are logged once per client:

```text
I NETWORK: TCP keepalive: idle 5 s, interval 2 s, 3 probes
```

This line appears on hardware for every accepted client, so it confirms that lwIP accepted and returned the configured values.

### 3.4 Detection Timing Analysis

This analysis is based on the lwIP source in ESP-IDF v6.1 (`components/lwip/lwip/src/core/tcp.c`, `tcp_slowtmr()`, and `tcp_in.c`):

- `tcp_slowtmr()` runs every `TCP_SLOW_INTERVAL` = 2 × `CONFIG_LWIP_TCP_TMR_INTERVAL` (250 ms) = **500 ms**.
- `pcb->tmr` is reset, and `keep_cnt_sent` is cleared, whenever a segment is received from the peer.
- A probe is sent when the time since the last received segment exceeds `keep_idle + keep_cnt_sent × keep_intvl`, which gives probes after 5 s, 7 s and 9 s of silence.
- The connection is aborted when the silence exceeds `keep_idle + keep_cnt × keep_intvl` = 5 + 3 × 2 = **11 s**. lwIP sends a RST, frees the PCB and reports `ERR_ABRT`, which the socket layer returns as `ECONNABORTED` (`errno 113` in the ESP-IDF newlib numbering).
- The time-only abort check doesn't depend on unacknowledged data, so it also bounds detection while data is in flight.

Expected detection time, measured from the moment the client becomes unreachable:

| Case | Last segment received from client | Expected abort after the block |
|---|---|---|
| Idle connection (C1) | The ACK to the last keepalive probe, at most about 5.5 s before the block | About 5.5 s to 11.5 s |
| Active connection (C2) | Close to the block | About 11 s to 11.5 s |
| Upper bound in all cases | Not applicable | 11.5 s, below the 30 s limit |

This is a design analysis, not a measurement. The physical results are in section 6.

### 3.5 Disconnect Detection and Error Handling

The bridge task runs one session at a time in `run_session()`. It waits on `select()` with a 10 ms timeout and reads with `recv(..., MSG_DONTWAIT)`. Each way a session can end produces a reason, which is logged as `TCP client closed: <reason>`:

| Event | Detected in | `errno` | Logged reason |
|---|---|---|---|
| Client closes normally (FIN) | `receive_from_client()`: `recv()` returns 0 | Not applicable | `disconnected by peer` |
| Client sends a RST | `receive_from_client()`: `recv()` fails | 104 `ECONNRESET` | `socket error`, after `TCP client receive failed: errno 104` |
| Half-open client, keepalive expired | `receive_from_client()`: `recv()` fails | 113 `ECONNABORTED` | `socket error`, after `TCP client receive failed: errno 113` |
| Ethernet link down | `on_eth_event()` → `bridge_request_disconnect()`, or lwIP aborts the socket | 113 if aborted | `disconnect requested` or `socket error` |
| Send failure | `send_to_client()` | Any non-temporary error | `socket error` |
| Client accepts no data for 5 s | `send_to_client()` (`BRIDGE_TX_STALL_TIMEOUT_US`) | Not applicable | `client stalled` |
| `select()` failure | `run_session()` | Any non-temporary error | `socket error` |

`EAGAIN`, `EWOULDBLOCK`, `EINTR` and `ENOMEM` are treated as temporary (`is_temporary_socket_error()`), so they don't end the session.

### 3.6 Socket Cleanup and Resource Management

When `run_session()` returns, `bridge_task()` always runs the same sequence:

1. `record_session_end()` counts the bytes left undelivered. They are counted as expected (`session_bytes`) after a normal close, and as loss (`tcp_tx_bytes`, `uart_tx_bytes`) otherwise. These counts appear in the FR-7 `LOSS:` report.
2. It logs `TCP client closed: <reason>`.
3. For `client stalled` only, `abort_on_close()` sets `SO_LINGER {1, 0}`, so `close()` sends a RST and frees the PCB and its buffers immediately.
4. It calls `close(fd)`.
5. It logs `Heap after session: <free> bytes free, <min> minimum ever`.
6. It clears `s_session_busy`. This is deliberately the last step, so a new client can only be attached after the previous socket is closed.

In the network task, `accept_client()` closes the socket itself whenever the client doesn't reach the bridge: when the keepalive configuration fails, when a session is already active, or when the queue fails. The listening socket isn't touched by any session, so it stays available for the next client. `CONFIG_LWIP_MAX_SOCKETS` is 10.

### 3.7 Heap Monitoring

`bridge_task()` logs `esp_get_free_heap_size()` and `esp_get_minimum_free_heap_size()` after every session. As the source comment notes, a single value says little, because lwIP can keep a closed connection for a while (TIME_WAIT, unacknowledged data). A leak would show up as free heap falling steadily across many sessions. Section 7 analyzes the hardware values.

## 4. Implementation Verification (Host, No Hardware)

These checks run the real firmware source on the PC, against stubs. They verify the logic, not lwIP or the ESP32.

**Command and run:** `bash tests/host/run.sh quick`, run on 2026-11-04 against commit `63cbb71`. Exit code 0.

| Suite | Result |
|---|---|
| FR-7 counters and reporter, 200 ms period | pass=62 fail=0 |
| Bridge logic (includes T5b: heap reported at every session end) | pass=90 fail=0 |
| FR-8 client socket keepalive (`tests/host/client_socket_test.c`) | pass=25 fail=0 |

FR-8 client socket test cases:

| Case | What it checks | Result |
|---|---|---|
| K1 | On a real Linux TCP socket: configuration succeeds, idle 5 s, interval 2 s, 3 probes, `TCP_NODELAY` on, one log line with the effective values, and an 11 s budget below 30 s | PASS |
| K2 | Each keepalive option that can't be set rejects the client | PASS (4 of 4) |
| K3 | Each keepalive option that can't be read back rejects the client | PASS (4 of 4) |
| K4 | A value read back differently (`SO_KEEPALIVE` 0, idle 7200 s, interval 75 s, 9 probes) rejects the client | PASS (4 of 4) |
| K5 | lwIP's `SO_KEEPALIVE` readback of `0x08` is accepted as on | PASS |
| K6 | A `TCP_NODELAY` failure only logs a warning, and keepalive is still applied | PASS |
| K7 | The first failure stops the configuration | PASS |

Excerpt from the run:

```text
=== FR-8 client socket keepalive
K1 keepalive applied on a real TCP socket
  ok   configuration succeeds
  ok   SO_KEEPALIVE is on
  ok   idle is 5 s
  ok   interval is 2 s
  ok   probes are 3
  ok   TCP_NODELAY is on
  ok   effective values logged once: "TCP keepalive: idle 5 s, interval 2 s, 3 probes"
  ok   no warning or error
  ok   a silent client is dropped after 11 s, within the 30 s FR-8 limit
...
K4 a keepalive value that reads back different rejects the client
  ok   SO_KEEPALIVE off: rejected, "SO_KEEPALIVE reads back 0 after it was set to 1"
  ok   lwIP default idle of 7200 s: rejected, "TCP_KEEPIDLE reads back 7200 after it was set to 5"
...
RESULT pass=25 fail=0
```

**Limits of host verification:** K1 uses the Linux TCP stack, not lwIP, and K5 only simulates lwIP's flag readback. The rejection path (`accept_client()` closing the client) isn't exercised on hardware, because no keepalive option failed there. Timing on the real stack can only be shown on hardware.

## 5. Physical Test Setup and Evidence

### 5.1 Environment

| Item | Value |
|---|---|
| Board | ESP32-Ethernet-Kit v1.2, ESP32-WROVER-E, chip revision v3.1 |
| Network | ESP32 at `192.168.1.200`, TCP port `4001`, 100 Mbps full duplex. Test client at `192.168.1.58` |
| Console | UART0 on `/dev/ttyUSB1`, 115200 baud, `idf.py -p /dev/ttyUSB1 monitor` |
| UART1 | TX GPIO32, RX GPIO33, 115200 8N1, with a loopback jumper as in the test plan |

### 5.2 Evidence Sources

| ID | Source | Content |
|---|---|---|
| E1 | `build/log/idf_py_stdout_output_5167` | ESP-IDF monitor log, one power-on boot of `63cbb71` / `8d79dbe83`. 307,200 bytes, ending mid-line at uptime 10,470 s, so the end of the capture was not written |
| E2 | `fr8-test-b.log` (repository root, untracked) | Monitor log captured with `idf.py -p /dev/ttyUSB1 monitor 2>&1 \| tee fr8-test-b.log`. A later power-on boot of the same firmware. The monitor was still running when this report was prepared, and the values here come from a snapshot taken at 2026-11-04 15:10:09, with 1,546 lines up to uptime 6,300 s |
| E2' | `build/log/idf_py_stdout_output_12630` | Copy of the same E2 monitor session, written by `idf.py`. Contents match E2 |
| E3 | `build/log/idf_py_stdout_output_12435` | Short boot of the same firmware between E1 and E2. About 100 s, with no client |
| H1 | `bash tests/host/run.sh quick` | Host test run, section 4 |
| O1 | Operator report | C1: the ESP32 detected the blocked idle connection with `errno 113` and closed the session in **about 9 s** |
| O2 | Operator report | C2: connection established, traffic exchanged, connection blocked with iptables, then `errno 113` and cleanup |
| O3 | Operator report | G: 200 of 200 connections succeeded, with zero client-side errors |
| O4 | Operator report | H: the session at 5,419.518 s → 5,419.528 s in E2 was Test H. The Python client sent exactly 95 bytes (`payload = b"H-GRACEFUL-PENDING-" * 5`). The client-side received-byte count was not captured |

Line numbers below refer to the raw log files.

**Retention:** `build/log/` is ignored by Git and is deleted by `idf.py fullclean`, and `fr8-test-b.log` is untracked. Keep copies of E1 and E2 if this report must stay auditable. No client-side output, no client-side scripts, and no packet captures were retained for these tests. For H, only the payload definition was reported. The `h6*.pcap` files in the repository date from 2026-11-03 and belong to H6.

### 5.3 How Tests Were Matched to Log Sessions

The serial logs don't name the tests. Each test was matched to a session by:

- the order the operator gave (1, C1, B, D, C2, G);
- for H, the operator's direct identification of the session (O4);
- the close reason and `errno`;
- the session duration.

E1 precedes E2: E1 was last written at 13:22:11, and E2 starts after E3, which ended at 13:24:47. The matches are inferences, and they are stated as such for each test.

| Order | Test | Log | Session (uptime) | Duration | Close |
|---|---|---|---|---|---|
| 1 | Test 1 | E1, E2 | Boot | Not applicable | Not applicable |
| 2 | C1 | E1 | 1,176.678 s → 1,369.428 s | 192.750 s | `errno 113`, socket error |
| 3 | B | E2 | 32.838 s → 34.848 s | 2.010 s | `errno 104`, socket error |
| 4 | D | E2 | 104.938 s → 405.028 s | 300.090 s | disconnected by peer |
| 5 | C2 | E2 | 1,807.448 s → 1,885.198 s | 77.750 s | `errno 113`, socket error |
| 6 | G | E2 | 2,438.928 s → 2,470.748 s | 200 sessions in 31.8 s | disconnected by peer (200 of 200) |
| 7 | H | E2 | 5,419.518 s → 5,419.528 s | 0.010 s | disconnected by peer. Identified by the operator, not inferred |

E1 and E2 contain exactly two `errno 113` events, one each, matching the two half-open tests. The durations above run from accept to close. They include the time before the block, so they are **not** detection times.

## 6. Physical Test Results

### Test 1: First Boot and Keepalive Configuration

**Objective:** Verify Ethernet initialization, TCP server startup and the keepalive configuration on hardware.

**Procedure:**

```bash
idf.py -p /dev/ttyUSB1 flash monitor   # or: monitor 2>&1 | tee fr8-test-b.log
nc -v 192.168.1.200 4001               # any client, to trigger the keepalive configuration
```

**Expected:**

- Power-on boot of `63cbb71` with no errors.
- Ethernet link up and static IP `192.168.1.200` applied.
- `TCP server listening on port 4001`.
- On the first accepted client, `TCP keepalive: idle 5 s, interval 2 s, 3 probes`, with no `Cannot set`, `Cannot read back` or `reads back` errors.

**Actual (E2, lines 8 to 80; E1 shows the same boot sequence):**

```text
rst:0x1 (POWERON_RESET),boot:0x12 (SPI_FAST_FLASH_BOOT)
I (258) app_init: App version:      63cbb71
I (267) app_init: ELF file SHA256:  8d79dbe83...
I (328) MAIN: Firmware: version 63cbb71, ELF SHA256 8d79dbe83, built Nov  4 2026 10:08:58, ESP-IDF v6.1
I (338) MAIN: UART1 initialized successfully
I (878) NETWORK: Ethernet driver installed, PHY address 1
I (2978) NETWORK: Ethernet started
I (2978) NETWORK: Ethernet link up: 100 Mbps, full duplex, MAC c0:cd:d6:61:ae:b3
I (2978) esp_netif_handlers: eth ip: 192.168.1.200, mask: 255.255.255.0, gw: 192.168.1.1
I (2978) NETWORK: TCP server listening on port 4001
I (2978) NETWORK: IPv4 ready: 192.168.1.200, mask 255.255.255.0, gateway 192.168.1.1
I (2978) main_task: Returned from app_main()
...
I (32838) NETWORK: TCP keepalive: idle 5 s, interval 2 s, 3 probes
I (32838) NETWORK: TCP client connected: 192.168.1.58:50748
```

The keepalive line appeared for all 210 clients accepted in E2 (209 sessions plus 1 client rejected because a session was active) and all 4 in E1. Neither log contains a keepalive configuration error, an `E (` line, a panic, a watchdog or an unexpected reset.

**Result: PASS.**

### Test C1: Half-Open Connection, Idle

**Objective:** With a connected idle client, block all traffic between client and ESP32, and verify that the ESP32 detects the dead connection and closes the session within 30 s.

**Procedure:** *Reconstructed. The exact commands used were not retained.* On the client host (`192.168.1.58`):

```bash
# 1. Open an idle connection.
python3 - <<'PY'
import socket, time
s = socket.create_connection(("192.168.1.200", 4001))
print(time.strftime("%H:%M:%S"), "connected, local port", s.getsockname()[1])
input("Apply the iptables block, then press Enter to exit without closing... ")
PY

# 2. In a second terminal, block both directions and record the time.
date +%H:%M:%S.%N
sudo iptables -I INPUT  -s 192.168.1.200 -p tcp --sport 4001 -j DROP
sudo iptables -I OUTPUT -d 192.168.1.200 -p tcp --dport 4001 -j DROP

# 3. Watch the ESP32 monitor for "receive failed: errno 113", then remove the rules.
sudo iptables -D INPUT  -s 192.168.1.200 -p tcp --sport 4001 -j DROP
sudo iptables -D OUTPUT -d 192.168.1.200 -p tcp --dport 4001 -j DROP
```

**Expected:** `TCP client receive failed: errno 113`, then `TCP client closed: socket error` and a heap line, between about 5.5 s and 11.5 s after the block (section 3.4), and in any case within 30 s. A new client is then accepted.

**Actual (E1, lines 187 to 228):**

```text
I (1176678) NETWORK: TCP keepalive: idle 5 s, interval 2 s, 3 probes
I (1176678) NETWORK: TCP client connected: 192.168.1.58:55332
W (1369428) BRIDGE: TCP client receive failed: errno 113
I (1369428) BRIDGE: TCP client closed: socket error
I (1369428) BRIDGE: Heap after session: 254724 bytes free, 250560 minimum ever
...
I (1504558) NETWORK: TCP keepalive: idle 5 s, interval 2 s, 3 probes
I (1504558) NETWORK: TCP client connected: 192.168.1.58:36926
I (1504738) BRIDGE: TCP client closed: disconnected by peer
```

- The session ended by keepalive abort (`ECONNABORTED`), not by the client. `errno 113` with no link-down event matches lwIP's keepalive timeout.
- Cleanup completed in the same millisecond (1,369.428 s), and the next client was accepted, so the slot was released.
- The LOSS reports during the session show zero counters, which is consistent with an idle session.
- The detection time can't be computed from E1, because the block time is not in any retained log. The operator observed **about 9 s** (O1), which is inside the expected 5.5 s to 11.5 s window.

**Result: INCONCLUSIVE.** Detection and cleanup are confirmed in the log. Detection within 30 s rests on the operator's observation, with no retained timestamp. The match to E1 is inferred from test order.

### Test B: Abrupt RST

**Objective:** Force a TCP reset from the client and verify that the ESP32 detects `ECONNRESET` and releases the connection.

**Procedure:** *Reconstructed. The 2 s delay matches the logged session duration.*

```python
import socket, struct, time

s = socket.create_connection(("192.168.1.200", 4001))
time.sleep(2)
# l_onoff=1, l_linger=0: close() sends a RST instead of a FIN.
s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
s.close()
```

**Expected:** `TCP client receive failed: errno 104`, `TCP client closed: socket error`, a heap line, and a later client accepted.

**Actual (E2, lines 79 to 92):**

```text
I (32838) NETWORK: TCP keepalive: idle 5 s, interval 2 s, 3 probes
I (32838) NETWORK: TCP client connected: 192.168.1.58:50748
W (34848) BRIDGE: TCP client receive failed: errno 104
I (34848) BRIDGE: TCP client closed: socket error
I (34858) BRIDGE: Heap after session: 254704 bytes free, 250560 minimum ever
...
I (104938) NETWORK: TCP keepalive: idle 5 s, interval 2 s, 3 probes
I (104938) NETWORK: TCP client connected: 192.168.1.58:59830
```

The RST was detected 2.010 s after connection, the socket was closed and the heap logged within 10 ms, and the next client (Test D) was accepted. E1 contains a second session with the same pattern (8,967.598 s → 8,969.598 s, `errno 104`, lines 975 to 979), which was not mapped to a test.

**Result: PASS.**

### Test D: Healthy Idle Connection, 300 s

**Objective:** Keep a reachable client connected for 300 s without traffic, and verify that keepalive doesn't disconnect it.

**Procedure:** *Reconstructed. The 300 s matches the logged session duration.*

```python
import socket, time

s = socket.create_connection(("192.168.1.200", 4001))
time.sleep(300)   # no data in either direction
s.close()         # graceful FIN from the client
```

**Expected:** No close by the ESP32 during the 300 s. The session ends only when the client closes it, with `disconnected by peer`. Over 300 s, the ESP32 sends a probe each time the connection has been silent for 5 s, and each answered probe resets the timer.

**Actual (E2, lines 91 to 124):**

```text
I (104938) NETWORK: TCP keepalive: idle 5 s, interval 2 s, 3 probes
I (104938) NETWORK: TCP client connected: 192.168.1.58:59830
...                       (30 LOSS reports, all "no data loss", nothing else)
I (405028) BRIDGE: TCP client closed: disconnected by peer
I (405028) BRIDGE: Heap after session: 254376 bytes free, 250560 minimum ever
```

The session lasted 300.090 s and ended by the client's FIN. There was no premature `errno 113`, so the keepalive probes were answered throughout. This test verifies that the short 5 s idle time causes no false disconnects. It doesn't measure the probes themselves, because no packet capture was retained.

**Result: PASS.**

### Test C2: Half-Open Connection with Traffic

**Objective:** Establish a connection, exchange data, then block traffic with iptables, and verify detection and session cleanup.

**Procedure:** *Reconstructed.*

```bash
# 1. Connect and exchange data. With the GPIO32-GPIO33 jumper, the bridge echoes it back.
python3 - <<'PY'
import socket, time
s = socket.create_connection(("192.168.1.200", 4001))
s.settimeout(2)
for i in range(5):
    msg = f"C2 message {i}\n".encode()
    s.sendall(msg)
    print("echo:", s.recv(64))
    time.sleep(1)
input("Apply the iptables block, then press Enter to exit without closing... ")
PY

# 2. In a second terminal: same iptables rules as C1, recording the time, then remove them.
```

**Expected:** Same as C1. Because the client was active until the block, the expected abort is about 11 s to 11.5 s after the block, and in any case within 30 s.

**Actual (E2, lines 265 to 277, and the next session at line 333):**

```text
I (1807448) NETWORK: TCP keepalive: idle 5 s, interval 2 s, 3 probes
I (1807448) NETWORK: TCP client connected: 192.168.1.58:34874
W (1885198) BRIDGE: TCP client receive failed: errno 113
I (1885198) BRIDGE: TCP client closed: socket error
I (1885198) BRIDGE: Heap after session: 254728 bytes free, 246500 minimum ever
...
I (2438928) NETWORK: TCP keepalive: idle 5 s, interval 2 s, 3 probes
I (2438928) NETWORK: TCP client connected: 192.168.1.58:47118
```

- The session ended by keepalive abort, and cleanup completed in the same millisecond. The next client (start of Test G) was accepted.
- The minimum-ever heap fell from 250,560 to 246,500 bytes between 405.0 s and the end of this session. That is the only drop in either log, and it is consistent with buffers used for traffic in this session. The log can't place it more precisely.
- The traffic exchange isn't visible in the ESP32 log. With the loopback jumper, fully echoed data leaves every `LOSS:` counter at zero, and all reports during this session are zero.
- The block time is not recorded, so the detection time can't be computed, and no operator timing was reported for C2.

**Result: INCONCLUSIVE.** Half-open detection and cleanup are confirmed in the log. The traffic phase and detection within 30 s have no retained evidence. The match to this session is inferred from test order.

### Test G: 200-Cycle Soak

**Objective:** Run 200 consecutive connect and graceful close cycles, and verify that every connection is accepted, every session is released, and the heap doesn't grow or shrink across cycles.

**Procedure:** *Reconstructed. The pacing matches the logged mean of 160 ms between connections.*

```python
import socket, time

H = ("192.168.1.200", 4001)
ok = errors = 0

for i in range(200):
    try:
        s = socket.create_connection(H, timeout=5)
        s.close()          # graceful FIN
        ok += 1
    except OSError as e:
        errors += 1
        print(i, "error:", e)
    time.sleep(0.15)

print(f"{ok}/200 connected, {errors} errors")
```

**Expected:** 200 accepts, 200 `disconnected by peer` closes, no rejected clients, no errors, and a free heap that returns to the same value after each session.

**Actual (E2, lines 333 to 1136):**

```text
I (2438928) NETWORK: TCP keepalive: idle 5 s, interval 2 s, 3 probes
I (2438928) NETWORK: TCP client connected: 192.168.1.58:47118
I (2438938) BRIDGE: TCP client closed: disconnected by peer
I (2438938) BRIDGE: Heap after session: 254376 bytes free, 246500 minimum ever
I (2439088) NETWORK: TCP keepalive: idle 5 s, interval 2 s, 3 probes
I (2439088) NETWORK: TCP client connected: 192.168.1.58:47120
I (2439088) BRIDGE: TCP client closed: disconnected by peer
I (2439098) BRIDGE: Heap after session: 254376 bytes free, 246500 minimum ever
...
I (2470748) NETWORK: TCP keepalive: idle 5 s, interval 2 s, 3 probes
I (2470748) NETWORK: TCP client connected: 192.168.1.58:55402
I (2470748) BRIDGE: TCP client closed: disconnected by peer
I (2470748) BRIDGE: Heap after session: 254372 bytes free, 246500 minimum ever
```

Figures from the E2 log:

| Measure | Value |
|---|---|
| Sessions accepted, with the keepalive line | 200 |
| Sessions closed with `disconnected by peer` | 200 |
| Rejected clients, socket errors, `W` or `E` lines during the soak | 0 |
| Distinct client source ports | 200 (47118 to 55402) |
| Soak duration | 31.82 s (2,438.928 s → 2,470.748 s) |
| Time between connections | 90 ms min, 160 ms mean, 250 ms max |
| Session duration | 0 ms to 10 ms, 3.6 ms mean |
| Free heap after session | 254,376 bytes after sessions 1 and 2, 254,372 bytes after sessions 3 to 200 |
| Minimum-ever heap | 246,500 bytes, unchanged throughout |

Client side: the operator reported 200 of 200 successful connections and zero client-side errors (O3). The client output wasn't retained, but the ESP32 log independently shows 200 accepted and 200 cleanly closed sessions.

**Limitation:** every session was a graceful close with no data. The soak doesn't repeat the half-open, RST or traffic paths. Free heap was 254,372 bytes after the soak, which matches the value after the single graceful sessions later in E2 (lines 1272 to 1274, 1455 to 1457).

**Result: PASS.**

### Test H: Graceful Close with Pending Data

**Objective:** Verify that the bridge handles a client FIN while data from that session is still in transit through the bridge, ends the session as a normal close, and counts the undelivered bytes as expected discards rather than as loss.

**Procedure:** The payload is as reported by the operator (O4). The rest of the script is *reconstructed*, matching the logged session of about 10 ms.

```python
import socket

payload = b"H-GRACEFUL-PENDING-" * 5   # 19 bytes x 5 = 95 bytes (operator-reported)

s = socket.create_connection(("192.168.1.200", 4001))
s.sendall(payload)
s.close()          # FIN right away, while the echo is still in transit
```

With the GPIO32 to GPIO33 jumper, the bridge writes the 95 bytes to UART1 TX and they return on UART1 RX. At 115200 8N1, they need about 8.2 ms on the line (95 × 86.8 µs), which is about as long as the whole session. The close therefore reaches the bridge while bytes are still on their way.

**Expected:**

- The session ends with `disconnected by peer`, with no `errno` and no socket error.
- The heap line is logged and the slot is released.
- In the next `LOSS:` report, the payload bytes appear under `expected:` (`session_bytes`, `no_client_bytes`). All loss counters (`tcp_tx_bytes`, `uart_tx_bytes`, `tcp_send_failures`, overflow and errors) stay at 0, and the report says `no data loss`.

How the firmware counts these bytes:

- **`session_bytes` (`DISCARD_SESSION_BYTES`)** is added by `record_session_end()` after a `disconnected by peer` close. It includes:
  - UART RX bytes not yet sent to the client;
  - the bytes `drain_session_input()` reads from UART1 when the session ends;
  - TCP bytes not yet written to UART1.
- **`no_client_bytes` (`DISCARD_NO_CLIENT_BYTES`)** is added by `discard_uart_read()` for UART1 input that arrives after the session, when no client is connected.

**Actual (E2, lines 1453 to 1459):**

```text
I (5410358) LOSS: 10.0 s: no data loss: ... | expected: session_bytes=0 no_client_bytes=0 | ...
I (5419518) NETWORK: TCP keepalive: idle 5 s, interval 2 s, 3 probes
I (5419518) NETWORK: TCP client connected: 192.168.1.58:58402
I (5419528) BRIDGE: TCP client closed: disconnected by peer
I (5419538) BRIDGE: Heap after session: 254372 bytes free, 246500 minimum ever
I (5420358) LOSS: 10.0 s: no data loss: uart_rx_overflow=0 uart_rx_errors=0 tcp_send_failures=0 tcp_tx_bytes=0 uart_tx_bytes=0 | expected: session_bytes=31 no_client_bytes=64 | pressure: rx_buffer_full=0 | line: uart_breaks=0 | accounting: event_queue_drops=0 rx_error_pauses=0
I (5430358) LOSS: 10.0 s: no data loss: ... | expected: session_bytes=0 no_client_bytes=0 | ...
```

- The session lasted about 10 ms and ended with `disconnected by peer`. No `W` or `E` line was logged.
- Free heap after the session was 254,372 bytes, the same as after the graceful soak sessions.
- The report covering the session shows `session_bytes=31` and `no_client_bytes=64`, with every loss counter at 0. **31 + 64 = 95 bytes**, which equals the payload size.
- The reports before (5,410.358 s) and after (5,430.358 s) show zero for both counters. This is the only session in the 5,410.358 s to 5,420.358 s window, so no other UART input contributed to these counts.

**Interpretation:** the ESP32 counters account for all 95 payload bytes, split between the closed session (31 bytes) and UART input that arrived after the close, with no client connected (64 bytes). This supports correct handling of a FIN with pending data, with the discarded bytes classified as expected, not as loss. The counters alone can't tell which of the 31 session bytes were still waiting to be written to UART1 and which had already returned on UART1 RX.

**Limitation:** the client-side received-byte count wasn't captured, so this report doesn't state how many bytes, if any, the client received before closing.

**Result: PASS.** The pass is based on the ESP32-side evidence: a normal close reason, cleanup and heap log, and 95 of 95 bytes counted as expected discards with no loss counted. Client-side delivery is not verified.

### Additional Sessions Not Mapped to a Test

These sessions appear in the logs but were not identified as part of the FR-8 tests. They are listed for completeness and not counted in any result.

| Log | Session (uptime) | Duration | Close | Notes |
|---|---|---|---|---|
| E1 | 1,504.558 s → 1,504.738 s | 0.180 s | disconnected by peer | First client after C1 |
| E1 | 8,967.598 s → 8,969.598 s | 2.000 s | `errno 104` | Same pattern as Test B |
| E1 | 10,344.818 s → not captured | Not known | Not known | E1 ends mid-line before this session closes |
| E2 | 3,810.508 s → 3,810.688 s | 0.180 s | disconnected by peer | |
| E2 | 3,970.068 s → 4,093.688 s | 123.620 s | disconnected by peer | A second client was rejected during it, at 4,051.828 s (`a client is already connected`, FR-2 behavior) |
| E2 | 4,104.938 s → 4,606.648 s | 501.710 s | `errno 104` | Ended by a RST from the client after 501.7 s. Not mapped to C2, as instructed. What the client did during the session is unknown |
| E2 | 4,693.508 s → 4,693.558 s | 0.050 s | disconnected by peer | |
| E2 | 4,760.998 s → 4,761.208 s | 0.210 s | disconnected by peer | Data exchanged: next report shows `session_bytes=42`. Heap 249,688 bytes after it |

None of them shows a failure: each was closed and logged its heap, and each was followed by a successfully accepted client or by an idle bridge.

## 7. Resource Stability

Free heap after each session, from all sessions in E1 and E2:

| Session type | Free heap after session (bytes) | Occurrences |
|---|---|---|
| Graceful close by client | 254,368 to 254,376 | 205 (E2) and 1 (E1) |
| Closed by RST or keepalive abort | 254,700 to 254,728 | 3 (E2) and 2 (E1) |
| Graceful close after data exchange | 249,688 | 1 (E2, session at 4,760.998 s) |

| Log | Minimum-ever heap |
|---|---|
| E1 | 250,560 bytes, unchanged across all its sessions, through 10,470 s of uptime |
| E2 | 250,560 bytes until 405.0 s. 246,500 bytes from the C2 session end (1,885.2 s) through the snapshot at 6,300 s |

Observations:

1. **No leak across sessions.** After 200 soak cycles, free heap was within 4 bytes of the value before the soak, and the same value recurs after later graceful sessions. Over the whole E2 capture, more than 1.7 hours of uptime and 209 sessions, the values don't trend downward.
2. **Aborted sessions leave about 330 bytes more free** than graceful ones. This is consistent with lwIP freeing the PCB at once on abort, while after a graceful close it keeps the closed connection for a while. This is an interpretation, not measured directly.
3. **The 249,688 byte value** (4,684 bytes lower) after one data session recovered to 254,372 bytes after the next session. This is consistent with lwIP still holding buffers of the closed connection, not with a leak.
4. **The minimum-ever heap** fell once, by 4,060 bytes, in the window that contains C2. It didn't fall again during the soak or the later sessions. The lowest recorded value, 246,500 bytes, is about 7.9 KB below the typical post-session free heap.
5. **No instability.** Neither log contains a panic, watchdog, backtrace, unexpected reset or `E (` line. The listener stayed available after every disconnect type tested.

**Limitations:** free heap is only sampled after each session, so the peak heap usage during a session isn't measured. The `LOSS: Reporter stack` line covers only the reporter task. Task stack high-water marks and lwIP PCB pool usage aren't logged.

**Result: PASS** for the observed duration and session types.

## 8. Final Test Summary

| Test ID | Description | Expected behavior | Actual result | Status | Evidence |
|---|---|---|---|---|---|
| H-FR8 | Host tests of keepalive configuration and validation (K1 to K7) | All checks pass | 25 of 25 pass. Bridge heap report 90 of 90 | PASS | H1 |
| Test 1 | First boot and keepalive configuration | Boot with no errors, link up, static IP, listener on 4001, keepalive 5/2/3 read back | All shown. Keepalive line on every accepted client, no configuration errors | PASS | E1, E2 lines 8 to 80 |
| C1 | Half-open, idle (iptables) | `errno 113`, cleanup, slot released, within 30 s of the block | `errno 113`, cleanup and next client accepted. Detection time not in the log; operator observed about 9 s | INCONCLUSIVE | E1 lines 187 to 228, O1 |
| B | Abrupt RST (`SO_LINGER {1, 0}`) | `errno 104`, cleanup, slot released | `errno 104` at 2.010 s, closed and heap logged within 10 ms, next client accepted | PASS | E2 lines 79 to 92 |
| D | Healthy idle client, 300 s | No disconnect by the ESP32; closed by the client | 300.090 s, `disconnected by peer`, no keepalive abort | PASS | E2 lines 91 to 124 |
| C2 | Half-open with traffic (iptables) | `errno 113`, cleanup, slot released, within 30 s of the block | `errno 113`, cleanup and next client accepted. Traffic and detection time not in the log | INCONCLUSIVE | E2 lines 265 to 333, O2 |
| G | 200-cycle connect and graceful close | 200 of 200 accepted and closed, no errors, stable heap | 200 accepted, 200 `disconnected by peer`, 0 rejects or errors. Heap 254,372 to 254,376 bytes. Client reported 200 of 200, 0 errors | PASS | E2 lines 333 to 1136, O3 |
| H | Graceful close with 95 bytes pending | `disconnected by peer`, cleanup, pending bytes counted as expected discards, no loss counted | `disconnected by peer` after about 10 ms, heap 254,372 bytes. `session_bytes=31` + `no_client_bytes=64` = 95 bytes, all loss counters 0. Client receive count not captured | PASS (ESP32-side evidence) | E2 lines 1453 to 1459, O4 |
| R | Resource stability | No heap decline, no crashes | No downward trend over 209 sessions. Minimum heap 246,500 bytes. No resets | PASS | E1, E2 |

**FR-8 coverage:**

| Requirement | Status | Basis |
|---|---|---|
| FR-8.1 Detect disconnections | Verified | B (RST), D, G and H (FIN, including H with data pending) |
| FR-8.2 Detect half-open connections | Verified (detection only) | C1, C2: keepalive abort `errno 113` |
| FR-8.3 Within 30 s | **Not verified by retained evidence** | Design bound 11.5 s (section 3.4), compile-time check, operator's about 9 s for C1 |
| FR-8.4 Release resources | Verified | Every session: close and heap log. Stable heap in G. Pending bytes accounted for in H |
| FR-8.5 Accept new clients | Verified | A new client accepted after every tested disconnect type |

**Overall FR-8: PARTIAL.** FR-8.3 needs a timed re-run. The link-loss items still open in [HW-VAL-008](../test-records/hw-validation-fr8.md) (HW-FR8-003, HW-FR8-004) were tested on older firmware and are outside this report.

## 9. Limitations and Required Follow-Up

1. **Half-open detection time (C1, C2).** The ESP32 log has uptime timestamps only, and the iptables block time wasn't recorded. To close FR-8.3, re-run C1 and C2 with timestamps on both sides:
   - start the monitor with `idf.py -p /dev/ttyUSB1 monitor --timestamps` so each line gets host wall-clock time;
   - log `date +%H:%M:%S.%N` immediately before inserting the iptables rules;
   - optionally capture `sudo tcpdump -i <iface> -nn -tttt 'host 192.168.1.200 and port 4001'` on the client host. Run it before the `OUTPUT` rule so the keepalive probes from the ESP32 are visible: they arrive on `INPUT`, which libpcap sees before iptables drops them.

   Pass criterion: block-to-`errno 113` time of 30 s or less, with about 5.5 s to 11.5 s expected for C1 and about 11 s to 11.5 s expected for C2.
2. **C2 traffic phase.** Keep the client-side output, such as the echoed messages, to show that data was exchanged before the block.
3. **Test-to-log mapping.** The logs don't name tests. Before each test, write a marker the log will show, such as a short client connection whose timing is distinctive, or keep a test sheet with the session's local port. With `--timestamps`, a wall-clock note is enough.
4. **Client-side scripts and outputs** weren't retained. The scripts in section 6 are reconstructions that match the logged behavior, and they are labeled as such. Store the scripts used in a future run under `tests/manual/`.
5. **Rejection path on hardware.** No keepalive option failed on hardware, so the `rejected: keepalive could not be configured` path is verified only by host tests (K2 to K4).
6. **Evidence retention.** E1 and E2' are in `build/log/`, which `idf.py fullclean` deletes, and E2 is untracked. E1 is truncated at 307,200 bytes, and E2 was still being written when this report was prepared.
7. **Soak scope.** G repeats only graceful closes with no data. A soak that mixes RST, half-open and data sessions isn't covered.
8. **H client-side delivery.** The client-side received-byte count for H wasn't captured. The ESP32 counters account for 95 of 95 bytes, but how many bytes the client received before closing isn't verified. On a re-run, print `len()` of everything the client received before `close()`, and keep the output.
9. **Unmapped sessions** in section 6 aren't explained, including the 501.7 s session ended by RST. They show no failure, but they aren't evidence for any test.
