# Main Component Configuration
#
# Registers the source files required to build the application
# and defines the directories where header files are located.


# Main Component Configuration
#
# Registers the source files required to build the application
# and defines the directories where header files are located.

idf_component_register(
    SRCS
        "main.c"
        "network/network.c"
        "uart/uart_driver.c"
        "bridge/bridge.c"
        "bridge/loss_reporter.c"
        "bridge/loss_stats.c"

    INCLUDE_DIRS
        "."
        "network"
        "uart"
        "bridge"

    PRIV_REQUIRES
        esp_app_format
        esp_netif
        esp_eth
        esp_event
        esp_driver_uart
        esp_hal_uart
        esp_timer
        lwip
)

# FR-7: the UART driver drops events without notice when its event queue is
# full. Wrapping the ISR queue send lets uart_driver.c count those drops.
# Without this option the build fails, because __real_xQueueGenericSendFromISR
# is then undefined.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=xQueueGenericSendFromISR")
