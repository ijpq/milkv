#!/bin/bash
# UVC 摄像头系统部署脚本 - 从 Mac 部署到 Milk-V Duo S

set -e

# 配置
BOARD_IP="${1:-192.168.31.219}"
BOARD_USER="root"

echo "=== UVC 系统部署脚本 ==="
echo "目标设备: ${BOARD_USER}@${BOARD_IP}"
echo ""

# 检查本地文件
echo "0. 检查本地文件..."
if [ ! -f build/camera_daemon ]; then
    echo "❌ 缺少 build/camera_daemon"
    echo "   请先编译: cd src/daemon && make"
    exit 1
fi

if [ ! -f src/web/app.py ]; then
    echo "❌ 缺少 src/web/app.py"
    exit 1
fi

if [ ! -f scripts/camera_monitor.sh ] || [ ! -f scripts/heartbeat.sh ] || [ ! -f scripts/auto.sh ]; then
    echo "❌ 缺少启动脚本"
    exit 1
fi

echo "✓ 本地文件检查通过"

# 检查连接
echo ""
echo "1. 测试连接..."
if ! ssh -o ConnectTimeout=5 ${BOARD_USER}@${BOARD_IP} "echo '连接成功'" > /dev/null 2>&1; then
    echo "❌ 无法连接到板子，请检查 IP 地址和网络"
    exit 1
fi
echo "✓ 连接正常"

# 停止服务
echo ""
echo "2. 停止现有服务..."
ssh ${BOARD_USER}@${BOARD_IP} "killall -9 camera_monitor.sh heartbeat.sh camera_daemon python3 2>/dev/null || true"
echo "✓ 服务已停止"

# 创建目录
echo ""
echo "3. 创建必要目录..."
ssh ${BOARD_USER}@${BOARD_IP} "mkdir -p /opt/camera/web /mnt/data/{logs,recordings} /etc/camera"
echo "✓ 目录创建完成"

# 部署二进制
echo ""
echo "4. 部署二进制文件..."
scp build/camera_daemon ${BOARD_USER}@${BOARD_IP}:/opt/camera/
ssh ${BOARD_USER}@${BOARD_IP} "chmod +x /opt/camera/camera_daemon"
echo "✓ camera_daemon 已部署"

# 部署 Web 应用
echo ""
echo "5. 部署 Web 应用..."
scp src/web/app.py ${BOARD_USER}@${BOARD_IP}:/opt/camera/web/
echo "✓ app.py 已部署"

# 部署启动脚本
echo ""
echo "6. 部署启动脚本..."
scp scripts/camera_monitor.sh ${BOARD_USER}@${BOARD_IP}:/mnt/system/
scp scripts/heartbeat.sh ${BOARD_USER}@${BOARD_IP}:/mnt/system/
scp scripts/auto.sh ${BOARD_USER}@${BOARD_IP}:/mnt/system/
ssh ${BOARD_USER}@${BOARD_IP} "chmod +x /mnt/system/*.sh"
echo "✓ 启动脚本已部署"

# 部署配置文件（如果不存在）
echo ""
echo "7. 检查配置文件..."

# 检查 config.ini
if ! ssh ${BOARD_USER}@${BOARD_IP} "test -f /etc/camera/config.ini" 2>/dev/null; then
    if [ -f config/config.ini.example ]; then
        echo "  ⚠️  /etc/camera/config.ini 不存在"
        read -p "  是否部署示例配置？(y/N) " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            scp config/config.ini.example ${BOARD_USER}@${BOARD_IP}:/etc/camera/config.ini
            echo "  ✓ 已部署 config.ini（请根据实际情况修改）"
        fi
    fi
else
    echo "  ✓ config.ini 已存在"
fi

# 检查 wpa_supplicant.conf
if ! ssh ${BOARD_USER}@${BOARD_IP} "test -f /etc/wpa_supplicant.conf" 2>/dev/null; then
    echo "  ⚠️  /etc/wpa_supplicant.conf 不存在（WiFi配置）"
    echo "     请手动配置 WiFi"
else
    echo "  ✓ wpa_supplicant.conf 已存在"
fi

# 检查 rclone.conf
if ! ssh ${BOARD_USER}@${BOARD_IP} "test -f /root/.config/rclone/rclone.conf" 2>/dev/null; then
    echo "  ⚠️  rclone.conf 不存在（NAS上传配置）"
    echo "     如需上传功能，请手动配置 rclone"
else
    echo "  ✓ rclone.conf 已存在"
fi

# 验证部署
echo ""
echo "8. 验证部署..."
ssh ${BOARD_USER}@${BOARD_IP} "ls -lh /opt/camera/camera_daemon /opt/camera/web/app.py /mnt/system/{camera_monitor,heartbeat,auto}.sh" > /dev/null
echo "✓ 所有文件部署成功"

# 重启服务
echo ""
echo "9. 启动服务..."
read -p "启动方式: [1]重启板子 [2]手动启动服务 [3]跳过 (1/2/3): " choice

case $choice in
    1)
        ssh ${BOARD_USER}@${BOARD_IP} "reboot"
        echo "✓ 板子正在重启..."
        echo "  等待 50 秒后可重新连接"
        ;;
    2)
        echo "手动启动服务:"
        ssh ${BOARD_USER}@${BOARD_IP} << 'EOFSTART'
            nohup /mnt/system/camera_monitor.sh >> /mnt/data/logs/monitor_startup.log 2>&1 &
            nohup /mnt/system/heartbeat.sh >> /mnt/data/logs/heartbeat_startup.log 2>&1 &
            echo "服务已启动"
EOFSTART
        echo "✓ 服务已启动"
        sleep 3
        echo ""
        echo "检查进程:"
        ssh ${BOARD_USER}@${BOARD_IP} "ps aux | grep -E 'camera_daemon|camera_monitor|heartbeat|app.py' | grep -v grep"
        ;;
    3)
        echo "跳过启动，稍后手动重启板子或执行:"
        echo "  ssh ${BOARD_USER}@${BOARD_IP} /mnt/system/auto.sh"
        ;;
esac

echo ""
echo "=== 部署完成 ==="
echo ""
echo "访问地址:"
echo "  Web:  http://${BOARD_IP}:8080"
echo "  实时流: 通过 Web 界面查看"
echo ""
echo "查看日志:"
echo "  ssh ${BOARD_USER}@${BOARD_IP} tail -f /mnt/data/logs/camera_daemon.log"
echo "  ssh ${BOARD_USER}@${BOARD_IP} tail -f /mnt/data/logs/monitor.log"
