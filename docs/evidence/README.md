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
