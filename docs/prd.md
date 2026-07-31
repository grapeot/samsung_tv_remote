# PRD — Samsung TV Remote (M5StickS3)

## 目标

在 M5StickS3 上实现一个极简的三星电视遥控器，通过本地 Tizen WebSocket API 控制电视开关、音量和静音。不依赖 SmartThings 云 PAT（24h 过期），使用一次性本地配对 token（长期有效）。

## 用户

拥有 M5StickS3 和三星 Tizen 智能电视（2016+）的个人用户。希望用一个袖珍设备替代或备份电视遥控器。

## 核心需求

1. **配对**：首次运行时连接电视 WebSocket，电视屏幕弹出授权框，用户按允许后 token 存入 NVS，后续自动复用。
2. **电源控制**：BtnA 短按 toggle 电源（KEY_POWER）。
3. **状态查询**：通过 REST 端点查询 PowerState，显示 ON/OFF。
4. **状态页**：BtnB 短按显示电视状态（电源、配对状态、IP、SSID）。
5. **省电**：背光三级省电（15s 减暗 → 60s 熄灭 → 120s light sleep），按钮唤醒。
6. **持久化**：配对 token 断电不丢失（存入 NVS）。

## 成功标准

- 首次配对成功后，token 存入 NVS，重启后免配对
- 能成功开关电视、调节音量、静音
- 状态页正确显示电源状态
- 背光省电和 light sleep 正常工作
- 续航在 250mAh 电池下可接受

## 非目标

- 不支持红外（三星高端电视走蓝牙遥控，红外不工作）
- 不做 SmartThings 云 API（PAT 24h 过期问题）
- 不做手机 App 或 OTA
- Volume 和 Muted 不可读（本地 WS 协议限制）