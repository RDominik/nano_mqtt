#include <WiFi.h>
#include "mqtt_client.h"
#include "motor.h"

void setSleepRequested(bool requested, uint64_t time_in_us = 0UL);
void message_control(char* topic, char * msg); 

SemaphoreHandle_t mqttMutex = NULL;
SemaphoreHandle_t valueMutex = NULL;

RTC_DATA_ATTR uint64_t sleepTimeUs = 0;  // sleep time in microseconds, retained across deep sleep
volatile bool sleepRequested = false;    // flag to indicate sleep request

void setup_mqtt() {
  mqttMutex = xSemaphoreCreateMutex();
  valueMutex = xSemaphoreCreateMutex();
}

// ── MQTT Reconnect (single attempt) ──────────────────────────
void mqtt_controller::mqttReconnect() {
  const char *mqtt_sub[2] = {"nano/esp32/engine", "nano/esp32/sleepms"};

  // diagnostics: check WiFi status
  Serial.printf("WiFi Status: %d, IP: %s, RSSI: %d dBm\n",
                WiFi.status(), WiFi.localIP().toString().c_str(), WiFi.RSSI());

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("MQTT aborted: WiFi not connected!");
    return;
  }

  Serial.print("Versuche MQTT-Verbindung zu 192.168.188.97:1883...");
  if (this->connect("NanoESP32-Client")) {
    Serial.println("connected");
    bool sub1 = this->subscribe(mqtt_sub[0]);
    bool sub2 = this->subscribe(mqtt_sub[1]);
    this->publish((String(mqtt_sub[0]) + "/status").c_str(), (sub1 ? "OK" : "FAIL"));
    this->publish((String(mqtt_sub[1]) + "/status").c_str(), (sub2 ? "OK" : "FAIL"));
    Serial.printf("Subscribe %s: %s\n", mqtt_sub[0], sub1 ? "OK" : "FAIL");
    Serial.printf("Subscribe %s: %s\n", mqtt_sub[1], sub2 ? "OK" : "FAIL");
  } else {
    Serial.print("failed, RC=");
    Serial.println(this->state());
  }
}

void mqtt_controller::mqttRun() {

  static unsigned long lastMsg = 0;

    if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (!this->connected()) {
        this->mqttReconnect();
        
      }
      this->loop();
      this->publish("nano/esp32/status", "online!");
      // publish every 10 seconds
      if (this->connected() && millis() - lastMsg > 10000) {
        lastMsg = millis();
        this->alive_counter[0]++;
        if (this->alive_counter[0] > '9') {
          this->alive_counter[0] = '0';
        }
        
        this->publish("nano/esp32/alive_counter", this->alive_counter);
        Serial.printf("alive counter2: %s\n", this->alive_counter);
      }
      xSemaphoreGive(mqttMutex);
    }


}

void mqtt_controller::sleep(const char* topic, const char* message) {
  if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    PubSubClient::publish(topic, message);
    delay(100);  // allow publish to be sent
    PubSubClient::disconnect();
    xSemaphoreGive(mqttMutex);
  } 
}
void mqtt_controller::disconnect() {
  if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    PubSubClient::disconnect();
    xSemaphoreGive(mqttMutex);
  }
  
}
// ── MQTT Callback (Subscribe) ──────────────────────────────────
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("Message received [%s]: ", topic);
  // Payload to string
  char msg[length + 1];
  memcpy(msg, payload, length);
  msg[length] = '\0';
  Serial.println(msg);
  message_control(topic, msg);

}

void message_control(char* topic, char * msg) {
  // ── Deep Sleep via MQTT ──
  if (strcmp(topic, "nano/esp32/sleepms") == 0) {
    long ms = atol(msg);
    Serial.printf("Message received [%lu]: ", ms);
    if (ms > 0) {
      Serial.printf("Sleep requested [%lu ms]: ", ms);
      setSleepRequested(true, (uint64_t)ms * 1000ULL);
    } else {
      setSleepRequested(false);
    }
    return;
  }

  // motor control via MQTT
  if (strcmp(topic, "nano/esp32/engine") == 0) {
    if (strcmp(msg, "forward") == 0) {
      motorForward(motorSpeed);
    } else if (strcmp(msg, "backward") == 0) {
      motorBackward(motorSpeed);
    } else if (strcmp(msg, "standby") == 0) {
      motorStandby();
    } else {
      motorStop();
    }
  }
}

void setSleepRequested(bool requested, uint64_t time_in_us) {
  if (xSemaphoreTake(valueMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    sleepRequested = requested;
    Serial.printf("request [%d]: \n", sleepRequested);
    if (requested) {
      sleepTimeUs = time_in_us;
      Serial.printf("Sleep time set [%llu us]: \n", sleepTimeUs);
    } else {
      sleepTimeUs = 0;
    }
    xSemaphoreGive(valueMutex);
  }
}

bool getSleepRequested() {
  bool requested = false;
  if (xSemaphoreTake(valueMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    requested = sleepRequested;
    sleepRequested = false;  // reset flag after reading
    xSemaphoreGive(valueMutex);
  }
  return requested;
}

uint64_t getSleepTimeUs() {
  uint64_t time = 0;
  if (xSemaphoreTake(valueMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    time = sleepTimeUs;
    xSemaphoreGive(valueMutex);
  }
  return time;
}