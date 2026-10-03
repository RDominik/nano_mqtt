#ifndef PINS_H
#define PINS_H

#include <Arduino.h>

namespace pins {

// DRV8838 motor driver pins
static constexpr int MOTOR_ENABLE = D9;   // PWM / enable
static constexpr int MOTOR_PHASE = D8;    // direction
static constexpr int MOTOR_SLEEP = D7;    // sleep control (HIGH = active)

// S13V30F5 regulator enable
static constexpr int REGULATOR_EN = D10;  // HIGH = enabled, LOW = disabled

// Input pins
static constexpr int BUTTON = D1;         // button (active LOW)
static constexpr int CLOSE_LIMIT = D2;    // closed-position limit switch (active LOW)
static constexpr int OPEN_LIMIT = D3;     // open-position limit switch (active LOW)

// Deep-sleep wakeup source (raw GPIO number for bit mask API)
static constexpr int BUTTON_WAKEUP_GPIO = BUTTON;

// XIAO ESP32C3 I2C pins
static constexpr int I2C_SDA = D4;
static constexpr int I2C_SCL = D5;

}  // namespace pins

#endif // PINS_H