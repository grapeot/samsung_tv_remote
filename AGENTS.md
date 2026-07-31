# AGENTS.md — Samsung TV Remote (M5StickS3)

## 项目概述

M5StickS3 Samsung 电视遥控器固件。WiFi + 本地 Tizen WebSocket API（wss://TV_IP:8002），竖屏白底 UI。BtnA 开关电视，BtnB 查看状态。首次运行自动配对，token 存 NVS。

## 项目结构

- `src/samsung_tv_remote/samsung_tv_remote.ino` — 主固件
- `src/samsung_tv_remote/secrets.h` — 凭证（gitignored）
- `src/samsung_tv_remote/secrets.example.h` — 凭证模板
- `scripts/build.sh` — arduino-cli 编译脚本
- `scripts/upload.sh` — arduino-cli 上传脚本
- `docs/prd.md` — 产品需求
- `docs/rfc.md` — 技术设计决策
- `docs/working.md` — 开发日志与避坑记录

## 开发环境

- **Board**: M5StickS3 (FQBN: `m5stack:esp32:m5stack_sticks3`)
- **Arduino Core**: M5Stack board package (>= 3.2.5)
- **Libraries**: M5Unified (>= 0.2.12), M5GFX (>= 0.2.18)
- **Toolchain**: arduino-cli（已通过 Homebrew 安装）
- **Board Manager URL**: `https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json`

## 编译与上传

```bash
scripts/build.sh        # 编译
scripts/upload.sh       # 上传到 /dev/cu.usbmodem101
```

上传前如果设备在运行固件（非 download mode），可能需要手动进 BOOT 模式（按住 BOOT 键插 USB）。

## working.md 维护

每次改动后更新 `docs/working.md` 的 Changelog。遇到坑记入 Lessons Learned。

## 版本控制

只有用户明确要求时才 commit。提交时使用小而可回滚的粒度。

## 硬件约束

- IR_TX = GPIO 46（内置 IR LED）
- IR_RX = GPIO 42（内置 IR 接收器，需 RMT 外设驱动）
- 接收红外前必须调用 `M5.Speaker.end()` 关闭功放，否则功放干扰 IR 接收
- IR 收发器位于设备顶部（USB-C 口对面）
- 发射距离约 2-3 米，需正对电视