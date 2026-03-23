#!/bin/bash
BOARD_IP="192.168.31.219"

echo "=== 同步 UVC 完整代码 ==="

# 1. Web应用
echo ""
echo "1. 同步 Web 应用..."
scp root@${BOARD_IP}:/opt/camera/web/app.py /tmp/board_app.py
if diff -q src/web/app.py /tmp/board_app.py > /dev/null 2>&1; then
    echo "  ✓ app.py 已是最新"
else
    echo "  ⬇ 更新 app.py"
    cp /tmp/board_app.py src/web/app.py
fi

# 2. 启动脚本
echo ""
echo "2. 同步启动脚本..."
for script in camera_monitor.sh heartbeat.sh; do
    scp root@${BOARD_IP}:/mnt/system/$script /tmp/board_$script
    if diff -q scripts/$script /tmp/board_$script > /dev/null 2>&1; then
        echo "  ✓ $script 已是最新"
    else
        echo "  ⬇ 更新 $script"
        cp /tmp/board_$script scripts/$script
    fi
done

# 3. 二进制
echo ""
echo "3. 同步二进制..."
scp root@${BOARD_IP}:/opt/camera/camera_daemon /tmp/board_camera_daemon
if diff -q build/camera_daemon /tmp/board_camera_daemon > /dev/null 2>&1; then
    echo "  ✓ camera_daemon 已是最新"
else
    echo "  ⬇ 更新 camera_daemon"
    cp /tmp/board_camera_daemon build/camera_daemon
    chmod +x build/camera_daemon
fi

# 4. 配置文件示例
echo ""
echo "4. 更新配置示例..."
mkdir -p config

scp root@${BOARD_IP}:/etc/camera/config.ini config/config.ini.example
echo "  ✓ config.ini.example"

scp root@${BOARD_IP}:/etc/wpa_supplicant.conf /tmp/wpa.conf
sed 's/psk=".*"/psk="YOUR_WIFI_PASSWORD"/' /tmp/wpa.conf > config/wpa_supplicant.conf.example
echo "  ✓ wpa_supplicant.conf.example"

scp root@${BOARD_IP}:/root/.config/rclone/rclone.conf /tmp/rclone.conf 2>/dev/null
if [ -f /tmp/rclone.conf ]; then
    sed 's/user = .*/user = YOUR_WEBDAV_USER/' /tmp/rclone.conf | \
    sed 's/pass = .*/pass = YOUR_WEBDAV_PASS/' > config/rclone.conf.example
    echo "  ✓ rclone.conf.example"
fi

echo ""
echo "=== 同步完成 ==="
echo ""
echo "UVC 文件清单:"
echo "  源代码: src/daemon/{camera_daemon.c, Makefile}"
echo "  Web应用: src/web/app.py"
echo "  脚本: scripts/{camera_monitor.sh, heartbeat.sh, auto.sh}"
echo "  二进制: build/camera_daemon"
echo "  配置: config/*.example"
