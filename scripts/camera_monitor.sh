#!/bin/sh

LOG_DIR="/mnt/data/logs"
PID_DIR="/var/run"
mkdir -p "$LOG_DIR" "$PID_DIR"

log_system_state() {
    echo "[$(date)] === System State ===" >> "$LOG_DIR/monitor.log"
    echo "Memory: $(free | awk '/Mem:/{printf "%dMB/%dMB", $3/1024, $2/1024}')" >> "$LOG_DIR/monitor.log"
    echo "Load: $(cat /proc/loadavg)" >> "$LOG_DIR/monitor.log"
    dmesg | tail -3 >> "$LOG_DIR/monitor.log"
    echo "" >> "$LOG_DIR/monitor.log"
}

sleep 10

start_camera_daemon() {
    if [ -f "$PID_DIR/camera_daemon.pid" ]; then
        OLD_PID=$(cat "$PID_DIR/camera_daemon.pid")
        if kill -0 "$OLD_PID" 2>/dev/null; then
            return
        fi
        echo "[$(date)] camera_daemon died (PID: $OLD_PID)" >> "$LOG_DIR/monitor.log"
        log_system_state
    fi
    
    echo "[$(date)] Starting camera_daemon..." >> "$LOG_DIR/monitor.log"
    nohup /opt/camera/camera_daemon > "$LOG_DIR/camera_daemon.log" 2>&1 &
    echo $! > "$PID_DIR/camera_daemon.pid"
    sleep 3
}

start_app_py() {
    if [ -f "$PID_DIR/app_py.pid" ]; then
        OLD_PID=$(cat "$PID_DIR/app_py.pid")
        if kill -0 "$OLD_PID" 2>/dev/null; then
            return
        fi
        echo "[$(date)] app.py died (PID: $OLD_PID)" >> "$LOG_DIR/monitor.log"
        log_system_state
    fi
    
    echo "[$(date)] Starting app.py..." >> "$LOG_DIR/monitor.log"
    nohup python3 /opt/camera/web/app.py > "$LOG_DIR/camera_web.log" 2>&1 &
    echo $! > "$PID_DIR/app_py.pid"
    sleep 3
}

start_camera_daemon
start_app_py

while true; do
    sleep 60
    
    if [ -f "$PID_DIR/camera_daemon.pid" ]; then
        PID=$(cat "$PID_DIR/camera_daemon.pid")
        if ! kill -0 "$PID" 2>/dev/null; then
            start_camera_daemon
        fi
    else
        start_camera_daemon
    fi
    
    if [ -f "$PID_DIR/app_py.pid" ]; then
        PID=$(cat "$PID_DIR/app_py.pid")
        if ! kill -0 "$PID" 2>/dev/null; then
            start_app_py
        fi
    else
        start_app_py
    fi
done
