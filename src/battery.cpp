#include "battery.h"
#include "mqtt_topics.h"
#include "pins.h"
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

static const unsigned long BATTERY_PUBLISH_INTERVAL_MS = 2000;
static const float BATTERY_VOLTAGE_DELTA = 0.02f;
static const float BATTERY_PERCENT_DELTA = 0.5f;
static const float BATTERY_RATE_DELTA = 0.10f;
// MAX17048 chargeRate can be small during gentle USB/trickle charging.
static const float BATTERY_CHARGING_THRESHOLD = 0.005f;
static const unsigned long BATTERY_REPROBE_INTERVAL_MS = 5000;
static const uint8_t BATTERY_INVALID_READ_LIMIT = 3;

/**
 * @brief Reinitialize the I2C bus used by the battery monitor.
 */
static void recoverBatteryI2cBus() {
  Wire.end();
  delay(5);
  Wire.begin(pins::I2C_SDA, pins::I2C_SCL);
  Wire.setClock(100000);  // 100 kHz is more robust on longer/noisier wiring
}

/**
 * @brief Probe MAX17048 and update availability state.
 * @retval true Monitor is available.
 * @retval false Monitor is unavailable.
 */
static bool probeBatteryMonitor() {
  bool ok = maxlipo.begin(&Wire);
  if (ok && !batteryOk) {
    Serial.println("MAX17048 found!");
  } else if (!ok && batteryOk) {
    Serial.println("MAX17048 lost!");
  }
  batteryOk = ok;
  return batteryOk;
}

/**
 * @brief Initialize I2C bus and probe MAX17048 monitor.
 */
void setup_battery() {
  Wire.begin(pins::I2C_SDA, pins::I2C_SCL);  // XIAO ESP32C3 I2C pins
  Wire.setClock(100000);         // keep I2C conservative for stability
  delay(200);  // give I2C bus time to settle

  if (!probeBatteryMonitor()) {
    Serial.println("MAX17048 not found at boot (will retry every 5s)");
  }
}

/**
 * @brief Publish monitor availability state.
 * @param[in,out] mqtt MQTT controller used for publishing.
 */
void publish_batteryStatus(mqtt_controller& mqtt) {
  if (batteryOk == false) {
    mqtt.publishSafe(mqtt_topics::BATTERY_MONITOR, "deactivated");
  } else {
    mqtt.publishSafe(mqtt_topics::BATTERY_MONITOR, "activated");
  }
}

/**
 * @brief Publish current battery percentage without threshold throttling.
 * @param[in,out] mqtt MQTT controller used for publishing.
 * @param[in] retained When true, publish as retained message.
 * @retval true Percentage was available and published.
 * @retval false Percentage was unavailable.
 */
bool publish_batteryPercentNow(mqtt_controller& mqtt, bool retained) {
  float percent = getBatteryPercent();
  if (percent < 0.0f || percent > 100.0f || !isfinite(percent)) {
    return false;
  }

  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", percent);
  mqtt.publishSafe(mqtt_topics::BATTERY_PERCENT, buf, retained);
  return true;
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
  float value = maxlipo.cellPercent();
  if (!isfinite(value)) return -1.0f;
  return value;
}

/**
 * @brief Read battery charge/discharge rate.
 * @return Rate in percent/hour, or 0.0f when unavailable.
 */
float getBatteryChargeRate() {
  if (!batteryOk) return 0.0f;
  float value = maxlipo.chargeRate();
  if (!isfinite(value)) return 0.0f;
  return value;
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
  static unsigned long lastProbeAttempt = 0;
  static bool lastPublishedMonitorState = false;
  static bool hasPublishedMonitorState = false;
  static uint8_t invalidReadStreak = 0;
  static unsigned long lastBatMsg = 0;
  static bool haveLastValues = false;
  static float lastVoltage = 0.0f;
  static float lastPercent = 0.0f;
  static float lastRate = 0.0f;
  static bool lastCharging = false;

  unsigned long now = millis();

  if (!batteryOk && ((now - lastProbeAttempt) >= BATTERY_REPROBE_INTERVAL_MS)) {
    lastProbeAttempt = now;
    probeBatteryMonitor();
  }

  if (!hasPublishedMonitorState || (batteryOk != lastPublishedMonitorState)) {
    mqtt.publishSafe(mqtt_topics::BATTERY_MONITOR, batteryOk ? "activated" : "deactivated", true);
    lastPublishedMonitorState = batteryOk;
    hasPublishedMonitorState = true;
  }

  float voltage = getBatteryVoltage();
  float percent = getBatteryPercent();
  float rate    = getBatteryChargeRate();
  bool charging = getBatteryCharging();

  if (voltage >= 0 && percent >= 0.0f && percent <= 100.0f && isfinite(rate)) {
    invalidReadStreak = 0;

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
    mqtt.publishSafe(mqtt_topics::BATTERY_VOLTAGE, buf);
    snprintf(buf, sizeof(buf), "%.1f", percent);
    mqtt.publishSafe(mqtt_topics::BATTERY_PERCENT, buf);
    snprintf(buf, sizeof(buf), "%.2f", rate);
    mqtt.publishSafe(mqtt_topics::BATTERY_RATE, buf);
    mqtt.publishSafe(mqtt_topics::BATTERY_CHARGING, charging ? "charging" : "not_charging");
    Serial.printf("Battery: %.2f V, %.1f %%, Rate: %.2f %%/h\n", voltage, percent, rate);

    lastBatMsg = now;
    lastVoltage = voltage;
    lastPercent = percent;
    lastRate = rate;
    lastCharging = charging;
    haveLastValues = true;
  } else if (batteryOk) {
    // Sensor returned invalid values (for example NaN) while device is present.
    Serial.println("Battery telemetry invalid (NaN/out-of-range), skipping publish");
    invalidReadStreak++;
    if (invalidReadStreak >= BATTERY_INVALID_READ_LIMIT) {
      Serial.println("Battery monitor read failed repeatedly, forcing I2C recovery and reprobe");
      batteryOk = false;
      haveLastValues = false;
      invalidReadStreak = 0;
      recoverBatteryI2cBus();
    }
  }
}
