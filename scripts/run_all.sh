#!/bin/bash
# Run all SAGE components in separate terminals

set -e

# Check if binaries exist
if [ ! -f "build/src/cal/sage_cal" ]; then
    echo "Error: Binaries not found. Run ./scripts/build.sh first"
    exit 1
fi

echo "Starting SAGE components..."

# Start each component in background
echo "Starting CAL..."
./build/src/cal/sage_cal &
CAL_PID=$!

sleep 1

echo "Starting ADE..."
./build/src/ade/sage_ade &
ADE_PID=$!

sleep 1

echo "Starting RME..."
./build/src/rme/sage_rme &
RME_PID=$!

sleep 1

echo "Starting POE..."
./build/src/poe/sage_poe &
POE_PID=$!

echo ""
echo "SAGE is running!"
echo "PIDs: CAL=$CAL_PID ADE=$ADE_PID RME=$RME_PID POE=$POE_PID"
echo "Press Ctrl+C to shutdown all components"

# Wait for interrupt
trap "kill $CAL_PID $ADE_PID $RME_PID $POE_PID 2>/dev/null; exit" INT TERM

wait
