# RFC — IR Copier 技术设计

## 硬件

M5StickS3 内置 IR 收发器：
- IR_TX: GPIO 46（IR LED 发射）
- IR_RX: GPIO 42（IR 接收器）

IR 收发使用 ESP32-S3 的 RMT 外设（不是 GPIO 轮询），分别用 `driver/rmt_tx.h` 和 `driver/rmt_rx.h` 驱动。38kHz 载波频率。

## NEC 协议

NEC 帧 = 9ms mark + 4.5ms space（引导码）+ 32 bit 数据 + 560us 结尾 mark。

32 bit 结构（LSB first）：
- bit 0-7: 地址
- bit 8-15: 地址反码
- bit 16-23: 命令
- bit 24-31: 命令反码

校验：地址 ^ 地址反 == 0xFF 且 命令 ^ 命令反 == 0xFF。

## 架构决策

### 单文件固件

项目逻辑简单（菜单 + 收 + 发 + 存），不需要拆分模块。全部放 `src/ir_copier.ino`。

### RMT 直接驱动 vs IRremoteESP8266 库

选择 RMT 直接驱动（跟官方例程一致），原因：
- M5Stack 官方 StickS3 IR NEC 例程用的就是 RMT API
- 不引入额外库依赖
- RMT 是 ESP32-S3 硬件外设，精度最高
- IRremoteESP8266 对 StickS3 的支持不如官方例程成熟

### 存储：Preferences (NVS) vs LittleFS

选择 Preferences (NVS)，原因：
- 只需存几个 32 位整数 + 标签，NVS 的 key-value 模型完全够用
- 不需要文件系统
- NVS 是 ESP-IDF 原生持久化，断电安全
- API 简单：`prefs.putULong("slot0", code)`

### 噪声去抖

Bruce 固件的问题是 IR Read 无差别地把所有 RMT 接收到的信号都显示出来，包括环境噪声。本固件的去抖策略：
1. 只接受通过 NEC 协议校验的帧（地址反码 + 命令反码双重校验）
2. 引导码时间窗口校验（mark 8000-10000us，space 4000-5000us）
3. 每个数据 bit 的 mark 时长校验（300-800us）
4. 校验失败的帧静默丢弃，不显示

### UI 状态机

```
MENU_COPY_REPLAY
  ├── Copy → COPY_WAITING → COPY_RECEIVED → MENU_COPY_REPLAY
  └── Replay → REPLAY_LIST → REPLAY_SEND → REPLAY_LIST
```

### 按钮

StickS3 有三个按钮：
- GPIO 11 (BtnA / 侧面下键) — 导航
- GPIO 12 (BtnB / 侧面上键) — 导航
- GPIO 0 (Power/Select) — 确认/选择

用 M5Unified 的 `M5.BtnA`/`M5.BtnB`/`M5.BtnPWR` 读取。

## 引脚定义

```
IR_TX = 46
IR_RX = 42
BTN_A = 11  (导航)
BTN_B = 12  (导航)
BTN_PWR = 0 (确认)
```

## 显示

- ST7789P3, 135x240, 旋转 3（landscape）
- 字体：FreeMonoBold9pt7b
- 菜单高亮：反白显示选中项