/**************************************************************
  ESP32-CAM Smart Doorbell – Part 2 (2025)
  Auto Light + Real Doorbell + SD-Card Backup, Single board
  Author: Kukil Kashyap Borgohain (updated 2025)
**************************************************************/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <SD.h>
#include <SPI.h>
#include "esp_camera.h"

//  Pin Definitions 
#define PIR_PIN        13
#define LDR_PIN        34
#define FLASH_LED_PIN   4
#define RELAY_PIN      12   // Porch light
#define DOOR_PIN       14   // Reed switch / hall sensor
#define SD_CS           5

//  Camera Model AI-Thinker 
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

//  Globals 
String BOT_TOKEN = "";
String CHAT_ID   = "";
WiFiClientSecure client;
UniversalTelegramBot bot("", client);

unsigned long lastTrigger = 0;
const long TRIGGER_INTERVAL = 12000;  // 12 s between motion photos
bool lightManualOverride = false;
bool doorOpened = false;

File photoFile;
bool sdOK = false;

void initCamera() {
  camera_config_t config;
  // (same as Part 1 – omitted for brevity)
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  in config.pin_d2 = Y4_GPIO_NUM;
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
    Serial.printf("Camera init failed 0x%x\n", err);
    ESP.restart();
  }
}


//  Photo Capture 
bool initSD() {
  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card Mount Failed");
    return false;
  }
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return false;
  }
  Serial.println("SD Card initialized");
  return true;
}

camera_fb_t *takePhoto() {
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(100);
  camera_fb_t *fb = esp_camera_fb_get();
  digitalWrite(FLASH_LED_PIN, LOW);
  return fb;
}


void savePhotoToSD(camera_fb_t *fb) {
  String path = "/pic_" + String(millis()) + ".jpg";
  photoFile = SD.open(path, FILE_WRITE);
  if (photoFile) {
    photoFile.write(fb->buf, fb->len);
    photoFile.close();
    Serial.println("Saved to " + path);
    bot.sendMessage(CHAT_ID, "WiFi down – photo saved to SD: " + path, "");
  }
}


void sendPhotoTelegram() {
  camera_fb_t *fb = takePhoto();
  if (!fb) {
    bot.sendMessage(CHAT_ID, "Camera capture failed", "");
    return;
  }

  bool ok = bot.sendPhotoByBinary(CHAT_ID, "photo.jpg", fb->len, fb->buf,
                                  "image/jpeg", true);

  if (!ok) {
    Serial.println("Telegram failed – saving to SD");
    savePhotoToSD(fb);
  }
  esp_camera_fb_return(fb);
}



//  Telegram Commands 

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;

    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "Unauthorized", "");
      continue;
    }

    if (text == "/lighton")  { digitalWrite(RELAY_PIN, HIGH); lightManualOverride = true; bot.sendMessage(chat_id, "Light ON (manual)", ""); }
    if (text == "/lightoff") { digitalWrite(RELAY_PIN, LOW);  lightManualOverride = false; bot.sendMessage(chat_id, "Light OFF", ""); }
    if (text == "/ring")     { digitalWrite(RELAY_PIN, HIGH); delay(1000); digitalWrite(RELAY_PIN, LOW); bot.sendMessage(chat_id, "Ding-dong!", ""); }
    if (text == "/photo")    { bot.sendMessage(chat_id, "Snapping...", ""); sendPhotoTelegram(); }
    if (text == "/sdlist") {
      File root = SD.open("/");
      String list = "SD card files:\n";
      File file = root.openNextFile();
      while (file) {
        list += String(file.name()) + " (" + file.size() + " bytes)\n";
        file = root.openNextFile();
      }
      bot.sendMessage(chat_id, list, "");
    }
  }
}


//  Setup 

void setup() {
  Serial.begin(115200);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  pinMode(FLASH_LED_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(DOOR_PIN, INPUT_PULLDOWN);

  digitalWrite(RELAY_PIN, LOW);

  initCamera();
  sdOK = initSD();

  WiFiManager wm;
  WiFiManagerParameter p_token("token", "Telegram Bot Token", "", 50);
  WiFiManagerParameter p_chat("chat", "Your Chat ID", "", 20);
  wm.addParameter(&p_token);
  wm.addParameter(&p_chat);
  wm.setSaveConfigCallback([&]() {
    BOT_TOKEN = p_token.getValue();
    CHAT_ID   = p_chat.getValue();
    bot = UniversalTelegramBot(BOT_TOKEN, client);
  });

  if (!wm.autoConnect("Doorbell-Setup")) ESP.restart();

  client.setInsecure();
  bot = UniversalTelegramBot(BOT_TOKEN, client);

  Serial.println("Ready! IP: " + WiFi.localIP().toString());
}


//  Main Loop 
void loop() {
  // 1. Motion detection
  if (digitalRead(PIR_PIN) == HIGH && (millis() - lastTrigger > TRIGGER_INTERVAL)) {
    lastTrigger = millis();
    sendPhotoTelegram();

    // Auto light when dark + motion
    int lightLevel = analogRead(LDR_PIN);
    if (lightLevel < 800 && !lightManualOverride) {  // tweak 800 for your room
      digitalWrite(RELAY_PIN, HIGH);
      delay(60000);  // keep light on 60 s
      digitalWrite(RELAY_PIN, LOW);
    }
  }

  // 2. Door actually opened → ring bell
  if (digitalRead(DOOR_PIN) == HIGH && !doorOpened) {
    doorOpened = true;
    digitalWrite(RELAY_PIN, HIGH);
    delay(1000);
    digitalWrite(RELAY_PIN, LOW);
    bot.sendMessage(CHAT_ID, "Door opened – bell rang!", "");
  }
  if (digitalRead(DOOR_PIN) == LOW) doorOpened = false;

  // 3. Telegram polling
  static unsigned long lastBot = 0;
  if (millis() - lastBot > 1200) {
    int num = bot.getUpdates(bot.last_message_received + 1);
    if (num) handleNewMessages(num);
    lastBot = millis();
  }
}



