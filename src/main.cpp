#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include "secrets.h"  // ssid, password, mqtt_server, ota_password
#include "motor.h"
#include "mqtt_client.h"

WiFiClient   wifiClient;
mqtt_controller mqtt(wifiClient);

// Task-Handle
TaskHandle_t mqttTaskHandle = NULL;

// function prototypes
void deepSleep_handling();
void setup_ota();

// ── WiFi verbinden ─────────────────────────────────────────────
void connectWiFi() {
  Serial.print("Verbinde WLAN...");
  WiFi.begin(ssid, password);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" verbunden!");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" WLAN Timeout!");
  }
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

// ── Setup ──────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  setup_motor();
  setup_mqtt();

  // WLAN verbinden
  connectWiFi();

  setup_ota();

  // MQTT-Task auf Core 0 starten (loop() läuft auf Core 1)
  xTaskCreatePinnedToCore(
    mqttTask,         // Task-Funktion
    "MQTT_Task",      // Name
    4096,             // Stack-Größe (Bytes)
    NULL,             // Parameter
    1,                // Priorität
    &mqttTaskHandle,  // Task-Handle
    0                 // Core 0
  );

  // Aufwachgrund anzeigen
  esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
  if (wakeup == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Aufgewacht aus Deep Sleep (Timer).");
  }

  Serial.println("Setup abgeschlossen. MQTT-Task gestartet.");
}

// ── Loop (Core 1) ─────────────────────────────────────────────
void loop() {
  ArduinoOTA.handle();

  // LED blinken
  static unsigned long lastMillis = 0;
  if (millis() - lastMillis > 200) {
    lastMillis = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  // Deep Sleep ausführen (im Loop, damit MQTT-Callback sauber zurückkehrt)
  if (getSleepRequested()) {
    deepSleep_handling();
  }

  // WiFi Reconnect
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  delay(1);
}

void deepSleep_handling() {

  // Motor stoppen
  motorStop();
  motorStandby();

  // LEDs ausschalten
  digitalWrite(LED_BUILTIN, LOW);

  // MQTT sauber trennen (Publish + Disconnect)
  mqtt.sleep("nano/esp32/status", "sleeping");

  // MQTT-Task stoppen BEVOR WiFi getrennt wird (Race Condition vermeiden)
  if (mqttTaskHandle != NULL) {
    vTaskDelete(mqttTaskHandle);
    mqttTaskHandle = NULL;
    Serial.println("MQTT-Task gestoppt.");
  }

  // WiFi off
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();
  Serial.flush();

  esp_sleep_enable_timer_wakeup(getSleepTimeUs());
  esp_deep_sleep_start();
    // ← hierhin kommt man nie, ESP32 startet nach Sleep neu
}

void setup_ota() {
  // OTA initialisieren
  ArduinoOTA.setHostname("NanoESP32");
  ArduinoOTA.setPassword(ota_password);

  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start – Stoppe MQTT...");
    mqtt.disconnect();
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA Ende");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Fortschritt: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Fehler [%u]: ", error);
    if (error == OTA_AUTH_ERROR)         Serial.println("Auth Fehler");
    else if (error == OTA_BEGIN_ERROR)   Serial.println("Begin Fehler");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Fehler");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Fehler");
    else if (error == OTA_END_ERROR)     Serial.println("End Fehler");
  });
  ArduinoOTA.begin();
}
