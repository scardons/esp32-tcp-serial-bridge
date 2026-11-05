# Validation Record: NFR-5 Error Handling and Build

| Field | Value |
|---|---|
| Record ID | VAL-NFR-005 |
| Requirement | NFR-5: All relevant function return codes are checked: ESP-IDF API calls, socket operations, memory allocation, UART operations and task creation. Errors are handled appropriately. The project builds without errors with ESP-IDF 6.1. |
| Project | ESP32 TCP Serial Bridge |
| Date | 2026-11-05 |
| Record prepared by | Claude Code, from a review of every call in `main/` and the build output |
| Source | Commit `7ffc134` |
| Status | **PASS** |

## Return Code Review

| Area | Calls | Handling |
|---|---|---|
| ESP-NETIF and events | `esp_netif_init`, `esp_event_loop_create_default` (`ESP_ERR_INVALID_STATE` accepted), `esp_event_handler_register` ×3 | `ESP_RETURN_ON_ERROR` / `ESP_GOTO_ON_ERROR`, with `network_cleanup()` |
| Ethernet | MAC and PHY creation, `esp_eth_driver_install`, `esp_eth_new_netif_glue`, `esp_netif_attach`, `esp_netif_dhcpc_stop`, `esp_netif_set_ip_info`, `esp_eth_start` | Each one is checked. A failed start stops the driver so it can be uninstalled |
| Ethernet details | `esp_eth_ioctl` (MAC, speed, duplex) | Checked. The log line falls back to "details are unavailable" |
| Tasks and queues | `xTaskCreate` ×3 (`bridge`, `tcp_server`, `loss_reporter`), `xQueueCreate`, `xSemaphoreCreateBinary` | Checked. On failure the queue or semaphore is freed and `ESP_ERR_NO_MEM` is returned |
| Memory allocation | No `malloc` or `calloc` in application code. Buffers are static | Not applicable |
| UART | `uart_driver_install`, `uart_param_config`, `uart_set_pin`, interrupt configuration | `ESP_RETURN_ON_ERROR` |
| UART data | `uart_driver_write`, `uart_driver_read` | Checked: a failure ends the session as `UART error` |
| Sockets: server | `socket`, `bind`, `listen`, `accept` | Checked. `accept` tells retryable errors (`EAGAIN`, `ECONNABORTED`, …) and socket exhaustion (`ENFILE`, `EMFILE`) apart from fatal ones |
| Sockets: session | `send`, `recv`, `select` | Checked. Temporary errors are retried, and the others end the session with a logged errno |
| Socket options | Keepalive `setsockopt` ×4 | Each is set and read back. Any failure rejects the client (FR-8) |
| Socket options | `TCP_NODELAY`, `SO_REUSEADDR` | Checked and logged as warnings (not fatal) |
| Start-up | `start_bridge`, `network_init`, `network_start_server` in `app_main` | Checked and logged. Ethernet stays up if the bridge fails, so the board can still be reached for diagnosis (a deliberate choice, commented in `main.c`) |

### Returns Deliberately Not Checked

| Call | Reason |
|---|---|
| `close()` | No recovery is possible, and the descriptor is released either way |
| `esp_event_handler_unregister` in `network_cleanup()` | Best-effort cleanup on a path that has already failed. Some handlers may never have been registered |
| `esp_eth_stop` after a failed `esp_eth_start` | Only needed to allow the uninstall. Commented in the code |
| `inet_ntop`, `esp_app_get_elf_sha256` | Their output buffers are pre-initialised, so a failure only affects a log line |
| The length returned by `loss_stats_format` | The line is truncation-safe, and its longest form (about 330 characters) fits the 384-byte buffer |

**Change made for NFR-5** (commit `f13d20a`): `esp_eth_del_netif_glue` in `network_cleanup()` is now checked and logged instead of being ignored.

## Tests

| Test ID | Test | Expected | Actual | Result |
|---|---|---|---|---|
| NFR5-001 | Return code review of `main/` | Every relevant call checked | As above | PASS |
| NFR5-002 | `idf.py build` with ESP-IDF v6.1, default configuration | No errors | Commit `7ffc134`: built with no errors and no warnings. ELF SHA256 `ae7ba8e58d739ca728c4e468d6e00e24bd2c2622ecb315bd66f916a0604a3259` | PASS |
| NFR5-003 | `idf.py build` with `CONFIG_BRIDGE_UART_BAUD_RATE=921600` | No errors | Commit `c8114ec` (same firmware source): built with no errors and no warnings | PASS |
| NFR5-004 | Error paths in the bridge, by host test | Failing and partial sends, peer reset, stall and UART errors end the session cleanly | Bridge logic 90/0 with ASan and UBSan, and with TSan ([VAL-NFR-004](validation-nfr4.md)) | PASS |
| NFR5-005 | Socket option failures, by host test | Each keepalive failure rejects the client | FR-8 keepalive suite 25/0 (K2 to K7) | PASS |

## Limitations

1. The start-up failure paths (`network_init`, task creation) were reviewed but not exercised on hardware, since forcing them would need fault injection.
