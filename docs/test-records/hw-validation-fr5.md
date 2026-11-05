# Hardware Validation Record: FR-5

| Field | Value |
|---|---|
| Record ID | HW-VAL-005 |
| Requirement | FR-5: Text not available in this repository. |
| Project | ESP32 TCP Serial Bridge |
| Test date | 2026-11-02 (UTC-5) |
| Tested by | Project operator |
| Record prepared by | Claude Code, from retained logs |
| Base commit | `78290c9`, with uncommitted changes (`78290c9-dirty`) |

## Purpose and Scope

This record reserves the test record for FR-5. The text of FR-5 is not stored in this repository and was not available when this record was prepared, so no description has been assumed.

## Test Cases

None. No H1 to H8 test has been mapped to FR-5, because its requirement text is needed for that mapping.

## How to Test (Proposed)

FR-5 (*Requirements v1.0*): UART1 shall run at 8N1 with no hardware flow control. The baud rate shall be set in menuconfig, default 115200. Optionally, it may also work at 921600.

These tests are proposed. None has been run for this record.

| Test | How to run it | What to check | Passes when |
|---|---|---|---|
| FR5-P01, configuration (inspection) | Read `main/uart/uart_driver.c`, and search `sdkconfig` or `idf.py menuconfig` for a UART1 baud option | `.data_bits = UART_DATA_8_BITS`, `.parity = UART_PARITY_DISABLE`, `.stop_bits = UART_STOP_BITS_1`, `.flow_ctrl = UART_HW_FLOWCTRL_DISABLE`, and where the baud rate comes from | 8N1 and no flow control are set, **and** the baud rate comes from a menuconfig option that defaults to 115200. **As of 2026-11-05, the baud rate is hardcoded** (`#define BRIDGE_UART_BAUD_RATE 115200` in `uart_driver.c`), and `sdkconfig` has no option for it, so this check does not pass yet |
| FR5-P02, 115200 8N1 on the wire | Remove the GPIO32 to GPIO33 jumper. Connect a 3.3 V USB-UART adapter (adapter RX to GPIO32, adapter TX to GPIO33, common ground), and open it with `picocom -b 115200 /dev/ttyUSB<n>`. Run `nc -v 192.168.1.200 4001` and type in both terminals | Text typed in `nc` appears in `picocom`, and the reverse. Reopening `picocom` at another rate, for example `-b 57600`, shows garbage | The text is correct only at 115200 |
| FR5-P03, rate | With the jumper back: `BRIDGE_FIRMWARE=<ELF SHA256> BRIDGE_REPORT_PATH=docs/test-reports/<new file>.md npm test -- -t "H4:"` | The measured rate in the report | About 11,520 bytes/s, the rate of 115200 8N1 (10 bits per byte) |
| FR5-P04, 921600 (optional) | Once the baud rate is configurable: set 921600 in menuconfig, then build, flash and repeat FR5-P02 with `picocom -b 921600` | The same as FR5-P02 | The text is correct at 921600. H3 and H4 cannot be used here unchanged, because their timing check assumes 115200 (`BAUD_BYTES_PER_SECOND` in `tests/support/config.ts`) |

## Status

**FR-5: PENDING.** No hardware validation is recorded.

## Pending Validation

1. Add the requirement text.
2. Map any applicable evidence from the existing records, then define the remaining tests.
