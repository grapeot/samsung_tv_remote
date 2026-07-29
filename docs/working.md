# Working — IR Copier 开发日志

## Changelog

### 2026-07-29

- 项目 scaffold 完成：目录结构、AGENTS.md、README、PRD、RFC
- 安装 M5Stack board package 3.3.8 + M5Unified 0.2.19 + M5GFX 0.2.26
- 确认 FQBN: `m5stack:esp32:m5stack_sticks3`
- 编写主固件 `src/ir_copier/ir_copier.ino`（Copy/Replay 菜单 + NEC 收发 + NVS 存储）
- 编写编译/上传脚本
- GPT review 发现多个 P0/P1 bug，全部修复：BtnPWR 硬件复位不可拦截、sketch 目录结构、长按删除无效、RX 取消、ISR 同步、NEC 校验、NVS blob 存储、满槽处理、Replay 返回菜单
- PWR 电源键短按触发硬件复位，固件无法拦截。改为只用 BtnA + BtnB
- 按钮映射确认：BtnA = 正面按钮（短按确认/进入，双击返回），BtnB = 侧面按钮（短按切换/翻页）
- 固件编译上传成功，设备正常运行

## Lessons Learned

- StickS3 的 USB 是 ESP32-S3 原生 USB-Serial/JTAG，不是传统 UART 桥接芯片。pyserial 在 macOS 上对这个 CDC 设备握手不稳定，端口会频繁消失。解决方法：按住 BOOT 键插 USB 进 download mode，端口就稳定了，esptool 能连上。
- Bruce 固件 v1.16 在 StickS3 上有已知 bug：背光初始化顺序导致黑屏（GitHub issue #2371），IR Read 抓到环境噪声不校验。这是自己写固件的动力。
- StickS3 IR 接收前必须调用 `M5.Speaker.end()` 关闭功放，否则功放干扰 IR RX 信号。
- M5Stack board package 和 espressif 原生 esp32 core 是两个不同的 package。StickS3 只在 M5Stack 的 package 里有 board 定义。FQBN 是 `m5stack:esp32:m5stack_sticks3`（注意不是 `m5sticks3`）。
- PWR 电源键是 M5PM1 PMIC 硬件级控制，短按触发硬件复位，固件无法拦截。不要用 PWR 做 UI 操作。
- Arduino 要求 .ino 文件放在同名目录下（`src/ir_copier/ir_copier.ino`），直接放 `src/ir_copier.ino` 会报 main file missing。
- StickS3 有内置电池（250mAh），拔 USB 不会断电也不会重启。刷完固件后如果设备停在 download mode，需要拔插 USB 或快速按一下 PWR 键才能正常启动。
- 按钮映射：正面按钮 = BtnA (GPIO11)，侧面按钮 = BtnB (GPIO12)。侧键翻页 + 正键确认符合横屏握持习惯。
- M5Unified Button_Class 支持 wasDoubleClicked()，双击检测可靠。