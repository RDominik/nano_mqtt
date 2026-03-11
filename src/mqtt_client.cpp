#include "mqtt_client.h"
#include "motor.h"

void setSleepRequested(bool requested, uint64_t time_in_us = 0UL);
void message_control(char* topic, char * msg); 

SemaphoreHandle_t mqttMutex = NULL;

RTC_DATA_ATTR uint64_t sleepTimeUs = 0;  // sleep time in microseconds, retained across deep sleep
volatile bool sleepRequested = false;  // flag to indicate sleep request

void setup_mqtt() {
  mqttMutex = xSemaphoreCreateMutex();

}

// ── MQTT Reconnect (ein Versuch) ──────────────────────────────
void mqtt_controller::mqttReconnect() {
  const char *mqtt_sub[2] = {"nano/esp32/engine", "nano/esp32/sleepms"};
  Serial.print("Versuche MQTT-Verbindung...");
  if (this->connect("NanoESP32-Client")) {
    Serial.println("verbunden");
    bool sub1 = this->subscribe(mqtt_sub[0]);
    bool sub2 = this->subscribe(mqtt_sub[1]);
    Serial.printf("Subscribe %s: %s\n", mqtt_sub[0], sub1 ? "OK" : "FAIL");
    Serial.printf("Subscribe %s: %s\n", mqtt_sub[1], sub2 ? "OK" : "FAIL");
  } else {
    Serial.print("fehlgeschlagen, RC=");
    Serial.println(this->state());
  }
}

void mqtt_controller::mqttRun() {
  char alive_counter[2] = {'0', '\0'};
  unsigned long lastMsg = 0;

    if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (!this->connected()) {
        this->mqttReconnect();
        
      }
      this->loop();
      this->publish("nano/esp32/status", "online!");
      // Publish alle 10 Sekunden
      if (this->connected() && millis() - lastMsg > 10000) {
        lastMsg = millis();
        alive_counter[0]++;
        if (alive_counter[0] > '9') {
          alive_counter[0] = '0';
        }
        
        this->publish("nano/esp32/alive_counter", alive_counter);
        Serial.printf("alive counter: %s\n", alive_counter);
      }
      xSemaphoreGive(mqttMutex);
    }


}

void mqtt_controller::sleep(const char* topic, const char* message) {
  if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    PubSubClient::publish(topic, message);
    delay(100);  // Publish abschicken lassen
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
  Serial.printf("Nachricht empfangen [%s]: ", topic);
  // Payload to string
  char msg[length + 1];
  memcpy(msg, payload, length);
  msg[length] = '\0';
  Serial.println(msg);
  message_control(topic, msg);

}

void message_control(char* topic, char * msg) {
  // ── Deep Sleep via MQTT ──
  if (strcmp(topic, "nano/esp32/sleepus") == 0) {
    long ms = atol(msg);
    if (ms > 0) {
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
  if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    sleepRequested = requested;
    if (requested) {
      sleepTimeUs = time_in_us;
    } else {
      sleepTimeUs = 0;
    }
    xSemaphoreGive(mqttMutex);
  }
}

bool getSleepRequested() {
  bool requested = false;
  if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    requested = sleepRequested;
    sleepRequested = false;  // reset flag after reading
    xSemaphoreGive(mqttMutex);
  }
  return requested;
}

uint64_t getSleepTimeUs() {
  uint64_t time = 0;
  if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    time = sleepTimeUs;
    xSemaphoreGive(mqttMutex);
  }
  return time;
}