# RFC — Samsung TV Remote 技术设计

## 架构变更：SmartThings 云 API → 本地 Tizen WebSocket API

原方案通过 SmartThings 云 API（HTTPS + PAT）控制电视。SmartThings PAT 24 小时过期，不适合 always-on 遥控器。新方案改为本地 Tizen WebSocket API（wss://TV_IP:8002），token 是设备本地一次性凭证，不过期。

## 本地 Tizen WS 协议

### 连接

```
wss://TV_IP:8002/api/v2/channels/samsung.remote.control?name=<base64_app_name>&token=<token>
```

- Port 8002 (WSS, 自签证书，跳过验证)
- `name`: base64 编码的应用名（显示在电视授权框和允许设备列表中）
- `token`: 首次配对时电视返回的本地 token，后续复用

### 配对流程

1. 连接 WS（不带 token）
2. 电视屏幕弹出"是否允许 [AppName] 连接"授权框
3. 用户在电视上按"允许"
4. 电视在 `ms.channel.connect` 事件中返回 token
5. token 存入 NVS（Preferences），后续连接带上 token 免授权

### Key 事件

```json
{
  "method": "ms.remote.control",
  "params": {
    "Cmd": "Click",
    "DataOfCmd": "KEY_POWER",
    "Option": "false",
    "TypeOfRemote": "SendRemoteKey"
  }
}
```

支持的 key：`KEY_POWER`, `KEY_VOLUP`, `KEY_VOLDOWN`, `KEY_MUTE`, `KEY_HOME`, `KEY_SOURCE` 等。

### 状态查询

REST 端点 `https://TV_IP:8002/api/v2/` 返回设备信息 JSON，包含 `device.PowerState`（`on` / `standby` / 瞬态空字符串）。

### 协议限制

- **KEY_POWER 是 toggle**，不是绝对 on/off。遥控器只做 toggle。
- **Volume 和 Muted 不可读**。本地 WS 协议只发 key 事件，不返回当前音量或静音状态。
- **一个连接一个 key**。连续多 key 会触发 `ConnectionResetError`，每次发 key 新建连接。
- **浅待机端口存活**。QN900C 待机态 8002 端口仍可达，KEY_POWER 可唤醒。

## 硬件

M5StickS3 (ESP32-S3-PICO-1-N8R8)：
- WiFi 用于 WS 连接
- 135x240 ST7789 LCD 竖屏
- 250mAh 电池
- BtnA (GPIO11) + BtnB (GPIO12)

## 库依赖

- M5Unified + M5GFX（UI）
- ArduinoWebsockets（WSS 连接，自签证书跳过）
- ArduinoJson（解析配对响应）
- Preferences（NVS 存储 token）

## 省电策略

- CPU 80MHz
- WiFi MAX_MODEM sleep
- TX 13dBm
- 背光三级：15s dim → 60s off → 120s light sleep
- Light sleep 保留 WiFi 关联，GPIO 唤醒

## 安全考量

- WSS 自签证书跳过验证（LAN 信任模型，同 smart_home service）
- Token 存 NVS，不写入 secrets.h
- Token 不过期但可被电视端撤销（重新配对即可）