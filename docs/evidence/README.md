# Evidence Logs

Raw ESP32 serial logs (UART0) cited by the hardware validation records. They are copies, unchanged, of logs that were originally saved in `build/log/` (which `idf.py fullclean` deletes) or in the repository root. They still contain the monitor's ANSI colour codes. To read one as plain text:

```bash
sed 's/\x1b\[[0-9;]*m//g' <file>
```

| File | Date | Firmware (ELF SHA256) | Contents | Cited by |
|---|---|---|---|---|
| `2026-11-02_h8_fw23f991546.log` | 2026-11-02 | `23f991546…` (`78290c9-dirty`) | One boot, including a 216 s Ethernet outage with recovery | [FR-1](../test-records/hw-validation-fr1.md), [FR-8](../test-records/hw-validation-fr8.md), [FR-9](../test-records/hw-validation-fr9.md) |
| `2026-11-04_fr8-E1_fw8d79dbe83.log` | 2026-11-04 | `8d79dbe83…` (`63cbb71`) | TR-FR8-002 evidence E1 (truncated at 307,200 bytes, as in the original) | [TR-FR8-002](../testing/FR-8-TEST-REPORT.md) |
| `2026-11-04_fr8-E2_fw8d79dbe83.log` | 2026-11-04 | `8d79dbe83…` (`63cbb71`) | TR-FR8-002 evidence E2 | [TR-FR8-002](../testing/FR-8-TEST-REPORT.md) |
| `2026-11-04_fr8-E3_fw8d79dbe83.log` | 2026-11-04 | `8d79dbe83…` (`63cbb71`) | TR-FR8-002 evidence E3 | [TR-FR8-002](../testing/FR-8-TEST-REPORT.md) |
| `2026-11-05_h9-and-npm-test_fwce94f6077.log` | 2026-11-05 | `ce94f6077…` (`63cbb71-dirty`, source committed as `81b7fa7`) | One boot from 09:27:36 to 12:18:27: the H9 run and the `npm test` run of 12:16 | [FR-3](../test-records/hw-validation-fr3.md), [FR-4](../test-records/hw-validation-fr4.md), [FR-6](../test-records/hw-validation-fr6.md), [FR-7](../test-records/hw-validation-fr7.md), [FR-9](../test-records/hw-validation-fr9.md) |
| `2026-11-05_h6_fwce94f6077.log` | 2026-11-05 | `ce94f6077…` | The same boot, continued from 12:18:30 to 13:05:00: the H6 run of 13:03 | [FR-4](../test-records/hw-validation-fr4.md), [FR-7](../test-records/hw-validation-fr7.md), [FR-8](../test-records/hw-validation-fr8.md) |
| `2026-11-05_921600_fwc903d6e80.log` | 2026-11-05 | `c903d6e80…` (921600 build of `c8114ec`) | Boot at 921600 and H2 to H4: H3 and H4 lose bytes, reported as `uart_rx_overflow`. Captured with a host timestamp per line | [FR-5](../test-records/hw-validation-fr5.md), [FR-6](../test-records/hw-validation-fr6.md), [FR-7](../test-records/hw-validation-fr7.md) |
| `2026-11-05_final-run_fwae7ba8e58.log` | 2026-11-05 | `ae7ba8e58…` (`7ffc134`, the final firmware) | Boot, `npm test` (H1 to H5) and the 15-minute NFR-3 soak, 14:05:48 to 14:25:08. One boot. Captured with a host timestamp per line | [FR-5](../test-records/hw-validation-fr5.md), [NFR-1](../test-records/validation-nfr1.md), [NFR-2](../test-records/validation-nfr2.md), [NFR-3](../test-records/validation-nfr3.md) |
