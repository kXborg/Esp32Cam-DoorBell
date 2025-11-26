/**************************************************************
  ESP32-CAM Smart Doorbell – Telegram Motion Alert (2025)
  Part 1 – Single board, WiFiManager
  Author: Kukil Kashyap Borgohain (updated 2025)
**************************************************************/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include "esp_camera.h"

// Camera model: AI-Thinker
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Pins
#define PIR_PIN           13
#define LDR_PIN           34   // ADC1_CH6
#define FLASH_LED_PIN     4

// Telegram
String BOT_TOKEN = "";      // will be filled by WiFiManager
String CHAT_ID   = "";      // will be filled by WiFiManager

WiFiClientSecure client;
UniversalTelegramBot bot("", client);

unsigned long lastTrigger = 0;
const long TRIGGER_INTERVAL = 10000;  // min 10 s between photos

//  Camera Init 
void configCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    delay(1000);
    ESP.restart();
  }
}

//  Take & Send Photo 
String sendPhoto() {
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(100);

  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    digitalWrite(FLASH_LED_PIN, LOW);
    return "Capture failed";
  }

  digitalWrite(FLASH_LED_PIN, LOW);

  String result = bot.sendPhotoByBinary(CHAT_ID, "photo.jpg", fb->len, fb->buf,
                                        "image/jpeg", true);

  esp_camera_fb_return(fb);
  return result;
}


//  Telegram Commands 
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text    = bot.messages[i].text;

    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }

    if (text == "/start") {
      String msg = "ESP32-CAM Doorbell\n\n";
      msg += "/photo – take photo now\n";
      msg += "/flashon /flashoff – control flash\n";
      msg += "/status – uptime & light level";
      bot.sendMessage(chat_id, msg, "");
    }

    if (text == "/photo") {
      bot.sendMessage(chat_id, "Taking photo...", "");
      String res = sendPhoto();
      if (!res.startsWith("OK")) bot.sendMessage(chat_id, "Failed: " + res, "");
    }

    if (text == "/flashon")  { digitalWrite(FLASH_LED_PIN, HIGH); bot.sendMessage(chat_id, "Flash ON", ""); }
    if (text == "/flashoff") { digitalWrite(FLASH_LED_PIN, LOW);  bot.sendMessage(chat_id, "Flash OFF", ""); }

    if (text == "/status") {
      int light = analogRead(LDR_PIN);
      String msg = "Uptime: " + String(millis()/1000) + " s\n";
      msg += "LDR raw value: " + String(light);
      bot.sendMessage(chat_id, msg, "");
    }
  }
}

//  Setup 
void setup() {
  Serial.begin(115200);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout

  pinMode(FLASH_LED_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);

  configCamera();

  WiFiManager wm;
  WiFiManagerParameter bot_token("token", "Telegram Bot Token", "", 50);
  WiFiManagerParameter chat_id("chat", "Your Chat ID", "", 20);

  wm.addParameter(&bot_token);
  wm.addParameter(&chat_id);
  wm.setSaveConfigCallback([&]() {
    BOT_TOKEN = bot_token.getValue();
    CHAT_ID   = chat_id.getValue();
    bot = UniversalTelegramBot(BOT_TOKEN, client);
  });

  if (!wm.autoConnect("ESP32-Doorbell-Setup")) {
    Serial.println("Failed to connect – restarting...");
    delay(3000);
    ESP.restart();
  }

  Serial.println("WiFi connected – IP: " + WiFi.localIP().toString());

  client.setInsecure(); // needed for api.telegram.org
  bot = UniversalTelegramBot(BOT_TOKEN, client);
}

//  Loop 
void loop() {
  // Motion trigger
  if (digitalRead(PIR_PIN) == HIGH && (millis() - lastTrigger > TRIGGER_INTERVAL)) {
    lastTrigger = millis();
    sendPhoto();
  }

  // Telegram polling
  static unsigned long lastBotCheck = 0;
  if (millis() - lastBotCheck > 1000) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    if (numNewMessages) handleNewMessages(numNewMessages);
    lastBotCheck = millis();
  }
}


