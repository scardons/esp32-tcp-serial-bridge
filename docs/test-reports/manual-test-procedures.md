# Manual Hardware Test Procedures: H7, H8 and H9

These tests need a person to move the Ethernet cable, so they are not automated and are not part of `npm test`. Their results are **manual hardware validation**, kept separate from `automated-test-results.md`.

## Common Setup

1. Connect GPIO32 to GPIO33 with a jumper.
2. Open the serial monitor and keep it open:

   ```bash
   idf.py -p /dev/ttyUSB1 monitor
   ```

3. Confirm that the boot log shows the expected `ELF file SHA256`.
4. Close any other TCP client.

## H7: Ethernet Disconnection with a Client Connected

**Requirements:** FR-1, FR-8.

**Assisted run:**

```bash
BRIDGE_FIRMWARE=<ELF SHA256> npm run manual:h7
```

The script asks you to pull the cable for at least 10 s, reconnect it, and confirm the serial log.

**Procedure without the script:**

1. Run `nc -v 192.168.1.200 4001`, type a line and confirm the echo.
2. Disconnect the Ethernet cable at the ESP32.
3. Wait at least 10 s, then reconnect the cable.
4. Wait for `IPv4 ready` in the monitor.
5. Run `ping -c 2 192.168.1.200`.
6. In a second terminal, run `nc -v 192.168.1.200 4001` and confirm the echo.

**Expected serial log, with no reset between the lines:**

```text
W NETWORK: Ethernet link down
W BRIDGE: TCP client receive failed: errno 113
I BRIDGE: TCP client closed: socket error
I NETWORK: Ethernet link up: 100 Mbps, full duplex, MAC ...
I NETWORK: IPv4 ready: 192.168.1.200, mask 255.255.255.0, gateway 192.168.1.1
I NETWORK: TCP client connected: ...
```

`BRIDGE: TCP client closed: disconnect requested` is also valid in place of the two `BRIDGE` lines.

The first client on the PC stays open, because it never learns about the outage. Writing to it after recovery ends it with a reset.

## H8: Outage Longer than 2 Minutes

**Requirements:** FR-1, FR-8.

**Assisted run:**

```bash
BRIDGE_FIRMWARE=<ELF SHA256> npm run manual:h8
```

**Procedure without the script:** Follow H7, but keep the cable disconnected for more than 2 minutes.

**Expected:** The same as H7. After 120 s, ESP-NETIF raises an internal lost-IP event, but the static IP must still return when the link does.

## H9: Repeated Link Recovery (FR-9)

**Requirement:** FR-9. The bridge must recover network connectivity after the Ethernet cable is disconnected and reconnected, without a reboot.

**Project test criterion:** the bridge accepts a new TCP client **within 10 s** of cable reconnection. FR-9 itself states no recovery time: the 10 s target is a project criterion, chosen because autonegotiation and the driver's 2 s link polling alone take about 3 s to 4 s.

**Assisted run:**

```bash
BRIDGE_FIRMWARE=<ELF SHA256> npm run manual:h9            # 5 cycles, outages of at least 10 s
BRIDGE_FIRMWARE=<ELF SHA256> npm run manual:h9 -- 20      # 20 cycles
H9_OUTAGE_S=130 BRIDGE_FIRMWARE=<ELF SHA256> npm run manual:h9 -- 1   # one outage past the 120 s lost-IP timer
```

Start the monitor with `idf.py -p /dev/ttyUSB1 monitor --timestamps`, so the serial log can be compared with the script's timing.

For each cycle, the script:

1. connects a client and checks the echo;
2. asks you to pull the cable at the ESP32, and waits for at least `H9_OUTAGE_S` seconds;
3. asks you to press Enter **and then reconnect the cable immediately**, and starts timing at Enter;
4. probes in parallel, every 200 ms, until the bridge answers or 30 s pass:
   - `ping`, recording the first reply;
   - a TCP connection, recording when the bridge first keeps a client open for 250 ms;
5. checks the echo on the new client, and closes both clients.

A cycle passes when the echo works before and after the outage, ping replies, and the client is accepted within 10 s. Your delay between Enter and the cable going in counts against the bridge, so the measured time can only be longer than the real one.

**Expected serial log for each cycle `n`, with no reset anywhere:**

```text
W NETWORK: Ethernet link down (#n)
W BRIDGE: TCP client receive failed: errno 113       (or, instead of both BRIDGE lines:
I BRIDGE: TCP client closed: socket error             I BRIDGE: TCP client closed: disconnect requested)
I NETWORK: Ethernet link up: 100 Mbps, full duplex, MAC ...
I NETWORK: Ethernet link restored after <x.x> s outage (#n)
I NETWORK: IPv4 ready: 192.168.1.200, mask 255.255.255.0, gateway 192.168.1.1
I NETWORK: Static IP restored after link recovery (#n)
I NETWORK: TCP client connected: ...
```

**ESP-NETIF lost-IP event.** ESP-NETIF starts a 120 s lost-IP timer at a link down. It does not cancel the timer when the link returns, and a later link down does not restart it. The event can therefore appear during a later outage that is much shorter than 120 s, if that outage is still going when the timer started by an earlier link down fires. The time in the line is only how long the current outage has lasted:

```text
W NETWORK: ESP-NETIF lost-IP event during outage #n (<s> s into it; the timer may date from an earlier link down). Static IP 192.168.1.200 kept for reconnection
```

This line is **not a failure on its own**, as long as it says the static IP was kept and the outage still ends with `Static IP restored after link recovery (#n)`. In a short outage it may or may not appear. In an outage longer than 120 s, it must appear once.

Any of these is a failure, even if the cycle passed over TCP:

- `IPv4 does not match the static configuration`;
- `static configuration was not kept`;
- an outage `#n` without `Static IP restored after link recovery (#n)`.

**If a cycle cannot start**, because no client is accepted before the outage, the script records that cycle as FAIL with the error, skips its outage, and goes on to the next cycle. The same applies if a cycle is interrupted. The report is always written, with every cycle's result.

The script writes `docs/test-reports/manual-h9-<time>.md`, with one row per cycle and the minimum, mean and maximum recovery times.

## Recording the Result

A run passes only if every step passes **and** the operator confirms the serial log.

Attach the generated `manual-h7-*.md` or `manual-h8-*.md` file and the serial log to `docs/test-records/hw-validation-fr8.md`. Attach `manual-h9-*.md` and its serial log to the FR-9 hardware validation record.
