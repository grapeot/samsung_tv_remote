# Samsung TV Remote — M5StickS3

运行在 M5StickS3 上的极简三星电视遥控器。通过 WiFi + SmartThings API 控制电视开关。

## 功能

- **主屏**：大字显示电视 ON / OFF 状态
- **BtnA (正面) 短按**：开关电视（toggle power）
- **BtnB (侧面) 短按**：状态页（音量、静音、IP、SSID）
- 10 秒自动刷新状态

## 硬件要求

- M5StickS3（ESP32-S3，WiFi）
- USB-C 数据线
- 与电视在同一局域网的 WiFi

## 配置

1. 复制 `src/samsung_tv_remote/secrets.example.h` 为 `src/samsung_tv_remote/secrets.h`
2. 填入 WiFi SSID、WiFi 密码、SmartThings token、电视 device ID
3. SmartThings token 从 https://account.smartthings.com/tokens 生成（需 `r:devices:*`、`w:devices:*`、`x:devices:*` 权限）
4. device ID 通过 `curl -H "Authorization: Bearer YOUR_TOKEN" https://api.smartthings.com/v1/devices` 查询

### 电视设置

在电视上开启 "Power On with Mobile"：
Settings → All Settings → Connections → Network → Expert Settings → Power On with Mobile → On

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

- SmartThings PAT 24 小时过期，需定期重新生成
- 仅支持 Samsung 电视（通过 SmartThings API）

## 许可证

MIT