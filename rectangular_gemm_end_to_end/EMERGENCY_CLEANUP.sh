#!/bin/bash
# Emergency cleanup script for excessive QEMU/XSIM processes
# This script will kill all running hardware emulation processes

set -e

echo "============================================================================"
echo "EMERGENCY SYSTEM CLEANUP - Stopping Excessive Emulation Processes"
echo "============================================================================"
echo ""

# Count running processes
QEMU_COUNT=$(ps aux | grep -E "qemu-system-aarch64|qemu-system-microblaze" | grep -v grep | wc -l)
XSIM_COUNT=$(ps aux | grep -E "xsim|xsimk" | grep -v grep | wc -l)

echo "Current Status:"
echo "  QEMU processes: $QEMU_COUNT"
echo "  XSIM processes: $XSIM_COUNT"
echo ""

if [ "$QEMU_COUNT" -eq 0 ] && [ "$XSIM_COUNT" -eq 0 ]; then
    echo "No emulation processes found. System is clean."
    exit 0
fi

echo "WARNING: This will kill ALL running QEMU and XSIM processes!"
echo "Press Ctrl+C within 5 seconds to cancel..."
sleep 5

echo ""
echo "Stopping processes..."

# Kill QEMU processes
if [ "$QEMU_COUNT" -gt 0 ]; then
    echo "Killing $QEMU_COUNT QEMU processes..."
    pkill -9 -f "qemu-system-aarch64" || true
    pkill -9 -f "qemu-system-microblaze" || true
    sleep 2
fi

# Kill XSIM processes
if [ "$XSIM_COUNT" -gt 0 ]; then
    echo "Killing $XSIM_COUNT XSIM processes..."
    pkill -9 -f "xsim" || true
    pkill -9 -f "xsimk" || true
    sleep 2
fi

# Kill stuck make processes
echo "Killing stuck make processes..."
pkill -9 -f "make.*run.*hw_emu" || true

# Verify cleanup
sleep 2
QEMU_AFTER=$(ps aux | grep -E "qemu-system-aarch64|qemu-system-microblaze" | grep -v grep | wc -l)
XSIM_AFTER=$(ps aux | grep -E "xsim|xsimk" | grep -v grep | wc -l)

echo ""
echo "Cleanup complete!"
echo "  QEMU processes remaining: $QEMU_AFTER"
echo "  XSIM processes remaining: $XSIM_AFTER"
echo ""

# Show current system status
echo "Current System Status:"
free -h | grep -E "Mem|Swap"
echo ""
uptime
