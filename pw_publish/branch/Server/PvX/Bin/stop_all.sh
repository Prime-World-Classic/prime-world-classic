#!/bin/bash
# Stop all UniServerApp processes

echo "=== Stopping all UniServerApp processes ==="
PIDS=$(pgrep -f "UniServerApp" 2>/dev/null)
if [ -z "$PIDS" ]; then
    echo "No UniServerApp processes found."
    exit 0
fi

echo "Killing $(echo "$PIDS" | wc -l) processes..."
echo "$PIDS" | xargs kill -TERM 2>/dev/null
sleep 2

# Force kill survivors
PIDS=$(pgrep -f "UniServerApp" 2>/dev/null)
if [ -n "$PIDS" ]; then
    echo "Force killing remaining processes..."
    echo "$PIDS" | xargs kill -KILL 2>/dev/null
fi

echo "All done."
