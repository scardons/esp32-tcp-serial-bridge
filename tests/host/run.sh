#!/usr/bin/env bash
# Builds firmware sources for the PC, against stubs, and tests them. No ESP32 is needed.
#   tests/host/run.sh          All suites, with ASan and UBSan
#   tests/host/run.sh quick    Without sanitizers, skipping the real 10-second period
#   tests/host/run.sh tsan     With ThreadSanitizer
set -u
cd "$(dirname "$0")/../.."

MODE="${1:-all}"
HOST=tests/host
OUT=$HOST/build
INCLUDES="-I$HOST/stubs -Imain/bridge -Imain/uart"
WRAP="-Dsend=test_send -Dclose=test_close -Dselect=test_select"
WARN="-Wall -Wextra -Wno-unused-parameter"
BRIDGE="main/bridge/bridge.c main/bridge/loss_stats.c"
LOSS="main/bridge/loss_stats.c main/bridge/loss_reporter.c main/uart/uart_driver.c"

mkdir -p "$OUT"

case "$MODE" in
    # The event flood is slow under TSan, so only its timing limit is relaxed.
    tsan) FLAGS="-g -O1 -fsanitize=thread -DTIMING_LIMIT_S=2.0"; RUN="setarch -R" ;;
    quick) FLAGS="-g -O1"; RUN="" ;;
    *) FLAGS="-g -O1 -fsanitize=address,undefined"; RUN="" ;;
esac

build() {
    gcc $FLAGS $WARN $INCLUDES "$@" -lpthread || exit 2
}

status=0

run() {
    echo "=== $1"
    shift
    $RUN "$@" 2> "$OUT/stderr.log"
    local code=$?
    if grep -qE 'ERROR: |WARNING: ThreadSanitizer|runtime error' "$OUT/stderr.log"; then
        grep -E 'ERROR: |WARNING: ThreadSanitizer|runtime error' "$OUT/stderr.log" | head
        code=1
    fi
    [ "$code" -eq 0 ] || status=1
}

build -DLOSS_REPORT_PERIOD_US=200000 $LOSS $HOST/loss_test.c $HOST/freertos_stub.c -o "$OUT/loss_fast"
run "FR-7 counters and reporter, 200 ms period" "$OUT/loss_fast"

if [ "$MODE" != quick ]; then
    build -DLOSS_REPORT_PERIOD_US=10000000 $LOSS $HOST/loss_test.c $HOST/freertos_stub.c -o "$OUT/loss_real"
    run "FR-7 reporter, real 10 s period" "$OUT/loss_real"
fi

# The wrappers apply to bridge.c only, so it is built apart from the test.
for source in $BRIDGE; do
    gcc -c $FLAGS $WARN $WRAP $INCLUDES "$source" -o "$OUT/$(basename "$source" .c).o" || exit 2
done
gcc -c $FLAGS -w $INCLUDES $HOST/bridge_test.c -o "$OUT/bridge_test.o" || exit 2
gcc -c $FLAGS $WARN $INCLUDES $HOST/freertos_stub.c -o "$OUT/freertos_stub.o" || exit 2
gcc $FLAGS "$OUT/bridge.o" "$OUT/loss_stats.o" "$OUT/bridge_test.o" "$OUT/freertos_stub.o" -lpthread -o "$OUT/bridge_test"
run "Bridge logic" "$OUT/bridge_test"

# The socket option wrappers apply to client_socket.c only.
gcc -c $FLAGS $WARN -Dsetsockopt=test_setsockopt -Dgetsockopt=test_getsockopt $INCLUDES -Imain/network \
    main/network/client_socket.c -o "$OUT/client_socket.o" || exit 2
gcc -c $FLAGS $WARN $INCLUDES -Imain/network $HOST/client_socket_test.c -o "$OUT/client_socket_test.o" || exit 2
gcc $FLAGS "$OUT/client_socket.o" "$OUT/client_socket_test.o" -o "$OUT/client_socket_test" || exit 2
run "FR-8 client socket keepalive" "$OUT/client_socket_test"

exit $status
