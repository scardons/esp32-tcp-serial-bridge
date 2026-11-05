# Hardware Validation Record: FR-9

| Field | Value |
|---|---|
| Record ID | HW-VAL-009 |
| Requirement | FR-9: The device shall recover from an Ethernet cable reconnection without a reboot. (*ESP32 Serial-to-TCP Bridge: Requirements v1.0*) |
| Project | ESP32 TCP Serial Bridge |
| Test date | 2026-11-05 (UTC-5) |
| Tested by | Project operator |
| Record prepared by | Claude Code, from the H9 report and a full review of the serial log |
| Base commit | `63cbb71`, with uncommitted changes (`63cbb71-dirty`). Those changes were later committed as `81b7fa7`, with no change to `main/` or `sdkconfig` |

## Purpose and Scope

This record maps the H9 test, repeated Ethernet link recovery, to FR-9. It records the procedure, the expected and actual results, and the evidence. Only what was actually tested is recorded.

The detailed per-outage analysis is in the [H9 report](../test-reports/manual-h9-2026-11-05T14-51-27-137Z.md). This record summarizes it and states the FR-9 status.

## Test Environment

| Item | Value |
|---|---|
| Board | ESP32-Ethernet-Kit v1.2, ESP32-WROVER-E |
| UART under test | UART1, TX GPIO32, RX GPIO33, 115200 8N1 |
| Hardware setup | GPIO32 connected to GPIO33 with a loopback jumper, as required by the test plan |
| Console | UART0 on `/dev/ttyUSB1`, 115200 baud, `idf.py monitor --timestamps` |
| Network | ESP32 at `192.168.1.200`, mask `255.255.255.0`, gateway `192.168.1.1`, TCP port `4001`. Client seen as `192.168.1.58` |
| Host | Ubuntu 24.04.5 LTS, Node.js test script `npm run manual:h9` |
| ESP-IDF | v6.1 |
| Firmware | App version `63cbb71-dirty`, compile time shown as `Nov 4 2026 15:41:03` (the app header's time, which ESP-IDF does not refresh on every build) |
| ELF SHA256 | `ce94f60773ad9e775294d72253025f176b3bea90d52169b7e96e2383cf9aa533` |
| Relevant configuration | `CONFIG_ESP_NETIF_LOST_IP_TIMER_ENABLE=y`, `CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL=120` |

The firmware was built from uncommitted changes, so the ELF SHA256 is its only exact identifier. The firmware source (`main/` and `sdkconfig`) was not changed between the build and commit `81b7fa7`.

## Evidence Sources

| ID | Type | Source | Notes |
|---|---|---|---|
| E9 | Captured tool output | `esptool write-flash`, `verify-flash` and a read-back of the app header, before the test | Flash hash verified. `verify-flash` digest matched `build/tcp_serial_bridge.bin`. The full ELF SHA256 read from flash matched the value above |
| E10 | Captured tool output | Pre-test check by Claude Code | Boot log, `ping` 3 of 3 replies, TCP connection to port 4001 with a working echo |
| E11 | Captured serial log | `fr9-h9-test.log`, one ESP-IDF monitor session from 09:27:36 | One boot. Reviewed from boot to 10:28:17, about 1 h of uptime |
| E12 | Test report | [manual-h9-2026-11-05T14-51-27-137Z.md](../test-reports/manual-h9-2026-11-05T14-51-27-137Z.md), from `npm run manual:h9` | 5 cycles, finished 09:51:27. Includes the full E11 review |

E11 is in the repository root, and Git does not track it. Keep a copy with this record. The excerpts here are its preserved copy.

The serial log does not record the operator's key presses. Cycles were matched to the log through their TCP sessions: each cycle begins with a "before" client, which an outage closes, and ends with an "after" client that the PC closes.

## Test Summary

| Test ID | Plan | Test | Evidence | Result |
|---|---|---|---|---|
| HW-FR9-001 | H9 | Recovery from repeated cable outages without a reboot | E11 | PASS (9 of 9 outages, 4.0 s to 109.9 s) |
| HW-FR9-002 | H9 | Static IP, mask and gateway restored after each recovery | E11 | PASS (9 of 9) |
| HW-FR9-003 | H9 | The TCP session cut by an outage is cleaned up | E11 | PASS (5 of 5 sessions) |
| HW-FR9-004 | H9 | A new TCP client is accepted after each recovery | E11, E12 | PASS (5 of 5 cycles) |
| HW-FR9-005 | H9 | TCP client accepted within 10 s of reconnection (project criterion) | E12 | PASS as measured from the operator's Enter (cycles 2 to 5). Cycle 1 NOT VERIFIED |
| HW-FR9-006 | H9 | Echo works before and after each outage | E12 | PASS by the script. NOT VERIFIED in the log |
| HW-FR9-007 | H9 | No reset, crash or IP configuration error | E11 | PASS |
| HW-FR9-008 | H9 | No data loss reported during recovery | E11 | PASS (364 of 364 reports at zero) |
| HW-FR9-009 | Not run | Outage longer than 120 s (ESP-NETIF lost-IP path) | None | PENDING |
| HW-FR9-010 | Not run | Recovery time measured from the physical reconnection | None | PENDING |

### HW-FR9-001 to HW-FR9-004: Repeated Link Recovery (H9)

**Procedure**

1. Flash the firmware, confirm the ELF SHA256, and open `idf.py -p /dev/ttyUSB1 monitor --timestamps`, saving its log.
2. Run `BRIDGE_FIRMWARE=<ELF SHA256> npm run manual:h9` (5 cycles, outages of at least 10 s).
3. For each cycle: the script connects a client and checks the echo; the operator pulls the cable, presses Enter, waits, presses Enter again and reconnects the cable; the script waits for a ping reply and an accepted client, then checks the echo.

The full procedure is in [manual-test-procedures.md](../test-reports/manual-test-procedures.md#h9-repeated-link-recovery-fr-9).

**Expected:** each outage logs link down and link restored. The static IP returns with the link, the session cut by the outage closes, and a new client is accepted, all without a reboot.

**Actual:** the log shows 9 outages, where the procedure planned 5: cycles 1, 2, 3 and 5 each contained two. Every outage was followed by a restored link, the static IP and, after each cycle, a new accepted client. No reset occurred.

| Cycle | Outage | Link down (uptime / time) | Link restored (uptime / time) | Duration | Session cleanup |
|---|---|---|---|---:|---|
| 1 | #1 | 153.088 s / 09:30:10 | 189.088 s / 09:30:46 | 35.9 s | `socket error` (client :43554) |
| 1 | #2 | 415.088 s / 09:34:32 | 447.088 s / 09:35:04 | 32.0 s | No session open |
| 2 | #3 | 663.088 s / 09:38:40 | 679.088 s / 09:38:56 | 15.9 s | `socket error` (client :49596) |
| 2 | #4 | 713.088 s / 09:39:30 | 737.088 s / 09:39:54 | 24.0 s | No session open |
| 3 | #5 | 805.088 s / 09:41:02 | 851.088 s / 09:41:48 | 45.9 s | `socket error` (client :57604) |
| 3 | #6 | 973.088 s / 09:43:50 | 1011.088 s / 09:44:28 | 38.0 s | No session open |
| 4 | #7 | 1177.088 s / 09:47:14 | 1287.088 s / 09:49:04 | 109.9 s | `socket error` (client :35146) |
| 5 | #8 | 1343.088 s / 09:50:00 | 1365.088 s / 09:50:22 | 21.9 s | `socket error` (client :50352) |
| 5 | #9 | 1369.088 s / 09:50:26 | 1373.088 s / 09:50:30 | 4.0 s | No session open |

Durations are the firmware's `Ethernet link restored after <x.x> s outage` values. The link is polled every 2 s, so each physical unplug or plug happened up to about 2 s before it was logged.

**Evidence (E11), outage #7, the longest:**

```text
I (1011778) NETWORK: TCP client connected: 192.168.1.58:35146
W (1177088) BRIDGE: TCP client receive failed: errno 113
W (1177088) NETWORK: Ethernet link down (#7)
I (1177088) BRIDGE: TCP client closed: socket error
I (1177088) BRIDGE: Heap after session: 254620 bytes free, 250496 minimum ever
I (1287088) NETWORK: Ethernet link up: 100 Mbps, full duplex, MAC c0:cd:d6:61:ae:b3
I (1287088) NETWORK: Ethernet link restored after 109.9 s outage (#7)
I (1287088) esp_netif_handlers: eth ip: 192.168.1.200, mask: 255.255.255.0, gw: 192.168.1.1
I (1287098) NETWORK: IPv4 ready: 192.168.1.200, mask 255.255.255.0, gateway 192.168.1.1
I (1287108) NETWORK: Static IP restored after link recovery (#7)
I (1288268) NETWORK: TCP client connected: 192.168.1.58:50336
I (1288538) BRIDGE: TCP client closed: disconnected by peer
```

- **Static IP (HW-FR9-002):** all 9 outages end with the same IP, mask and gateway, followed by `Static IP restored after link recovery (#n)`.
- **Session cleanup (HW-FR9-003):** each of the 5 sessions cut by an outage closed with `TCP client receive failed: errno 113` and `TCP client closed: socket error`. The 4 outages with no session open logged no BRIDGE lines. Free heap after each session stayed between 254 284 and 254 620 bytes, and the minimum ever stayed at 250 496 bytes.
- **New clients (HW-FR9-004):** 10 clients were accepted in total, 5 before and 5 after an outage. Each "after" client closed with `disconnected by peer`.

### HW-FR9-005: Recovery Time

**Expected:** a TCP client is accepted within 10 s of reconnection. This is a project criterion: FR-9 states no recovery time.

**Actual:** the script times from the operator's Enter, which the operator should press just before reconnecting the cable.

| Cycle | Client accepted, from Enter (E12) | Last link restored → client connected (E11, ESP32 clock) |
|---|---:|---:|
| 1 | 0.3 s, not valid | 174.9 s, not a recovery measurement |
| 2 | 6.8 s | 0.29 s |
| 3 | 5.2 s | 0.41 s |
| 4 | 6.3 s | 1.18 s |
| 5 | 4.0 s | 1.21 s |

- **Cycles 2 to 5 pass** as measured from the operator's Enter, with a worst case of 6.8 s and a mean of 5.6 s. This holds only if the operator pressed Enter before reconnecting the cable. The log cannot verify that sequence.
- **Cycle 1 is NOT VERIFIED:** the link was restored about 175 s before the next TCP activity.
- **Link restored to client connected** includes the script's retry pacing (an attempt every 200 ms, with a 1 s timeout), so it is an upper bound for the firmware.

### HW-FR9-006: Echo

The script recorded the echo as PASS before and after each of the 5 cycles (E12). The UART loopback data is not logged, so E11 cannot confirm it.

### HW-FR9-007: No Reset or Error

- **One boot only**, at 09:27:36 (`rst:0x1 (POWERON_RESET)`), almost 2 minutes before the test's first client. This was most likely the reset done by opening the monitor.
- **No reset during the test:** the uptime increases in every line, and the `LOSS` reports arrive every 10 000 ms with no gap.
- **No crash or error lines:** no `Guru Meditation`, panic, abort, watchdog, brownout or backtrace, and no error-level (`E`) line.
- **No IP configuration errors:** no `IPv4 does not match the static configuration` and no `static configuration was not kept`.

### HW-FR9-008: No Data Loss

All **364** `LOSS` reports from 09:27:47 to 10:28:17 say `no data loss`, with every counter at 0.

That includes `tcp_tx_bytes`, `uart_tx_bytes` and `session_bytes`, which count bytes left pending when a session ends: no session, whether cut by an outage or closed by the PC, left data undelivered. These counters cover only what the firmware can detect.

### HW-FR9-009: Outage Longer Than 120 s

Not run. The longest real outage was 109.9 s, so the ESP-NETIF lost-IP timer never expired while the link was down, and no lost-IP event was logged (N/A for this run).

The H9 script's "outage" values of 185.1 s and 163.7 s are times between the operator's Enter presses. They are not link outages.

To test it: `H9_OUTAGE_S=130 npm run manual:h9 -- 1`, with one unplug per cycle. Expected: `ESP-NETIF lost-IP event during outage #n (...). Static IP 192.168.1.200 kept for reconnection`, followed by `Static IP restored after link recovery (#n)` and an accepted client.

### HW-FR9-010: Recovery Time From the Physical Reconnection

Not measured. The script times from the operator's Enter, and the log records when the firmware detected the link, which is up to about 2 s after the plug, but not the plug itself. A reliable measurement needs the reconnection time captured independently, for example with a switch port log or a timed capture on the PC.

## How to Run These Tests

**Before you start**

1. Flash the firmware with `idf.py -p /dev/ttyUSB1 flash`, and note the ELF SHA256 from the `MAIN: Firmware:` boot line.
2. Connect the GPIO32 to GPIO33 jumper.
3. Open the serial monitor with timestamps and save its log: `idf.py -p /dev/ttyUSB1 monitor --timestamps | tee fr9-test.log`. Use a new log name for each run.
4. Close any other TCP client.

**Run H9 (HW-FR9-001 to 008):**

```bash
BRIDGE_FIRMWARE=<ELF SHA256> npm run manual:h9            # 5 cycles, outages of at least 10 s
BRIDGE_FIRMWARE=<ELF SHA256> npm run manual:h9 -- 20      # 20 cycles
```

In each cycle, pull the cable **once**, and press the second Enter **immediately before** plugging it back in, so the timing starts at the reconnection. The script writes `docs/test-reports/manual-h9-<time>.md`.

**Check the serial log:**

| Test | Command | Passes when |
|---|---|---|
| HW-FR9-001 | `grep -E "link down \(#\|link restored" fr9-test.log` | Every `link down (#n)` has a matching `link restored ... (#n)` |
| HW-FR9-002 | `grep -E "IPv4 ready\|Static IP restored\|does not match\|not kept" fr9-test.log` | Each outage ends with `IPv4 ready: 192.168.1.200, mask 255.255.255.0, gateway 192.168.1.1` and `Static IP restored after link recovery (#n)`, with no `does not match` and no `not kept` |
| HW-FR9-003 | `grep -E "receive failed\|TCP client closed\|Heap after session" fr9-test.log` | Each outage that cut a session shows `errno 113` and `socket error` (or `disconnect requested`) |
| HW-FR9-004 | `grep "TCP client connected" fr9-test.log` | A new client connects after each `Static IP restored` |
| HW-FR9-005 | Read the H9 report | Every cycle accepts a client within 10 s of Enter |
| HW-FR9-006 | Read the H9 report | "Echo before" and "Echo after" are PASS in every cycle |
| HW-FR9-007 | `grep -E "rst:\|Guru\|panic\|abort\|wdt\|E \(" fr9-test.log` | Only the boot banner matches (`rst:` and the boot lines), with no panic, watchdog or error line, and the uptime never goes back |
| HW-FR9-008 | `grep "LOSS:" fr9-test.log \| grep -vc "no data loss"` | `0` |

**HW-FR9-009, outage longer than 120 s:**

```bash
BRIDGE_FIRMWARE=<ELF SHA256> H9_OUTAGE_S=130 npm run manual:h9 -- 1
```

Keep the cable out for the whole 130 s. It passes when the log shows `ESP-NETIF lost-IP event during outage #n (...). Static IP 192.168.1.200 kept for reconnection`, then `Static IP restored after link recovery (#n)`, and the script accepts a client within 10 s.

**HW-FR9-010, time from the physical reconnection:** the reconnection must be timed independently. One way:
1. Connect the ESP32 directly to a PC Ethernet port, with no switch in between.
2. Run `ip -ts monitor link dev <interface>` on that PC during H9.
3. The `state UP` timestamp marks the physical reconnection. Compare it with the time the script's client was accepted.

## Status

**FR-9: PARTIAL, PASS for the scenarios exercised.**

The bridge recovered from 9 cable outages of 4.0 s to 109.9 s without a reboot. It restored the static IP every time, cleaned up every cut session, accepted new clients, and reported no data loss.

Not yet validated:
- recovery after an outage longer than 120 s, which is the ESP-NETIF lost-IP path;
- the recovery time measured from the physical reconnection.

## Limitations and Pending Validation

1. No outage longer than 120 s was tested (HW-FR9-009).
2. The recovery time was measured from the operator's Enter, not from the physical reconnection (HW-FR9-010). Cycle 1's time is not valid.
3. The echo was checked only by the script, not by the serial log.
4. Four of the five cycles contained two physical outages, where the procedure expects one per cycle. The firmware recovered from all of them.
5. E11 is not tracked by Git. A copy should be kept with this record.
