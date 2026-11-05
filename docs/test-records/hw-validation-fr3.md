# Hardware Validation Record: FR-3

| Field | Value |
|---|---|
| Record ID | HW-VAL-003 |
| Requirement | FR-3: Data received from the TCP client must be transmitted through UART1 TX (GPIO32). |
| Project | ESP32 TCP Serial Bridge |
| Test date | 2026-11-02 (UTC-5) |
| Tested by | Project operator |
| Record prepared by | Claude Code, from retained logs |
| Base commit | `78290c9`, with uncommitted changes (`78290c9-dirty`) |

## Purpose and Scope

This record maps the completed bridge tests to FR-3. Because the only UART observation point is the GPIO32-to-GPIO33 jumper, FR-3 can only be checked together with FR-4, through the echo returned to the client. GPIO32 was not observed directly with a logic analyzer.

## Test Environment

| Item | Value |
|---|---|
| Board | ESP32-Ethernet-Kit v1.2, ESP32-WROVER-E |
| UART under test | UART1, TX GPIO32, RX GPIO33, 115200 8N1 |
| Hardware setup | GPIO32 connected to GPIO33 with a loopback jumper, as required by the test plan |
| Console | UART0 on `/dev/ttyUSB1`, 115200 baud |
| Network | ESP32 at `192.168.1.200`, TCP port `4001`; client seen as `192.168.1.170` |
| Host | Ubuntu 24.04.5 LTS, OpenBSD netcat, Python 3 |
| ESP-IDF | v6.1 |
| Firmware | App version `78290c9-dirty`, compile time `Nov 2 2026 13:25:46` |
| ELF SHA256 | `23f9915465ac02e8daec17c3cecff8f5fd4f9a777a9c4da64803fc5bae5042ba` |

The firmware was built from uncommitted changes, so the ELF SHA256 is its only exact identifier.

## Evidence Sources

| ID | Type | Source | Notes |
|---|---|---|---|
| E7 | Captured serial log | ESP-IDF monitor session ending 14:11:59, firmware `23f991546` | One continuous boot, about 865 s of device uptime. Complete, not truncated |
| E8 | Captured tool output | `idf.py` flash log | Bridge firmware flashed and hash verified at 13:57:17 |

E7 comes from `build/log/`, which Git ignores and `idf.py fullclean` can delete. The excerpts here are its preserved copy.

The log does not label tests. Each session was matched to a planned test by order and duration, so the match is an inference. Terminal output from the client was not retained for any session, and the test plan commands are shown as the intended procedure.

The E7 session shows no reset after boot, and no `task_wdt`, panic, backtrace or `E (` error lines.

## Test Summary

| Test ID | Plan | Test | Evidence | Result |
|---|---|---|---|---|
| HW-FR3-001 | H2 | Text echo | None retained | NOT VERIFIED |
| HW-FR3-002 | H3 | Binary echo, all 256 byte values | E7, timing only | PARTIAL |
| HW-FR3-003 | H4 | Sustained 256,000-byte transfer | E7, timing only | PARTIAL |

### HW-FR3-001: Text Echo (H2)

**Procedure**

```bash
nc -v 192.168.1.200 4001
```

Type `hola`, press Enter, and close with `Ctrl+C`.

**Expected:** `hola` returns through the jumper.

**Actual:** Not verified. The serial log does not record payloads, and no terminal output was retained. The session from `192.168.1.170:56058` (uptime 149.3 s to 399.0 s) may include this test, but the log cannot confirm it.

### HW-FR3-002: Binary Echo (H3)

**Procedure:** Run this script with the jumper in place. It sends 20,480 bytes containing every value from 0x00 to 0xFF.

```python
import socket, threading, hashlib
data = bytes(range(256)) * 80
s = socket.create_connection(("192.168.1.200", 4001))
threading.Thread(target=s.sendall, args=(data,), daemon=True).start()
s.settimeout(10)
rx = b""
while len(rx) < len(data):
    chunk = s.recv(4096)
    if not chunk:
        break
    rx += chunk
s.close()
print(len(rx), "de", len(data), "igual:", rx == data, hashlib.sha256(rx).hexdigest()[:16])
```

**Expected:** `20480 de 20480 igual: True`.

**Actual:** The session lasted 1.79 s. Moving 20,480 bytes at 115200 baud should take 1.78 s, so the duration matches a complete transfer.

**Evidence (E7)**

```text
I (400686) NETWORK: TCP client connected: 192.168.1.170:63491
I (402476) BRIDGE: TCP client closed: disconnected by peer
```

**Evidence needed to close this test:** the terminal output of the script (`igual: True`). The serial log cannot show payload content.

### HW-FR3-003: Sustained Transfer (H4)

**Procedure:** Run the HW-FR3-002 script with `* 1000` in place of `* 80`, and `s.settimeout(30)`.

**Expected:** `igual: True`, with no `task_wdt` error.

**Actual:** The session lasted 22.26 s, against 22.22 s expected for 256,000 bytes. No `task_wdt`, panic or reset appears anywhere in E7.

**Evidence (E7)**

```text
I (434856) NETWORK: TCP client connected: 192.168.1.170:51235
I (457116) BRIDGE: TCP client closed: disconnected by peer
```

**Evidence needed to close this test:** the terminal output of the script (`igual: True`). The serial log cannot show payload content.

## How to Run These Tests

**Before you start**

1. Flash the firmware, and note the ELF SHA256 from the `MAIN: Firmware:` boot line.
2. Connect the GPIO32 to GPIO33 jumper. Without it, every echo test fails.
3. Open the serial monitor and save its log: `idf.py -p /dev/ttyUSB1 monitor --timestamps | tee fr3-test.log`.
4. Close any other TCP client.

| Test | How to run it | What to check | Passes when |
|---|---|---|---|
| HW-FR3-001 | `BRIDGE_FIRMWARE=<ELF SHA256> BRIDGE_REPORT_PATH=docs/test-reports/<new file>.md npm test -- -t "H2:"` | Jest output and the report | Both H2 tests pass: the text returns unchanged and in order |
| HW-FR3-002 | Same, with `-t "H3:"` | Jest output, plus the session lines in the serial log | 20,480 bytes covering 0x00 to 0xFF return unchanged |
| HW-FR3-003 | Same, with `-t "H4:"` | Jest output, plus the `LOSS:` lines in the serial log | 256,000 bytes return unchanged and in order, at about 11,520 bytes/s, and `LOSS:` shows `no data loss` |

Keep the generated report and the serial log. HW-FR3-002 and 003 are PARTIAL only because no Jest output was retained, so rerunning them with a saved report closes them.

**Testing FR-3 on its own (optional).** With the jumper, a test cannot tell whether a byte failed on UART1 TX or on RX. To check TX alone:
1. Remove the jumper, and connect GPIO32 to the RX pin of a 3.3 V USB-UART adapter, with a common ground.
2. Open the adapter at 115200 8N1, for example `picocom -b 115200 /dev/ttyUSB<n>`.
3. Run `nc -v 192.168.1.200 4001` and type text.

The text must appear in the adapter terminal unchanged.

## Status

**FR-3: PARTIAL.** Session timing and the absence of watchdog errors are consistent with correct operation, but payload integrity has no retained evidence.

## Limitations and Pending Validation

1. Terminal output for H2, H3 and H4 is needed to confirm that the data was correct.
2. The match between sessions and tests is an inference from order and duration.
3. GPIO32 was not measured directly, and no external serial device was tested.
4. Traffic used the jumper at 115200 8N1 only.
