#include <WiFi.h>
#include "mqtt_client.h"
#include "motor.h"
#include "mqtt_topics.h"

/**
 * @file mqtt_client.cpp
 * @brief MQTT runtime, callback dispatch, and shared sleep-request state.
 */

/**
 * @brief Store deep-sleep request state and optional sleep duration.
 * @param[in] requested True to request deep sleep, false to clear request.
 * @param[in] time_in_ms Sleep duration in milliseconds.
 */
void set_sleepRequested(bool requested, uint64_t time_in_ms = 0UL);
/**
 * @brief Dispatch one decoded MQTT message to local handlers.
 * @param[in] topic MQTT topic.
 * @param[in] msg Zero-terminated payload string.
 */
void message_control(char* topic, char * msg); 

static constexpr size_t MQTT_MSG_BUFFER_SIZE = 128;

/**
 * @brief Build a stable, device-unique MQTT client ID.
 * @param[out] out Buffer for zero-terminated client id.
 * @param[in] outSize Size of output buffer.
 */
static void buildMqttClientId(char* out, size_t outSize) {
  uint64_t chipId = ESP.getEfuseMac();
  snprintf(out, outSize, "NanoESP32-%04X%08X",
           (uint16_t)(chipId >> 32), (uint32_t)chipId);
}

SemaphoreHandle_t mqttMutex = NULL;
SemaphoreHandle_t valueMutex = NULL;

RTC_DATA_ATTR uint64_t sleepTimeMs = 0;  // sleep time in milliseconds, retained across deep sleep
volatile bool sleepRequested = false;    // flag to indicate sleep request

/**
 * @brief Initialize mutexes used for MQTT operations and shared values.
 */
void setup_mqtt() {
  // Separate mutexes keep MQTT I/O and sleep flags independent.
  mqttMutex = xSemaphoreCreateMutex();
  valueMutex = xSemaphoreCreateMutex();
}

// ── MQTT Reconnect (single attempt) ──────────────────────────
/**
 * @brief Perform one MQTT reconnect attempt and re-subscribe topics.
 */
void mqtt_controller::mqttReconnect() {
  // diagnostics: check WiFi status
  Serial.printf("WiFi Status: %d, IP: %s, RSSI: %d dBm\n",
                WiFi.status(), WiFi.localIP().toString().c_str(), WiFi.RSSI());

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("MQTT aborted: WiFi not connected!");
    return;
  }

  Serial.print("Versuche MQTT-Verbindung zu 192.168.188.97:1883...");
  char mqttClientId[32];
  buildMqttClientId(mqttClientId, sizeof(mqttClientId));

  if (this->connect(mqttClientId, mqtt_topics::STATUS, 1, true, "offline")) {
    Serial.println("connected");
    Serial.printf("MQTT Client ID: %s\n", mqttClientId);
    String ip = WiFi.localIP().toString();
    bool sub1 = this->subscribe(mqtt_topics::ENGINE);
    bool sub2 = this->subscribe(mqtt_topics::SLEEP_MS);
    this->publish(mqtt_topics::IP, ip.c_str());
    this->publish(mqtt_topics::STATUS, "online!", true);
    this->publish(mqtt_topics::ENGINE_STATUS, (sub1 ? "OK" : "FAIL"));
    this->publish(mqtt_topics::SLEEP_MS_STATUS, (sub2 ? "OK" : "FAIL"));
    Serial.printf("Publish %s: %s\n", mqtt_topics::IP, ip.c_str());
    Serial.printf("Publish %s: %s\n", mqtt_topics::STATUS, "online!");
    Serial.printf("Subscribe %s: %s\n", mqtt_topics::ENGINE, sub1 ? "OK" : "FAIL");
    Serial.printf("Subscribe %s: %s\n", mqtt_topics::SLEEP_MS, sub2 ? "OK" : "FAIL");
  } else {
    Serial.print("failed, RC=");
    Serial.println(this->state());
  }
}

/**
 * @brief Execute one protected MQTT processing cycle.
 */
void mqtt_controller::mqttRun() {

  static unsigned long lastMsg = 0;

    if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (!this->connected()) {
        this->mqttReconnect();
        
      }
      this->loop();

      // publish every second
      if (this->connected() && millis() - lastMsg > 1000) {
        lastMsg = millis();
        this->alive_counter[0]++;
        if (this->alive_counter[0] > '9') {
          this->alive_counter[0] = '0';
        }
        this->publish(mqtt_topics::STATUS, "online!", true);
        this->publish(mqtt_topics::ALIVE_COUNTER, this->alive_counter);
        Serial.printf("alive counter2: %s\n", this->alive_counter);
      }
      xSemaphoreGive(mqttMutex);
    }


}

/**
 * @brief Publish a final sleep status and disconnect.
 * @param[in] topic Target MQTT topic.
 * @param[in] message Payload message.
 */
void mqtt_controller::sleep(const char* topic, const char* message) {
  if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    PubSubClient::publish(topic, message);
    delay(100);  // allow publish to be sent
    PubSubClient::disconnect();
    xSemaphoreGive(mqttMutex);
  } 
}

/**
 * @brief Disconnect MQTT client under mutex protection.
 */
void mqtt_controller::disconnect() {
  if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    PubSubClient::disconnect();
    xSemaphoreGive(mqttMutex);
  }
  
}

/**
 * @brief Publish one message with mutex protection.
 * @param[in] topic Topic name.
 * @param[in] payload Payload string.
 * @retval true Publish accepted by PubSubClient.
 * @retval false Publish skipped (mutex timeout or client not ready).
 */
bool mqtt_controller::publishSafe(const char* topic, const char* payload, bool retained) {
  bool result = false;
  if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    result = PubSubClient::publish(topic, payload, retained);
    xSemaphoreGive(mqttMutex);
  }
  return result;
}

// ── MQTT Callback (Subscribe) ──────────────────────────────────
/**
 * @brief MQTT callback bridge from PubSubClient to local command handling.
 * @param[in] topic Topic string.
 * @param[in] payload Payload bytes.
 * @param[in] length Payload byte length.
 */
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("Message received [%s]: ", topic);
  // Payload conversion is explicitly bounded to avoid variable stack growth.
  char msg[MQTT_MSG_BUFFER_SIZE];
  size_t copyLen = (length < (MQTT_MSG_BUFFER_SIZE - 1U)) ? length : (MQTT_MSG_BUFFER_SIZE - 1U);
  memcpy(msg, payload, copyLen);
  msg[copyLen] = '\0';
  Serial.println(msg);
  message_control(topic, msg);

}

/**
 * @brief Handle parsed control messages for sleep and motor commands.
 * @param[in] topic Topic name.
 * @param[in] msg Zero-terminated payload content.
 */
void message_control(char* topic, char * msg) {
  // ── Deep Sleep via MQTT ──
  if (strcmp(topic, mqtt_topics::SLEEP_MS) == 0) {
    // Convert decimal payload text (milliseconds) to long integer; parsing stops at first non-digit.
    long ms = atol(msg);
    if (ms > 0) {
      set_sleepRequested(true, (uint64_t)ms);
    } else {
      set_sleepRequested(false);
    }
    return;
  }

  // motor control via MQTT
  if (strcmp(topic, mqtt_topics::ENGINE) == 0) {
    // Route command into motor queue; do not access motor hardware from MQTT task.
    if ((strcmp(msg, "open") == 0) || (strcmp(msg, "\"open\"") == 0)) {
      request_motor_command(MOTOR_CMD_FORWARD);
    } else if ((strcmp(msg, "close") == 0) || (strcmp(msg, "\"close\"") == 0)) {
      request_motor_command(MOTOR_CMD_BACKWARD);
    } else if ((strcmp(msg, "standby") == 0) || (strcmp(msg, "\"standby\"") == 0)) {
      request_motor_command(MOTOR_CMD_STANDBY);
    } else {
      request_motor_command(MOTOR_CMD_STOP);
    }
  }
}

/**
 * @brief Update shared sleep-request values.
 * @param[in] requested New sleep-request flag.
 * @param[in] time_in_ms Sleep duration in milliseconds.
 */
void set_sleepRequested(bool requested, uint64_t time_in_ms) {
  if (xSemaphoreTake(valueMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    sleepRequested = requested;
    sleepTimeMs = time_in_ms;
    xSemaphoreGive(valueMutex);
  }
}

/**
 * @brief Read and clear sleep-request flag atomically.
 * @retval true Sleep was requested.
 * @retval false No sleep request pending.
 */
bool get_sleepRequested() {
  bool requested = false;
  if (xSemaphoreTake(valueMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    requested = sleepRequested;
    sleepRequested = false;  // reset flag after reading
    xSemaphoreGive(valueMutex);
  }
  return requested;
}

/**
 * @brief Read configured sleep time in milliseconds.
 * @return Sleep duration in milliseconds.
 */
uint64_t get_sleepTimeMs() {
  uint64_t time = 0;
  if (xSemaphoreTake(valueMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    time = sleepTimeMs;
    xSemaphoreGive(valueMutex);
  }
  return time;
}

/**
 * @brief Reset configured sleep duration to zero.
 */
void reset_sleepTimeMs() {
  if (xSemaphoreTake(valueMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    sleepTimeMs = 0;
    xSemaphoreGive(valueMutex);
  }
}