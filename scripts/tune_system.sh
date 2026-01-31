#!/bin/bash
# System tuning script for production deployment

echo "SAGE System Tuning Script"
echo "=========================="
echo ""
echo "WARNING: This script requires root privileges and will modify system settings."
echo "Only run this on dedicated trading hardware."
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo "Please run as root (sudo)"
    exit 1
fi

echo "1. Checking CPU isolation..."
if grep -q "isolcpus" /proc/cmdline; then
    echo "   ✓ CPU isolation configured"
    grep "isolcpus" /proc/cmdline
else
    echo "   ✗ CPU isolation NOT configured"
    echo "   Add 'isolcpus=2,3,4,5' to GRUB_CMDLINE_LINUX in /etc/default/grub"
    echo "   Then run: sudo update-grub && sudo reboot"
fi

echo ""
echo "2. Checking transparent hugepages..."
THP_STATUS=$(cat /sys/kernel/mm/transparent_hugepage/enabled)
if [[ $THP_STATUS == *"[never]"* ]]; then
    echo "   ✓ Transparent hugepages disabled"
else
    echo "   ✗ Transparent hugepages enabled (causes latency spikes)"
    echo "   Disabling now..."
    echo never > /sys/kernel/mm/transparent_hugepage/enabled
fi

echo ""
echo "3. Checking shared memory limits..."
SHMMAX=$(cat /proc/sys/kernel/shmmax)
if [ $SHMMAX -ge 8589934592 ]; then
    echo "   ✓ Shared memory limit sufficient ($SHMMAX bytes)"
else
    echo "   ✗ Shared memory limit too low ($SHMMAX bytes)"
    echo "   Setting to 8GB..."
    sysctl -w kernel.shmmax=8589934592
fi

echo ""
echo "4. CPU governor check..."
GOVERNOR=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)
if [ "$GOVERNOR" == "performance" ]; then
    echo "   ✓ CPU governor set to performance"
else
    echo "   ✗ CPU governor is $GOVERNOR (should be performance)"
    echo "   Setting to performance..."
    for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        echo performance > $cpu
    done
fi

echo ""
echo "Tuning complete!"
echo ""
echo "BIOS settings (manual):"
echo "  - Disable C-States"
echo "  - Disable Hyper-Threading"
echo "  - Disable Turbo Boost"
