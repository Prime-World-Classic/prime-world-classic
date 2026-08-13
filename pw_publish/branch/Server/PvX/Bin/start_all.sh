#!/bin/bash
# Start UniServerApp coordinator (Linux)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$SCRIPT_DIR/UniServerApp"
LOG_DIR="$SCRIPT_DIR/logs"

mkdir -p "$LOG_DIR"

if [ ! -x "$BIN" ]; then
    echo "ERROR: $BIN not found or not executable"
    exit 1
fi

echo "Starting coordinator..."
nohup "$BIN" coordinator > "$LOG_DIR/coordinator.log" 2>&1 &
echo "PID: $!"
echo "Log: $LOG_DIR/coordinator.log"
