# Milk-V Duo S Camera Surveillance System

A lightweight surveillance system running on Milk-V Duo S (SG2000) with MJPEG streaming, recording, and NAS auto-upload.

## Features

- 📹 Real-time MJPEG streaming (1920x1080@30fps)
- 🎥 On-demand recording to SD card
- ☁️ Auto-upload to NAS with retry mechanism
- 🔄 Process monitoring and auto-restart
- 💓 System health heartbeat logging
- 🚀 Boot auto-start

## Hardware Requirements

- Milk-V Duo S (SG2000, 512MB RAM)
- UVC USB Camera (tested with W19 HD Webcam)
- MicroSD card (16GB+)
- NAS with WebDAV (optional)

## Quick Start

### 1. Deploy to Board
```bash
# Copy files to board
scp -r scripts/* root@192.168.31.219:/mnt/system/
scp -r src/web/* root@192.168.31.219:/opt/camera/web/
scp bin/camera_daemon.arm64 root@192.168.31.219:/opt/camera/camera_daemon

# SSH to board
ssh root@192.168.31.219

# Set permissions
chmod +x /opt/camera/camera_daemon
chmod +x /opt/camera/web/app.py
chmod +x /mnt/system/*.sh

# Reboot to auto-start
reboot
```

### 2. Access Services

- Web UI: http://192.168.31.219:8080
- MJPEG Stream: http://192.168.31.219:8554

## Architecture
```
┌─────────────────────────────────────┐
│  Web Browser (8080)  │  MJPEG (8554)│
└──────────┬────────────┴──────┬───────┘
           │                   │
    ┌──────▼────────┐   ┌─────▼──────────┐
    │   app.py      │   │ camera_daemon  │
    │  (Flask)      │◄──┤  (V4L2 + HTTP) │
    └───────────────┘   └────────────────┘
           │                   │
    ┌──────▼─────────────────▼──────────┐
    │  camera_monitor.sh + heartbeat.sh │
    └───────────────────────────────────┘
```

## Configuration

### NAS Upload (Optional)

1. Install rclone:
```bash
wget https://downloads.rclone.org/rclone-current-linux-arm64.zip
unzip rclone-*.zip && cp rclone-*/rclone /usr/bin/
```

2. Configure WebDAV:
```bash
mkdir -p /root/.config/rclone
cp config/rclone.conf.example /root/.config/rclone/rclone.conf
# Edit with your credentials
vi /root/.config/rclone/rclone.conf
```

3. Configure camera:
```bash
mkdir -p /etc/camera
cp config/config.ini.example /etc/camera/config.ini
# Edit remote settings
vi /etc/camera/config.ini
```

## File Structure
```
/opt/camera/
├── camera_daemon           # C daemon (video capture + HTTP server)
└── web/
    └── app.py             # Flask web app

/mnt/system/
├── auto.sh                # Boot auto-start
├── camera_monitor.sh      # Process monitor
└── heartbeat.sh           # Health monitor

/mnt/data/
├── recordings/            # Video files (.mjpeg)
└── logs/                  # System logs
```

## Logs

| Log File | Purpose |
|----------|---------|
| `/mnt/data/logs/heartbeat.log` | System health (PID, memory, load) |
| `/mnt/data/logs/monitor.log` | Process restart events |
| `/mnt/data/logs/upload.log` | Upload queue and retry |
| `/mnt/data/logs/camera_daemon.log` | Daemon output |
| `/mnt/data/logs/camera_web.log` | Flask app logs |

## Troubleshooting

**Services not starting:**
```bash
ps aux | grep -E "camera_daemon|app.py" | grep -v grep
tail -50 /mnt/data/logs/monitor.log
```

**Upload failing:**
```bash
tail -30 /mnt/data/logs/upload.log
rclone --config /root/.config/rclone/rclone.conf ls nas:recordings
```

**System reboot diagnosis:**
```bash
grep "SYSTEM BOOT" /mnt/data/logs/monitor.log | tail -3
tail -10 /mnt/data/logs/heartbeat.log
```

## Known Limitations

- **Large file size**: MJPEG format (100MB per 30s)
- **No hardware encoding**: SG2000 VENC doesn't support UVC cameras
- **No motion detection**: Records continuously when started
- **No authentication**: Web UI and stream are publicly accessible on LAN

## Performance

- CPU Load: 3.00 (full load, normal)
- Memory: ~85MB used (camera_daemon + app.py)
- Recording: 3.3 MB/s (1920x1080@30fps MJPEG)
- Temperature: ~40°C

## Future Improvements

- [ ] Reduce file size (lower resolution to 1280x720@15fps)
- [ ] Motion detection recording
- [ ] H.264 post-processing on NAS
- [ ] Multi-camera support
- [ ] Web UI authentication

## License

MIT

## Documentation

See [docs/TECHNICAL.md](docs/TECHNICAL.md) for complete technical documentation.
