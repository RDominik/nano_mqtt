#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <esp_sleep.h>
#include "secrets.h"  // ssid, password, mqtt_server, ota_password
#include "motor.h"
#include "mqtt_client.h"
#include "battery.h"

/**
 * @file main.cpp
 * @brief Firmware entry point: setup, main loop, Wi-Fi/MQTT task orchestration, OTA, and deep sleep.
 */

WiFiClient   wifiClient;
mqtt_controller mqtt(wifiClient);

// ── Button ─────────────────────────────────────────────────────

const int BUTTON_GPIO = 3;    // raw GPIO number for wake-up mask

String wakeup_reason = "";

// Task-Handle
TaskHandle_t mqttTaskHandle = NULL;

// function prototypes
/**
 * @brief Prepare and enter deep sleep mode.
 */
void deepSleep_handling();
/**
 * @brief Configure OTA handlers and start OTA service.
 */
void setup_ota();
/**
 * @brief Run one-time startup publishes after MQTT connection is available.
 */
void startup_task();
/**
 * @brief Dedicated MQTT FreeRTOS task.
 * @param[in] parameter Unused task parameter.
 */
void mqttTask(void* parameter);
/**
 * @brief Trigger non-blocking Wi-Fi connect attempts.
 * @param[in] force When true, skip retry interval throttling.
 */
void connectWiFi(bool force = false);
/**
 * @brief Configure GPIO directions and pull-ups.
 */
void setup_pins();

const unsigned long WIFI_RETRY_INTERVAL_MS = 5000;

// ── Setup ──────────────────────────────────────────────────────
/**
 * @brief Arduino setup routine.
 */
void setup() {
  Serial.begin(115200);

  setup_pins();
  setup_motor();
  setup_mqtt();
  setup_battery();

  // connect WiFi
  WiFi.mode(WIFI_STA);
  connectWiFi(true);

  setup_ota();

  // start MQTT task (ESP32-C3 is single-core)
  xTaskCreate(
    mqttTask,         // task function
    "MQTT_Task",      // name
    8192,             // stack size (bytes) - increased for MQTT
    NULL,             // parameter
    1,                // priority
    &mqttTaskHandle   // task handle
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
  // pinMode(LED_BUILTIN, OUTPUT);
  // button with internal pull-up, active LOW
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(REED1_PIN, INPUT_PULLUP);
  // initialize TB6612FNG pins
  pinMode(MOTOR_AIN1, OUTPUT);
  pinMode(MOTOR_AIN2, OUTPUT);
  pinMode(MOTOR_STBY, OUTPUT);
  digitalWrite(MOTOR_STBY, LOW);  // standby until MQTT command 
}

// ── Loop (Core 1) ─────────────────────────────────────────────
/**
 * @brief Arduino loop routine.
 * @details
 * Keeps OTA, motor logic, battery telemetry, sleep handling, and Wi-Fi reconnect
 * responsive by avoiding long blocking waits.
 */
void loop() {
  static wl_status_t lastWiFiStatus = WL_DISCONNECTED;

  // Keep loop responsive: OTA, motor control, telemetry, and sleep checks.
  ArduinoOTA.handle();
  startup_task();
  run_motor(mqtt);
  // blink LED
  static unsigned long lastMillis = 0;
  if (millis() - lastMillis > 2000) {
    lastMillis = millis();
    run_battery(mqtt);
  
  //   lastMillis = millis();
  //   Serial.print("REED1_PIN: ");
  //   Serial.println(digitalRead(REED1_PIN));
    // digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  // execute deep sleep (in loop so MQTT callback returns cleanly)
  if (get_sleepRequested()) {
    deepSleep_handling();
  }

  // WiFi reconnect without blocking the loop.
  wl_status_t wifiStatus = WiFi.status();
  if (wifiStatus != lastWiFiStatus) {
    if (wifiStatus == WL_CONNECTED) {
      Serial.println("WiFi connected!");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("WiFi disconnected.");
    }
    lastWiFiStatus = wifiStatus;
  }

  if (wifiStatus != WL_CONNECTED) {
    connectWiFi();
  }

  delay(1);
}

// ── MQTT FreeRTOS-Task (runs on Core 0) ─────────────────────
/**
 * @brief MQTT background task.
 * @param[in] parameter Unused task argument provided by FreeRTOS.
 */
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
/**
 * @brief Schedule or force a Wi-Fi connect attempt.
 * @param[in] force When true, start a connection attempt immediately.
 */
void connectWiFi(bool force) {
  static unsigned long lastAttempt = 0;

  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  // Retry is throttled to avoid long blocking reconnect loops.
  unsigned long now = millis();
  if (!force && (now - lastAttempt) < WIFI_RETRY_INTERVAL_MS) {
    return;
  }

  lastAttempt = now;
  Serial.print("Connecting WiFi...");
  WiFi.begin(ssid, password);
}

/**
 * @brief Stop runtime services and enter deep sleep with timer and button wake-up.
 */
void deepSleep_handling() {

  // stop motor before sleeping (avoid blocking sleep with running motor) --- IGNORE ---s
  motorStop();
  motorStandby();

  // turn off LEDs
  // digitalWrite(LED_BUILTIN, LOW);

  // cleanly disconnect MQTT (publish + disconnect)
  uint64_t sleepTimeMs = get_sleepTimeMs();
  mqtt.publishSafe("nano/esp32/sleepms", "0");
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

  // Timer Wake-Up API expects microseconds; stored value is milliseconds.
  esp_sleep_enable_timer_wakeup(sleepTimeMs * 1000ULL);

  // button wake-up (GPIO LOW = pressed), for ESP32-C3
  esp_deep_sleep_enable_gpio_wakeup(BIT(BUTTON_GPIO), ESP_GPIO_WAKEUP_GPIO_LOW);

  Serial.println("Deep sleep with timer + button wake-up...");
  Serial.flush();

  esp_deep_sleep_start();
  // ← never reached, ESP32 restarts after sleep
}

/**
 * @brief Configure OTA callbacks and start OTA listener.
 */
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

/**
 * @brief Publish startup-only telemetry once MQTT becomes connected.
 */
void startup_task() {
  static boolean initialized = false;
  if (!initialized && mqtt.connected()) {
    initialized = true;
    run_battery(mqtt);
    publish_batteryStatus(mqtt);
    mqtt.publishSafe("nano/esp32/sleepms/wakeup_reason", wakeup_reason.c_str());
  }

}