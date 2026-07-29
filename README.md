# IR Copier — M5StickS3 红外遥控器复制器

一个运行在 M5StickS3 上的极简红外遥控器复制工具。屏幕显示两个选项：**Copy**（抓取红外码）和 **Replay**（回放已存储的红外码）。用侧边按钮切换菜单、选择操作。

## 功能

- **Copy 模式**：将电视遥控器对准 StickS3 顶部，按下遥控器按键，设备抓取 NEC 协议红外码并存入 flash（断电不丢失）
- **Replay 模式**：从 flash 读出已存的码，通过内置 IR LED 发射出去
- 最多存储 8 个红外码，每个带标签名
- 支持 NEC 协议（绝大多数电视遥控器使用此协议）

## 硬件要求

- M5StickS3（内置 IR 收发器，IR_TX=GPIO46，IR_RX=GPIO42）
- USB-C 数据线

## 开发环境

- Arduino CLI 或 Arduino IDE
- M5Stack Board Manager (>= 3.2.5)
- M5Unified 库 (>= 0.2.12)
- M5GFX 库 (>= 0.2.18)

## 编译与上传

```bash
# 编译
scripts/build.sh

# 上传（设备需通过 USB 连接）
scripts/upload.sh
```

## 使用方法

1. 开机后屏幕显示菜单：Copy / Replay
2. 用侧边按钮上下切换，正面 Select 键确认
3. Copy 模式下，对准遥控器按按键，抓到码后屏幕显示码值并自动存入下一个槽位
4. Replay 模式下，选择槽位，按 Select 发射

## 项目结构

```
ir_copier/
├── AGENTS.md          # AI agent 工作指南
├── README.md          # 本文件
├── .gitignore
├── docs/
│   ├── prd.md         # 产品需求
│   ├── rfc.md         # 技术设计
│   └── working.md     # 开发日志
├── src/
│   └── ir_copier.ino  # 主固件
└── scripts/
    ├── build.sh       # 编译脚本
    └── upload.sh      # 上传脚本
```

## 协议说明

本工具支持 NEC 红外协议。NEC 是最常见的电视遥控器协议，一个按键对应一个 32 位整数（含 8 位地址 + 8 位地址反码 + 8 位命令 + 8 位命令反码）。空调遥控器使用长协议，暂不支持。

## 许可证

MIT