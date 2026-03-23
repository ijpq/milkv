#!/bin/sh
export LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/lib:/mnt/system/usr/lib/3rd:$LD_LIBRARY_PATH

echo "=== 摄像头自动检测 ==="

echo "停止所有摄像头服务..."
killall -9 camera_monitor.sh heartbeat.sh camera_daemon python3 2>/dev/null
killall -9 sample_vi_fd hw_recorder.sh 2>/dev/null
sleep 2

if [ -c /dev/video0 ]; then
    echo "✓ 检测到 UVC 摄像头 (/dev/video0)"
    echo "启动 UVC 模式..."
    
    mkdir -p /mnt/data/logs
    nohup /mnt/system/camera_monitor.sh >> /mnt/data/logs/monitor_startup.log 2>&1 &
    nohup /mnt/system/heartbeat.sh >> /mnt/data/logs/heartbeat_startup.log 2>&1 &
    echo "[$(date)] UVC mode started" >> /mnt/data/logs/startup.log

elif grep -q "GC2083" /mnt/data/sensor_cfg.ini 2>/dev/null; then
    echo "✓ 检测到 GC2083 CSI 摄像头"
    echo "启动硬件编码模式..."
    
    mkdir -p /mnt/data/logs
    
    # 只启动 sample_vi_fd 和 Web，不启动自动录像
    nohup /mnt/system/usr/bin/ai/sample_vi_fd /mnt/cvimodel/scrfd_768_432_int8_1x.cvimodel >> /mnt/data/logs/sample_vi_fd.log 2>&1 &
    sleep 5
    
    cd /opt/camera/web
    nohup python3 app_hw.py > /mnt/data/logs/camera_web_hw.log 2>&1 &
    
    echo "[$(date)] GC2083 mode started" >> /mnt/data/logs/startup.log

else
    echo "✗ 未检测到任何摄像头"
    exit 1
fi
