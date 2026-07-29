# Working — IR Copier 开发日志

## Changelog

### 2026-07-29

- 项目 scaffold 完成：目录结构、AGENTS.md、README、PRD、RFC
- 安装 M5Stack board package 3.3.8 + M5Unified 0.2.19 + M5GFX 0.2.26
- 确认 FQBN: `m5stack:esp32:m5stack_sticks3`
- 刷 Bruce 固件验证 IR 硬件——发现黑屏 bug (#2371) + IR Read 抓噪声，放弃 Bruce
- 编写 IR Copier 固件 v1：裸 RMT 收发 + NEC 解码 + NVS 存储
- GPT review 发现 10 个 P0/P1 bug，全部修复
- PWR 电源键短按触发硬件复位，不可拦截，改为只用 BtnA + BtnB
- 按钮映射确认：BtnA=正面（短按确认/双击返回），BtnB=侧面（短按切换）
- 刷入设备，UI 正常，但 IR RX 收到三星 TM2360E 信号 timing 严重失真（d0=76us）
- GPT 分析确认：三星 SolarCell 遥控器走蓝牙，私有短协议非标准 NEC，StickS3 IR 接收器无法正确解调
- 尝试 raw symbol 录音机模式回放——76us mark 太短，电视无反应
- 改为 IRremoteESP8266 预设 Samsung32 码发送——电视仍无反应（电视日常走蓝牙控制）
- 调研确认：三星 Neo QLED 8K 电视 SolarCell 遥控器走蓝牙，红外 fallback 不工作
- 转向 SmartThings API 方案：通过 WiFi + HTTPS 控制 Samsung 电视
- SmartThings PAT 生成、电视添加到 SmartThings、API 开关测试全部成功
- 固件改为 WiFi + SmartThings API：菜单选 Power/Vol+/-/Mute/Source/CH+ → HTTP POST 到 SmartThings
- secrets.h（gitignored）存 WiFi + SmartThings token + device ID
- 编译上传成功，StickS3 成功开关电视

## Lessons Learned

### 硬件 / 开发环境

- StickS3 USB 是 ESP32-S3 原生 USB-Serial/JTAG，pyserial 在 macOS 上 CDC 握手不稳定。按住 BOOT 键插 USB 进 download mode 后端口稳定。
- M5Stack board package ≠ espressif esp32 core。StickS3 board 定义只在 `m5stack:esp32` 包里。FQBN 是 `m5stack:esp32:m5stack_sticks3`。
- Arduino 要求 .ino 放在同名目录下（`src/ir_copier/ir_copier.ino`）。
- StickS3 内置 250mAh 电池，拔 USB 不断电。刷完固件后可能需要拔插或按一下 PWR 键才能正常启动。

### 按钮系统

- PWR 键是 M5PM1 PMIC 硬件级控制，短按触发硬件复位，固件无法拦截。不要用 PWR 做 UI。
- BtnA = 正面按钮 (GPIO11)，BtnB = 侧面按钮 (GPIO12)。侧键翻页 + 正键确认符合横屏握持习惯。
- wasPressed() 在按下瞬间触发，会吞掉 wasDoubleClicked()。双击+单击并存时用 wasSingleClicked() + wasDoubleClicked()。

### 红外 IR

- StickS3 IR 接收前必须 `M5.Speaker.end()` 关闭功放。
- Bruce 固件 v1.16 在 StickS3 有黑屏 bug (#2371) + IR Read 不校验噪声。
- 三星 SolarCell Smart Remote (VG-TM2360E) 日常走蓝牙不走红外，红外是 fallback。私有短协议（13 symbol / 21.5ms）无法被 StickS3 IR 接收器正确解调。
- Samsung32 协议：address byte 发两次，然后 command + inverse。标准 power toggle = 0xE0E040BF。
- IRremoteESP8266 的 API 是 `sendSAMSUNG`（全大写）。
- 结论：对高端三星电视，红外方案不适用，改用 SmartThings API。

### SmartThings API

- PAT 24 小时过期（2024-12-30 后创建的）。长期方案需要 OAuth2 app。
- 命令格式：POST `https://api.smartthings.com/v1/devices/{deviceId}/commands`，body `{"commands":[{"component":"main","capability":"switch","command":"on"}]}`
- 查状态：GET `https://api.smartthings.com/v1/devices/{deviceId}/components/main/status`
- 返回 `COMPLETED` = 同步完成，`ACCEPTED` = 异步已接受（开机可能先 ACCEPTED）
- 电视需在设置中开启 "Power On with Mobile"（Settings → All Settings → Connections → Network → Expert Settings）
- Wake-on-LAN 不需要——SmartThings API 可以直接开机
- ESP32 WiFiClientSecure + `setInsecure()` 跳过证书验证，HTTPS 正常工作
- 36% Flash 使用量（WiFi+HTTPS 库较大）