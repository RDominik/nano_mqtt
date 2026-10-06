#include "sleep.h"

#include <WiFi.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

#include "battery.h"
#include "motor.h"
#include "mqtt_topics.h"
#include "pins.h"

/**
 * @file sleep.cpp
 * @brief Deep-sleep request state and shutdown sequence.
 */

static constexpr unsigned long MQTT_STATUS_SETTLE_DELAY_MS = 300UL;

static SemaphoreHandle_t sleepMutex = NULL;
RTC_DATA_ATTR static uint64_t sleepTimeMs = 0;
static volatile bool sleepRequested = false;

static void prepare_pins_for_deepsleep() {
  ledcWrite(PWM_CHANNEL, 0);
  ledcDetachPin(pins::MOTOR_ENABLE);

  pinMode(pins::MOTOR_ENABLE, OUTPUT);
  pinMode(pins::MOTOR_PHASE, OUTPUT);
  pinMode(pins::MOTOR_SLEEP, OUTPUT);
  pinMode(pins::REGULATOR_EN, OUTPUT);

  digitalWrite(pins::MOTOR_ENABLE, LOW);
  digitalWrite(pins::MOTOR_PHASE, LOW);
  digitalWrite(pins::MOTOR_SLEEP, LOW);
  digitalWrite(pins::REGULATOR_EN, LOW);

  gpio_hold_en((gpio_num_t)pins::MOTOR_ENABLE);
  gpio_hold_en((gpio_num_t)pins::MOTOR_PHASE);
  gpio_hold_en((gpio_num_t)pins::MOTOR_SLEEP);
  gpio_hold_en((gpio_num_t)pins::REGULATOR_EN);
  gpio_deep_sleep_hold_en();
}

static void ensure_sleep_mutex() {
  if (sleepMutex == NULL) {
    sleepMutex = xSemaphoreCreateMutex();
  }
}

void set_sleepRequested(bool requested, uint64_t time_in_ms) {
  ensure_sleep_mutex();
  if (xSemaphoreTake(sleepMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    sleepRequested = requested;
    sleepTimeMs = time_in_ms;
    xSemaphoreGive(sleepMutex);
  }
}

bool get_sleepRequested() {
  bool requested = false;
  ensure_sleep_mutex();
  if (xSemaphoreTake(sleepMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    requested = sleepRequested;
    sleepRequested = false;
    xSemaphoreGive(sleepMutex);
  }
  return requested;
}

uint64_t get_sleepTimeMs() {
  uint64_t time = 0;
  ensure_sleep_mutex();
  if (xSemaphoreTake(sleepMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    time = sleepTimeMs;
    xSemaphoreGive(sleepMutex);
  }
  return time;
}

void reset_sleepTimeMs() {
  ensure_sleep_mutex();
  if (xSemaphoreTake(sleepMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    sleepTimeMs = 0;
    xSemaphoreGive(sleepMutex);
  }
}

void deepSleep_handling(mqtt_controller& mqtt, TaskHandle_t& mqttTaskHandle) {
  motorStandby();
  mqtt.publishSafe(mqtt_topics::ENGINE_SET, "standby");
  prepare_pins_for_deepsleep();

  delay(MQTT_STATUS_SETTLE_DELAY_MS);

  if (mqttTaskHandle != NULL) {
    vTaskDelete(mqttTaskHandle);
    mqttTaskHandle = NULL;
  }

  uint64_t sleepDurationMs = get_sleepTimeMs();
  publish_batteryPercentNow(mqtt);
  mqtt.sleep(mqtt_topics::STATUS, "sleeping", true);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();
  Serial.flush();

  esp_sleep_enable_timer_wakeup(sleepDurationMs * 1000ULL);
  esp_deep_sleep_enable_gpio_wakeup(
      BIT(pins::BUTTON_WAKEUP_GPIO), ESP_GPIO_WAKEUP_GPIO_LOW);

  esp_deep_sleep_start();
}
