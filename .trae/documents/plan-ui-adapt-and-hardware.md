# IN OS 全屏适配 + 触摸/指纹/摄像头 + 设计稿 UI 补全 实施计划

## Context（背景）
真机面板为 **2160×1440（3:2）**，但 [efi64.asm](../../efi64.asm) GOP 选择逻辑"优先 1024×768/800x600"，导致真机被固件拉伸变形（比例不对）。设计稿逻辑分辨率 2160×1400 与真机几乎同比例，修复引导选模式后即可近乎点对点。同时用户要求：补全设计稿 03 负一屏 / 04 多任务（+消息页）、dock 升级为四 tab 胶囊、触摸屏真驱动（USB-HID 与 I2C-HID 两条路）、指纹/摄像头检测与状态真实化。裸机无驱动框架，MIPI 摄像头（需 ISP 固件）物理上不可驱，本轮做到"检测+状态显示"硬上限；触摸屏 I2C 路径为盲写，全部探测失败时必须优雅降级且绝不卡死主循环。

## 关键约束
- **内核体积硬上限**：内核加载物理 0x10000、-Ttext 0x10200，kernel.bin 超过 ~574KB（0x8FE00）会越过 0xA0000 保留区。当前 337KB，新增字库+驱动预计 ~480KB，**build_fat32_iso.py 加体积闸门 assert**，超限先砍 12 档字库。
- TCC：不支持函数内数组初始化（新页数据提为文件级 static）、嵌套宏 ##（沿用 gen_font.py 模板生成）。
- 所有 UI 绘制走 UX/UY；直连 g_fb_w 的仅背景填充与光标 stamp（保持全屏）。
- I2C/I2C-HID 所有等待循环必须带迭代上限，禁止裸 while。

## 阶段 A：引导选模式 + 等比缩放（地基）
**efi64.asm**
- L272-329 重写 GOP mode_loop：QueryMode 枚举全部模式，记录**总像素最高**的模式号（并列取宽度大者），失败回退现逻辑。真机即选中 2160×1440。
- ExitBootServices 前遍历 EFI SystemTable→ConfigurationTable（SystemTable+112 数量、+120 数组，以实际结构体为准核对偏移），匹配 ACPI 2.0 GUID（8868E871-E4F1-11D3-BC22-0080C73E8889），把 RSDP 物理地址写约定地址 **0x510**（写前确认 0x500-0x5FF 未被 loader/kernel 占用，冲突则改 0x710）。

**ui_shell.h**
- 新增 `g_ux_s`(千分比)/`g_ux_ox`/`g_ux_oy` 与 `ui_scale_init()`（[ui_enter](../../ui_shell.h#L498) 首行调用）：`s=min(gw*1000/2160, gh*1000/1400)`，ox/oy 居中。
- [UX/UY](../../ui_shell.h#L33-L34) 改为 `ox + x*s/1000`；ui_tick 命中区逆映射（L521-522）同步改 letterbox 版。
- 文字选档：[T16/T22](../../ui_shell.h#L166-L180) 从 {12,16,22} 选最近档、T160 从 {104,128,160}。T22B 竖向偏移跟随所选档半高。
- 真机 2160×1440 时 s=1000‰，上下各 ~20px 同色边带，视觉无感。

**gen_font.py / build_fat32_iso.py**
- 字库档位循环加 `(12,'full')`、`(128,'digits')`；拷贝 font_cn12.h/font_cn128.h；gui_cn.h 增 include。
- **体积闸门**：链接后 assert kernel.bin ≤ 560KB，超限报错并提示砍 12 档。
- 新建（重建）`C:\qemu_iso\_uishot.py`：QMP screendump + 逻辑→物理 letterbox 映射的点击封装（HMP 大步长+100ms 防事件合并丢失，沿用已验证参数）。

**验证**：std VGA 1024×768（上下边带、内容不变形）；`-vga virtio -device virtio-gpu-pci,xres=2160,yres=1440` 模拟真机面板（近点对点）；`xres=1920,yres=1080`（16:9 左右边带应力用例）。

## 阶段 B：设计稿补全 —— 新屏 + 四 tab dock（依赖 A）
**ui_shell.h**
- 新增 `ui_render_hub`(负一屏, page=4)、`ui_render_multi`(多任务, page=5)、`ui_render_msg`(消息占位, page=7)；ui_render() 分支扩展。
  - 负一屏（设计稿 03）：智能中心标题+小艺推荐 chip；蓝色渐变横幅（今日推荐/通勤路上，听点轻松的/右侧白圆播放钮，复用 ui_vgrad+ui_disc+ui_tri）；三列卡片栅格：天气+日程卡（22°、10:30 产品评审会、14:00 设计走查、蓝 pill 带伞提醒）、常用服务 6 圆角图标（扫码/付款/乘车码/快递/卡包/更多）、最近使用 5 图标、通勤模式卡（开始导航/换乘路线按钮）、办公场景卡、出行助手卡（2号线+进度线）、晚安模式深色卡、健康提醒卡。
  - 多任务（设计稿 04）：标题+副题"4 个应用在后台运行"；右侧 chips（全屏/分屏/悬浮窗+全部关闭）；4 张应用卡（图库/浏览器/视频/备忘录），缩略图全部矢量画（渐变+形状，图库=山景三角、浏览器=城市条块、视频=红色剧场、备忘录=横线本），名称栏+× 关闭钮（可点移除、计数联动）；分屏组合卡（图库|浏览器双窗格）+ 右侧蓝色"长按应用卡片"卡。
  - 消息占位：静态会话列表（复用卡片样式）。
- `ui_draw_dock(page)`：四 tab 胶囊（主页/发现/消息/个人中心，活动 tab 蓝胶囊白字，非活动灰字）替换现有三处旧 dock；命中→1/4/7/3。
- `ui_draw_dots()`：底部页面圆点可点，翻主层级页 {1,4,7,5}。
- 设置菜单数组加"多任务与分屏"（→5）与"硬件检测"（→6）行；ui_tick 扩 page4-7 命中。
- TCC 限制：所有新页表格数据用文件级 static 数组。

**验证**：逐屏 screendump 与设计稿比对；八页互跳无死路；dock/圆点/关闭钮命中正确。

## 阶段 C：USB-HID digitizer + 指纹/摄像头 USB 识别（依赖 A，可与 B 并行）
**usb_xhci.h**
- [usb_enumerate](../../usb_xhci.h#L517-L604) 放宽接口 proto 门（proto=0 也枚举），GET_DESCRIPTOR(0x22) 读报告描述符 ≤256B。
- 新增 `usb_parse_digitizer()`：usage page 0x0D、X(0x30)/Y(0x31)/Tip(0x42) 位域提取（解析结果文件级 static）。
- usb_dev_t 加 is_digi/logic_max 字段；[usb_poll](../../usb_xhci.h#L336-L410) 加绝对坐标分支→写 cur_x/cur_y 并置 cur_btn 按下/抬起边沿（触摸=移动+点击，与 USB 鼠标共用光标，最后动作者生效）。
- VID 白名单（Goodix 27C6 等）记 `usb_fp_present`；接口类 0x0E（UVC）记 `usb_cam_present` 后跳过配置。

**验证**：QEMU `-device usb-tablet`（绝对坐标 HID）先验解析与点击；真机烧录后验实物。解析失败仅禁用该设备，不伤键鼠。

## 阶段 D：I2C-HID 盲写全栈（独立、最需防御）
新建 **i2c_hid.h**（kernel_full.c L1064 后 include）：
- 复用 [pci_read32](../../usb_xhci.h#L28) 扫 Intel LPSS I2C 控制器（8086:9D60-9D68/A160-A168，class 0x0C8000 兜底）；复用 paging_map_mmio 映 BAR；LPSS 私有寄存器复位/CLK_GATE/分频初始化。
- `i2c_xfer()` 每步轮询带迭代上限；地址 0x03-0x77 扫描，读 2 字节判 I2C-HID 描述符 magic（bcd 0x0100 + ASCII "HIDI"）→ 读 HID 描述符 → 读报告描述符 → 最小 digitizer 解析（触点数/X/Y/触压字段）。
- ui_tick 每 tick 一次**非阻塞** `i2c_hid_poll()`（上次传输未完则跳过）；触点映射逻辑坐标注入 UI（等效触摸）。

**防御设计（最坏情况全部覆盖）**：BAR 全 F / 控制器复位不来 / 地址 NACK / 描述符超限 / 解析失败 / 轮询超时 → 一律置 `i2c_degraded=1` 永久禁用，UI 显示"未检测到"；保证任何情况不阻塞主循环。
**验证**：QEMU 判据 = 检测屏显示"no LPSS controller, disabled"且 UI 一切正常（无 LPSS 是预期）。真机结果视探测而定。回退：注释一行 include 即整体关闭。

## 阶段 E：硬件检测屏(page=6) + 状态真实化（依赖 A/C/D）
新建 **hwprobe.h**：
- 启动一次 PCI 全扫（bus0-255 全域、缓存 ≤256 条：bus/dev/fn/class/ven:dev）。
- ACPI：读 0x510 RSDP → RSDT/XSDT 签名列表；DSDT AML 限次扫描 "_HID" 后 ASCII 设备 ID（ELAN/MSFT0001/INTC 等），辅助判断 I2C 触摸屏存在。
- 汇总 GOP 分辨率/USB 设备/指纹/摄像头/I2C 状态到 `hw_info`。
- `ui_render_hw()`：分组表格渲染（超一页可点翻页）。
- 状态真实化：锁屏指纹提示（无指纹→"上滑或点击解锁"）、设置"关于本机"外设行、摄像头/触摸屏/指纹状态行按 hw_info 输出。

**验证**：QEMU 检测屏出现 1234:1111 VGA、8086 xHCI 等 PCI 表与 ACPI 签名，8 页回归无异常。

## 阶段 F：回归 + 烧录
- 三分辨率全屏截图回归（std 1024×768 / virtio 2160×1440 / virtio 1920×1080）。
- QMP 键鼠回归八页交互 + usb-tablet 触摸点击闭环。
- 体积闸门复查（kernel.bin ≤560KB）。
- `burn_usb.py` 四重闸门 + SHA256 烧录真机；真机判据：3:2 不拉伸、触摸可点（USB 或 I2C 路径其一）、检测屏信息与 Windows 设备管理器对照。

## 关键文件
| 文件 | 改动 |
|---|---|
| [efi64.asm](../../efi64.asm) | GOP 最高像素选模 + RSDP→0x510 |
| [ui_shell.h](../../ui_shell.h) | letterbox UX/UY、选档文字、3 新屏、四 tab dock、圆点翻页、命中扩展 |
| [usb_xhci.h](../../usb_xhci.h) | digitizer 枚举/解析/绝对指针、指纹/摄像头识别 |
| **i2c_hid.h（新）** | LPSS I2C + I2C-HID 全栈（防御式） |
| **hwprobe.h（新）** | PCI/ACPI/汇总检测 |
| [gen_font.py](../../gen_font.py) / [build_fat32_iso.py](../../build_fat32_iso.py) | 12/128 档、体积闸门 |
| [kernel_full.c](../../kernel_full.c) | include 两个新头 |
| C:\qemu_iso\_uishot.py（重建） | letterbox 映射 + 多分辨率用例 |
