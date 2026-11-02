# Hardware Validation Record: FR-1

| Field | Value |
|---|---|
| Record ID | HW-VAL-001 |
| Project | ESP32 TCP Serial Bridge |
| Test date | 2026-11-02 (UTC-5) |
| Tested by | Project operator |
| Record prepared by | Claude Code, from project history and retained logs |
| Base commit | `25847e6`, with uncommitted changes (`25847e6-dirty`) |

## 1. Purpose and Scope

This record provides traceability for the hardware validation of **FR-1, Static IPv4 Ethernet Configuration**, on the ESP32-Ethernet-Kit.

It records what was tested, how it was tested, the commands used, the expected and actual results, and the available evidence. Only tests actually performed are recorded.

FR-2 is recorded separately in [hw-validation-fr2.md](hw-validation-fr2.md).

## 2. Evidence Sources

Each result cites one or more of these sources.

| ID | Type | Source | Notes |
|---|---|---|---|
| E1 | Captured serial log | ESP-IDF monitor session ending 12:12:44, firmware `6f7d38aea` | Three boots. Truncated at 12,288 bytes, in the middle of a log line |
| E2 | Captured serial log | ESP-IDF monitor session ending 12:53:22, firmware `58bae9063` | One boot. Truncated at 4,096 bytes, in the middle of a log line |
| E3 | Captured tool output | `idf.py` flash logs | Successful flash at 11:57:14 |
| E5 | Captured tool output | `ping` run from Ubuntu at 12:49:15, firmware `6f7d38aea` | Run by Claude Code during troubleshooting |
| E6 | Operator report | Operator statement that the Ethernet recovery tests passed | No raw output retained |

E1, E2 and E3 come from `build/log/`, which Git ignores and `idf.py fullclean` can delete. The excerpts in this record are their preserved copy.

## 3. Test Environment

### 3.1 Hardware

| Item | Value |
|---|---|
| Board | ESP32-Ethernet-Kit v1.2 |
| Module | ESP32-WROVER-E |
| Chip | ESP32-D0WD-V3, revision v3.1 (E3) |
| Flash | 4 MB (E1) |
| Ethernet PHY | IP101, using the ESP-IDF generic IEEE 802.3 PHY driver |
| PHY address | 1, detected automatically (E1, E2) |
| MAC-PHY interface | RMII, 50 MHz clock from the PHY on GPIO0 |
| MDC / MDIO / PHY reset | GPIO23 / GPIO18 / GPIO5 |
| Ethernet MAC address | `c0:cd:d6:61:ae:b3` (E1, E2) |
| Serial console | `/dev/ttyUSB1`, 115200 baud |

### 3.2 Software

| Item | Value |
|---|---|
| ESP-IDF | v6.1 |
| esptool | v5.6.0 |
| Host OS | Ubuntu 24.04.5 LTS, Linux 7.0.0-38-generic |

### 3.3 Firmware Under Test

| Build | ELF SHA256 | Flashed | Content |
|---|---|---|---|
| Ethernet build | `6f7d38aea...` | 11:57:14 (E3) | Ethernet and static IP |

The full ELF SHA256 is `6f7d38aea8fd76be789f07af7b3f1691cb46d3c57ae0354895fe5f18691775fb`.

### 3.4 Network Configuration

| Parameter | Value |
|---|---|
| ESP32 IPv4 address | `192.168.1.200` |
| Subnet mask | `255.255.255.0` |
| Gateway | `192.168.1.1` |
| DHCP | Disabled |

The Ubuntu interface had address `10.0.2.15/24` when this record was prepared, while the ESP32 logged TCP clients as `192.168.1.170` during FR-2. This suggests that Ubuntu reaches the network through a virtual machine NAT, but the topology was not confirmed.

## 4. Prerequisites

1. The ESP32-Ethernet-Kit is powered and connected over USB for the serial console.
2. The Ethernet port is connected to the test network.
3. The Ubuntu host can reach the `192.168.1.0/24` network.
4. The firmware listed in section 3.3 is flashed.
5. The serial monitor is open:

```bash
idf.py -p /dev/ttyUSB1 monitor
```

## 5. FR-1: Static IPv4 Ethernet Configuration

### Test Summary

| Test ID | Test | Evidence | Result |
|---|---|---|---|
| HW-FR1-001 | Ethernet initialization with the IP101 PHY | E1 | PASS |
| HW-FR1-002 | Static IPv4 configuration | E1 | PASS |
| HW-FR1-003 | Link at 100 Mbps, full duplex | E1 | PASS |
| HW-FR1-004 | Ping from Ubuntu | E5 | PASS |
| HW-FR1-005 | Link disconnection and restoration | E1 | PASS |
| HW-FR1-006 | Static IP recovered after link restoration | E6 | PASS, with the evidence limitation below |

### HW-FR1-001: Ethernet Initialization

**Procedure**

1. Reset or power on the board.
2. Observe the boot log.

**Expected:** The driver installs, a PHY is detected and Ethernet starts without errors.

**Actual:** The driver installed, the PHY was found at address 1, and Ethernet started. The log identifies the PHY by address only; the IP101 model comes from the board specification.

**Evidence (E1)**

```text
I (857) NETWORK: Ethernet driver installed, PHY address 1
I (857) esp_eth.netif.netif_glue: c0:cd:d6:61:ae:b3
I (857) esp_eth.netif.netif_glue: ethernet attached to netif
I (3057) NETWORK: Ethernet started
```

### HW-FR1-002: Static IPv4 Configuration

**Procedure**

1. Boot with the Ethernet cable connected.
2. Observe the addressing reported in the log.

**Expected:** IP `192.168.1.200`, mask `255.255.255.0` and gateway `192.168.1.1`.

**Actual:** All three values matched.

**Evidence (E1)**

```text
I (3057) esp_netif_handlers: eth ip: 192.168.1.200, mask: 255.255.255.0, gw: 192.168.1.1
I (3067) NETWORK: IPv4 ready: 192.168.1.200, mask 255.255.255.0, gateway 192.168.1.1
```

### HW-FR1-003: Link Speed and Duplex

**Procedure**

1. Boot with the Ethernet cable connected.
2. Observe the link message.

**Expected:** Link up at 100 Mbps, full duplex.

**Actual:** The link came up at 100 Mbps, full duplex, in every captured boot and link restoration.

**Evidence (E1)**

```text
I (3057) NETWORK: Ethernet link up: 100 Mbps, full duplex, MAC c0:cd:d6:61:ae:b3
```

### HW-FR1-004: Ping from Ubuntu

**Procedure**

1. With the link up, run:

```bash
ping -c 2 -W 1 192.168.1.200
```

**Expected:** Replies from the ESP32 with no packet loss.

**Actual:** 2 packets transmitted, 2 received, 0% packet loss.

**Evidence (E5)**

```text
--- 192.168.1.200 ping statistics ---
2 packets transmitted, 2 received, 0% packet loss, time 1002ms
rtt min/avg/max/mdev = 2.882/3.644/4.406/0.762 ms
```

### HW-FR1-005: Link Disconnection and Restoration

**Procedure**

1. Start with the link up.
2. Disconnect the Ethernet link and observe the log.
3. Restore the Ethernet link and observe the log.

**Expected:** The firmware reports link down and then link up, without rebooting.

**Actual:** The link loss was logged 176 s after startup messages, and the link returned 16 s later at 100 Mbps. No reboot appears between those lines.

**Evidence (E1)**

```text
I (3167) NETWORK: IPv4 ready: 192.168.1.200, mask 255.255.255.0, gateway 192.168.1.1
W (179157) NETWORK: Ethernet link down
I (195157) NETWORK: Ethernet link up: 100 Mbps, full duplex, MAC c0:cd:d6
```

The last line is cut off in the captured log.

### HW-FR1-006: Static IP Recovery

**Procedure**

1. Complete HW-FR1-005.
2. Confirm that the device is again operational at its static IP.

**Expected:** The device returns to `192.168.1.200` without a reboot and without DHCP.

**Actual:** The operator reported that the recovery tests passed (E6).

**Evidence limitation:** The retained log ends in the middle of the link-up line, before any `IPv4 ready` message. No serial line or post-recovery ping from this test was retained. This result therefore rests on the operator report, not on captured output.

## 6. Final Status

| Requirement | Status | Basis |
|---|---|---|
| FR-1 | **PASS — Hardware Validated** | HW-FR1-001 to HW-FR1-006 |
| FR-8 | Partially validated, **not complete** | Link loss detection (HW-FR1-005) was observed. Half-open connection detection and Ethernet disconnection scenarios remain pending |

## 7. Known Limitations

1. **Static IP recovery evidence:** HW-FR1-006 rests on an operator report, because the serial log was truncated first.
2. **FR-1 on the current firmware:** the full FR-1 sequence ran on the Ethernet build. On the TCP server build, E2 captures the boot only up to the link-up line, and the FR-2 connections show that the device was reachable. The link recovery tests were not repeated on this build.
3. **Short test duration:** the only captured link outage lasted about 16 s.
4. **Firmware traceability:** the tested firmware was built from uncommitted changes, so the ELF SHA256 is the only exact identifier. It cannot yet be tied to a commit.
5. **Network topology:** the path between Ubuntu and the ESP32 was not confirmed. See section 3.4.
6. **Generic PHY driver:** speed and duplex are read through autonegotiation. A switch port with a forced speed was not tested.

## 8. Tests Pending

These tests were proposed but have no retained evidence, so they are not reported as passed.

| Item | Test | Requirement |
|---|---|---|
| 1 | Ethernet outage longer than 2 minutes, then ping | FR-1, FR-8 |
| 2 | Ping and static IP confirmation after link recovery, with serial evidence | FR-1 |
| 3 | Link recovery tests repeated on the TCP server build | FR-1, FR-8 |
| 4 | Boot with no cable connected, then connect it | FR-1 |
| 5 | Repeated resets and power cycles | FR-1 |
| 6 | Packet capture confirming that the device sends no DHCP traffic | FR-1 |
