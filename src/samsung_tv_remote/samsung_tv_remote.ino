/*
 * Samsung TV Remote — M5StickS3
 *
 * Local Tizen WebSocket API 版本（替代 SmartThings 云 PAT）。
 * 通过 wss://TV_IP:8002 本地 WebSocket 控制电视，无需 24h 过期 token。
 *
 * UI:
 *   主屏：大字显示电视 ON / OFF 状态
 *   BtnA (正面) 短按 = 开关电视 (KEY_POWER toggle)
 *   BtnB (侧面) 短按 = 状态页 (IP、SSID、配对状态)
 *   状态页按任意键返回主屏
 *
 * 配对：首次运行时连接电视 WS，电视屏幕弹出授权框，
 *       用户按允许后 token 存入 NVS，后续自动复用。
 *
 * 硬件：M5StickS3 (ESP32-S3, WiFi)
 */

#include "M5Unified.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "driver/gpio.h"
#include "secrets.h"

#define IR_TX_PIN  46
#define IR_RX_PIN  42

// ==================== 常量 ====================

#define WS_PORT   8002
#define WS_PATH   "/api/v2/channels/samsung.remote.control"
#define REST_PATH "/api/v2/"
#define POWER_SETTLE_MS  3000
#define APP_NAME_B64 "U21hcnRIb21l"  // base64("SmartHome")

// ==================== UI 状态 ====================

enum Screen { SCREEN_MAIN, SCREEN_STATUS, SCREEN_PAIRING };
Screen currentScreen = SCREEN_MAIN;

bool tvOn = false;
bool tvStatusValid = false;
bool tvConfigured = false;
bool actionBusy = false;

const int DEFAULT_BRIGHTNESS = 128;
const int DIM_BRIGHTNESS = 26;
#define BACKLIGHT_DIM_TIMEOUT   15000
#define BACKLIGHT_OFF_TIMEOUT   60000
#define LIGHTSLEEP_TIMEOUT     120000
unsigned long lastActivity = 0;
int currentBrightness = DEFAULT_BRIGHTNESS;
bool sleeping = false;

#define COLOR_DIM  0x630C

// ==================== WS 客户端 ====================

websockets::WebsocketsClient wsClient;
Preferences prefs;
String wsToken;
bool pairingInProgress = false;
bool pairSuccess = false;
unsigned long pairStartTime = 0;
#define PAIR_TIMEOUT_MS  30000

// ==================== 函数声明 ====================

bool connectWiFi();
bool fetchTVStatus();
bool sendKey(const char *key);
void startPairing();
void saveToken(const String &token);
void drawMain(bool busy = false);
void drawStatus();
void drawConnecting();
void drawPairing(bool waiting);
void drawBatteryIcon(int x, int y, int level, bool charging = false);
void enterLightSleep();
void wakeUp();

// ==================== Setup ====================

void setup() {
  auto cfg = M5.config();
  cfg.internal_imu = false;
  cfg.internal_mic = false;
  cfg.internal_spk = false;
  cfg.output_power = false;
  M5.begin(cfg);

  setCpuFrequencyMhz(80);

  Serial.begin(115200);
  delay(500);
  Serial.println("Samsung TV Remote (Local WS) starting...");

  M5.Display.setRotation(0);
  M5.Display.setBrightness(DEFAULT_BRIGHTNESS);
  M5.Display.clear();

  drawConnecting();

  if (!connectWiFi()) {
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setTextColor(0xC000, TFT_WHITE);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setFont(&fonts::FreeSansBold9pt7b);
    M5.Display.drawCenterString("WiFi Failed", M5.Display.width() / 2, 100);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(COLOR_DIM, TFT_WHITE);
    M5.Display.drawCenterString("Reset to retry", M5.Display.width() / 2, 130);
    while (true) delay(1000);
  }

  // 加载已存 token
  prefs.begin("samsung_tv", true);
  wsToken = prefs.getString("ws_token", "");
  prefs.end();

  tvConfigured = (strlen(TV_HOST) > 0 && wsToken.length() > 0);

  if (tvConfigured) {
    fetchTVStatus();
    drawMain();
  } else if (strlen(TV_HOST) > 0) {
    startPairing();
  } else {
    tvConfigured = false;
    drawMain();
  }
  lastActivity = millis();
  Serial.println("Ready.");
}

// ==================== Loop ====================

void loop() {
  M5.update();

  // 配对超时检查
  if (pairingInProgress && (millis() - pairStartTime > PAIR_TIMEOUT_MS)) {
    Serial.println("Pairing timed out");
    pairingInProgress = false;
    drawPairing(false);
  }

  // 按钮活动检测
  bool anyButton = M5.BtnA.wasPressed() || M5.BtnA.wasSingleClicked() ||
                   M5.BtnA.wasDoubleClicked() || M5.BtnB.wasPressed() ||
                   M5.BtnB.wasSingleClicked() || M5.BtnB.wasDoubleClicked();
  if (anyButton) {
    lastActivity = millis();
    if (currentBrightness != DEFAULT_BRIGHTNESS) {
      currentBrightness = DEFAULT_BRIGHTNESS;
      M5.Display.setBrightness(DEFAULT_BRIGHTNESS);
      return;
    }
    if (sleeping) {
      wakeUp();
      return;
    }
  }

  // 背光三级省电
  if (!actionBusy && !sleeping && !pairingInProgress) {
    unsigned long idle = millis() - lastActivity;
    if (idle > LIGHTSLEEP_TIMEOUT) {
      enterLightSleep();
      return;
    } else if (idle > BACKLIGHT_OFF_TIMEOUT) {
      if (currentBrightness != 0) {
        M5.Display.setBrightness(0);
        currentBrightness = 0;
      }
    } else if (idle > BACKLIGHT_DIM_TIMEOUT) {
      if (currentBrightness != DIM_BRIGHTNESS) {
        M5.Display.setBrightness(DIM_BRIGHTNESS);
        currentBrightness = DIM_BRIGHTNESS;
      }
    }
  }

  if (actionBusy) {
    vTaskDelay(pdMS_TO_TICKS(10));
    return;
  }

  if (currentScreen == SCREEN_MAIN) {
    if (M5.BtnA.wasSingleClicked()) {
      if (currentBrightness != DEFAULT_BRIGHTNESS) {
        currentBrightness = DEFAULT_BRIGHTNESS;
        M5.Display.setBrightness(DEFAULT_BRIGHTNESS);
      }
      actionBusy = true;
      drawMain(true);
      bool ok = sendKey("KEY_POWER");
      if (ok) {
        tvOn = !tvOn;
        tvStatusValid = true;
        delay(POWER_SETTLE_MS);
        fetchTVStatus();
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
  else if (currentScreen == SCREEN_PAIRING) {
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) {
      currentScreen = SCREEN_MAIN;
      fetchTVStatus();
      drawMain();
    }
  }

  vTaskDelay(pdMS_TO_TICKS(50));
}

// ==================== 配对 ====================

void onPairMessage(websockets::WebsocketsMessage message) {
  Serial.printf("[WS] %s\n", message.c_str());

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, message.c_str());
  if (err) return;

  const char *event = doc["event"];
  if (!event) return;

  if (strcmp(event, "ms.channel.connect") == 0) {
    if (doc["data"].containsKey("token")) {
      const char *token = doc["data"]["token"];
      if (token && strlen(token) > 0) {
        Serial.printf("Got token: %s\n", token);
        saveToken(String(token));
        wsToken = String(token);
        pairSuccess = true;
        pairingInProgress = false;
        tvConfigured = true;
        wsClient.close();
        fetchTVStatus();
        drawMain();
      }
    }
  }
}

void startPairing() {
  Serial.println("Starting pairing...");
  currentScreen = SCREEN_PAIRING;
  drawPairing(true);
  pairingInProgress = true;
  pairSuccess = false;
  pairStartTime = millis();

  // 配对连接不带 token
  char wsPath[128];
  snprintf(wsPath, sizeof(wsPath), "%s?name=%s", WS_PATH, APP_NAME_B64);

  // 配置回调
  wsClient.onMessage(onPairMessage);

  if (!wsClient.connectSecure(TV_HOST, WS_PORT, wsPath)) {
    Serial.println("WS connect failed for pairing");
    drawPairing(false);
    return;
  }

  Serial.println("WS connected, waiting for TV allow...");
}

// ==================== WS 控制 ====================

bool sendKey(const char *key) {
  if (!tvConfigured || wsToken.length() == 0) {
    Serial.println("Not configured, starting pairing...");
    startPairing();
    return false;
  }

  // 每次发 key 建立新连接（Tizen 重置多 key 连接）
  char wsPath[160];
  snprintf(wsPath, sizeof(wsPath), "%s?name=%s&token=%s", WS_PATH, APP_NAME_B64, wsToken.c_str());

  bool success = false;

  // 临时回调只解析 connect 事件
  auto handler = [&](websockets::WebsocketsMessage msg) {
    JsonDocument doc;
    if (deserializeJson(doc, msg.c_str())) return;
    const char *event = doc["event"];
    if (event && strcmp(event, "ms.channel.connect") == 0) {
      // 连接已授权，发 key
      JsonDocument cmd;
      cmd["method"] = "ms.remote.control";
      cmd["params"]["Cmd"] = "Click";
      cmd["params"]["DataOfCmd"] = key;
      cmd["params"]["Option"] = "false";
      cmd["params"]["TypeOfRemote"] = "SendRemoteKey";

      String out;
      serializeJson(cmd, out);
      wsClient.send(out);
      success = true;
      Serial.printf("Key sent: %s\n", key);
    }
  };

  wsClient.onMessage(handler);

  if (!wsClient.connectSecure(TV_HOST, WS_PORT, wsPath)) {
    Serial.println("sendKey: WS connect failed");
    return false;
  }

  // 轮询等待 connect 帧和 key 发送
  unsigned long startMs = millis();
  while (millis() - startMs < 10000 && !success) {
    wsClient.poll();
    delay(10);
  }

  // 断开连接
  wsClient.close();
  delay(200);

  if (!success) {
    Serial.println("sendKey failed (no connect frame)");
  }
  return success;
}

void saveToken(const String &token) {
  prefs.begin("samsung_tv", false);
  prefs.putString("ws_token", token);
  prefs.end();
}

// ==================== REST 状态查询 ====================

bool fetchTVStatus() {
  if (strlen(TV_HOST) == 0) {
    tvStatusValid = false;
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  char url[128];
  snprintf(url, sizeof(url), "https://%s:%d%s", TV_HOST, WS_PORT, REST_PATH);

  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code != 200) {
    http.end();
    Serial.printf("Status fetch failed: %d\n", code);
    tvStatusValid = false;
    return false;
  }

  String resp = http.getString();
  http.end();

  int psIdx = resp.indexOf("\"PowerState\":\"");
  if (psIdx >= 0) {
    String ps = resp.substring(psIdx + 14, psIdx + 24);
    ps = ps.substring(0, ps.indexOf("\""));
    if (ps == "on") {
      tvOn = true;
      tvStatusValid = true;
    } else if (ps == "standby") {
      tvOn = false;
      tvStatusValid = true;
    } else if (ps.length() == 0) {
      Serial.println("Transient PowerState");
    } else {
      tvStatusValid = false;
    }
  } else {
    tvStatusValid = false;
  }

  Serial.printf("Status: on=%d valid=%d\n", tvOn, tvStatusValid);
  return tvStatusValid;
}

// ==================== 省电 ====================

void enterLightSleep() {
  Serial.println("Entering light sleep...");
  sleeping = true;

  gpio_wakeup_enable(GPIO_NUM_11, gpio_int_type_t::GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(GPIO_NUM_12, gpio_int_type_t::GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();

  esp_light_sleep_start();

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

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, reconnecting...");
    WiFi.reconnect();
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start < 5000)) {
      delay(100);
    }
  }

  fetchTVStatus();
  if (currentScreen == SCREEN_MAIN) drawMain();
  else drawStatus();
}

// ==================== WiFi ====================

bool connectWiFi() {
  Serial.printf("Connecting to %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setTxPower(WIFI_POWER_13dBm);
  esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
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

// ==================== UI ====================

void drawBatteryIcon(int x, int y, int level, bool charging) {
  int w = 22, h = 12;
  uint16_t color;
  if (charging) {
    color = 0xFE40;
  } else if (level < 20) {
    color = TFT_RED;
  } else {
    color = TFT_BLACK;
  }

  M5.Display.fillRect(x, y + 3, 3, 6, color);
  int bodyX = x + 3;
  M5.Display.drawRect(bodyX, y, w, h, color);

  int innerW = w - 4;
  int barW = innerW * level / 100;
  if (barW > 0) {
    M5.Display.fillRect(bodyX + 2 + (innerW - barW), y + 2, barW, h - 4, color);
  }

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
  String ssid = WIFI_SSID;
  if (ssid.length() > 20) ssid = ssid.substring(0, 19) + "...";
  M5.Display.drawCenterString(ssid, M5.Display.width() / 2, 155);
}

void drawPairing(bool waiting) {
  M5.Display.clear();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setFont(&fonts::FreeSansBold9pt7b);
  M5.Display.setTextSize(1);
  M5.Display.drawCenterString("Pairing", M5.Display.width() / 2, 28);

  M5.Display.setFont(&fonts::FreeSans9pt7b);
  if (waiting) {
    M5.Display.drawCenterString("Press ALLOW", M5.Display.width() / 2, 80);
    M5.Display.drawCenterString("on TV screen", M5.Display.width() / 2, 110);
  } else {
    M5.Display.setTextColor(0xC000, TFT_WHITE);
    M5.Display.drawCenterString("Timeout!", M5.Display.width() / 2, 90);
  }

  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextColor(COLOR_DIM, TFT_WHITE);
  M5.Display.drawCenterString("A/B to continue", M5.Display.width() / 2, 180);
}

void drawMain(bool busy) {
  M5.Display.clear();
  M5.Display.fillScreen(TFT_WHITE);

  int battLevel = M5.Power.getBatteryLevel();
  if (battLevel < 0) battLevel = 0;
  if (battLevel > 100) battLevel = 100;
  bool charging = (M5.Power.isCharging() == m5::Power_Class::is_charging);
  drawBatteryIcon(68, 4, battLevel, charging);

  M5.Display.setTextDatum(MC_DATUM);

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

  M5.Display.setFont(&fonts::FreeSans9pt7b);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.drawCenterString("Samsung TV", M5.Display.width() / 2, 135);

  M5.Display.drawLine(20, 190, M5.Display.width() - 20, 190, COLOR_DIM);

  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextColor(COLOR_DIM, TFT_WHITE);
  M5.Display.drawCenterString("A  POWER   B  INFO", M5.Display.width() / 2, 205);
}

void drawStatus() {
  M5.Display.clear();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);

  M5.Display.setTextDatum(TL_DATUM);
  M5.Display.setFont(&fonts::FreeSansBold9pt7b);
  M5.Display.setTextSize(1);
  M5.Display.drawString("TV Status", 8, 12);
  M5.Display.drawLine(8, 38, M5.Display.width() - 8, 38, COLOR_DIM);

  M5.Display.setFont(&fonts::FreeSans9pt7b);
  M5.Display.drawString("Power", 8, 52);
  if (tvOn) {
    M5.Display.setTextColor(TFT_DARKGREEN, TFT_WHITE);
  } else {
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  }
  M5.Display.setTextDatum(TR_DATUM);
  M5.Display.drawString(tvStatusValid ? (tvOn ? "ON" : "OFF") : "--", M5.Display.width() - 8, 52);

  // Volume 和 Muted 本地协议不可读
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(TL_DATUM);
  M5.Display.drawString("Volume", 8, 81);
  M5.Display.setTextDatum(TR_DATUM);
  M5.Display.setTextColor(COLOR_DIM, TFT_WHITE);
  M5.Display.drawString("N/A", M5.Display.width() - 8, 81);

  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(TL_DATUM);
  M5.Display.drawString("Muted", 8, 110);
  M5.Display.setTextDatum(TR_DATUM);
  M5.Display.setTextColor(COLOR_DIM, TFT_WHITE);
  M5.Display.drawString("N/A", M5.Display.width() - 8, 110);

  M5.Display.drawLine(8, 141, M5.Display.width() - 8, 141, COLOR_DIM);

  M5.Display.setTextDatum(TL_DATUM);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextColor(COLOR_DIM, TFT_WHITE);
  M5.Display.drawString("IP:", 8, 153);
  M5.Display.drawString(WiFi.localIP().toString(), 8, 168);

  String ssid = WIFI_SSID;
  if (ssid.length() > 20) ssid = ssid.substring(0, 19) + "...";
  M5.Display.drawString("SSID:", 8, 185);
  M5.Display.drawString(ssid, 8, 198);

  // 配对状态
  M5.Display.drawString("Paired:", 8, 215);
  M5.Display.setTextColor(tvConfigured ? TFT_DARKGREEN : TFT_RED, TFT_WHITE);
  M5.Display.drawString(tvConfigured ? "Yes" : "No", 70, 215);

  M5.Display.setTextColor(COLOR_DIM, TFT_WHITE);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.drawCenterString("A / B  BACK", M5.Display.width() / 2, 232);
}