#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <esp_sleep.h>
#include "secrets.h"  // ssid, password, mqtt_server, ota_password
#include "motor.h"
#include "mqtt_client.h"
#include "battery.h"

WiFiClient   wifiClient;
mqtt_controller mqtt(wifiClient);

// ── Button ─────────────────────────────────────────────────────

const int BUTTON_GPIO = 9;    // raw GPIO number for wake-up mask

String wakeup_reason = "";

// Task-Handle
TaskHandle_t mqttTaskHandle = NULL;

// function prototypes
void deepSleep_handling();
void setup_ota();
void startup_task();
void mqttTask(void* parameter);
void connectWiFi();
void setup_pins();

// ── Setup ──────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  setup_pins();
  setup_motor();
  setup_mqtt();
  setup_battery();

  // connect WiFi
  connectWiFi();

  setup_ota();

  // start MQTT task on Core 0 (loop() runs on Core 1)
  xTaskCreatePinnedToCore(
    mqttTask,         // task function
    "MQTT_Task",      // name
    4096,             // stack size (bytes)
    NULL,             // parameter
    1,                // priority
    &mqttTaskHandle,  // task handle
    0                 // Core 0
  );

  // display wakeup reason
  esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
  switch (wakeup) {
    case ESP_SLEEP_WAKEUP_TIMER:
      wakeup_reason = "ESP_SLEEP_WAKEUP_TIMER";
      break;
    case ESP_SLEEP_WAKEUP_GPIO:
      wakeup_reason = "ESP_SLEEP_WAKEUP_GPIO";
      break;
    default:
      wakeup_reason = "NORMAL_START";
      break;
  }
}

void setup_pins() {
  pinMode(LED_BUILTIN, OUTPUT);
  // button with internal pull-up, active LOW
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // initialize TB6612FNG pins
  pinMode(MOTOR_AIN1, OUTPUT);
  pinMode(MOTOR_AIN2, OUTPUT);
  pinMode(MOTOR_STBY, OUTPUT);
  digitalWrite(MOTOR_STBY, LOW);  // standby until MQTT command 
}

// ── Loop (Core 1) ─────────────────────────────────────────────
void loop() {
  ArduinoOTA.handle();
  startup_task();
  run_motor(mqtt);
  // blink LED
  static unsigned long lastMillis = 0;
  if (millis() - lastMillis > 200) {
    lastMillis = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  // execute deep sleep (in loop so MQTT callback returns cleanly)
  if (getSleepRequested()) {
    deepSleep_handling();
  }

  // WiFi Reconnect
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  delay(1);
}

// ── MQTT FreeRTOS-Task (runs on Core 0) ─────────────────────
void mqttTask(void* parameter) {
  // wait until WiFi is connected
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    mqtt.mqttRun();

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ── WiFi connect ─────────────────────────────────────────────
void connectWiFi() {
  Serial.print("Connecting WiFi...");
  WiFi.begin(ssid, password);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" connected!");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" WiFi timeout!");
  }
}

void deepSleep_handling() {

  // stop motor before sleeping (avoid blocking sleep with running motor) --- IGNORE ---s
  motorStop();
  motorStandby();

  // turn off LEDs
  digitalWrite(LED_BUILTIN, LOW);

  // cleanly disconnect MQTT (publish + disconnect)
  mqtt.sleep("nano/esp32/status", "sleeping");

  // stop MQTT task BEFORE WiFi is disconnected (avoid race condition)
  if (mqttTaskHandle != NULL) {
    vTaskDelete(mqttTaskHandle);
    mqttTaskHandle = NULL;
    Serial.println("MQTT task stopped.");
  }

  // WiFi off
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();
  Serial.flush();

  // Timer Wake-Up
  esp_sleep_enable_timer_wakeup(getSleepTimeUs());

  // button wake-up (GPIO LOW = pressed), for ESP32-C3
  // esp_deep_sleep_enable_gpio_wakeup(BIT(BUTTON_GPIO), ESP_GPIO_WAKEUP_GPIO_LOW);

  Serial.println("Deep sleep with timer + button wake-up...");
  Serial.flush();

  esp_deep_sleep_start();
  // ← never reached, ESP32 restarts after sleep
}

void setup_ota() {
  // initialize OTA
  ArduinoOTA.setHostname("NanoESP32");
  ArduinoOTA.setPassword(ota_password);

  ArduinoOTA.onStart([]() {
    Serial.println("OTA start - stopping MQTT...");
    mqtt.disconnect();
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA Ende");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA error [%u]: ", error);
    if (error == OTA_AUTH_ERROR)         Serial.println("Auth error");
    else if (error == OTA_BEGIN_ERROR)   Serial.println("Begin error");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect error");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive error");
    else if (error == OTA_END_ERROR)     Serial.println("End error");
  });
  ArduinoOTA.begin();
}

void startup_task() {
  static boolean initialized = false;
  if (!initialized) {
    initialized = true;
    run_battery(mqtt);
    publish_batteryStatus(mqtt);
    mqtt.publish("nano/esp32/engine/wakeup_reason", wakeup_reason.c_str());
  }

}