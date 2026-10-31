# IN OS QEMU 虚拟机双端 App（安卓版 + 鸿蒙版）实施计划

## Context

把已验证的 IN OS 系统镜像（`uefi_boot.img`，68157440B，SHA256 `AFF3BB02…4C6E8`，含开机动画）做成两个 QEMU 虚拟机软件：
- **安卓版**：Kotlin APK，内置交叉编译的 aarch64 `qemu-system-x86_64`
- **鸿蒙版**：HarmonyOS NEXT（DevEco 6.1 已就绪）ArkTS 工程

共同功能：RAM 分配、ROM（虚拟磁盘）分配、NPU 开关 + 华为旧版 HIAI / 新版 NN 接口选择、启动虚拟机并显示画面。

已确认的决策（用户选定）：
1. NPU 本期 = **设置 + 真实能力检测 + 桥接口预留**（真实 guest 加速 = Phase 2 自研 virtio-npu）
2. 鸿蒙版 = **HarmonyOS NEXT 原生工程**（QEMU 引擎 Phase 2，本期 UI/设置/NPU 检测可跑）
3. 安卓环境本机缺失 = **自动安装 JDK17 + SDK + NDK**
4. QEMU 引擎 = **WSL + NDK 源码交叉编译**

## 关键技术决策

### 显示/输入通道：VNC 回环（不用 SDL）
- QEMU 以 `-vnc 127.0.0.1:1` 启动，App 内嵌**自研精简 RFB 客户端**（raw 编码，Kotlin/ArkTS 各约 500 行）
- 理由：VNC 解耦 qemu 进程与 UI；键盘事件走 QEMU 标准注入路径 → guest 的 PS/2 i8042（IN OS 的 `kb_poll`/动画跳过键均兼容）；鸿蒙侧无现成 VNC 库，自研两侧代码对称
- guest 分辨率由 OVMF/GOP 决定（~1024×768×32），raw 编码回环带宽可接受

### ROM 分配 = qcow2 overlay
- `qemu-img create -f qcow2 -b INOS.img overlay.qcow2 <用户设定 1–32GB>`
- 启动挂 overlay，基础镜像只读不被写坏；"重置系统"= 删 overlay 重建

### 安卓可执行文件限制（Android 10+）
- qemu 二进制必须放 `jniLibs/` 改名 `libqemu.so`，从 `nativeLibraryDir` exec（系统只允许执行该目录）
- RAM 分配 → `-m <64–512>`；`-accel tcg,thread=multi`；`-M pc -vga std` + OVMF pflash（CODE readonly + VARS 拷到 App 私有目录可写）

### NPU（本期真实可交付范围）
- 安卓侧：`NpuDetector` 接口 → 华为 HiAI NNR SDK（AAR 放 libs/，华为仓库下载；下载不到则退化为 Kirin 芯片探测并如实标注）
- 鸿蒙侧：`@ohos.ai.neuralNetworkRuntime`（NNRt）真实 API 检测 + 设备信息核对；旧版 HIAI 在 NEXT 已由 NNRt 承接（UI 如实标注）
- 桥接口预留：`VmConfig{ramMB, diskGB, npuEnabled, npuApi: hiai|nn}` 持久化 + 启动参数生成器中预留 NPU 设备参数位（Phase 2 接 virtio-npu）

## 实施步骤

### 阶段 1：安卓环境安装
- winget 装 Temurin/MS OpenJDK 17；下载 cmdline-tools → `sdkmanager` 装 `platform-tools`、`platforms;android-34`、`build-tools;34.0.0`、`ndk;26.x`（装到 `%LOCALAPPDATA%\Android\Sdk`）
- 验证：`java -version`、`adb version`、`sdkmanager --list_installed`

### 阶段 2：WSL 交叉编译 QEMU（成败关键，风险最高）
- WSL 内下载 NDK r26d linux 版，构建链：**glib（含 libffi/pcre2 静态）→ pixman → QEMU 9.x**
- QEMU 配置：`--cross-prefix=aarch64-linux-android- --target-list=x86_64-softmmu --enable-vnc --disable-sdl --disable-gtk --disable-x11 --disable-opengl --disable-curl --disable-seccomp`，保留 tools（要 qemu-img）
- 参考 termux-packages 的 qemu 补丁集解决 bionic 兼容问题
- 产出：`libqemu.so`（可执行）+ `qemu-img`
- 冒烟验证：二进制推到用户手机 Termux 直接跑 INOS.img（快速验证引擎，不等 APK）

### 阶段 3：安卓 App（`c:\Users\PC\Desktop\INOS_VM_Android\`）
- Kotlin，minSdk 26 / target 34，经典 View + Material（不引 Compose，降低构建复杂度）
- 模块：`MainActivity`（设置页+屏幕页）、`VmConfig`（持久化）、`QemuLauncher`（argv 生成 + 进程管理 + logcat 面板）、`MiniVncView`（RFB raw + 触摸指针 + 软键盘按键）、`NpuDetector`
- assets 内置：INOS.img、OVMF_CODE.fd、OVMF_VARS.fd（首启拷私有目录）
- 构建：`gradlew assembleDebug` 出 APK；**真机验证需用户手机**（本机 x86 模拟器跑不了 aarch64 so）

### 阶段 4：鸿蒙 App（`c:\Users\PC\Desktop\INOS_VM_Harmony\`）
- ArkTS / stage 模型：`Index.ets`（设置+状态）、`VmConfig.ets`、`NpuDetector.ets`（NNRt）、`MiniVnc.ets`（TCPSocket + PixelMap）、`QemuBridge.ets`（Phase 2 NAPI 占位，含接口注释）
- 验证：`hvigorw assembleHap` → DevEco Emulator 6.1（本机）安装跑 UI + NPU 检测页；真机按 OpenHarmony 自动签名流程

### 阶段 5：交付
- 桌面产出：`INOS_VM_Android.apk`、`INOS_VM_Harmony/` 工程与 HAP、NPU 桥接协议说明（VmConfig JSON schema + Phase 2 virtio-npu 路线图）
- 更新项目记忆

## 明确不在本期范围
- guest 真实 NPU 加速（需 virtio-npu 虚拟设备 + IN OS 内核驱动 + 宿主桥服务）
- 鸿蒙端 QEMU 原生引擎（NAPI .so 移植）——鸿蒙端本期可完整运行设置/NPU 检测，启动虚拟机按钮给出 Phase 2 提示
- 音频/网络设备直通

## 验证方式汇总
1. 阶段 2 产物在 Termux 真机跑通 INOS.img（VNC 用任意手机 VNC 客户端连回环）
2. APK 装用户真机：设置 RAM/ROM → 启动 → 看到开机动画 → 控制台 → 键盘输入
3. HAP 装 DevEco 模拟器：设置页操作 + NPU 检测输出真实设备能力
