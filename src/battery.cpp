#include "battery.h"
#include <Wire.h>
#include <Adafruit_MAX1704X.h>
#include <math.h>

/**
 * @file battery.cpp
 * @brief MAX17048 battery monitor setup, reading, and throttled MQTT publishing.
 */

/**
 * @brief Read battery voltage from MAX17048.
 * @return Battery voltage in volts, or -1.0f if monitor is unavailable.
 */
float getBatteryVoltage();    // voltage in volts
/**
 * @brief Read battery state-of-charge from MAX17048.
 * @return Battery percentage, or -1.0f if monitor is unavailable.
 */
float getBatteryPercent();    // state of charge in %
/**
 * @brief Read battery charge/discharge rate from MAX17048.
 * @return Charge rate in percent per hour, or 0.0f if monitor is unavailable.
 */
float getBatteryChargeRate(); // charge/discharge rate in %/h


static Adafruit_MAX17048 maxlipo;
static bool batteryOk = false;

// XIAO ESP32C3 I2C pins
#define I2C_SDA 6
#define I2C_SCL 7

static const unsigned long BATTERY_PUBLISH_INTERVAL_MS = 30000;
static const float BATTERY_VOLTAGE_DELTA = 0.02f;
static const float BATTERY_PERCENT_DELTA = 0.5f;
static const float BATTERY_RATE_DELTA = 0.10f;
static const float BATTERY_CHARGING_THRESHOLD = 0.05f;

/**
 * @brief Initialize I2C bus and probe MAX17048 monitor.
 */
void setup_battery() {
  Wire.begin(I2C_SDA, I2C_SCL);  // XIAO ESP32C3: SDA=GPIO6, SCL=GPIO7
  delay(200);  // give I2C bus time to settle

  if (!maxlipo.begin(&Wire)) {
    Serial.println("MAX17048 not found!");
    batteryOk = false;
  } else {
    Serial.println("MAX17048 found!");
    batteryOk = true;
  }
}

/**
 * @brief Publish monitor availability state.
 * @param[in,out] mqtt MQTT controller used for publishing.
 */
void publish_batteryStatus(mqtt_controller& mqtt) {
  if (batteryOk == false) {
    mqtt.publishSafe("nano/esp32/battery/monitor", "deactivated");
  } else {
    mqtt.publishSafe("nano/esp32/battery/monitor", "activated");
  }
}

/**
 * @brief Read battery voltage.
 * @return Voltage in volts, or -1.0f when unavailable.
 */
float getBatteryVoltage() {
  if (!batteryOk) return -1.0f;
  return maxlipo.cellVoltage();
}

/**
 * @brief Read battery percentage.
 * @return Percentage in range 0..100, or -1.0f when unavailable.
 */
float getBatteryPercent() {
  if (!batteryOk) return -1.0f;
  return maxlipo.cellPercent();
}

/**
 * @brief Read battery charge/discharge rate.
 * @return Rate in percent/hour, or 0.0f when unavailable.
 */
float getBatteryChargeRate() {
  if (!batteryOk) return 0.0f;
  return maxlipo.chargeRate();
}

/**
 * @brief Determine whether the battery is currently charging.
 * @return True when charge rate is above the charging threshold.
 */
bool getBatteryCharging() {
  if (!batteryOk) return false;
  return getBatteryChargeRate() > BATTERY_CHARGING_THRESHOLD;
}

/**
 * @brief Print current battery values to serial output.
 * @param[in,out] mqtt Unused parameter kept for API compatibility.
 */
void read_battery(mqtt_controller& mqtt) {
  (void)mqtt;
  float voltage = getBatteryVoltage();
  float percent = getBatteryPercent();
  float rate    = getBatteryChargeRate();
  if (voltage >= 0) {
    Serial.printf("Battery: %.2f V, %.1f %%, Rate: %.2f %%/h\n", voltage, percent, rate);
  } else {
    Serial.println("Battery monitor not available");
  }
}

/**
 * @brief Publish battery telemetry using value and time thresholds.
 * @param[in,out] mqtt MQTT controller used for publishing.
 */
void run_battery(mqtt_controller& mqtt) {
  static unsigned long lastBatMsg = 0;
  static bool haveLastValues = false;
  static float lastVoltage = 0.0f;
  static float lastPercent = 0.0f;
  static float lastRate = 0.0f;
  static bool lastCharging = false;

  unsigned long now = millis();
  float voltage = getBatteryVoltage();
  float percent = getBatteryPercent();
  float rate    = getBatteryChargeRate();
  bool charging = getBatteryCharging();

  if (voltage >= 0) {
    // Emit telemetry on relevant value changes or periodic heartbeat timeout.
    bool valueChanged = !haveLastValues ||
      (fabsf(voltage - lastVoltage) >= BATTERY_VOLTAGE_DELTA) ||
      (fabsf(percent - lastPercent) >= BATTERY_PERCENT_DELTA) ||
      (fabsf(rate - lastRate) >= BATTERY_RATE_DELTA) ||
      (charging != lastCharging);

    bool intervalReached = !haveLastValues || ((now - lastBatMsg) >= BATTERY_PUBLISH_INTERVAL_MS);
    if (!valueChanged && !intervalReached) {
      return;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", voltage);
    mqtt.publishSafe("nano/esp32/battery/voltage", buf);
    snprintf(buf, sizeof(buf), "%.1f", percent);
    mqtt.publishSafe("nano/esp32/battery/percent", buf);
    snprintf(buf, sizeof(buf), "%.2f", rate);
    mqtt.publishSafe("nano/esp32/battery/rate", buf);
    mqtt.publishSafe("nano/esp32/battery/charging", charging ? "charging" : "not_charging");
    Serial.printf("Battery: %.2f V, %.1f %%, Rate: %.2f %%/h\n", voltage, percent, rate);

    lastBatMsg = now;
    lastVoltage = voltage;
    lastPercent = percent;
    lastRate = rate;
    lastCharging = charging;
    haveLastValues = true;
  }
}
