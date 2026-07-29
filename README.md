# Samsung TV Controller — M5StickS3

运行在 M5StickS3 上的三星电视控制器。通过 WiFi + SmartThings API 控制电视开关、音量、静音、信号源、频道。

> **注意：** 本项目最初尝试红外方案（IR Copy/Replay），但三星 Neo QLED 8K 电视的 SolarCell 遥控器日常走蓝牙控制，电视不接受红外电源命令。最终改用 SmartThings API 方案。

## 功能

- **Power**：开关电视（toggle，先查当前状态再发反向命令）
- **Vol+ / Vol-**：音量加减
- **Mute**：静音 toggle
- **Source**：切换信号源
- **CH+**：频道加

## 硬件要求

- M5StickS3（ESP32-S3，WiFi）
- USB-C 数据线
- 与电视在同一局域网的 WiFi

## 开发环境

- Arduino CLI 或 Arduino IDE
- M5Stack Board Manager (>= 3.2.5)
- M5Unified 库 (>= 0.2.12)
- M5GFX 库 (>= 0.2.18)

## 配置

1. 复制 `src/ir_copier/secrets.example.h` 为 `src/ir_copier/secrets.h`
2. 填入你的 WiFi SSID、WiFi 密码、SmartThings token、电视 device ID
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

上传前如果设备在运行固件，按住 BOOT 键插 USB 进 download mode。

## 使用方法

1. 开机后自动连接 WiFi，屏幕显示菜单
2. 正面键 (BtnA) 进入命令列表
3. 侧面键 (BtnB) 上下翻选命令
4. 正面键 (BtnA) 执行命令
5. 双击正面键返回主菜单

## 项目结构

```
ir_copier/
├── AGENTS.md
├── README.md
├── .gitignore
├── docs/
│   ├── prd.md
│   ├── rfc.md
│   └── working.md
├── src/
│   └── ir_copier/
│       ├── ir_copier.ino    # 主固件
│       ├── secrets.h        # 凭证（gitignored）
│       └── secrets.example.h # 凭证模板
└── scripts/
    ├── build.sh
    └── upload.sh
```

## 已知限制

- SmartThings PAT 24 小时过期，需定期重新生成
- 仅支持 Samsung 电视（通过 SmartThings API）
- 不支持红外 copy/replay（三星高端电视不走红外）

## 许可证

MIT