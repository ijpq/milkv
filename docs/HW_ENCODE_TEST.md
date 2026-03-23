# GC2083 硬件编码测试指南

## 背景

从 UVC 摄像头（W19 HD Webcam）升级到 GC2083 CSI 摄像头后，理论上可以使用 SG2000 的硬件编码器（VENC）进行 H.264/H.265 编码，大幅减少文件体积和 CPU 占用。

## 测试目标

验证以下数据通路是否可用：

```
GC2083 (CSI) → ISP → VI → VPSS → VENC (H.264) → 文件
```

对比当前的 UVC 方案：

```
W19 (UVC) → V4L2 → MJPEG → 文件 (100MB/30s)
```

预期硬件编码效果：

```
GC2083 → VENC (H.264) → 文件 (5-10MB/30s)  ← 减少 90%+
```

---

## 快速测试（自动化）

### 从开发机运行

```bash
cd ~/milkv/scripts
chmod +x run_hw_test.sh
./run_hw_test.sh
```

这会自动：
1. 上传测试脚本到板子
2. 检测 GC2083 摄像头
3. 编译硬件编码测试程序
4. 运行 10 秒编码测试
5. 报告结果

### 直接在板子上运行

SSH 到板子：

```bash
ssh root@192.168.31.219
cd /tmp
./run_hw_test.sh on-board
```

---

## 手动测试（分步骤）

### 步骤 1：检测 GC2083 摄像头

```bash
# 在板子上执行
ssh root@192.168.31.219

# 运行检测脚本
bash /tmp/test_gc2083.sh
```

**期望输出**：
```
[1] Video Devices:
crw-rw---- 1 root video 81, 0 Mar 21 14:30 /dev/video0
crw-rw---- 1 root video 81, 1 Mar 21 14:30 /dev/video1

[2] V4L2 Device Info:
--- /dev/video0 ---
Driver: cvi-vi
Card: cvi-vi-0
Bus info: platform:cvi-vi
Capabilities: 0x84201000

[7] dmesg Camera Messages:
[    3.123456] gc2083 0-003f: probe success
[    3.234567] cvi-vi cvi-vi: cvi_vi_probe success
```

**关键指标**：
- ✅ `/dev/video0` 存在
- ✅ Driver 为 `cvi-vi`（不是 `uvcvideo`）
- ✅ dmesg 显示 `gc2083 probe success`

### 步骤 2：检查 SDK 可用性

```bash
# 检查 SDK 库
ls -l /mnt/system/lib/libcvi*.so

# 检查头文件
ls -l /mnt/system/include/cvi*.h
```

**如果 SDK 不存在**：

需要从 Milk-V SDK 中提取并部署到板子：

```bash
# 在开发机上
git clone https://github.com/milkv-duo/duo-buildroot-sdk.git
cd duo-buildroot-sdk

# 提取 middleware 库和头文件
# (具体路径根据 SDK 版本可能不同)

# 上传到板子
scp -r middleware/v2/lib/* root@192.168.31.219:/mnt/system/lib/
scp -r middleware/v2/include/* root@192.168.31.219:/mnt/system/include/
```

### 步骤 3：编译测试程序

```bash
# 在板子上
cd /tmp

# 有 SDK 的情况
gcc -DHAS_CVI_SDK \
    -I/mnt/system/include \
    -L/mnt/system/lib \
    -o test_hw_encode test_hw_encode.c \
    -lcvi_common -lcvi_sys -lcvi_vi -lcvi_vpss -lcvi_venc \
    -lpthread -lm

# 无 SDK 的情况（只做系统检查）
gcc -o test_hw_encode test_hw_encode.c
```

### 步骤 4：运行测试

```bash
# 设置库路径
export LD_LIBRARY_PATH=/mnt/system/lib:$LD_LIBRARY_PATH

# 运行测试（10 秒）
./test_hw_encode
```

**成功标志**：
```
[Step 1] Initialize SYS...
  OK: SYS initialized

[Step 2] Initialize VI (Video Input)...
  OK: VI device enabled

[Step 3] Initialize VPSS (Video Processing)...
  OK: VPSS group created

[Step 4] Initialize VENC (Hardware Encoder)...
  OK: VENC channel created

[Step 5] Start encoding (10 seconds)...
  Encoded 30 frames...
  Encoded 60 frames...
  ...

========================================
RESULT: SUCCESS!
  Frames encoded: 300
  Output file: /mnt/data/test_hw_encode.h264
  File size: 8.2M
Hardware encoding is WORKING!
========================================
```

### 步骤 5：验证视频文件

```bash
# 在板子上检查文件
ls -lh /mnt/data/test_hw_encode.h264

# 下载到开发机
scp root@192.168.31.219:/mnt/data/test_hw_encode.h264 ~/

# 播放验证
ffplay ~/test_hw_encode.h264
```

---

## 测试结果判断

### ✅ 测试成功

如果看到：
- `/mnt/data/test_hw_encode.h264` 文件创建成功
- 文件大小约 5-15MB（10 秒录像）
- ffplay 可以正常播放

**恭喜！硬件编码可用，可以创建新分支开发。**

进入下一步：[创建硬件编码分支](#创建硬件编码分支)

### ❌ 测试失败

可能的原因和解决方案：

#### 1. GC2083 驱动未加载

**症状**：`/dev/video0` 不存在，或 driver 显示为 `uvcvideo`

**解决**：
```bash
# 检查内核模块
lsmod | grep gc2083

# 如果没有，手动加载
modprobe gc2083

# 检查 dmesg
dmesg | grep gc2083
```

#### 2. SDK 库不存在

**症状**：编译时报错 `cannot find -lcvi_venc`

**解决**：参考步骤 2 安装 SDK

#### 3. 权限问题

**症状**：`CVI_VI_EnableDev() = 0x80000001`

**解决**：
```bash
# 检查设备权限
ls -l /dev/cvi*

# 添加权限
chmod 666 /dev/cvi*
```

#### 4. 内存不足

**症状**：初始化时崩溃或 OOM

**解决**：
```bash
# 检查可用内存
free -h

# 清理缓存
sync && echo 3 > /proc/sys/vm/drop_caches
```

---

## 创建硬件编码分支

如果测试成功，创建新分支开发硬件编码版本：

```bash
cd ~/milkv

# 确保主分支代码已提交
git status
git add .
git commit -m "Add GC2083 hardware encoding test suite"

# 创建新分支
git checkout -b feature/hw-encode

# 标记当前为 MJPEG 基线
git tag v1.0-mjpeg-baseline
```

### 硬件编码版本开发任务

- [ ] 修改 `camera_daemon.c`：
  - 替换 V4L2 采集为 CVI_VI
  - 添加 VPSS 处理
  - 使用 VENC 进行 H.264 编码
  - 输出 H.264 流（替代 MJPEG）

- [ ] 修改 `app.py`：
  - 适配 H.264 文件格式
  - 更新上传逻辑

- [ ] 修改 Web 界面：
  - H.264 流播放（使用 HLS 或 WebRTC）

- [ ] 性能测试：
  - CPU 占用率
  - 内存占用
  - 文件体积
  - 编码延迟

- [ ] 文档更新：
  - 硬件编码原理
  - 性能对比数据
  - 部署指南

---

## 性能对比预期

| 指标 | MJPEG (UVC) | H.264 (CSI+VENC) | 改进 |
|------|-------------|------------------|------|
| 文件体积 (30s) | 100MB | 8-12MB | -90% |
| CPU 占用 | 3.00 (满载) | 0.5-1.0 | -70% |
| 内存占用 | 85MB | 100MB | +15MB |
| 实时性 | 优秀 | 优秀 | = |
| 画质 | 优秀 | 优秀 | = |

---

## 参考文档

- **SG2000 SDK**: https://github.com/milkv-duo/duo-buildroot-sdk
- **CVI MPI 文档**: duo-buildroot-sdk/middleware/v2/doc/
- **GC2083 数据手册**: (厂商提供)

---

## 故障排查日志位置

```bash
# 检测日志
/tmp/gc2083_detection.log

# 编码测试日志
/tmp/hw_encode_test.log

# 编译日志
/tmp/compile.log

# 系统日志
dmesg | tail -100
```

---

**最后更新**: 2026-03-23  
**测试环境**: Milk-V Duo S (SG2000), GC2083 CSI Camera
