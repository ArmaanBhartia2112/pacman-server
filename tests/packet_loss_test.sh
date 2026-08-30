#!/bin/bash
# packet_loss_test.sh
# Runs the load test with simulated network conditions.
# On macOS: uses pfctl + dnctl (Network Link Conditioner via command line)
# On Linux: uses tc netem
#
# Usage: ./packet_loss_test.sh [--clients N] [--duration S] [--latency MS] [--loss PCT]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"
SERVER_BIN="$BUILD_DIR/server/pacman_server"
LOADTEST_BIN="$BUILD_DIR/tests/load_test"

# Defaults
CLIENTS=8
DURATION=30
LATENCY_MS=100
LOSS_PCT=10
TCP_PORT=7777
UDP_PORT=7778
TICK_RATE=25

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --clients)  CLIENTS=$2;  shift 2 ;;
        --duration) DURATION=$2; shift 2 ;;
        --latency)  LATENCY_MS=$2; shift 2 ;;
        --loss)     LOSS_PCT=$2; shift 2 ;;
        *)          echo "Unknown arg: $1"; exit 1 ;;
    esac
done

# Check binaries exist
if [ ! -f "$SERVER_BIN" ]; then
    echo "ERROR: Server binary not found at $SERVER_BIN"
    echo "Run: cmake --build $BUILD_DIR first"
    exit 1
fi
if [ ! -f "$LOADTEST_BIN" ]; then
    echo "ERROR: Load test binary not found at $LOADTEST_BIN"
    exit 1
fi

echo "======================================================"
echo "Multiplayer Pacman Network Conditioning Test"
echo "Clients:    $CLIENTS"
echo "Duration:   $DURATION seconds"
echo "Latency:    ${LATENCY_MS}ms"
echo "Loss:       ${LOSS_PCT}%"
echo "======================================================"

# ============================================================
# Detect OS and apply network conditioning
# ============================================================
OS="$(uname -s)"
NETEM_APPLIED=0

apply_netem_linux() {
    echo "[netem] Applying tc netem on loopback: delay ${LATENCY_MS}ms loss ${LOSS_PCT}%"
    sudo tc qdisc add dev lo root netem \
        delay "${LATENCY_MS}ms" \
        loss "${LOSS_PCT}%"
    NETEM_APPLIED=1
}

remove_netem_linux() {
    if [ $NETEM_APPLIED -eq 1 ]; then
        echo "[netem] Removing tc netem"
        sudo tc qdisc del dev lo root 2>/dev/null || true
        NETEM_APPLIED=0
    fi
}

apply_netem_macos() {
    echo "[dnctl] Applying macOS network conditioning: delay ${LATENCY_MS}ms loss ${LOSS_PCT}%"
    # Use dnctl + pfctl to add a dummynet pipe on loopback
    # Pipe 1: outbound from server port
    LOSS_FRAC=$(echo "scale=4; $LOSS_PCT / 100" | bc)
    sudo dnctl pipe 1 config delay $LATENCY_MS plr $LOSS_FRAC bw 100Mbit/s 2>/dev/null || true

    # Create pfctl anchor
    PFCONF=$(mktemp /tmp/pacman_pf.XXXXXX.conf)
    cat > "$PFCONF" <<EOF
dummynet-anchor "pacman_test"
anchor "pacman_test"
EOF
    PFANCHOR=$(mktemp /tmp/pacman_anchor.XXXXXX.conf)
    cat > "$PFANCHOR" <<EOF
dummynet out proto udp from any port $UDP_PORT to any pipe 1
dummynet in  proto udp from any to any port $UDP_PORT pipe 1
EOF
    sudo pfctl -f "$PFCONF" 2>/dev/null || true
    sudo pfctl -a pacman_test -f "$PFANCHOR" 2>/dev/null || true
    sudo pfctl -e 2>/dev/null || true
    rm -f "$PFCONF" "$PFANCHOR"
    NETEM_APPLIED=1
    echo "[dnctl] Conditioning applied"
}

remove_netem_macos() {
    if [ $NETEM_APPLIED -eq 1 ]; then
        echo "[dnctl] Removing macOS network conditioning"
        sudo pfctl -a pacman_test -F rules 2>/dev/null || true
        sudo dnctl pipe 1 delete 2>/dev/null || true
        NETEM_APPLIED=0
    fi
}

cleanup() {
    echo ""
    echo "[cleanup] Stopping server..."
    kill $SERVER_PID 2>/dev/null || true
    if [ "$OS" = "Linux" ]; then
        remove_netem_linux
    else
        remove_netem_macos
    fi
}
trap cleanup EXIT INT TERM

# ============================================================
# Start server
# ============================================================
echo ""
echo "[test] Starting server (tick rate ${TICK_RATE}Hz)..."
"$SERVER_BIN" \
    --tcp-port $TCP_PORT \
    --udp-port $UDP_PORT \
    --tick-rate $TICK_RATE \
    > /tmp/pacman_server.log 2>&1 &
SERVER_PID=$!
echo "[test] Server PID: $SERVER_PID"
sleep 1  # Give server time to bind

# Check server is running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "ERROR: Server failed to start. Check /tmp/pacman_server.log"
    cat /tmp/pacman_server.log
    exit 1
fi

# ============================================================
# Baseline run (no conditioning)
# ============================================================
echo ""
echo "[test] Phase 1: Baseline run (no network conditioning)..."
cd "$BUILD_DIR"
"$LOADTEST_BIN" \
    --clients $CLIENTS \
    --duration 15 \
    --tick-rate $TICK_RATE \
    --latency-ms 0 \
    --loss-pct 0 \
    > /tmp/baseline_metrics.txt 2>&1
cat /tmp/baseline_metrics.txt
cp metrics_result.json /tmp/baseline_result.json 2>/dev/null || true

# ============================================================
# Apply network conditioning
# ============================================================
echo ""
echo "[test] Phase 2: Applying network conditioning..."
if [ "$OS" = "Linux" ]; then
    apply_netem_linux
else
    apply_netem_macos
fi
sleep 1

# ============================================================
# Conditioned run
# ============================================================
echo ""
echo "[test] Phase 3: Conditioned run (${LATENCY_MS}ms delay, ${LOSS_PCT}% loss)..."
"$LOADTEST_BIN" \
    --clients $CLIENTS \
    --duration $DURATION \
    --tick-rate $TICK_RATE \
    --latency-ms $LATENCY_MS \
    --loss-pct $LOSS_PCT \
    > /tmp/conditioned_metrics.txt 2>&1
cat /tmp/conditioned_metrics.txt
cp metrics_result.json /tmp/conditioned_result.json 2>/dev/null || true

# Remove conditioning
if [ "$OS" = "Linux" ]; then
    remove_netem_linux
else
    remove_netem_macos
fi

# ============================================================
# Compare and produce report data
# ============================================================
echo ""
echo "======================================================"
echo "TEST COMPLETE"
echo "Baseline result:    /tmp/baseline_result.json"
echo "Conditioned result: /tmp/conditioned_result.json"
echo "Server log:         /tmp/pacman_server.log"
echo "======================================================"

# Output server log tail
echo ""
echo "--- Server log (last 20 lines) ---"
tail -20 /tmp/pacman_server.log

echo ""
echo "Run: python3 generate_report.py to produce REPORT.md"
