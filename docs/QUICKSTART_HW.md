# 硬件编码版本 - 快速开始指南

**当前状态**: 已创建代码框架，需要完成 SDK 集成和编译

---

## 📋 当前进度

✅ **已完成**:
- GC2083 摄像头验证通过
- 硬件 VENC 验证通过（通过 sample_vi_fd）
- 创建 feature/hw-encode 分支
- 编写 camera_daemon_hw.c 框架代码

⏳ **进行中**:
- 获取 CVI SDK 头文件
- 实现 CVI MPI 调用
- 设置交叉编译环境

---

## 🎯 下一步（3种方案）

### 方案 A：使用官方 SDK（推荐，完整功能）

**优势**: 完整的 CVI MPI API，官方支持  
**耗时**: 2-3小时（首次设置）  
**难度**: 中等

#### 步骤

1. **获取 SDK**
   ```bash
   cd ~
   git clone https://github.com/milkv-duo/duo-buildroot-sdk.git
   cd duo-buildroot-sdk
   ```

2. **提取头文件**
   ```bash
   # 复制到项目
   cp -r middleware/v2/include/* ~/milkv/src/daemon/include/
   ```

3. **设置交叉编译**
   ```bash
   # 安装工具链
   cd duo-buildroot-sdk
   ./build.sh milkv-duo256m
   
   # 设置环境变量
   export PATH=$PATH:$(pwd)/host-tools/gcc/riscv64-linux-musl-x86_64/bin
   export CROSS_COMPILE=riscv64-unknown-linux-musl-
   ```

4. **编译**
   ```bash
   cd ~/milkv/src/daemon
   # TODO: 创建 Makefile.hw
   make -f Makefile.hw
   ```

---

### 方案 B：基于现有程序修改（最快，功能受限）

**优势**: 无需交叉编译，立即可用  
**耗时**: 30分钟  
**难度**: 简单

#### 思路

**直接使用** `sample_vi_fd` + **包装脚本**

```bash
# 在板子上创建包装脚本
cat > /opt/camera/camera_daemon_hw.sh << 'EOF'
#!/bin/sh
# Hardware encoding daemon wrapper

export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd

# 启动 RTSP 流（使用 sample_vi_fd，但不加载 AI 模型）
/mnt/system/usr/bin/sample_venc \
    -c 264 \
    -w 1920 \
    -h 1080 \
    --sensorEn 1 \
    --bitrate 4000 \
    --gop 30 \
    --rcMode 0 \
    -o /mnt/data/stream.h264 &

# 等待初始化
sleep 5

# TODO: 添加控制接口（Unix Socket）
EOF

chmod +x /opt/camera/camera_daemon_hw.sh
```

**限制**: 
- 需要研究如何绕过 sample_venc 的 segfault
- 功能可能不完整

---

### 方案 C：在板子上直接编译（中等方案）

**优势**: 不需要交叉编译环境  
**耗时**: 1-2小时  
**难度**: 中等

#### 步骤

1. **在板子上安装 gcc**
   ```bash
   # 检查是否有包管理器
   ssh root@192.168.31.219
   which opkg apt-get yum
   
   # 如果有 opkg
   opkg update
   opkg install gcc make
   ```

2. **SDK 已经在板子上**
   ```bash
   # 头文件可能在这里（需要确认）
   find /mnt/system -name "cvi_*.h" 2>/dev/null
   ```

3. **直接在板子上编译**
   ```bash
   # 上传源码
   scp camera_daemon_hw.c root@192.168.31.219:/tmp/
   
   # SSH 到板子
   ssh root@192.168.31.219
   
   # 编译
   cd /tmp
   gcc -o camera_daemon_hw camera_daemon_hw.c \
       -I/mnt/system/include \
       -L/mnt/system/lib -L/mnt/system/usr/lib \
       -lsys -lvi -lvpss -lvenc -lcvi_rtsp \
       -lpthread
   ```

---

## 🤔 我的建议

**对于现在**：

我建议你先尝试 **方案 C（板子上编译）**，原因：
1. ✅ 不需要设置交叉编译环境
2. ✅ SDK 库已经在板子上
3. ✅ 可以立即验证代码是否工作
4. ✅ 如果失败，再切换到方案 A

**具体步骤**：

```bash
# 1. 检查板子上是否能找到头文件
ssh root@192.168.31.219 "find /mnt/system -name 'cvi_sys.h' 2>/dev/null"

# 2. 如果找到了，就可以直接编译
# 3. 如果没找到，需要从 GitHub 下载并上传
```

---

## 📝 立即执行

**请执行以下命令**：

```bash
# 检查板子上的头文件
ssh root@192.168.31.219 << 'EOF'
echo "=== Searching for CVI SDK headers ==="
find /mnt/system -name "*.h" | grep -i cvi | head -20

echo ""
echo "=== Checking for gcc ==="
which gcc || echo "gcc not found"

echo ""
echo "=== SDK lib directory ==="
ls -lh /mnt/system/usr/lib/lib*.so | head -10
EOF
```

**把输出发给我**，我会根据结果决定下一步：
- 如果有头文件 → 立即在板子上编译
- 如果没有 → 帮你从 GitHub 获取并上传

---

**目标**: 在今天内完成第一个可运行的硬件编码版本！ 🚀
