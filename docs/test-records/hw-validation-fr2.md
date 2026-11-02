# Hardware Validation Record: FR-2

| Field | Value |
|---|---|
| Record ID | HW-VAL-002 |
| Project | ESP32 TCP Serial Bridge |
| Test date | 2026-11-02 (UTC-5) |
| Tested by | Project operator |
| Record prepared by | Claude Code, from project history and retained logs |
| Base commit | `25847e6`, with uncommitted changes (`25847e6-dirty`) |

## 1. Purpose and Scope

This record provides traceability for the hardware validation of **FR-2, TCP Server and Single Active Client**, on the ESP32-Ethernet-Kit.

It records what was tested, how it was tested, the commands used, the expected and actual results, and the available evidence. Only tests actually performed are recorded. UART-to-TCP forwarding is out of scope because it is not implemented.

FR-1 is recorded separately in [hw-validation-fr1.md](hw-validation-fr1.md).

## 2. Evidence Sources

Each result cites one or more of these sources.

| ID | Type | Source | Notes |
|---|---|---|---|
| E2 | Captured serial log | ESP-IDF monitor session ending 12:53:22, firmware `58bae9063` | One boot. Truncated at 4,096 bytes, in the middle of a log line |
| E3 | Captured tool output | `idf.py` flash logs | Successful flash at 12:53:18. Failed flash attempts around 12:42 and 12:44 |
| E4 | Operator-supplied log excerpts | Serial lines provided by the operator for FR-2 | Without device timestamps |

E2 and E3 come from `build/log/`, which Git ignores and `idf.py fullclean` can delete. The excerpts in this record are their preserved copy.

## 3. Test Environment

### 3.1 Hardware

| Item | Value |
|---|---|
| Board | ESP32-Ethernet-Kit v1.2 |
| Module | ESP32-WROVER-E |
| Chip | ESP32-D0WD-V3, revision v3.1 (E3) |
| Flash | 4 MB (E2) |
| Ethernet PHY | IP101, using the ESP-IDF generic IEEE 802.3 PHY driver |
| PHY address | 1, detected automatically (E2) |
| MAC-PHY interface | RMII, 50 MHz clock from the PHY on GPIO0 |
| MDC / MDIO / PHY reset | GPIO23 / GPIO18 / GPIO5 |
| Ethernet MAC address | `c0:cd:d6:61:ae:b3` (E2) |
| Serial console | `/dev/ttyUSB1`, 115200 baud |

### 3.2 Software

| Item | Value |
|---|---|
| ESP-IDF | v6.1 |
| esptool | v5.6.0 |
| Host OS | Ubuntu 24.04.5 LTS, Linux 7.0.0-38-generic |
| TCP client | OpenBSD netcat, Debian patchlevel 1.226-1ubuntu2 |

### 3.3 Firmware Under Test

| Build | ELF SHA256 | Flashed | Content |
|---|---|---|---|
| TCP server build | `58bae9063...` | 12:53:18 (E3) | Ethernet, static IP and TCP server |

The full ELF SHA256 is `58bae90631db66e9c7767aee1d96e16d823b32e3a4e26bb5f2eefdcfdee8199a`.

### 3.4 Network Configuration

| Parameter | Value |
|---|---|
| ESP32 IPv4 address | `192.168.1.200` |
| Subnet mask | `255.255.255.0` |
| Gateway | `192.168.1.1` |
| DHCP | Disabled |
| TCP port | `4001` |
| Client address seen by the ESP32 | `192.168.1.170` (E4) |

The Ubuntu interface had address `10.0.2.15/24` when this record was prepared, while the ESP32 logged clients as `192.168.1.170`. This suggests that Ubuntu reaches the network through a virtual machine NAT, but the topology was not confirmed.

## 4. Prerequisites

1. The ESP32-Ethernet-Kit is powered and connected over USB for the serial console.
2. The Ethernet port is connected to the test network.
3. The Ubuntu host can reach the `192.168.1.0/24` network.
4. The firmware listed in section 3.3 is flashed.
5. The serial monitor is open:

```bash
idf.py -p /dev/ttyUSB1 monitor
```

## 5. FR-2: TCP Server and Single Active Client

All FR-2 tests used the TCP server build, `58bae9063`. The boot of this build is confirmed by E2:

```text
I (261) app_init: ELF file SHA256:  58bae9063...
I (861) NETWORK: Ethernet driver installed, PHY address 1
I (2961) NETWORK: Ethernet started
```

### Commands Used

Port check:

```bash
nc -vz -w 3 192.168.1.200 4001
```

Client connection:

```bash
nc -v 192.168.1.200 4001
```

### Test Summary

| Test ID | Test | Evidence | Result |
|---|---|---|---|
| HW-FR2-001 | TCP server starts on port 4001 | E4 | PASS |
| HW-FR2-002 | Client A connects | E4 | PASS |
| HW-FR2-003 | Client B attempts to connect while A is active | E4 | PASS |
| HW-FR2-004 | Server rejects Client B | E4 | PASS |
| HW-FR2-005 | Client A disconnects normally | E4 | PASS |
| HW-FR2-006 | Server releases the client connection | E4 | PASS |
| HW-FR2-007 | Client B reconnects without restarting the ESP32 | E4 | PASS |

### Procedure and Results

The tests ran as one sequence, in this order.

| Test ID | Step | Expected | Actual |
|---|---|---|---|
| HW-FR2-001 | Boot the firmware, then run the port check | Server logs that it is listening on port 4001 | Log shows the server listening on port 4001 |
| HW-FR2-002 | In terminal A, run the client connection command and keep it open | Server logs the client connection | Client connected from `192.168.1.170:64742` |
| HW-FR2-003 | In terminal B, run the client connection command while A is connected | Server receives a second connection | Connection arrived from `192.168.1.170:53653` |
| HW-FR2-004 | Observe the server log | Server rejects B because a client is already connected | Log shows B rejected for that reason |
| HW-FR2-005 | Close Client A | Server detects a normal disconnect | Log shows `disconnected by peer` |
| HW-FR2-006 | Observe the server log | The client socket is closed and the slot is free | Slot released, confirmed by HW-FR2-007 |
| HW-FR2-007 | In terminal B, run the client connection command again, without restarting the ESP32 | New connection accepted | Client connected from `192.168.1.170:56892` |

### Serial Log Evidence (E4)

As supplied by the operator, in test order:

```text
NETWORK: TCP server listening on port 4001

NETWORK: TCP client connected: 192.168.1.170:64742

NETWORK: TCP client 192.168.1.170:53653 rejected: a client is already connected

NETWORK: TCP client closed: disconnected by peer

NETWORK: TCP client connected: 192.168.1.170:56892
```

### Notes

- `nc` can report `succeeded!` for a rejected client, because the TCP handshake completes before the server closes the socket. The rejection is confirmed by the serial log.
- The `nc` terminal output and the exact way Client A was closed were not retained.

## 6. Troubleshooting Note

Before the FR-2 tests, a connection to port 4001 returned `Connection refused`. The board was still running the Ethernet build, `6f7d38aea`, which has no TCP server. Flash attempts around 12:42 and 12:44 had failed (E3), and their cause was not recorded in the portion reviewed. The flash at 12:53:18 succeeded, and E2 confirms that the board booted the TCP server build. This was a deployment issue, not a firmware defect.

## 7. Final Status

| Requirement | Status | Basis |
|---|---|---|
| FR-2 | **PASS — Hardware Validated** | HW-FR2-001 to HW-FR2-007 |
| FR-3 | Pending | No hardware validation recorded |
| FR-4 | Pending | No hardware validation recorded |
| FR-5 | Pending | No hardware validation recorded |
| FR-6 | Pending | No hardware validation recorded |
| FR-7 | Pending | No hardware validation recorded |
| FR-8 | Partially validated, **not complete** | Normal client disconnection (HW-FR2-005, HW-FR2-006) was observed. Half-open connection detection and Ethernet disconnection scenarios remain pending |

The text of FR-3 through FR-8 is not stored in this repository, so their status is limited to what this record can show.

## 8. Known Limitations

1. **Log timestamps:** the E4 excerpts have no device timestamps, so the sequence relies on the operator's order. The absence of a reboot is the operator's statement.
2. **Firmware traceability:** the tested firmware was built from uncommitted changes, so the ELF SHA256 is the only exact identifier. It cannot yet be tied to a commit.
3. **Network topology:** the path between Ubuntu and the ESP32 was not confirmed. See section 3.4.
4. **TCP data:** data sent by clients is discarded, because the bridge is not implemented.

## 9. Tests Pending

These tests were proposed but have no retained evidence, so they are not reported as passed.

| Item | Test | Requirement |
|---|---|---|
| 1 | 20 consecutive connect and disconnect cycles, to detect socket leaks | FR-2 robustness |
| 2 | 20 consecutive rejected connections while a client stays connected | FR-2 robustness |
| 3 | Ethernet disconnection with a TCP client connected, then reconnection without restart | FR-8 |
| 4 | Half-open client detection, with the expected drop after about 12 s | FR-8 |
| 5 | Ethernet outage longer than 2 minutes, then TCP reconnection | FR-8 |
| 6 | Bidirectional TCP-to-UART data transfer, once the bridge exists | Pending requirements |
