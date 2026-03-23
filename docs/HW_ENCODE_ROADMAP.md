# 硬件编码版本开发路线图

**分支**: `feature/hw-encode`  
**基线**: `v1.0-mjpeg-baseline` (MJPEG + UVC)  
**目标**: GC2083 + 硬件 VENC (H.264)

---

## 阶段 1：项目设置 ✅

- [x] 验证 GC2083 摄像头工作
- [x] 验证硬件 VENC 可用
- [x] 验证 RTSP 流可用
- [x] 创建 feature/hw-encode 分支
- [x] 标记 MJPEG 基线版本

---

## 阶段 2：代码开发（进行中）

### 2.1 获取 SDK 头文件

**来源**: https://github.com/milkv-duo/duo-buildroot-sdk

**需要的头文件**：
```
middleware/v2/include/
├── cvi_comm.h          # 通用定义
├── cvi_type.h          # 数据类型
├── cvi_sys.h           # 系统初始化
├── cvi_vi.h            # 视频输入
├── cvi_vpss.h          # 视频处理
├── cvi_venc.h          # 视频编码
├── cvi_buffer.h        # 缓冲区管理
└── cvi_rtsp.h          # RTSP 服务
```

**存放位置**: `src/daemon/include/`

### 2.2 编写核心代码

**文件结构**：
```
src/daemon/
├── camera_daemon_hw.c       # 主程序（硬件编码版本）
├── cvi_init.c               # CVI 系统初始化
├── cvi_vi.c                 # VI 视频输入
├── cvi_vpss.c               # VPSS 视频处理
├── cvi_venc.c               # VENC 编码器
├── cvi_rtsp.c               # RTSP 流服务
├── record.c                 # 录像管理
├── control.c                # 控制接口（Unix Socket）
└── include/                 # SDK 头文件
```

**核心功能**：
- [x] VI 从 GC2083 采集 1920x1080@30fps
- [x] ISP 自动白平衡/自动曝光
- [x] VPSS 视频处理（可选缩放）
- [x] VENC H.264 编码（4 Mbps，CBR）
- [x] RTSP 流服务（8554 端口）
- [x] 录像到文件（.h264 格式）
- [x] Unix Socket 控制接口
- [x] Web API 兼容（app.py 无需改动）

### 2.3 设置交叉编译

**工具链选择**：
- **选项 A**: 使用 Milk-V 官方工具链
- **选项 B**: 使用 Docker 容器编译
- **选项 C**: 在板子上直接编译（需安装 gcc）

**推荐**: 选项 B（Docker）- 环境隔离，可复现

---

## 阶段 3：编译和测试

### 3.1 本地编译

```bash
cd ~/milkv/src/daemon
make -f Makefile.hw clean
make -f Makefile.hw
```

**输出**: `camera_daemon_hw` (arm64 二进制)

### 3.2 部署到板子

```bash
scp camera_daemon_hw root@192.168.31.219:/opt/camera/
ssh root@192.168.31.219 "chmod +x /opt/camera/camera_daemon_hw"
```

### 3.3 功能测试

**测试项**：
- [ ] 系统初始化成功
- [ ] GC2083 采集正常
- [ ] H.264 编码成功
- [ ] RTSP 流可访问（`rtsp://192.168.31.219:8554`）
- [ ] 录像功能正常
- [ ] 文件体积符合预期（~10-15MB/30s）
- [ ] CPU 占用降低（< 1.0）
- [ ] 内存占用可接受（< 150MB）

### 3.4 性能对比

| 指标 | MJPEG (UVC) | H.264 (CSI+VENC) | 改进 |
|------|-------------|------------------|------|
| 文件体积 (30s) | 100MB | 10-15MB | -85% |
| CPU 占用 | 3.00 | 0.5-1.0 | -70% |
| 内存占用 | 85MB | 100-120MB | +15MB |
| 实时性 | 优秀 | 优秀 | = |
| 画质 | 优秀 | 优秀 | = |

---

## 阶段 4：集成和优化

### 4.1 替换旧版 daemon

```bash
# 备份旧版
ssh root@192.168.31.219 "mv /opt/camera/camera_daemon /opt/camera/camera_daemon.mjpeg"

# 部署新版
ssh root@192.168.31.219 "mv /opt/camera/camera_daemon_hw /opt/camera/camera_daemon"

# 重启服务
ssh root@192.168.31.219 "killall camera_daemon; sleep 2; /opt/camera/camera_daemon &"
```

### 4.2 更新 Web 界面

**app.py 修改**：
- 更新流 URL（如果需要）
- 支持 H.264 文件下载
- 显示编码参数（码率、GOP 等）

**Web 前端**：
- 使用 HLS 或 WebRTC 播放 H.264 流
- 或继续使用 MJPEG over HTTP（需要转码）

### 4.3 更新监控脚本

**camera_monitor.sh**：
- 监控新的进程名称
- 添加 VENC 健康检查

### 4.4 文档更新

- [ ] 更新 README.md
- [ ] 更新 TECHNICAL.md
- [ ] 添加硬件编码配置指南
- [ ] 性能测试报告

---

## 阶段 5：稳定性测试

### 5.1 长期运行测试

- [ ] 24 小时连续运行
- [ ] 内存泄漏检查
- [ ] 崩溃恢复验证

### 5.2 压力测试

- [ ] 多客户端 RTSP 连接
- [ ] 长时间录像（2 小时+）
- [ ] 频繁开始/停止录像

### 5.3 边界测试

- [ ] SD 卡满时的行为
- [ ] 网络中断恢复
- [ ] 摄像头断开/重连

---

## 阶段 6：发布

### 6.1 合并到主分支

```bash
git checkout main
git merge feature/hw-encode
git tag v2.0-hw-encode -m "Hardware encoding with GC2083"
git push origin main
git push origin v2.0-hw-encode
```

### 6.2 Release Notes

**v2.0 - 硬件编码版本**

**重大变更**：
- 从 UVC 摄像头切换到 GC2083 CSI 摄像头
- 使用 SG2000 硬件编码器（VENC）进行 H.264 编码
- 文件体积减少 85%（100MB → 15MB / 30秒）
- CPU 占用降低 70%（3.00 → 0.5-1.0）

**升级指南**：
- 需要 GC2083 CSI 摄像头（16-pin J1 接口）
- 需要刷入支持 CSI 的固件（如果尚未刷入）
- 备份现有配置和录像
- 按照 docs/UPGRADE_v2.md 步骤升级

**已知问题**：
- Web 界面暂不支持 H.264 流播放（需要浏览器支持）
- 录像文件格式变更（.mjpeg → .h264）

---

## 技术债务和未来优化

### 短期
- [ ] H.265 编码支持（更高压缩率）
- [ ] 可配置的编码参数（码率、GOP、分辨率）
- [ ] 运动检测录像（节省存储）

### 中期
- [ ] WebRTC 实时流（低延迟）
- [ ] AI 功能集成（人脸检测、目标跟踪）
- [ ] 多摄像头支持

### 长期
- [ ] 边缘计算能力（本地 AI 推理）
- [ ] 云存储集成
- [ ] 移动 APP 支持

---

**最后更新**: 2026-03-23  
**当前状态**: 阶段 2 - 代码开发中
