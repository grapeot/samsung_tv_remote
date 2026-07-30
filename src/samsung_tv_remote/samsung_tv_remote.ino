/*
 * Samsung TV Remote — M5StickS3
 *
 * 极简三星电视遥控器。通过 WiFi + SmartThings API 控制。
 *
 * UI:
 *   主屏：大字显示电视 ON / OFF 状态
 *   BtnA (正面) 短按 = 开关电视 (toggle power)
 *   BtnB (侧面) 短按 = 状态页 (音量、静音、IP)
 *   状态页按任意键返回主屏
 *
 * 硬件：M5StickS3 (ESP32-S3, WiFi)
 */

#include "M5Unified.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "driver/gpio.h"
#include "secrets.h"

#define IR_TX_PIN  46
#define IR_RX_PIN  42

// ==================== UI 状态 ====================

enum Screen { SCREEN_MAIN, SCREEN_STATUS };
Screen currentScreen = SCREEN_MAIN;

bool tvOn = false;
bool tvStatusValid = false;
int tvVolume = 0;
bool tvMuted = false;
bool tvConfigured = false;
bool actionBusy = false;
unsigned long lastStatusFetch = 0;
const unsigned long STATUS_INTERVAL = 10000; // 10 秒自动刷新

// 背光与省电
const int DEFAULT_BRIGHTNESS = 128;
const int DIM_BRIGHTNESS = 26;  // ~20% of 128
#define BACKLIGHT_DIM_TIMEOUT   15000   // 15 秒无操作减暗到 20%
#define BACKLIGHT_OFF_TIMEOUT   60000   // 60 秒无操作熄灭背光
#define LIGHTSLEEP_TIMEOUT     120000   // 120 秒无操作进 light sleep
unsigned long lastActivity = 0;
int currentBrightness = DEFAULT_BRIGHTNESS;
bool sleeping = false;

#define COLOR_HINT TFT_DARKGREY
#define COLOR_DIM  0x630C  // ~#505050 深灰

// ==================== 函数声明 ====================

bool connectWiFi();
bool fetchTVStatus();
bool togglePower();
void drawMain(bool busy = false);
void drawStatus();
void drawConnecting();
void drawBatteryIcon(int x, int y, int level, bool charging = false);
void enterLightSleep();
void wakeUp();

// ==================== Setup ====================

void setup() {
  auto cfg = M5.config();
  cfg.internal_imu = false;    // 不用 IMU，省电
  cfg.internal_mic = false;     // 不用麦克风
  cfg.internal_spk = false;     // 不用扬声器
  cfg.output_power = false;     // 不用外部 5V
  M5.begin(cfg);

  // CPU 降到 80MHz，省电；WiFi 和 TLS 在此频率仍可工作
  setCpuFrequencyMhz(80);

  Serial.begin(115200);
  delay(500);
  Serial.println("Samsung TV Remote starting...");

  M5.Display.setRotation(0);  // 竖屏 135x240
  M5.Display.setBrightness(DEFAULT_BRIGHTNESS);
  M5.Display.clear();

  drawConnecting();

  if (!connectWiFi()) {
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setTextColor(0xC000, TFT_WHITE);  // 深红
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setFont(&fonts::FreeSansBold9pt7b);
    M5.Display.drawCenterString("WiFi Failed", M5.Display.width() / 2, 100);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(COLOR_DIM, TFT_WHITE);
    M5.Display.drawCenterString("Reset to retry", M5.Display.width() / 2, 130);
    while (true) delay(1000);
  }

  tvConfigured = (strlen(ST_TOKEN) > 0 && strlen(ST_DEVICE_ID) > 0);
  fetchTVStatus();
  drawMain();
  lastActivity = millis();
  Serial.println("Ready.");
}

// ==================== Loop ====================

void loop() {
  M5.update();

  // 检测任意按钮活动
  bool anyButton = M5.BtnA.wasPressed() || M5.BtnA.wasSingleClicked() ||
                   M5.BtnA.wasDoubleClicked() || M5.BtnB.wasPressed() ||
                   M5.BtnB.wasSingleClicked() || M5.BtnB.wasDoubleClicked();
  if (anyButton) {
    lastActivity = millis();
    // 如果背光不亮，先亮起来，不处理按钮动作
    if (currentBrightness != DEFAULT_BRIGHTNESS) {
      currentBrightness = DEFAULT_BRIGHTNESS;
      M5.Display.setBrightness(DEFAULT_BRIGHTNESS);
      // 消费掉这次按钮事件，下一轮再处理实际动作
      return;
    }
    if (sleeping) {
      wakeUp();
      return;
    }
  }

  // 背光三级省电：15s 减暗 → 60s 熄灭 → 120s light sleep
  if (!actionBusy && !sleeping) {
    unsigned long idle = millis() - lastActivity;
    if (idle > LIGHTSLEEP_TIMEOUT) {
      enterLightSleep();
      return;
    } else if (idle > BACKLIGHT_OFF_TIMEOUT) {
      if (currentBrightness != 0) {
        M5.Display.setBrightness(0);
        currentBrightness = 0;
        Serial.println("Backlight off");
      }
    } else if (idle > BACKLIGHT_DIM_TIMEOUT) {
      if (currentBrightness != DIM_BRIGHTNESS) {
        M5.Display.setBrightness(DIM_BRIGHTNESS);
        currentBrightness = DIM_BRIGHTNESS;
        Serial.println("Backlight dim");
      }
    }
  }

  // 不再自动轮询状态。只在按钮操作后刷新。
  // 按钮操作后会 fetchTVStatus + 重绘一次。

  if (actionBusy) {
    vTaskDelay(pdMS_TO_TICKS(10));
    return;
  }

  if (currentScreen == SCREEN_MAIN) {
    if (M5.BtnA.wasSingleClicked()) {
      // 确保背光亮着
      if (currentBrightness != DEFAULT_BRIGHTNESS) {
        currentBrightness = DEFAULT_BRIGHTNESS;
        M5.Display.setBrightness(DEFAULT_BRIGHTNESS);
      }
      actionBusy = true;
      drawMain(true);  // 显示 busy
      bool ok = togglePower();
      if (ok) {
        tvOn = !tvOn;
        tvStatusValid = true;
      }
      drawMain();
      actionBusy = false;
    }
    if (M5.BtnB.wasPressed()) {
      currentScreen = SCREEN_STATUS;
      fetchTVStatus();
      drawStatus();
    }
  }
  else if (currentScreen == SCREEN_STATUS) {
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) {
      currentScreen = SCREEN_MAIN;
      drawMain();
    }
  }

  // yield 让 CPU 进 idle，避免忙转发热
  vTaskDelay(pdMS_TO_TICKS(50));
}

// ==================== 省电 ====================

void enterLightSleep() {
  Serial.println("Entering light sleep...");
  sleeping = true;

  // 配置 GPIO11 (BtnA) 和 GPIO12 (BtnB) 为低电平唤醒
  gpio_wakeup_enable(GPIO_NUM_11, gpio_int_type_t::GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(GPIO_NUM_12, gpio_int_type_t::GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();

  // 进入 light sleep，WiFi 保持关联但降功耗
  esp_light_sleep_start();

  // 唤醒后
  Serial.println("Woke up from light sleep");
  sleeping = false;
  lastActivity = millis();
}

void wakeUp() {
  Serial.println("Waking up...");
  sleeping = false;
  currentBrightness = DEFAULT_BRIGHTNESS;
  M5.Display.setBrightness(DEFAULT_BRIGHTNESS);
  lastActivity = millis();

  // 确认 WiFi 还连着
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, reconnecting...");
    WiFi.reconnect();
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start < 5000)) {
      delay(100);
    }
  }

  // 刷新状态
  fetchTVStatus();
  if (currentScreen == SCREEN_MAIN) drawMain();
  else drawStatus();
}

// ==================== WiFi ====================

bool connectWiFi() {
  Serial.printf("Connecting to %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setTxPower(WIFI_POWER_13dBm);  // 室内 13dBm 够用
  esp_wifi_set_ps(WIFI_PS_MAX_MODEM);  // 最激进 modem sleep
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > 20000) {
      Serial.println(" timeout");
      return false;
    }
  }
  Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

// ==================== SmartThings API ====================

bool sendCommand(const char *capability, const char *command) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  char url[256];
  snprintf(url, sizeof(url), "https://api.smartthings.com/v1/devices/%s/commands", ST_DEVICE_ID);

  if (!http.begin(client, url)) return false;
  http.addHeader("Authorization", "Bearer " ST_TOKEN);
  http.addHeader("Content-Type", "application/json");

  char body[256];
  snprintf(body, sizeof(body),
    "{\"commands\":[{\"component\":\"main\",\"capability\":\"%s\",\"command\":\"%s\"}]}",
    capability, command);

  int code = http.POST(body);
  http.end();
  Serial.printf("CMD %s/%s -> %d\n", capability, command, code);
  return (code >= 200 && code < 300);  // 接受 2xx
}

bool fetchTVStatus() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  char url[256];
  snprintf(url, sizeof(url), "https://api.smartthings.com/v1/devices/%s/components/main/status", ST_DEVICE_ID);

  if (!http.begin(client, url)) return false;
  http.addHeader("Authorization", "Bearer " ST_TOKEN);

  int code = http.GET();
  if (code != 200) {
    http.end();
    Serial.printf("Status fetch failed: %d\n", code);
    lastStatusFetch = millis();
    return false;
  }

  String resp = http.getString();
  http.end();

  // 简单字符串解析
  tvOn = resp.indexOf("\"switch\":{\"switch\":{\"value\":\"on\"") >= 0 ||
         (resp.indexOf("\"value\":\"on\"") >= 0 && resp.indexOf("switch") >= 0);
  tvStatusValid = true;

  int volIdx = resp.indexOf("\"audioVolume\":{\"volume\":{\"value\":\"");
  if (volIdx >= 0) {
    tvVolume = resp.substring(volIdx + 34, volIdx + 40).toInt();
  }

  tvMuted = resp.indexOf("\"mute\":{\"value\":\"mute\"") >= 0;

  lastStatusFetch = millis();
  Serial.printf("Status: on=%d vol=%d muted=%d\n", tvOn, tvVolume, tvMuted);
  return true;
}

bool togglePower() {
  if (tvOn) return sendCommand("switch", "off");
  return sendCommand("switch", "on");
}

// ==================== UI ====================

void drawBatteryIcon(int x, int y, int level, bool charging) {
  // 电池图标：电极在左侧（正极朝左），电量条从右往左长
  // level: 0-100, charging: 充电时变黄
  int w = 22, h = 12;
  uint16_t color;
  if (charging) {
    color = 0xFE40;  // 黄色
  } else if (level < 20) {
    color = TFT_RED;
  } else {
    color = TFT_BLACK;
  }

  // 电极（左侧小凸起）
  M5.Display.fillRect(x, y + 3, 3, 6, color);
  // 外壳
  int bodyX = x + 3;
  M5.Display.drawRect(bodyX, y, w, h, color);

  // 电量条：从外壳内部右端开始，向左填充
  int innerW = w - 4;
  int barW = innerW * level / 100;
  if (barW > 0) {
    M5.Display.fillRect(bodyX + 2 + (innerW - barW), y + 2, barW, h - 4, color);
  }

  // 百分比文字
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(TL_DATUM);
  M5.Display.setTextColor(color, TFT_WHITE);
  M5.Display.drawString(String(level) + "%", bodyX + w + 6, y + 2);
}

void drawConnecting() {
  M5.Display.clear();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setFont(&fonts::FreeSansBold9pt7b);
  M5.Display.setTextSize(1);
  M5.Display.drawCenterString("Samsung TV", M5.Display.width() / 2, 28);
  M5.Display.setFont(&fonts::FreeSans9pt7b);
  M5.Display.drawCenterString("Connecting", M5.Display.width() / 2, 90);
  M5.Display.drawCenterString("to WiFi...", M5.Display.width() / 2, 122);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextColor(COLOR_DIM, TFT_WHITE);
  // 截断长 SSID
  String ssid = WIFI_SSID;
  if (ssid.length() > 20) ssid = ssid.substring(0, 19) + "...";
  M5.Display.drawCenterString(ssid, M5.Display.width() / 2, 155);
}

void drawMain(bool busy) {
  M5.Display.clear();
  M5.Display.fillScreen(TFT_WHITE);

  // 电池图标右上角，充电时变黄
  int battLevel = M5.Power.getBatteryLevel();
  if (battLevel < 0) battLevel = 0;
  if (battLevel > 100) battLevel = 100;
  bool charging = (M5.Power.isCharging() == m5::Power_Class::is_charging);
  drawBatteryIcon(68, 4, battLevel, charging);

  M5.Display.setTextDatum(MC_DATUM);

  // 大字 ON/OFF/-- 用原生大字体
  M5.Display.setFont(&fonts::FreeSansBold24pt7b);
  M5.Display.setTextSize(1);
  if (busy) {
    M5.Display.setTextColor(TFT_NAVY, TFT_WHITE);
    M5.Display.drawCenterString("...", M5.Display.width() / 2, 70);
  } else if (!tvStatusValid) {
    M5.Display.setTextColor(COLOR_DIM, TFT_WHITE);
    M5.Display.drawCenterString("--", M5.Display.width() / 2, 70);
  } else if (tvOn) {
    M5.Display.setTextColor(TFT_DARKGREEN, TFT_WHITE);
    M5.Display.drawCenterString("ON", M5.Display.width() / 2, 70);
  } else {
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.drawCenterString("OFF", M5.Display.width() / 2, 70);
  }

  // 副标题
  M5.Display.setFont(&fonts::FreeSans9pt7b);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.drawCenterString("Samsung TV", M5.Display.width() / 2, 135);

  // 分割线
  M5.Display.drawLine(20, 190, M5.Display.width() - 20, 190, COLOR_DIM);

  // 底部提示
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextColor(COLOR_DIM, TFT_WHITE);
  M5.Display.drawCenterString("A  POWER   B  INFO", M5.Display.width() / 2, 205);
}

void drawStatus() {
  M5.Display.clear();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);

  // 标题
  M5.Display.setTextDatum(TL_DATUM);
  M5.Display.setFont(&fonts::FreeSansBold9pt7b);
  M5.Display.setTextSize(1);
  M5.Display.drawString("TV Status", 8, 12);
  M5.Display.drawLine(8, 38, M5.Display.width() - 8, 38, COLOR_DIM);

  // 主状态行
  M5.Display.setFont(&fonts::FreeSans9pt7b);
  M5.Display.drawString("Power", 8, 52);
  if (tvOn) {
    M5.Display.setTextColor(TFT_DARKGREEN, TFT_WHITE);
  } else {
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  }
  M5.Display.setTextDatum(TR_DATUM);
  M5.Display.drawString(tvStatusValid ? (tvOn ? "ON" : "OFF") : "--", M5.Display.width() - 8, 52);

  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(TL_DATUM);
  M5.Display.drawString("Volume", 8, 81);
  M5.Display.setTextDatum(TR_DATUM);
  M5.Display.drawString(String(tvVolume), M5.Display.width() - 8, 81);

  M5.Display.setTextDatum(TL_DATUM);
  M5.Display.drawString("Muted", 8, 110);
  M5.Display.setTextDatum(TR_DATUM);
  M5.Display.drawString(tvMuted ? "Yes" : "No", M5.Display.width() - 8, 110);

  // 网络分割线
  M5.Display.drawLine(8, 141, M5.Display.width() - 8, 141, COLOR_DIM);

  // 网络信息
  M5.Display.setTextDatum(TL_DATUM);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextColor(COLOR_DIM, TFT_WHITE);
  M5.Display.drawString("IP:", 8, 153);
  M5.Display.drawString(WiFi.localIP().toString(), 8, 168);

  String ssid = WIFI_SSID;
  if (ssid.length() > 20) ssid = ssid.substring(0, 19) + "...";
  M5.Display.drawString("SSID:", 8, 185);
  M5.Display.drawString(ssid, 8, 198);

  // 底部提示
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.drawCenterString("A / B  BACK", M5.Display.width() / 2, 222);
}