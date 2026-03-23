#!/bin/sh
# Put the program you want to run automatically here
interface="wlan0"
max_attempts=100
attempt=0
log_file="/var/log/auto.sh.log"

# Continuously attempt to detect if the interface exists, up to $max_attempts times
echo "start auto.sh" > "$log_file"
while [ $attempt -lt $max_attempts ]; do
    # Check if the wlan0 interface exists
    ip link show "$interface" > /dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo "$(date +'%Y-%m-%d %H:%M:%S') $interface interface exists, starting wpa_supplicant..." >> "$log_file"
        wpa_supplicant -B -i "$interface" -c /etc/wpa_supplicant.conf >> "$log_file"
        break  # Exit the loop if the interface is found
    else
        echo "$(date +'%Y-%m-%d %H:%M:%S') $interface interface not found, waiting..." >> "$log_file"
        sleep 1  # Wait for 1 second before checking again
        attempt=$((attempt + 1))  # Increment the attempt counter
    fi
done

# If the maximum number of attempts is reached and the interface still not found, output an error message
if [ $attempt -eq $max_attempts ]; then
    echo "$(date +'%Y-%m-%d %H:%M:%S') Interface $interface not found after $max_attempts attempts" >> "$log_file"
fi

sleep 3

# 开始同步时间
echo "Syncing time from ntp.aliyun.com..." >> "$log_file"
/usr/sbin/ntpd -p ntp.aliyun.com -qn >> "$log_file" 2>&1

# 打印当前时间到日志，方便确认
echo "Current time: $(date)" >> "$log_file"

# === 摄像头监控系统自动启动 ===
mkdir -p /mnt/data/logs

# 记录系统启动事件（添加分隔线，便于区分新旧日志）
echo "======================================" >> /mnt/data/logs/monitor.log
echo "[$(date)] SYSTEM BOOT - Uptime: $(cat /proc/uptime | awk '{print $1}')s" >> /mnt/data/logs/monitor.log
echo "======================================" >> /mnt/data/logs/monitor.log

# 备份上次系统日志
if [ -f /var/log/messages ]; then
    cp /var/log/messages /mnt/data/logs/messages_backup_$(date +%Y%m%d_%H%M%S) 2>/dev/null || true
fi

# 启动进程监控
nohup /mnt/system/camera_monitor.sh >> /mnt/data/logs/monitor_startup.log 2>&1 &

# 启动心跳监控
nohup /mnt/system/heartbeat.sh >> /mnt/data/logs/heartbeat_startup.log 2>&1 &

# 记录启动完成
echo "[$(date)] Camera services started" >> /mnt/data/logs/startup.log
