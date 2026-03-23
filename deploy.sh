#!/bin/bash
# GC2083 部署脚本 - 从 Mac 部署到 Milk-V Duo S

set -e

# 配置
BOARD_IP="${1:-192.168.31.219}"
BOARD_USER="root"

echo "=== GC2083 部署脚本 ==="
echo "目标设备: ${BOARD_USER}@${BOARD_IP}"
echo ""

# 检查连接
echo "1. 测试连接..."
if ! ssh -o ConnectTimeout=5 ${BOARD_USER}@${BOARD_IP} "echo '连接成功'" > /dev/null 2>&1; then
    echo "❌ 无法连接到板子，请检查 IP 地址和网络"
    exit 1
fi
echo "✓ 连接正常"

# 部署脚本
echo ""
echo "2. 部署启动脚本..."
scp scripts/auto_camera.sh ${BOARD_USER}@${BOARD_IP}:/mnt/system/
ssh ${BOARD_USER}@${BOARD_IP} "chmod +x /mnt/system/auto_camera.sh"
echo "✓ auto_camera.sh 已部署"

# 部署 Web 应用
echo ""
echo "3. 部署 Web 应用..."
scp web/app_hw.py ${BOARD_USER}@${BOARD_IP}:/opt/camera/web/
echo "✓ app_hw.py 已部署"

# 创建必要目录
echo ""
echo "4. 创建必要目录..."
ssh ${BOARD_USER}@${BOARD_IP} "mkdir -p /mnt/data/{logs,recordings}"
echo "✓ 目录创建完成"

# 检查传感器配置
echo ""
echo "5. 检查传感器配置..."
if ssh ${BOARD_USER}@${BOARD_IP} "test -f /mnt/data/sensor_cfg.ini"; then
    echo "✓ sensor_cfg.ini 已存在"
else
    echo "⚠️  sensor_cfg.ini 不存在，请手动配置"
    echo "   参考: gc2083/config/sensor_cfg.ini.example"
fi

# 重启服务
echo ""
echo "6. 重启服务..."
read -p "是否重启板子以应用更改？(y/N) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    ssh ${BOARD_USER}@${BOARD_IP} "reboot"
    echo "✓ 板子正在重启..."
    echo "  等待 50 秒后可重新连接"
else
    echo "跳过重启，手动重启命令："
    echo "  ssh ${BOARD_USER}@${BOARD_IP} reboot"
fi

echo ""
echo "=== 部署完成 ==="
echo "访问地址:"
echo "  RTSP: rtsp://${BOARD_IP}/h264"
echo "  Web:  http://${BOARD_IP}:8080"
