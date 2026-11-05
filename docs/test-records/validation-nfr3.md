# Validation Record: NFR-3 15-Minute Continuous Run

| Field | Value |
|---|---|
| Record ID | VAL-NFR-003 |
| Requirement | NFR-3: The firmware completes a 15-minute continuous operation test, with free heap stable throughout. Record the method, initial heap, final heap, minimum free heap and results, and identify any memory leak or unexpected memory use. |
| Project | ESP32 TCP Serial Bridge |
| Test date | 2026-11-05, 14:06:33 to 14:21:50 (UTC-5) |
| Record prepared by | Claude Code, from the soak report and the serial log of the same run |
| Firmware | Version `7ffc134`, ELF SHA256 `ae7ba8e58d739ca728c4e468d6e00e24bd2c2622ecb315bd66f916a0604a3259`, verified in flash before the run |
| Status | **PASS** |

## Method

1. Flash the firmware, verify the app image (`esptool verify-flash`), and read the ELF SHA256 back from flash.
2. Reset the board and capture UART0 from boot, with a host timestamp on every line ([2026-11-05_final-run_fwae7ba8e58.log](../evidence/2026-11-05_final-run_fwae7ba8e58.log)).
3. Run `npm test` (H1 to H5), which passed 8 of 8 ([report](../test-reports/automated-test-results-2026-11-05-fw7ffc134.md)).
4. Run `BRIDGE_FIRMWARE=<ELF SHA256> SOAK_MINUTES=15 npm run manual:soak` (`tests/manual/soak.ts`). For 15 minutes, back-to-back sessions each:
   - send a different pseudo-random payload of 311,040 bytes, which fills about 27 s at 115200 baud;
   - read it back through the GPIO32 to GPIO33 jumper;
   - check that the echo is identical.
5. The firmware logs its free heap and minimum-ever heap after every session (`Heap after session`), which gives one heap sample per session. The bridge and server stacks are logged whenever they reach a new low.

The bridge was busy for the whole 15 minutes: each session's 27.3 s transfer was followed by a 0.5 s pause before the next one.

## Results

| Item | Value |
|---|---|
| Duration | 15.3 min (14:06:33 to 14:21:50) |
| Sessions | **33 of 33 passed**, each with identical echo (SHA256 match) |
| Data echoed and verified | 10,264,320 bytes |
| Resets during the run | **0** (one boot at 14:05:48, before the test) |
| `E`, panic, abort or watchdog lines | **0** |
| `LOSS:` reports during the soak | 92, all `no data loss` with every counter at 0 |
| Stack margins | No new low during the soak. Lowest of the whole run: `bridge` 2,760 B, `tcp_server` 2,952 B, `loss_reporter` 2,028 B unused |

### Heap

| Measurement | Free heap |
|---|---|
| First sample after boot (first `npm test` session, 14:06:00) | 254,292 B |
| Initial: last sample before the soak (14:06:30) | 253,856 B |
| First soak session (14:07:01) | 253,868 B |
| **Final soak session (14:21:50)** | **254,284 B** |
| Range over the 33 soak sessions | 253,856 to 254,292 B (436 B) |
| Mean, first half / second half of the soak | 254,182 B / 254,289 B |
| **Minimum free heap ever** | **239,252 B**, reached during `npm test` (H4/H5) and **unchanged during the whole soak** |

All 33 soak samples, in order:

```text
253868 253856 253868 253868 254292 254288 254288 254292 254292 254292 254292
254288 254292 254272 254284 254288 254288 254292 254292 254292 254288 254288
254288 254284 254292 254288 254288 254292 254288 254288 254292 254292 254284
```

## Analysis

- **No leak.** A leak would make free heap fall from session to session. Instead, the final value is 428 B *higher* than the first, the second half's mean is higher than the first half's, and from session 5 onwards the samples stay within 20 B of each other (254,272 to 254,292 B).
- **The first four samples are about 430 B lower**, which is expected. They follow the burst of short `npm test` sessions, whose closed connections lwIP keeps briefly (TIME_WAIT) before freeing them. That is the reason the firmware's comment gives for comparing across sessions.
- **No unexpected use.** The lowest free heap ever (239,252 B) was set by the larger transfers in `npm test` and was not lowered by 15 minutes of sustained traffic. No stack reached a new low during the soak.

## Supporting Evidence From Earlier Runs

| Firmware | Run | Heap |
|---|---|---|
| `ce94f6077` | One boot of more than 3.5 h (H9, 9 cable outages, H1 to H6), 2026-11-05 | 253,876 to 254,636 B after each of 28 sessions, no trend |
| `8d79dbe83` | 200 connect and close cycles (TR-FR8-002 Test G) | 254,372 to 254,376 B |

## Tests

| Test ID | Test | Expected | Actual | Result |
|---|---|---|---|---|
| NFR3-001 | 15 minutes of continuous operation without reset | No reset, panic or watchdog | 15.3 min, 0 resets, 0 error lines | PASS |
| NFR3-002 | Data integrity during the run | Every session's echo identical | 33 of 33, 10,264,320 bytes | PASS |
| NFR3-003 | Free heap stable | No downward trend, no new minimum | 436 B range, final above initial, minimum unchanged | PASS |
| NFR3-004 | No loss reported | Every `LOSS:` line `no data loss` | 92 of 92 during the soak | PASS |

## Limitations

1. Heap is sampled once per session, about every 28 s, not continuously. A transient peak inside a session would show only in the minimum-ever value, which did not change.
2. The run used the loopback jumper at 115200, so the UART carried both directions of the same data.
