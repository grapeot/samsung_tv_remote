# Samsung TV Remote — M5StickS3

运行在 M5StickS3 上的极简三星电视遥控器。通过本地 Tizen WebSocket API 控制电视开关。无需 SmartThings 云 PAT（24h 过期），使用一次性本地配对 token（长期有效）。

## 功能

- **主屏**：大字显示电视 ON / OFF 状态
- **BtnA (正面) 短按**：开关电视（KEY_POWER toggle）
- **BtnB (侧面) 短按**：状态页（电源、配对状态、IP、SSID）
- 首次运行自动配对（电视屏幕按"允许"）

## 硬件要求

- M5StickS3（ESP32-S3，WiFi）
- USB-C 数据线
- 与电视在同一局域网的 WiFi
- 三星 Tizen 智能电视（2016+，支持 `TokenAuthSupport`）

## 配置

1. 复制 `src/samsung_tv_remote/secrets.example.h` 为 `src/samsung_tv_remote/secrets.h`
2. 填入 WiFi SSID、WiFi 密码、电视 IP 地址
3. 电视 IP 通过 mDNS 发现：`dns-sd -B _samsungmsf._tcp local`
4. 编译上传后，首次运行会自动配对——电视屏幕弹出"Allow"提示，按允许即可
5. 配对 token 存入设备 NVS，后续启动自动复用，无需重复配对

## 编译与上传

```bash
scripts/build.sh
scripts/upload.sh
```

## 项目结构

```
samsung_tv_remote/
├── AGENTS.md
├── README.md
├── .gitignore
├── docs/
│   ├── prd.md
│   ├── rfc.md
│   └── working.md
├── src/
│   └── samsung_tv_remote/
│       ├── samsung_tv_remote.ino  # 主固件
│       ├── secrets.h              # 凭证（gitignored）
│       └── secrets.example.h      # 凭证模板
└── scripts/
    ├── build.sh
    └── upload.sh
```

## 已知限制

- Volume 和 Muted 不可读（本地 WS 协议限制，状态页显示 N/A）
- KEY_POWER 是 toggle，不是绝对 on/off
- 电视完全断电后需手动开机（浅待机可 WS 唤醒）
- 仅支持 Samsung Tizen 电视

## 许可证

MIT