#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include "secrets.h"  // ssid, password, mqtt_server, ota_password
#include "motor.h"

WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

// Mutex für thread-sicheren Zugriff auf PubSubClient
SemaphoreHandle_t mqttMutex;

// Task-Handle
TaskHandle_t mqttTaskHandle = NULL;

// ── Deep Sleep ──────────────────────────────────────────────────
RTC_DATA_ATTR uint64_t sleepTimeUs = 0;  // bleibt über Deep Sleep erhalten
volatile bool sleepRequested = false;    // Flag: Sleep angefordert

// ── MQTT Callback (Subscribe) ──────────────────────────────────
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("Nachricht empfangen [%s]: ", topic);

  // Payload in String umwandeln
  char msg[length + 1];
  memcpy(msg, payload, length);
  msg[length] = '\0';
  Serial.println(msg);

  // ── Deep Sleep via MQTT ──
  if (strcmp(topic, "nano/esp32/sleepms") == 0) {
    long ms = atol(msg);
    if (ms > 0) {
      sleepTimeUs = (uint64_t)ms * 1000ULL;
      Serial.printf("Deep Sleep angefordert: %ld ms\n", ms);
      sleepRequested = true;
    } else if (ms == 0) {
      Serial.println("Sleep abgebrochen / deaktiviert.");
      sleepTimeUs = 0;
      sleepRequested = false;
    } else {
      Serial.println("Ungültiger Sleep-Wert (muss >= 0 sein)");
    }
    return;
  }

  // ── Motor-Steuerung via MQTT ──
  if (strcmp(topic, "nano/esp32/engine") == 0) {
    if (strcmp(msg, "forward") == 0) {
      motorForward(motorSpeed);
    } else if (strcmp(msg, "backward") == 0) {
      motorBackward(motorSpeed);
    } else if (strcmp(msg, "stop") == 0) {
      motorStop();
    } else if (strcmp(msg, "standby") == 0) {
      motorStandby();
    } else {
      // Zahl → Geschwindigkeit setzen + Richtung beibehalten
      int val = atoi(msg);
      if (val >= -255 && val <= 255 && val != 0) {
        motorSpeed = abs(val);
        if (val > 0) motorForward(motorSpeed);
        else         motorBackward(motorSpeed);
      } else if (val == 0) {
        motorStop();
      }
    }
  }
}

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

// ── MQTT Reconnect (ein Versuch) ──────────────────────────────
void mqttReconnect() {
  const char *mqtt_sub[2] = {"nano/esp32/engine", "nano/esp32/sleepms"};
  Serial.print("Versuche MQTT-Verbindung...");
  if (mqttClient.connect("NanoESP32-Client")) {
    Serial.println("verbunden");
    bool sub1 = mqttClient.subscribe(mqtt_sub[0]);
    bool sub2 = mqttClient.subscribe(mqtt_sub[1]);
    Serial.printf("Subscribe %s: %s\n", mqtt_sub[0], sub1 ? "OK" : "FAIL");
    Serial.printf("Subscribe %s: %s\n", mqtt_sub[1], sub2 ? "OK" : "FAIL");
  } else {
    Serial.print("fehlgeschlagen, RC=");
    Serial.println(mqttClient.state());
  }
}

// ── MQTT FreeRTOS-Task (läuft auf Core 0) ─────────────────────
void mqttTask(void* parameter) {
  char alive_counter[2] = {'0', '\0'};
  unsigned long lastMsg = 0;

  // Warte bis WiFi verbunden ist
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(mqttCallback);

  for (;;) {
    // Kein WiFi → warten
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (!mqttClient.connected()) {
        mqttReconnect();
        
      }
      mqttClient.loop();
      mqttClient.publish("nano/esp32/status", "online!");
      // Publish alle 10 Sekunden
      if (mqttClient.connected() && millis() - lastMsg > 10000) {
        lastMsg = millis();
        alive_counter[0]++;
        if (alive_counter[0] > '9') {
          alive_counter[0] = '0';
        }
        
        mqttClient.publish("nano/esp32/alive_counter", alive_counter);
        Serial.printf("alive counter: %s\n", alive_counter);
      }

      xSemaphoreGive(mqttMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ── Setup ──────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  setup_motor();
  
  mqttMutex = xSemaphoreCreateMutex();

  // WLAN verbinden
  connectWiFi();

  // OTA initialisieren
  ArduinoOTA.setHostname("NanoESP32");
  ArduinoOTA.setPassword(ota_password);

  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start – Stoppe MQTT...");
    if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      mqttClient.disconnect();
      xSemaphoreGive(mqttMutex);
    }
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
  if (sleepRequested) {
    sleepRequested = false;

    // Motor stoppen
    motorStop();
    motorStandby();

    // LEDs ausschalten
    digitalWrite(LED_BUILTIN, LOW);

    // MQTT sauber trennen
    if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      mqttClient.publish("nano/esp32/status", "sleeping");
      delay(100);  // Publish abschicken lassen
      mqttClient.disconnect();
      xSemaphoreGive(mqttMutex);
    }

    // WiFi abschalten
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    btStop();
    Serial.printf("Gehe in Deep Sleep für %llu ms...\n", sleepTimeUs / 1000ULL);
    Serial.flush();

    esp_sleep_enable_timer_wakeup(sleepTimeUs);
    esp_deep_sleep_start();
    // ← hierhin kommt man nie, ESP32 startet nach Sleep neu
  }

  // WiFi Reconnect
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  delay(1);
}