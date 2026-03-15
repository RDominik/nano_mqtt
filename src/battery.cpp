#include "battery.h"
#include <Wire.h>
#include <Adafruit_MAX1704X.h>

// prototype for I2C device
float getBatteryVoltage();    // voltage in volts
float getBatteryPercent();    // state of charge in %
float getBatteryChargeRate(); // charge/discharge rate in %/h


static Adafruit_MAX17048 maxlipo;
static bool batteryOk = false;

void setup_battery() {
  Wire.begin();  // default I2C pins (A4=SDA, A5=SCL on Nano ESP32)

  if (!maxlipo.begin()) {
    batteryOk = false;
  } else {
    batteryOk = true;
  }
}

void publish_batteryStatus(mqtt_controller mqtt) {
  if (batteryOk == false) {
    mqtt.publish("nano/esp32/battery/monitor", "deactivated");
  } else {
    mqtt.publish("nano/esp32/battery/monitor", "activated");
  }
}

float getBatteryVoltage() {
  if (!batteryOk) return -1.0f;
  return maxlipo.cellVoltage();
}

float getBatteryPercent() {
  if (!batteryOk) return -1.0f;
  return maxlipo.cellPercent();
}

float getBatteryChargeRate() {
  if (!batteryOk) return 0.0f;
  return maxlipo.chargeRate();
}

void run_battery(mqtt_controller mqtt) {
  static unsigned long lastBatMsg = 0;
  if (millis() - lastBatMsg > 30000) {
    lastBatMsg = millis();
    float voltage = getBatteryVoltage();
    float percent = getBatteryPercent();
    float rate    = getBatteryChargeRate();
    if (voltage >= 0) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.2f", voltage);
      mqtt.publish("nano/esp32/battery/voltage", buf);
      snprintf(buf, sizeof(buf), "%.1f", percent);
      mqtt.publish("nano/esp32/battery/percent", buf);
      snprintf(buf, sizeof(buf), "%.2f", rate);
      mqtt.publish("nano/esp32/battery/rate", buf);
      Serial.printf("Battery: %.2f V, %.1f %%, Rate: %.2f %%/h\n", voltage, percent, rate);


    }
  }
}
