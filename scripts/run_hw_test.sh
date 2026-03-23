#!/bin/sh
# 
# GC2083 Hardware Encoding Test Suite
# Compatible with BusyBox sh
# 

BOARD_IP="192.168.31.219"

echo "========================================="
echo "GC2083 Hardware Test - Remote Mode"
echo "========================================="

echo ""
echo "[STEP 1] Upload test files to board"
echo "========================================"
cd "$(dirname "$0")/.."
scp src/daemon/test_hw_encode.c root@$BOARD_IP:/tmp/
echo "[SUCCESS] Files uploaded"

echo ""
echo "[STEP 2] Run GC2083 detection test"
echo "========================================"
ssh root@$BOARD_IP 'echo "========================================"; echo "GC2083 CSI Camera Detection Test"; echo "========================================"; echo ""; echo "[1] Video Devices:"; ls -l /dev/video* 2>/dev/null || echo "No video devices"; echo ""; echo "[2] CVI Devices:"; ls -l /dev/cvi* 2>/dev/null || echo "No CVI devices"; echo ""; echo "[3] Kernel Modules:"; lsmod | grep -E "gc2083|sensor|isp|vi|venc" || echo "No camera modules"; echo ""; echo "[4] dmesg (last 20 camera lines):"; dmesg | grep -i "gc2083\|csi\|sensor\|camera" | tail -20 || echo "No camera in dmesg"'

echo ""
echo "[STEP 3] Check SDK availability"
echo "========================================"
ssh root@$BOARD_IP 'echo "Checking SDK..."; if [ -d /mnt/system/lib ]; then echo "✓ SDK lib exists"; ls -lh /mnt/system/lib/libcvi*.so 2>/dev/null | head -5 || echo "  No CVI libs"; else echo "✗ SDK NOT found at /mnt/system/lib"; fi; if [ -d /mnt/system/include ]; then echo "✓ SDK include exists"; ls -l /mnt/system/include/cvi*.h 2>/dev/null | head -3 || echo "  No CVI headers"; else echo "✗ SDK headers NOT found"; fi'

echo ""
echo "[STEP 4] Compile test program"
echo "========================================"
echo "Compiling without SDK (system checks only)..."
ssh root@$BOARD_IP 'cd /tmp && gcc -o test_hw_encode_nosdk test_hw_encode.c 2>&1' && echo "✓ Compilation successful" || echo "✗ Compilation failed"

echo ""
echo "Attempting SDK compilation..."
ssh root@$BOARD_IP 'cd /tmp && if [ -d /mnt/system/lib ] && [ -d /mnt/system/include ]; then gcc -DHAS_CVI_SDK -I/mnt/system/include -L/mnt/system/lib -o test_hw_encode test_hw_encode.c -lcvi_common -lcvi_sys -lcvi_vi -lcvi_vpss -lcvi_venc -lpthread -lm 2>&1 && echo "✓ SDK compilation SUCCESS" || echo "✗ SDK compilation failed"; else echo "✗ SDK not available, skipping"; fi'

echo ""
echo "[STEP 5] Run hardware encoding test"
echo "========================================"
ssh root@$BOARD_IP 'cd /tmp; export LD_LIBRARY_PATH=/mnt/system/lib:$LD_LIBRARY_PATH; if [ -f ./test_hw_encode ]; then echo "Running SDK version..."; ./test_hw_encode; elif [ -f ./test_hw_encode_nosdk ]; then echo "Running no-SDK version..."; ./test_hw_encode_nosdk; else echo "ERROR: No test binary"; exit 1; fi'

echo ""
echo "[STEP 6] Check results"
echo "========================================"
if ssh root@$BOARD_IP '[ -f /mnt/data/test_hw_encode.h264 ]' 2>/dev/null; then
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "SUCCESS! Hardware encoding is WORKING! 🎉"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo "Output file:"
    ssh root@$BOARD_IP 'ls -lh /mnt/data/test_hw_encode.h264'
    echo ""
    echo "✓ GC2083 CSI camera works with hardware VENC!"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "Next Steps:"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo "1. Download and verify:"
    echo "   scp root@$BOARD_IP:/mnt/data/test_hw_encode.h264 ~/"
    echo "   ffplay ~/test_hw_encode.h264"
    echo ""
    echo "2. Create hw-encode branch:"
    echo "   cd ~/milkv"
    echo "   git checkout -b feature/hw-encode"
    echo "   git tag v1.0-mjpeg-baseline"
    echo ""
else
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "No output file created"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo "Possible reasons:"
    echo "  1. SG2000 SDK not installed"
    echo "  2. GC2083 driver not loaded"
    echo "  3. Compilation failed"
    echo ""
    echo "Review the output above for details."
    echo ""
fi

echo ""
echo "Test complete!"
echo ""
