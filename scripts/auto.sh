#!/bin/sh
interface="wlan0"
max_attempts=100
attempt=0
log_file="/var/log/auto.sh.log"

echo "start auto.sh" > "$log_file"
while [ $attempt -lt $max_attempts ]; do
    ip link show "$interface" > /dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo "$(date +'%Y-%m-%d %H:%M:%S') $interface interface exists, starting wpa_supplicant..." >> "$log_file"
        wpa_supplicant -B -i "$interface" -c /etc/wpa_supplicant.conf >> "$log_file"
        break
    else
        echo "$(date +'%Y-%m-%d %H:%M:%S') $interface interface not found, waiting..." >> "$log_file"
        sleep 1
        attempt=$((attempt + 1))
    fi
done

if [ $attempt -eq $max_attempts ]; then
    echo "$(date +'%Y-%m-%d %H:%M:%S') Interface $interface not found after $max_attempts attempts" >> "$log_file"
fi

sleep 3

echo "Syncing time from ntp.aliyun.com..." >> "$log_file"
/usr/sbin/ntpd -p ntp.aliyun.com -qn >> "$log_file" 2>&1
echo "Current time: $(date)" >> "$log_file"

# === UVC 摄像头监控系统自动启动 ===
mkdir -p /mnt/data/logs

echo "======================================" >> /mnt/data/logs/monitor.log
echo "[$(date)] SYSTEM BOOT - Uptime: $(cat /proc/uptime | awk '{print $1}')s" >> /mnt/data/logs/monitor.log
echo "======================================" >> /mnt/data/logs/monitor.log

# 启动进程监控
nohup /mnt/system/camera_monitor.sh >> /mnt/data/logs/monitor_startup.log 2>&1 &

# 启动心跳监控
nohup /mnt/system/heartbeat.sh >> /mnt/data/logs/heartbeat_startup.log 2>&1 &

echo "[$(date)] UVC camera services started" >> /mnt/data/logs/startup.log
