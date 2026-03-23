# GC2083 项目结构
```
./
├── README.md                      # 完整文档
├── STRUCTURE.md                   # 本文件
├── deploy.sh                      # 部署脚本
├── scripts/
│   └── auto_camera.sh            # 自动检测并启动脚本
├── web/
│   └── app_hw.py                 # Web 界面（Flask）
└── config/
    └── sensor_cfg.ini.example    # 传感器配置示例
```

## 部署位置映射

| 本地文件 | 板子路径 |
|---------|---------|
| scripts/auto_camera.sh | /mnt/system/auto_camera.sh |
| web/app_hw.py | /opt/camera/web/app_hw.py |
| config/sensor_cfg.ini.example | /mnt/data/sensor_cfg.ini（需手动配置） |

## 使用方法
```bash
# 部署到板子（默认 IP: 192.168.31.219）
./deploy.sh

# 部署到其他 IP
./deploy.sh 192.168.31.177
```
