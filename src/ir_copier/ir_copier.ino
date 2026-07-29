/*
 * IR Copier — M5StickS3 红外遥控器复制器
 *
 * 功能：预设三星电视红外码 + Replay 直接发送
 * 硬件：M5StickS3 (IR_TX=GPIO46, IR_RX=GPIO42)
 * 协议：Samsung32 (通过 IRremoteESP8266 库发送)
 *
 * 按钮映射 (StickS3)：
 *   BtnA (GPIO11, 正面)  — 短按选择/发送，双击返回
 *   BtnB (GPIO12, 侧面)  — 短按切换/上下翻
 */

#include "M5Unified.h"
#include <IRsend.h>

#define IR_TX_PIN  46

IRsend irsend(IR_TX_PIN);

// ==================== 预设红外码 ====================
// Samsung32 协议，address=0x07
// 完整 32-bit value = address(8) + address_inv(8) + command(8) + command_inv(8)
// LSB first，IRremoteESP8266 的 sendSamsung() 接受完整 32-bit value

struct PresetCode {
  const char *name;
  uint64_t    value;   // 32-bit Samsung code
  uint16_t    bits;    // always 32 for Samsung32
};

#define NUM_PRESETS 10

static const PresetCode presets[NUM_PRESETS] = {
  {"Power",     0xE0E040BF, 32},
  {"Vol+",      0xE0E0E01F, 32},
  {"Vol-",      0xE0E0D02F, 32},
  {"Mute",      0xE0E0F00F, 32},
  {"CH+",       0xE0E048B7, 32},
  {"CH-",       0xE0E008F7, 32},
  {"Source",    0xE0E0807F, 32},
  {"Menu",      0xE0E058A7, 32},
  {"Up",        0xE0E006F9, 32},
  {"Down",      0xE0E08679, 32},
};

// ==================== UI 状态机 ====================

enum UIState {
  STATE_MENU,
  STATE_PRESET_LIST,
  STATE_PRESET_SENT,
};

UIState currentState = STATE_MENU;
int menuIndex = 0;
int presetIndex = 0;

// ==================== 函数声明 ====================

void drawMenu();
void drawPresetList();
void drawPresetSent(int idx);
void drawHeader(const char *title);
void drawFooter(const char *text);

// ==================== Setup ====================

void setup() {
  M5.begin();
  M5.Speaker.end();

  Serial.begin(115200);
  unsigned long serial_start = millis();
  while (!Serial && (millis() - serial_start < 3000)) {
    delay(10);
  }
  Serial.println("IR Copier (Samsung presets) starting...");

  M5.Power.setExtOutput(true, m5::ext_none);
  irsend.begin();
  Serial.println("IRsend initialized");

  M5.Display.setRotation(3);
  M5.Display.clear();
  M5.Display.setTextColor(WHITE, TFT_BLACK);

  drawMenu();
  Serial.println("Ready.");
}

// ==================== Loop ====================

void loop() {
  M5.update();

  switch (currentState) {

  case STATE_MENU: {
    if (M5.BtnA.wasSingleClicked()) {
      currentState = STATE_PRESET_LIST;
      presetIndex = 0;
      drawPresetList();
    }
    break;
  }

  case STATE_PRESET_LIST: {
    if (M5.BtnB.wasPressed()) {
      presetIndex = (presetIndex + 1) % NUM_PRESETS;
      drawPresetList();
    }
    // 先检查双击，再检查单击，避免双击被单击吞掉
    if (M5.BtnA.wasDoubleClicked()) {
      currentState = STATE_MENU;
      drawMenu();
    } else if (M5.BtnA.wasSingleClicked()) {
      Serial.printf("Sending: %s = 0x%08lX\n",
        presets[presetIndex].name,
        (unsigned long)presets[presetIndex].value);
      irsend.sendSAMSUNG(presets[presetIndex].value, presets[presetIndex].bits);
      Serial.println("Sent");
      currentState = STATE_PRESET_SENT;
      drawPresetSent(presetIndex);
    }
    break;
  }

  case STATE_PRESET_SENT: {
    // 用 BtnB 返回，避免 BtnA 的点击状态泄漏
    if (M5.BtnB.wasPressed() || M5.BtnA.wasDoubleClicked()) {
      currentState = STATE_PRESET_LIST;
      drawPresetList();
    }
    break;
  }
  }

  delay(10);
}

// ==================== UI ====================

void drawHeader(const char *title) {
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
  M5.Display.fillRect(0, 122, 240, 13, TFT_BLACK);
  M5.Display.setCursor(5, 124);
  M5.Display.printf("%s", text);
}

void drawMenu() {
  M5.Display.clear();
  drawHeader("Menu");
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::FreeMonoBold9pt7b);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setCursor(25, 45);
  M5.Display.printf("> Samsung TV");
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setCursor(25, 75);
  M5.Display.printf("%d presets", NUM_PRESETS);
  drawFooter("A:enter");
}

void drawPresetList() {
  M5.Display.clear();
  drawHeader("Samsung TV");
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);

  int start = (presetIndex / 5) * 5;
  for (int i = start; i < start + 5 && i < NUM_PRESETS; i++) {
    int y = 28 + (i - start) * 18;
    if (i == presetIndex) {
      M5.Display.fillRect(3, y - 2, 234, 16, TFT_BLUE);
      M5.Display.setTextColor(WHITE, TFT_BLUE);
    } else {
      M5.Display.setTextColor(WHITE, TFT_BLACK);
    }
    M5.Display.setCursor(8, y);
    M5.Display.printf("[%d] %s", i, presets[i].name);
  }

  drawFooter("B:dn A:send Ax2:back");
}

void drawPresetSent(int idx) {
  M5.Display.clear();
  drawHeader("Samsung TV");
  M5.Display.setFont(&fonts::FreeMonoBold9pt7b);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setCursor(20, 45);
  M5.Display.printf("Sent: %s", presets[idx].name);
  M5.Display.setTextColor(WHITE, TFT_BLACK);
  M5.Display.setCursor(20, 70);
  M5.Display.printf("0x%08lX", (unsigned long)presets[idx].value);
  drawFooter("B:back");
}