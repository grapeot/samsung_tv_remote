/*
 * IR Copier — M5StickS3 红外遥控器复制器
 *
 * 功能：Copy（抓取 NEC 红外码并存入 flash）+ Replay（回放已存红外码）
 * 硬件：M5StickS3 (IR_TX=GPIO46, IR_RX=GPIO42)
 * 协议：NEC（绝大多数电视遥控器）
 * 存储：NVS (Preferences blob)，断电不丢失
 *
 * 按钮映射 (StickS3)：
 *   BtnA (GPIO11, 正面)  — 短按选择/进入/发送，双击返回/取消
 *   BtnB (GPIO12, 侧面)  — 短按切换/上下翻
 */

#include "M5Unified.h"
#include "Preferences.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"

// ==================== 引脚定义 ====================

#define IR_TX_PIN  46
#define IR_RX_PIN  42

// ==================== NEC 协议常量 ====================

#define NEC_HEADER_MARK     9000
#define NEC_HEADER_SPACE    4500
#define NEC_BIT_MARK        560
#define NEC_BIT_0_SPACE     560
#define NEC_BIT_1_SPACE     1690

#define IR_CARRIER_FREQ_HZ  38000
#define IR_DUTY_CYCLE       0.33

// NEC 帧的 RMT symbol 数：1 header + 32 data + 1 trailing = 34
#define NEC_EXPECTED_SYMBOLS  34

// ==================== 存储配置 ====================

#define MAX_SLOTS    8
#define NVS_NAMESPACE "ir_copier"

// ==================== UI 状态机 ====================

enum UIState {
  STATE_MENU,          // 主菜单：Copy / Replay
  STATE_COPY_WAITING,  // 等待接收红外信号
  STATE_COPY_RESULT,   // 显示抓到的码
  STATE_COPY_FULL,     // 槽位已满
  STATE_REPLAY_LIST,   // 选择要回放的槽位
  STATE_REPLAY_SENT,   // 显示发送结果
  STATE_REPLAY_EMPTY,  // 选中空槽
};

UIState currentState = STATE_MENU;
int menuIndex = 0;       // 主菜单选中项 (0=Copy, 1=Replay)
int replayIndex = 0;     // Replay 列表选中槽位

// ==================== RMT TX ====================

rmt_channel_handle_t tx_chan = NULL;
rmt_encoder_handle_t copy_encoder = NULL;

// ==================== RMT RX ====================

rmt_channel_handle_t rx_chan = NULL;
static rmt_symbol_word_t rx_symbols[68];
static volatile bool rx_done = false;
static volatile size_t rx_symbol_count = 0;
static volatile bool rx_busy = false;

// ==================== NVS ====================

Preferences prefs;

// ==================== 存储结构 ====================

struct IRRSlot {
  uint32_t code;   // 32-bit NEC raw data (0 = 空槽)
  uint16_t addr;   // NEC 地址（8 位标准或 16 位扩展）
  uint8_t  cmd;    // NEC 命令字节
  uint8_t  is_ext; // 1 = 扩展 NEC（16 位地址），0 = 标准 NEC
};

// ==================== 函数声明 ====================

void setup_rmt_tx();
void setup_rmt_rx();
void start_rmt_receive();
void stop_rmt_receive();
bool sendNEC(uint32_t raw);
void encodeNEC(uint32_t raw, rmt_symbol_word_t *symbols, size_t *count);
bool decodeNEC(rmt_symbol_word_t *symbols, size_t sym_count, uint32_t *out_raw, bool *out_repeat);
bool rmt_rx_callback(rmt_channel_handle_t chan, const rmt_rx_done_event_data_t *edata, void *user_ctx);

int  findEmptySlot();
bool saveSlot(int slot, uint32_t raw, uint16_t addr, uint8_t cmd, uint8_t is_ext);
bool loadSlot(int slot, IRRSlot *out);
int  countUsedSlots();
void clearSlot(int slot);

void drawMenu();
void drawCopyWaiting();
void drawCopyResult(uint32_t raw, uint16_t addr, uint8_t cmd, int slot, uint8_t is_ext);
void drawCopyFull();
void drawReplayList();
void drawReplaySent(int slot, bool ok);
void drawReplayEmpty();
void drawHeader(const char *title);
void drawFooter(const char *text);

// ==================== Setup ====================

void setup() {
  auto cfg = M5.config();
  // 不启动扬声器，避免功放干扰 IR 接收
  cfg.internal_spk = false;
  M5.begin(cfg);

  // 确保 Speaker 已关闭（双保险）
  M5.Speaker.end();

  // 开启外部供电
  M5.Power.setExtOutput(true, m5::ext_none);

  // 显示初始化
  M5.Display.setRotation(3);
  M5.Display.clear();
  M5.Display.setTextColor(WHITE, TFT_BLACK);

  // 初始化 RMT
  setup_rmt_tx();
  setup_rmt_rx();

  // 打开 NVS
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.printf("NVS init failed!");
    while (true) delay(1000);
  }

  // 显示主菜单
  drawMenu();
}

// ==================== Loop ====================

void loop() {
  M5.update();

  switch (currentState) {

  // ========== 主菜单 ==========
  case STATE_MENU: {
    if (M5.BtnB.wasPressed()) {
      menuIndex = 1 - menuIndex;
      drawMenu();
    }
    if (M5.BtnA.wasPressed()) {
      if (menuIndex == 0) {
        // 进入 Copy 模式
        rx_done = false;
        rx_busy = false;
        currentState = STATE_COPY_WAITING;
        start_rmt_receive();
        drawCopyWaiting();
      } else {
        // 进入 Replay 模式
        currentState = STATE_REPLAY_LIST;
        replayIndex = 0;
        drawReplayList();
      }
    }
    break;
  }

  // ========== Copy 等待 ==========
  case STATE_COPY_WAITING: {
    // 双击 A 返回菜单
    if (M5.BtnA.wasDoubleClicked()) {
      stop_rmt_receive();
      currentState = STATE_MENU;
      drawMenu();
      break;
    }
    if (rx_done) {
      rx_done = false;
      rx_busy = false;

      uint32_t raw = 0;
      bool repeat = false;

      if (decodeNEC(rx_symbols, rx_symbol_count, &raw, &repeat) && !repeat) {
        // 解析地址和命令
        uint8_t addr_lo  = raw & 0xFF;
        uint8_t addr_hi  = (raw >> 8) & 0xFF;
        uint8_t cmd_byte = (raw >> 16) & 0xFF;

        // 判断是标准 NEC 还是扩展 NEC
        uint8_t is_ext = ((addr_lo ^ addr_hi) != 0xFF) ? 1 : 0;
        uint16_t addr = is_ext ? (raw & 0xFFFF) : addr_lo;

        int slot = findEmptySlot();
        if (slot >= 0) {
          if (saveSlot(slot, raw, addr, cmd_byte, is_ext)) {
            currentState = STATE_COPY_RESULT;
            drawCopyResult(raw, addr, cmd_byte, slot, is_ext);
          } else {
            currentState = STATE_COPY_RESULT;
            drawCopyResult(raw, addr, cmd_byte, -1, is_ext);
          }
        } else {
          stop_rmt_receive();
          currentState = STATE_COPY_FULL;
          drawCopyFull();
        }
      } else {
        start_rmt_receive();
      }
    }
    break;
  }

  // ========== Copy 结果 ==========
  case STATE_COPY_RESULT: {
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() ||
        M5.BtnA.wasDoubleClicked()) {
      currentState = STATE_MENU;
      drawMenu();
    }
    break;
  }

  // ========== Copy 槽位满 ==========
  case STATE_COPY_FULL: {
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() ||
        M5.BtnA.wasDoubleClicked()) {
      currentState = STATE_MENU;
      drawMenu();
    }
    break;
  }

  // ========== Replay 列表 ==========
  case STATE_REPLAY_LIST: {
    if (M5.BtnB.wasPressed()) {
      // 上下翻
      replayIndex = (replayIndex + 1) % MAX_SLOTS;
      drawReplayList();
    }
    if (M5.BtnA.wasPressed()) {
      // 发送当前槽
      IRRSlot slot_data;
      if (loadSlot(replayIndex, &slot_data) && slot_data.code != 0) {
        bool ok = sendNEC(slot_data.code);
        currentState = STATE_REPLAY_SENT;
        drawReplaySent(replayIndex, ok);
      } else {
        currentState = STATE_REPLAY_EMPTY;
        drawReplayEmpty();
      }
    }
    if (M5.BtnA.wasDoubleClicked()) {
      // 双击 A 返回主菜单
      currentState = STATE_MENU;
      drawMenu();
    }
    break;
  }

  // ========== Replay 发送完成 ==========
  case STATE_REPLAY_SENT: {
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() ||
        M5.BtnA.wasDoubleClicked()) {
      currentState = STATE_REPLAY_LIST;
      drawReplayList();
    }
    break;
  }

  // ========== Replay 空槽提示 ==========
  case STATE_REPLAY_EMPTY: {
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() ||
        M5.BtnA.wasDoubleClicked()) {
      currentState = STATE_REPLAY_LIST;
      drawReplayList();
    }
    break;
  }
  }

  delay(10);
}

// ==================== RMT TX 初始化 ====================

void setup_rmt_tx() {
  rmt_tx_channel_config_t tx_cfg = {
    .gpio_num = (gpio_num_t)IR_TX_PIN,
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = 1000000,
    .mem_block_symbols = 64,
    .trans_queue_depth = 4,
    .flags = {
      .invert_out = false,
      .with_dma = false,
    },
  };
  ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &tx_chan));

  rmt_carrier_config_t carrier_cfg = {
    .frequency_hz = IR_CARRIER_FREQ_HZ,
    .duty_cycle = IR_DUTY_CYCLE,
    .flags = {
      .polarity_active_low = false,
    },
  };
  ESP_ERROR_CHECK(rmt_apply_carrier(tx_chan, &carrier_cfg));

  rmt_copy_encoder_config_t enc_cfg = {};
  ESP_ERROR_CHECK(rmt_new_copy_encoder(&enc_cfg, &copy_encoder));

  ESP_ERROR_CHECK(rmt_enable(tx_chan));
}

// ==================== RMT RX 初始化 ====================

void setup_rmt_rx() {
  rmt_rx_channel_config_t rx_cfg = {
    .gpio_num = (gpio_num_t)IR_RX_PIN,
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = 1000000,
    .mem_block_symbols = 128,
  };
  ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_cfg, &rx_chan));

  rmt_rx_event_callbacks_t cbs = {
    .on_recv_done = rmt_rx_callback,
  };
  ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_chan, &cbs, NULL));
  ESP_ERROR_CHECK(rmt_enable(rx_chan));
}

void start_rmt_receive() {
  // 清除陈旧的完成标志，再启动新一轮接收
  rx_done = false;
  rx_busy = true;
  rmt_receive_config_t recv_cfg = {
    .signal_range_min_ns = 1000,
    .signal_range_max_ns = 20000000,
  };
  esp_err_t ret = rmt_receive(rx_chan, rx_symbols, sizeof(rx_symbols), &recv_cfg);
  if (ret != ESP_OK) {
    rx_busy = false;
  }
}

void stop_rmt_receive() {
  // 禁用再重新启用 RX channel 来中断正在进行的接收
  rmt_disable(rx_chan);
  rmt_enable(rx_chan);
  rx_done = false;
  rx_busy = false;
}

bool rmt_rx_callback(rmt_channel_handle_t chan, const rmt_rx_done_event_data_t *edata, void *user_ctx) {
  rx_symbol_count = edata->num_symbols;
  rx_done = true;
  rx_busy = false;
  // 没有 wake higher priority task，返回 false
  return false;
}

// ==================== NEC 发送 ====================

bool sendNEC(uint32_t raw) {
  rmt_symbol_word_t symbols[68];
  size_t count = 0;
  encodeNEC(raw, symbols, &count);

  rmt_transmit_config_t tx_cfg = {
    .loop_count = 0,
    .flags = {
      .eot_level = 0,
    },
  };

  esp_err_t ret = rmt_transmit(tx_chan, copy_encoder, symbols, count * sizeof(rmt_symbol_word_t), &tx_cfg);
  if (ret != ESP_OK) return false;

  // NEC 帧约 68ms，给 200ms 足够余量
  ret = rmt_tx_wait_all_done(tx_chan, 200);
  return (ret == ESP_OK);
}

void encodeNEC(uint32_t raw, rmt_symbol_word_t *symbols, size_t *count) {
  size_t idx = 0;

  // 引导码
  symbols[idx].duration0 = NEC_HEADER_MARK;
  symbols[idx].level0 = 1;
  symbols[idx].duration1 = NEC_HEADER_SPACE;
  symbols[idx].level1 = 0;
  idx++;

  // 32 bit 数据 (LSB first)
  for (int i = 0; i < 32; i++) {
    symbols[idx].duration0 = NEC_BIT_MARK;
    symbols[idx].level0 = 1;
    if (raw & (1UL << i)) {
      symbols[idx].duration1 = NEC_BIT_1_SPACE;
    } else {
      symbols[idx].duration1 = NEC_BIT_0_SPACE;
    }
    symbols[idx].level1 = 0;
    idx++;
  }

  // 结尾 mark
  symbols[idx].duration0 = NEC_BIT_MARK;
  symbols[idx].level0 = 1;
  symbols[idx].duration1 = 0;
  symbols[idx].level1 = 0;
  idx++;

  *count = idx;
}

// ==================== NEC 接收与校验 ====================

bool decodeNEC(rmt_symbol_word_t *symbols, size_t sym_count, uint32_t *out_raw, bool *out_repeat) {
  *out_raw = 0;
  *out_repeat = false;

  if (sym_count < 2) return false;

  uint32_t header_mark  = symbols[0].duration0;
  uint32_t header_space = symbols[0].duration1;

  // 标准 NEC 引导码：~9ms mark + ~4.5ms space
  if (header_mark > 8000 && header_mark < 10000 &&
      header_space > 4000 && header_space < 5000) {
    // 有效引导码，继续
  }
  // NEC repeat 帧：~9ms mark + ~2.25ms space
  else if (header_mark > 8000 && header_mark < 10000 &&
           header_space > 2000 && header_space < 3000) {
    *out_repeat = true;
    return false;
  }
  // 不是 NEC 引导码，丢弃
  else {
    return false;
  }

  // 检查 symbol 数量：34 个（header + 32 data + trailing mark）
  // 允许少量偏差 (33-36)
  if (sym_count < 33 || sym_count > 36) return false;

  // 解码 32 bit (LSB first)
  for (int i = 0; i < 32; i++) {
    if ((size_t)(i + 1) >= sym_count) return false;

    uint32_t mark  = symbols[i + 1].duration0;
    uint32_t space = symbols[i + 1].duration1;

    // mark 时长校验 (~560us，容差 300-800)
    if (mark < 300 || mark > 800) return false;

    // space 区分 0/1，用时间窗口而非单一阈值
    // 0: ~560us (300-1000)，1: ~1690us (1200-2200)
    if (space > 1200 && space < 2200) {
      *out_raw |= (1UL << i);
    } else if (space > 300 && space < 1000) {
      // bit = 0
    } else {
      // space 不在合理窗口内，丢弃
      return false;
    }
  }

  // 校验命令字节与反码
  uint8_t cmd     = (*out_raw >> 16) & 0xFF;
  uint8_t cmd_inv = (*out_raw >> 24) & 0xFF;
  if ((cmd ^ cmd_inv) != 0xFF) return false;

  // 地址校验：标准 NEC 的地址反码必须匹配
  // 扩展 NEC 用 16 位地址，反码不匹配是正常的
  // 两种都接受，但标记 is_ext 供 UI 显示
  uint8_t addr_lo  = (*out_raw >> 0) & 0xFF;
  uint8_t addr_hi  = (*out_raw >> 8) & 0xFF;
  bool is_standard_nec = ((addr_lo ^ addr_hi) == 0xFF);

  // 如果既不是标准 NEC 也不是合理的扩展 NEC（两个地址字节都一样），
  // 可能是噪声，保守丢弃
  if (!is_standard_nec && addr_lo == addr_hi) return false;

  return true;
}

// ==================== NVS 存储 ====================

int findEmptySlot() {
  for (int i = 0; i < MAX_SLOTS; i++) {
    IRRSlot s;
    if (loadSlot(i, &s) && s.code == 0) return i;
  }
  return -1;
}

// 用单个 blob 存储每个槽位，避免多次写入的不一致问题
bool saveSlot(int slot, uint32_t raw, uint16_t addr, uint8_t cmd, uint8_t is_ext) {
  if (slot < 0 || slot >= MAX_SLOTS) return false;

  IRRSlot data;
  data.code   = raw;
  data.addr   = addr;
  data.cmd    = cmd;
  data.is_ext = is_ext;

  char key[8];
  snprintf(key, sizeof(key), "s%d", slot);

  size_t written = prefs.putBytes(key, &data, sizeof(IRRSlot));
  return (written == sizeof(IRRSlot));
}

bool loadSlot(int slot, IRRSlot *out) {
  if (slot < 0 || slot >= MAX_SLOTS) return false;

  char key[8];
  snprintf(key, sizeof(key), "s%d", slot);

  // 先检查 key 是否存在
  if (!prefs.isKey(key)) {
    out->code = 0;
    out->addr = 0;
    out->cmd = 0;
    out->is_ext = 0;
    return true;
  }

  size_t read = prefs.getBytes(key, out, sizeof(IRRSlot));
  if (read != sizeof(IRRSlot)) {
    out->code = 0;
    return true;
  }
  return true;
}

int countUsedSlots() {
  int n = 0;
  for (int i = 0; i < MAX_SLOTS; i++) {
    IRRSlot s;
    if (loadSlot(i, &s) && s.code != 0) n++;
  }
  return n;
}

void clearSlot(int slot) {
  if (slot < 0 || slot >= MAX_SLOTS) return;
  char key[8];
  snprintf(key, sizeof(key), "s%d", slot);
  prefs.remove(key);
}

// ==================== UI 绘制 ====================

void drawHeader(const char *title) {
  // 固定使用 Font0 防止继承前一个屏幕的字体
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
  M5.Display.fillRect(0, 0, 240, 20, TFT_DARKGREY);
  M5.Display.setCursor(5, 2);
  M5.Display.setTextColor(WHITE, TFT_DARKGREY);
  M5.Display.printf("IR Copier - %s", title);
  M5.Display.setTextColor(WHITE, TFT_BLACK);
}

void drawFooter(const char *text) {
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.setCursor(5, 124);
  M5.Display.fillRect(0, 122, 240, 13, TFT_BLACK);
  M5.Display.printf("%s", text);
}

void drawMenu() {
  M5.Display.clear();
  drawHeader("Menu");

  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::FreeMonoBold9pt7b);

  const char *items[] = {"Copy", "Replay"};
  for (int i = 0; i < 2; i++) {
    int y = 45 + i * 30;
    if (i == menuIndex) {
      M5.Display.fillRect(15, y - 3, 210, 25, TFT_BLUE);
      M5.Display.setTextColor(WHITE, TFT_BLUE);
    } else {
      M5.Display.setTextColor(WHITE, TFT_BLACK);
    }
    M5.Display.setCursor(25, y);
    M5.Display.printf("> %s", items[i]);
  }

  int used = countUsedSlots();
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setCursor(25, 105);
  M5.Display.printf("Slots: %d/%d", used, MAX_SLOTS);

  drawFooter("B:switch A:enter Ax2:back");
}

void drawCopyWaiting() {
  M5.Display.clear();
  drawHeader("Copy");
  M5.Display.setFont(&fonts::FreeMonoBold9pt7b);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setCursor(20, 40);
  M5.Display.printf("Waiting...");
  M5.Display.setTextColor(WHITE, TFT_BLACK);
  M5.Display.setCursor(20, 65);
  M5.Display.printf("Point remote");
  M5.Display.setCursor(20, 85);
  M5.Display.printf("at top of");
  M5.Display.setCursor(20, 105);
  M5.Display.printf("StickS3");
  drawFooter("Ax2: cancel");
}

void drawCopyResult(uint32_t raw, uint16_t addr, uint8_t cmd, int slot, uint8_t is_ext) {
  M5.Display.clear();
  drawHeader("Copy OK");
  M5.Display.setFont(&fonts::FreeMonoBold9pt7b);
  if (slot >= 0) {
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.setCursor(15, 35);
    M5.Display.printf("Saved slot %d", slot);
  } else {
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.setCursor(15, 35);
    M5.Display.printf("Save FAILED");
  }
  M5.Display.setTextColor(WHITE, TFT_BLACK);
  M5.Display.setCursor(15, 58);
  if (is_ext) {
    M5.Display.printf("Addr: 0x%04X", addr);
  } else {
    M5.Display.printf("Addr: 0x%02X", addr & 0xFF);
  }
  M5.Display.setCursor(15, 78);
  M5.Display.printf("Cmd:  0x%02X", cmd);
  M5.Display.setCursor(15, 98);
  M5.Display.printf("Raw:  0x%08lX", (unsigned long)raw);
  drawFooter("Any btn: back");
}

void drawCopyFull() {
  M5.Display.clear();
  drawHeader("Copy");
  M5.Display.setFont(&fonts::FreeMonoBold9pt7b);
  M5.Display.setTextColor(TFT_RED, TFT_BLACK);
  M5.Display.setCursor(15, 40);
  M5.Display.printf("Slots full!");
  M5.Display.setTextColor(WHITE, TFT_BLACK);
  M5.Display.setCursor(15, 65);
  M5.Display.printf("Delete one in");
  M5.Display.setCursor(15, 85);
  M5.Display.printf("Replay mode");
  M5.Display.setCursor(15, 105);
  M5.Display.printf("(Ax2 on slot)");
  drawFooter("Any btn: back");
}

void drawReplayList() {
  M5.Display.clear();
  drawHeader("Replay");

  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);

  int start = (replayIndex / 5) * 5;
  for (int i = start; i < start + 5 && i < MAX_SLOTS; i++) {
    IRRSlot s;
    loadSlot(i, &s);
    int y = 28 + (i - start) * 18;
    if (i == replayIndex) {
      M5.Display.fillRect(3, y - 2, 234, 16, TFT_BLUE);
      M5.Display.setTextColor(WHITE, TFT_BLUE);
    } else {
      M5.Display.setTextColor(WHITE, TFT_BLACK);
    }
    M5.Display.setCursor(8, y);
    if (s.code != 0) {
      M5.Display.printf("[%d] 0x%08lX A:%02X C:%02X", i, (unsigned long)s.code, s.addr & 0xFF, s.cmd);
    } else {
      M5.Display.printf("[%d] (empty)", i);
    }
  }

  drawFooter("B:up-dn A:send Ax2:back");
}

void drawReplaySent(int slot, bool ok) {
  M5.Display.clear();
  drawHeader("Replay");
  M5.Display.setFont(&fonts::FreeMonoBold9pt7b);
  if (ok) {
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.setCursor(30, 50);
    M5.Display.printf("Sent slot %d", slot);
  } else {
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.setCursor(30, 50);
    M5.Display.printf("Send FAIL");
  }
  drawFooter("Any btn: back");
}

void drawReplayEmpty() {
  M5.Display.clear();
  drawHeader("Replay");
  M5.Display.setFont(&fonts::FreeMonoBold9pt7b);
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setCursor(30, 50);
  M5.Display.printf("Empty slot");
  drawFooter("Any btn: back");
}