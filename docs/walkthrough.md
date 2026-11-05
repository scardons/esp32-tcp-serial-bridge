# D-3 Walkthrough: Agenda and Live Change

A 15-minute call that explains the design and demonstrates one small code change, live, on the hardware.

| Field | Value |
|---|---|
| Status | **PENDING**: the call has not taken place yet |
| Date | *(fill in after the call)* |
| Attendees | *(fill in after the call)* |
| Firmware before and after the demo | Version `7ffc134`, ELF SHA256 `ae7ba8e58d739ca728c4e468d6e00e24bd2c2622ecb315bd66f916a0604a3259` |

## Agenda (15 minutes)

| Minutes | Topic | What to show |
|---|---|---|
| 0 to 2 | Problem, requirements and hardware | The README diagram. The board with the GPIO32 to GPIO33 jumper. FR-1 to FR-9 and NFR-1 to NFR-5 in one sentence each |
| 2 to 6 | Architecture | `main/network/`, `main/uart/` and `main/bridge/`. The data path in `bridge.c` (`run_session`). The three tasks and their priorities. Backpressure through the 100 ms UART TX budget. The single client ([Design Decisions](../README.md#design-decisions)) |
| 6 to 8 | Robustness | Keepalive and the 11 s half-open detection (FR-8). Link recovery and the numbered outage log (FR-9). The `LOSS:` report (FR-7) |
| 8 to 10 | Testing strategy and evidence | `bash tests/host/run.sh` (real `bridge.c` against stubs, sanitizers). H1 to H9. The 15-minute soak. The records in `docs/test-records/` and the logs in `docs/evidence/`. The status tables in the README |
| 10 to 14 | **Live change** | Below |
| 14 to 15 | Limitations, improvements, questions | [Known Limitations](../README.md#known-limitations) and [Potential Improvements](../README.md#potential-improvements) |

## Live Change: Log the Session Duration

When a session ends, the bridge logs `TCP client closed: <reason>`. The change adds how long the session lasted:

```text
before:  I (12345) BRIDGE: TCP client closed: disconnected by peer
after:   I (12345) BRIDGE: TCP client closed: disconnected by peer, after 1532 ms
```

**Why this change:**
- **It's safe.** It only adds data to one log line. The data path, timing, buffers and every requirement are unaffected, and no automated test depends on that line's text.
- **It's quick.** The build is incremental, and the result shows in the monitor seconds after flashing.
- **It's meaningful.** It touches the session loop, which is the core of the design.

**The edit,** in `main/bridge/bridge.c`, in `bridge_task()`, around the call to `run_session()`:

```c
        const int64_t session_start_us = esp_timer_get_time();
        const char *reason = run_session(fd, &pending_tx, &pending_uart);

        record_session_end(reason, &pending_tx, &pending_uart);

        ESP_LOGI(TAG, "TCP client closed: %s, after %" PRId64 " ms", reason,
                 (esp_timer_get_time() - session_start_us) / 1000);
```

`esp_timer.h` and `inttypes.h` are already included in `bridge.c`.

### Steps During the Call

```bash
git switch demo/session-duration           # created before the call, never main
# make the edit above
idf.py build                               # incremental
idf.py -p /dev/ttyUSB1 flash monitor
# second terminal:
nc -v 192.168.1.200 4001                   # type a few characters, wait, then Ctrl+C
BRIDGE_FIRMWARE=<new ELF SHA256> npm test -- -t "H1:"
```

The change works when the monitor shows `TCP client closed: disconnected by peer, after <n> ms`, with `<n>` matching how long `nc` was connected, and H1 passes.

### Before the Call

1. **Keep a copy of the validated binaries.** A rebuild never reproduces the same ELF SHA256, because the build time and version are embedded in it. With the `7ffc134` build in `build/` (check it with `sha256sum build/tcp_serial_bridge.elf`), run `cp -r build ~/fw-7ffc134`.
2. Flash the validated firmware and confirm its boot line: `MAIN: Firmware: version 7ffc134, ELF SHA256 ae7ba8e58…`.
3. Create the demo branch (`git switch -c demo/session-duration`) and run `idf.py build` once, so the live build is incremental.
4. Check the jumper. Close every other TCP client and serial monitor.
5. Open three terminals: the editor, `idf.py monitor`, and a shell for `nc` and `npm test`.
6. Have a fallback ready in case the live build fails, for example the change already built on the demo branch, or a recording.

### After the Call

```bash
git switch main
git branch -D demo/session-duration
cd ~/fw-7ffc134 && python -m esptool --chip esp32 -p /dev/ttyUSB1 write-flash @flash_args
```

This flashes back the exact validated binary. Its boot line must show `ELF SHA256 ae7ba8e58`.

Then fill in the date and attendees above, and set the status to **DONE**. The demo branch is not merged: the validated firmware stays `7ffc134`.
