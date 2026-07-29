/*
 * IR Copier — M5StickS3 Samsung TV 控制
 *
 * 通过 WiFi + SmartThings API 控制三星电视
 * 硬件：M5StickS3 (ESP32-S3, WiFi)
 *
 * 按钮映射 (StickS3)：
 *   BtnA (GPIO11, 正面)  — 短按选择/执行，双击返回
 *   BtnB (GPIO12, 侧面)  — 短按切换/上下翻
 */

#include "M5Unified.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "secrets.h"

// ==================== SmartThings API ====================

#define ST_API_BASE "https://api.smartthings.com/v1/devices"
#define ST_CMD_PATH "/commands"
#define ST_STATUS_PATH "/components/main/status"

// ==================== UI 状态机 ====================

enum UIState {
  STATE_MENU,
  STATE_CMD_LIST,
  STATE_CMD_SENT,
  STATE_WIFI_CONNECTING,
};

UIState currentState = STATE_MENU;
int cmdIndex = 0;

// ==================== 预设命令 ====================

struct TVCommand {
  const char *name;
  const char *capability;   // SmartThings capability
  const char *command;      // SmartThings command
};

#define NUM_COMMANDS 6

static const TVCommand commands[NUM_COMMANDS] = {
  {"Power",     "switch",        "on"},      // toggle in code
  {"Vol+",      "audioVolume",   "volumeUp"},
  {"Vol-",      "audioVolume",   "volumeDown"},
  {"Mute",      "audioMute",     "mute"},    // toggle in code
  {"Source",    "mediaInputSource", "nextInput"},
  {"CH+",       "tvChannel",     "channelUp"},
};

// ==================== 函数声明 ====================

bool connectWiFi();
bool sendCommand(const char *capability, const char *command);
bool getSwitchState();
void drawMenu();
void drawCmdList();
void drawCmdSent(const char *name, bool ok);
void drawConnecting();
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
  Serial.println("Samsung TV Controller starting...");

  M5.Display.setRotation(3);
  M5.Display.clear();
  M5.Display.setTextColor(WHITE, TFT_BLACK);

  currentState = STATE_WIFI_CONNECTING;
  drawConnecting();

  if (!connectWiFi()) {
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.setCursor(10, 80);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1);
    M5.Display.printf("WiFi failed! Reset to retry.");
    Serial.println("WiFi connection failed");
    while (true) delay(1000);
  }

  Serial.println("WiFi connected, ready.");
  currentState = STATE_MENU;
  drawMenu();
}

// ==================== Loop ====================

void loop() {
  M5.update();

  switch (currentState) {

  case STATE_MENU: {
    if (M5.BtnA.wasSingleClicked()) {
      currentState = STATE_CMD_LIST;
      cmdIndex = 0;
      drawCmdList();
    }
    break;
  }

  case STATE_CMD_LIST: {
    if (M5.BtnB.wasPressed()) {
      cmdIndex = (cmdIndex + 1) % NUM_COMMANDS;
      drawCmdList();
    }
    if (M5.BtnA.wasDoubleClicked()) {
      currentState = STATE_MENU;
      drawMenu();
    } else if (M5.BtnA.wasSingleClicked()) {
      const TVCommand *cmd = &commands[cmdIndex];
      bool ok = false;

      // Power 和 Mute 是 toggle，先查当前状态再发反向命令
      if (strcmp(cmd->command, "on") == 0) {
        // Power toggle
        bool isOn = getSwitchState();
        ok = sendCommand(cmd->capability, isOn ? "off" : "on");
      } else {
        ok = sendCommand(cmd->capability, cmd->command);
      }

      currentState = STATE_CMD_SENT;
      drawCmdSent(cmd->name, ok);
    }
    break;
  }

  case STATE_CMD_SENT: {
    if (M5.BtnA.wasSingleClicked() || M5.BtnA.wasDoubleClicked() ||
        M5.BtnB.wasPressed()) {
      currentState = STATE_CMD_LIST;
      drawCmdList();
    }
    break;
  }

  case STATE_WIFI_CONNECTING: {
    // 等待连接完成，不做按钮处理
    break;
  }
  }

  delay(10);
}

// ==================== WiFi ====================

bool connectWiFi() {
  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
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
  client.setInsecure();  // 跳过证书验证（ESP32 资源有限）
  HTTPClient http;

  char url[256];
  snprintf(url, sizeof(url), "%s/%s%s", ST_API_BASE, ST_DEVICE_ID, ST_CMD_PATH);

  Serial.printf("Sending: %s/%s -> %s\n", capability, command, url);

  if (!http.begin(client, url)) {
    Serial.println("HTTP begin failed");
    return false;
  }

  http.addHeader("Authorization", "Bearer " ST_TOKEN);
  http.addHeader("Content-Type", "application/json");

  char body[256];
  snprintf(body, sizeof(body),
    "{\"commands\":[{\"component\":\"main\",\"capability\":\"%s\",\"command\":\"%s\"}]}",
    capability, command);

  int code = http.POST(body);
  http.end();

  Serial.printf("HTTP response: %d\n", code);
  return (code == 200);
}

bool getSwitchState() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  char url[256];
  snprintf(url, sizeof(url), "%s/%s%s", ST_API_BASE, ST_DEVICE_ID, ST_STATUS_PATH);

  if (!http.begin(client, url)) return true;  // 失败时默认返回 on
  http.addHeader("Authorization", "Bearer " ST_TOKEN);

  int code = http.GET();
  if (code != 200) {
    http.end();
    return true;
  }

  String response = http.getString();
  http.end();

  // 简单解析：找 "switch":{"switch":{"value":"on"|"off"}
  bool isOn = response.indexOf("\"value\":\"on\"") >= 0;
  Serial.printf("Switch state: %s\n", isOn ? "on" : "off");
  return isOn;
}

// ==================== UI ====================

void drawHeader(const char *title) {
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
  M5.Display.fillRect(0, 0, 240, 20, TFT_DARKGREY);
  M5.Display.setCursor(5, 2);
  M5.Display.setTextColor(WHITE, TFT_DARKGREY);
  M5.Display.printf("TV Ctrl - %s", title);
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

void drawConnecting() {
  M5.Display.clear();
  drawHeader("WiFi");
  M5.Display.setFont(&fonts::FreeMonoBold9pt7b);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setCursor(20, 45);
  M5.Display.printf("Connecting");
  M5.Display.setCursor(20, 65);
  M5.Display.printf("to WiFi...");
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setCursor(20, 90);
  M5.Display.printf("SSID: %s", WIFI_SSID);
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
  M5.Display.printf("%d commands", NUM_COMMANDS);
  M5.Display.setCursor(25, 90);
  if (WiFi.status() == WL_CONNECTED) {
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.printf("WiFi: connected");
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(25, 105);
    M5.Display.printf("IP: %s", WiFi.localIP().toString().c_str());
  }
  drawFooter("A:enter");
}

void drawCmdList() {
  M5.Display.clear();
  drawHeader("Samsung TV");
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);

  for (int i = 0; i < NUM_COMMANDS; i++) {
    int y = 25 + i * 15;
    if (i == cmdIndex) {
      M5.Display.fillRect(3, y - 2, 234, 14, TFT_BLUE);
      M5.Display.setTextColor(WHITE, TFT_BLUE);
    } else {
      M5.Display.setTextColor(WHITE, TFT_BLACK);
    }
    M5.Display.setCursor(8, y);
    M5.Display.printf("%s", commands[i].name);
  }

  drawFooter("B:dn A:run Ax2:back");
}

void drawCmdSent(const char *name, bool ok) {
  M5.Display.clear();
  drawHeader("Samsung TV");
  M5.Display.setFont(&fonts::FreeMonoBold9pt7b);
  if (ok) {
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.setCursor(20, 45);
    M5.Display.printf("Sent: %s", name);
  } else {
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.setCursor(20, 45);
    M5.Display.printf("FAIL: %s", name);
  }
  drawFooter("Any btn: back");
}