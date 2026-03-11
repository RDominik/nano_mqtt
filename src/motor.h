#include <Arduino.h>

// ── TB6612FNG Motor-Treiber Pins ───────────────────────────────
const int MOTOR_AIN1 = D2;   // direction 1
const int MOTOR_AIN2 = D3;   // direction 2
const int MOTOR_PWMA = D5;   // PWM speed
const int MOTOR_STBY = D4;   // Standby (HIGH = active)

// PWM configuration (ESP32 LEDC)
const int PWM_CHANNEL  = 0;
const int PWM_FREQ     = 20000;  // 20 kHz
const int PWM_RES      = 8;     // 8 Bit → 0-255

// Current motor speed (0-255)
extern int motorSpeed;

void setup_motor();
// motor control functions for TB6612FNG motor driver
void motorForward(int speed);
void motorBackward(int speed);
void motorStop();
void motorStandby();