#!/bin/sh

LOG_FILE="/mnt/data/logs/heartbeat.log"

while true; do
    # 检查 camera_daemon PID
    DAEMON_PID=$(ps aux | grep -v grep | grep camera_daemon | awk '{print $1}')
    if [ -z "$DAEMON_PID" ]; then
        DAEMON_PID="X"
    fi
    
    # 检查 app.py PID
    APP_PID=$(ps aux | grep -v grep | grep "app.py" | awk '{print $1}')
    if [ -z "$APP_PID" ]; then
        APP_PID="X"
    fi
    
    echo "[$(date +%Y-%m-%d_%H:%M:%S)] UP:$(cat /proc/uptime | awk '{print int($1)}')s MEM:$(free | awk '/Mem:/{printf "%dMB/%dMB", $3/1024, $2/1024}') LOAD:$(cat /proc/loadavg | awk '{print $1}') DAEMON:$DAEMON_PID APP:$APP_PID" >> "$LOG_FILE"
    
    sleep 60
done
