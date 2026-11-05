# Hardware Validation Record: FR-6

| Field | Value |
|---|---|
| Record ID | HW-VAL-006 |
| Requirement | FR-6: Text not available in this repository. |
| Project | ESP32 TCP Serial Bridge |
| Test date | 2026-11-02 (UTC-5) |
| Tested by | Project operator |
| Record prepared by | Claude Code, from retained logs |
| Base commit | `78290c9`, with uncommitted changes (`78290c9-dirty`) |

## Purpose and Scope

This record reserves the test record for FR-6. The text of FR-6 is not stored in this repository and was not available when this record was prepared, so no description has been assumed.

## Test Cases

None. No H1 to H8 test has been mapped to FR-6, because its requirement text is needed for that mapping.

## How to Test (Proposed)

FR-6 (*Requirements v1.0*): in both directions, at the FR-5 baud rates, the bridge shall not lose, duplicate or reorder bytes.

These tests are proposed. None has been run for this record. The existing echo tests already check integrity and order, so they can provide the evidence.

**Before you start**

1. Flash the firmware, and note the ELF SHA256.
2. Connect the GPIO32 to GPIO33 jumper.
3. Open the serial monitor and save its log: `idf.py -p /dev/ttyUSB1 monitor --timestamps | tee fr6-test.log`.

| Test | How to run it | What to check | Passes when |
|---|---|---|---|
| FR6-P01, order | `BRIDGE_FIRMWARE=<ELF SHA256> BRIDGE_REPORT_PATH=docs/test-reports/<new file>.md npm test -- -t "H2:"` | Several messages echoed in sequence | They return in the order sent |
| FR6-P02, every byte value | Same, with `-t "H3:"` | 20,480 bytes covering 0x00 to 0xFF | They return unchanged, with no byte missing or repeated |
| FR6-P03, sustained transfer | Same, with `-t "H4:"` | 256,000 bytes, compared by SHA256 | The hashes match, and every `LOSS:` line during the run says `no data loss` |
| FR6-P04, no loss reported | After FR6-P01 to P03: `grep "LOSS:" fr6-test.log \| grep -vc "no data loss"` | The number of `LOSS:` lines that report loss | `0` |
| FR6-P05, 921600 (optional) | Once FR-5 allows 921600, repeat with a USB-UART adapter in place of the jumper, sending a known file both ways and comparing `sha256sum` on each side | The hashes | They match |

With the jumper, a pass covers both directions together. A byte lost in one direction cannot be told apart from one lost in the other.

## Status

**FR-6: PENDING.** No hardware validation is recorded.

## Pending Validation

1. Add the requirement text.
2. Map any applicable evidence from the existing records, then define the remaining tests.
