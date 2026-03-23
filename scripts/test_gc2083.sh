#!/bin/bash
# GC2083 CSI Camera Hardware Test Script

echo "========================================"
echo "GC2083 CSI Camera Detection Test"
echo "========================================"

# 1. 检查视频设备
echo -e "\n[1] Video Devices:"
ls -l /dev/video* 2>/dev/null || echo "No video devices found"

# 2. V4L2 设备信息
echo -e "\n[2] V4L2 Device Info:"
for dev in /dev/video*; do
    if [ -e "$dev" ]; then
        echo "--- $dev ---"
        v4l2-ctl -d "$dev" --info 2>/dev/null || echo "v4l2-ctl not available"
        v4l2-ctl -d "$dev" --list-formats-ext 2>/dev/null || echo "Cannot list formats"
    fi
done

# 3. 检查 Media 设备
echo -e "\n[3] Media Devices:"
ls -l /dev/media* 2>/dev/null || echo "No media devices found"

# 4. 检查 VI (Video Input) 设备
echo -e "\n[4] VI Devices:"
ls -l /dev/vi* 2>/dev/null || echo "No VI devices found"

# 5. 检查内核模块
echo -e "\n[5] Loaded Kernel Modules (related to camera):"
lsmod | grep -E "gc2083|sensor|isp|vi|venc" || echo "No camera modules found"

# 6. 检查设备树 (如果可访问)
echo -e "\n[6] Device Tree Info:"
if [ -d /proc/device-tree ]; then
    find /proc/device-tree -name "*gc2083*" -o -name "*csi*" 2>/dev/null || echo "No GC2083 in device tree"
fi

# 7. 检查 dmesg 中的摄像头初始化信息
echo -e "\n[7] dmesg Camera Messages:"
dmesg | grep -i "gc2083\|csi\|sensor\|camera" | tail -20

# 8. 检查 /dev/cvi-* 设备（SG2000 特有）
echo -e "\n[8] CVI Devices (SG2000 specific):"
ls -l /dev/cvi* 2>/dev/null || echo "No CVI devices found"

# 9. 测试简单采集（如果 v4l2-ctl 可用）
echo -e "\n[9] Test Capture (5 frames):"
if command -v v4l2-ctl &> /dev/null; then
    v4l2-ctl -d /dev/video0 --set-fmt-video=width=1920,height=1080,pixelformat=NV12 \
             --stream-mmap --stream-count=5 2>&1 | head -20
else
    echo "v4l2-ctl not installed, skipping capture test"
fi

echo -e "\n========================================"
echo "Test Complete"
echo "========================================"
