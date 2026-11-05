# Evidence Logs

Raw ESP32 serial logs (UART0) cited by the hardware validation records. They are copies, unchanged, of logs that were originally saved in `build/log/` (which `idf.py fullclean` deletes) or in the repository root. They still contain the monitor's ANSI colour codes. To read one as plain text:

```bash
sed 's/\x1b\[[0-9;]*m//g' <file>
```

| File | Date | Firmware (ELF SHA256) | Contents | Cited by |
|---|---|---|---|---|
| `2026-11-02_h8_fw23f991546.log` | 2026-11-02 | `23f991546…` (`78290c9-dirty`) | One boot, including a 216 s Ethernet outage with recovery | [FR-1](../test-records/hw-validation-fr1.md), [FR-8](../test-records/hw-validation-fr8.md), [FR-9](../test-records/hw-validation-fr9.md) |
| `2026-11-04_fr8-E1_fw8d79dbe83.log` | 2026-11-04 | `8d79dbe83…` (`63cbb71`) | TR-FR8-002 evidence E1 (truncated at 307,200 bytes, as in the original) | [TR-FR8-002](../testing/FR-8-TEST-REPORT.md) |
| `2026-11-04_fr8-E2_fw8d79dbe83.log` | 2026-11-04 | `8d79dbe83…` (`63cbb71`) | TR-FR8-002 evidence E2. Originally `fr8-test-b.log` in the repository root, which was an identical copy and has been removed | [TR-FR8-002](../testing/FR-8-TEST-REPORT.md) |
| `2026-11-04_fr8-E3_fw8d79dbe83.log` | 2026-11-04 | `8d79dbe83…` (`63cbb71`) | TR-FR8-002 evidence E3 | [TR-FR8-002](../testing/FR-8-TEST-REPORT.md) |
| `2026-11-05_h9-and-npm-test_fwce94f6077.log` | 2026-11-05 | `ce94f6077…` (`63cbb71-dirty`, source committed as `81b7fa7`) | One boot from 09:27:36 to 12:18:27: the H9 run and the `npm test` run of 12:16 | [FR-3](../test-records/hw-validation-fr3.md), [FR-4](../test-records/hw-validation-fr4.md), [FR-6](../test-records/hw-validation-fr6.md), [FR-7](../test-records/hw-validation-fr7.md), [FR-9](../test-records/hw-validation-fr9.md) |
| `2026-11-05_h6_fwce94f6077.log` | 2026-11-05 | `ce94f6077…` | The same boot, continued from 12:18:30 to 13:05:00: the H6 run of 13:03 | [FR-4](../test-records/hw-validation-fr4.md), [FR-7](../test-records/hw-validation-fr7.md), [FR-8](../test-records/hw-validation-fr8.md) |
| `2026-11-05_921600_fwc903d6e80.log` | 2026-11-05 | `c903d6e80…` (921600 build of `c8114ec`) | Boot at 921600 and H2 to H4: H3 and H4 lose bytes, reported as `uart_rx_overflow`. Captured with a host timestamp per line | [FR-5](../test-records/hw-validation-fr5.md), [FR-6](../test-records/hw-validation-fr6.md), [FR-7](../test-records/hw-validation-fr7.md) |
| `2026-11-05_final-run_fwae7ba8e58.log` | 2026-11-05 | `ae7ba8e58…` (`7ffc134`, the final firmware) | Boot, `npm test` (H1 to H5) and the 15-minute NFR-3 soak, 14:05:48 to 14:25:08. One boot. Captured with a host timestamp per line | [FR-5](../test-records/hw-validation-fr5.md), [NFR-1](../test-records/validation-nfr1.md), [NFR-2](../test-records/validation-nfr2.md), [NFR-3](../test-records/validation-nfr3.md) |
| `2026-11-05_fr7-004_link-loss_fwae7ba8e58.log` | 2026-11-05 | `ae7ba8e58…` (`7ffc134`, the final firmware) | Boot, then continuous 60 s transfer sessions with the cable pulled mid-transfer at 14:57:20 and the link restored 51.9 s later (HW-FR7-004), 14:56:46 to 15:01:36. Captured with a host timestamp per line | [FR-7](../test-records/hw-validation-fr7.md), [NFR-2](../test-records/validation-nfr2.md) |
| `2026-11-05_fr7-009_break_diag-fw42e1853cf.log` | 2026-11-05 | `42e1853cf…` (diagnostic build `28f96fb`, branch `diag/fr7-break`) | Boot and three UART1 breaks looped through the jumper (HW-FR7-009), 15:12:02 to 15:13:12. Captured with a host timestamp per line | [FR-7](../test-records/hw-validation-fr7.md) |
| `2026-11-05_monitor-session_fwce94f6077.log` | 2026-11-05 | `ce94f6077…` | The complete monitor session, 09:27:36 to 14:01:17, originally `fr9-h9-test.log` in the repository root. It contains the H9 run, `npm test` at 12:16 and H6 at 13:03. The two files above are excerpts of it | [FR-3](../test-records/hw-validation-fr3.md), [FR-4](../test-records/hw-validation-fr4.md), [FR-6](../test-records/hw-validation-fr6.md), [FR-7](../test-records/hw-validation-fr7.md), [FR-9](../test-records/hw-validation-fr9.md), [H9 report](../test-reports/manual-h9-2026-11-05T14-51-27-137Z.md) |

## Network Captures

`pcap/` holds packet captures of the H6 stalled-client test on 2026-11-03, taken on the test PC. They were originally `h6.pcap`, `h6-bridged.pcap` and `h6-fixed.pcap` in the repository root, and are named here by their first packet's time. The firmware that day was `1044bbf1b` (`0da27aa-dirty`); the match to a firmware build is inferred from the date, not recorded in the capture. Open them with `wireshark` or `tcpdump -r <file>`.

| File | First packet |
|---|---|
| `pcap/2026-11-03_1210_h6.pcap` | 2026-11-03 12:10:57 |
| `pcap/2026-11-03_1341_h6-bridged.pcap` | 2026-11-03 13:41:43 |
| `pcap/2026-11-03_1750_h6-fixed.pcap` | 2026-11-03 17:50:16 |
