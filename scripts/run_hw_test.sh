#!/bin/bash
# 
# GC2083 Hardware Encoding Complete Test Suite
# 
# Usage: ./run_hw_test.sh [on-board]
#        on-board: Run this script ON the Milk-V Duo S board
#

set -e

BOARD_IP="192.168.31.219"
LOCAL_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_step() {
    echo -e "\n${GREEN}[STEP $1]${NC} $2"
    echo "========================================"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# ============================================
# Mode 1: Run on development machine
# ============================================
if [ "$1" != "on-board" ]; then
    echo "========================================="
    echo "GC2083 Hardware Test - Remote Mode"
    echo "========================================="
    
    print_step "1" "Upload test scripts to board"
    scp "$LOCAL_DIR/scripts/test_gc2083.sh" root@$BOARD_IP:/tmp/
    scp "$LOCAL_DIR/src/daemon/test_hw_encode.c" root@$BOARD_IP:/tmp/
    scp "$LOCAL_DIR/src/daemon/Makefile.test" root@$BOARD_IP:/tmp/
    print_success "Files uploaded"
    
    print_step "2" "Run GC2083 detection test on board"
    ssh root@$BOARD_IP "chmod +x /tmp/test_gc2083.sh && /tmp/test_gc2083.sh" | tee /tmp/gc2083_detection.log
    
    print_step "3" "Compile and run hardware encoding test"
    echo "Attempting to compile with SDK..."
    
    # Try compiling with SDK first
    ssh root@$BOARD_IP "cd /tmp && make -f Makefile.test test_hw_encode 2>&1" > /tmp/compile.log || {
        print_warning "SDK compilation failed (expected if SDK not installed)"
        echo "Compiling without SDK for system checks only..."
        ssh root@$BOARD_IP "cd /tmp && make -f Makefile.test test_hw_encode_nosdk"
    }
    
    print_step "4" "Run hardware encoding test"
    ssh root@$BOARD_IP "cd /tmp && ./test_hw_encode* 2>&1" | tee /tmp/hw_encode_test.log
    
    print_step "5" "Check results"
    if ssh root@$BOARD_IP "[ -f /mnt/data/test_hw_encode.h264 ]" 2>/dev/null; then
        print_success "Hardware encoding SUCCESS!"
        echo ""
        echo "Output file created: /mnt/data/test_hw_encode.h264"
        ssh root@$BOARD_IP "ls -lh /mnt/data/test_hw_encode.h264"
        echo ""
        print_success "✓ GC2083 CSI camera is working with hardware encoder!"
        echo ""
        echo "Next steps:"
        echo "  1. Review logs: /tmp/gc2083_detection.log and /tmp/hw_encode_test.log"
        echo "  2. Download test video: scp root@$BOARD_IP:/mnt/data/test_hw_encode.h264 ."
        echo "  3. Play with: ffplay test_hw_encode.h264"
        echo "  4. Create hw-encode branch: cd $LOCAL_DIR && git checkout -b feature/hw-encode"
        echo ""
    else
        print_error "Hardware encoding test failed or SDK not available"
        echo ""
        echo "Possible reasons:"
        echo "  1. SG2000 SDK not installed on board"
        echo "  2. GC2083 driver not loaded"
        echo "  3. Device permissions issue"
        echo ""
        echo "Check detection log:"
        cat /tmp/gc2083_detection.log
        echo ""
        echo "Check compile log:"
        cat /tmp/compile.log
    fi
    
    exit 0
fi

# ============================================
# Mode 2: Run directly ON the board
# ============================================

print_step "1" "Detect GC2083 camera"
echo "Video devices:"
ls -l /dev/video* 2>/dev/null || echo "  No video devices"
echo ""

echo "CVI devices:"
ls -l /dev/cvi* 2>/dev/null || echo "  No CVI devices"
echo ""

echo "Kernel modules:"
lsmod | grep -E "gc2083|sensor|isp" || echo "  No camera modules"
echo ""

print_step "2" "Check SDK availability"
if [ -d "/mnt/system/lib" ]; then
    echo "SDK libraries found:"
    ls -l /mnt/system/lib/libcvi*.so 2>/dev/null | head -5
    SDK_AVAILABLE=1
else
    print_warning "SDK not found at /mnt/system/lib"
    SDK_AVAILABLE=0
fi
echo ""

print_step "3" "Compile test program"
cd /tmp
if [ $SDK_AVAILABLE -eq 1 ]; then
    gcc -DHAS_CVI_SDK \
        -I/mnt/system/include \
        -L/mnt/system/lib \
        -o test_hw_encode test_hw_encode.c \
        -lcvi_common -lcvi_sys -lcvi_vi -lcvi_vpss -lcvi_venc \
        -lpthread -lm || {
        print_error "Compilation with SDK failed"
        exit 1
    }
    print_success "Compiled with SDK support"
else
    gcc -o test_hw_encode test_hw_encode.c || {
        print_error "Compilation failed"
        exit 1
    }
    print_warning "Compiled without SDK (system checks only)"
fi
echo ""

print_step "4" "Run hardware encoding test"
./test_hw_encode

print_step "5" "Verify output"
if [ -f "/mnt/data/test_hw_encode.h264" ]; then
    print_success "Test video created!"
    ls -lh /mnt/data/test_hw_encode.h264
    echo ""
    echo "Play with: ffplay /mnt/data/test_hw_encode.h264"
else
    print_error "No output file created"
fi

echo ""
echo "========================================="
echo "Test Complete"
echo "========================================="
