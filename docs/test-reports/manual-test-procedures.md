# Manual Hardware Test Procedures: H7 and H8

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

## Recording the Result

A run passes only if every step passes **and** the operator confirms the serial log.

Attach the generated `manual-h7-*.md` or `manual-h8-*.md` file and the serial log to `docs/test-records/hw-validation-fr8.md`.
