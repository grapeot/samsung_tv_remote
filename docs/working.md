# Working — Samsung TV Remote 开发日志

## Changelog

### 2026-07-30

- 从 SmartThings 云 API 迁移到本地 Tizen WebSocket API（wss://TV_IP:8002）
- 消除 SmartThings PAT 24h 过期问题
- 配对 token 存入 NVS（Preferences），首次配对后长期复用
- 固件重写：ArduinoWebsockets 库替代 HTTPClient，通过 WS 发 key 事件
- 状态查询改用 REST 端点 `https://TV_IP:8002/api/v2/` 的 PowerState
- 状态页 volume/muted 改为 "N/A"（本地协议不可读）
- secrets.h 简化：只需 WiFi + TV_HOST，token 自动配对存 NVS
- 新增配对流程 UI：首次运行显示 "Press ALLOW on TV screen"
- 文档全部重写（PRD/RFC/working.md/README/AGENTS.md）

### 2026-07-29 (final)

- 项目从 IR Copier 改名为 Samsung TV Remote
- UI 重设计：竖屏白底黑字，FreeSansBold24pt7b 大字 ON/OFF
- 按钮方案：BtnA 短按=开关电视，BtnB 短按=状态页
- 转向 SmartThings API 方案（已废弃，见上方迁移）
- SmartThings PAT 生成、电视添加到 SmartThings、API 开关测试成功
- 固件改为 WiFi + SmartThings API
- 编译上传成功，StickS3 成功开关电视

## Lessons Learned

### 硬件 / 开发环境

- StickS3 USB 是 ESP32-S3 原生 USB-Serial/JTAG，pyserial 在 macOS 上 CDC 握手不稳定。按住 BOOT 键插 USB 进 download mode 后端口稳定。
- M5Stack board package ≠ espressif esp32 core。StickS3 board 定义只在 `m5stack:esp32` 包里。FQBN 是 `m5stack:esp32:m5stack_sticks3`。
- StickS3 内置 250mAh 电池，拔 USB 不断电。刷完固件后可能需要拔插或按一下 PWR 键才能正常启动。

### 按钮系统

- PWR 键是 M5PM1 PMIC 硬件级控制，短按触发硬件复位，固件无法拦截。不要用 PWR 做 UI。
- BtnA = 正面按钮 (GPIO11)，BtnB = 侧面按钮 (GPIO12)。
- wasPressed() 在按下瞬间触发，会吞掉 wasDoubleClicked()。双击+单击并存时用 wasSingleClicked() + wasDoubleClicked()。

### 红外 IR（已废弃）

- 三星 SolarCell Smart Remote 走蓝牙，红外 fallback 不工作。StickS3 IR 接收器无法解调私有短协议。
- 结论：对高端三星电视，红外方案不适用。

### SmartThings API（已废弃）

- PAT 24 小时过期（2024-12-30 后创建的）。长期方案需要本地 WS API。
- 命令格式：POST `https://api.smartthings.com/v1/devices/{deviceId}/commands`
- ESP32 WiFiClientSecure + `setInsecure()` 跳过证书验证，HTTPS 正常工作
- 36% Flash 使用量（WiFi+HTTPS 库较大）

### 本地 Tizen WebSocket API

- Token 是设备本地一次性凭证，不过期。首次配对在电视屏幕按"允许"即可。
- KEY_POWER 是 toggle，不是绝对 on/off。遥控器只做 toggle。
- Volume 和 Muted 不可读。本地 WS 协议只发 key 事件，不返回音量或静音状态。
- 一个连接一个 key。连续多 key 触发 ConnectionResetError，每次新建连接。
- QN900C 浅待机态端口 8002 仍存活，KEY_POWER 可从待机唤醒。
- PowerState 瞬态：关机后短暂返回空字符串，需等几秒后查询才稳定。
- ArduinoWebsockets 库支持 WSS + 自签证书跳过（beginSslWithBin）。
- 配对 token 存 NVS（Preferences），不写入 secrets.h，断电不丢失。