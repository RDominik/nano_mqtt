#include "motor.h"

int motorSpeed = 200;

void setup_motor() {

  // configure PWM for motor
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(MOTOR_PWMA, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);
  Serial.println("TB6612FNG motor driver initialized.");

  // start motor in standby
  motorStandby();
}
// ── Motor Control ──────────────────────────────────────────────
void motorForward(int speed) {
  digitalWrite(MOTOR_STBY, HIGH);
  digitalWrite(MOTOR_AIN1, HIGH);
  digitalWrite(MOTOR_AIN2, LOW);
  ledcWrite(PWM_CHANNEL, speed);
  Serial.printf("Motor: FORWARD, Speed=%d\n", speed);
}

void motorBackward(int speed) {
  digitalWrite(MOTOR_STBY, HIGH);
  digitalWrite(MOTOR_AIN1, LOW);
  digitalWrite(MOTOR_AIN2, HIGH);
  ledcWrite(PWM_CHANNEL, speed);
  Serial.printf("Motor: BACKWARD, Speed=%d\n", speed);
}

void motorStop() {
  digitalWrite(MOTOR_AIN1, LOW);
  digitalWrite(MOTOR_AIN2, LOW);
  ledcWrite(PWM_CHANNEL, 0);
  Serial.println("Motor: STOP");
}

void motorStandby() {
  digitalWrite(MOTOR_STBY, LOW);
  ledcWrite(PWM_CHANNEL, 0);
  Serial.println("Motor: STANDBY");
}

void run_motor(mqtt_controller mqtt) {
  static volatile bool motorRunning = false;  // motor toggle state
  static unsigned long lastButtonPress = 0;
  // ── button handler (debounce 300 ms) ──

  if (digitalRead(BUTTON_PIN) == LOW && millis() - lastButtonPress > 300) {
    lastButtonPress = millis();
    motorRunning = !motorRunning;
    if (motorRunning) {
      motorForward(motorSpeed);
      Serial.println("Button: Motor ON");
      mqtt.publish("nano/esp32/engine/status/set", "forward");
    } else {
      motorStop();
      Serial.println("Button: Motor OFF");
      mqtt.publish("nano/esp32/engine/status/set", "stop");
    }
  }
}