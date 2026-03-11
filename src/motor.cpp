#include "motor.h"

int motorSpeed = 200;

void setup_motor() {
  // TB6612FNG Pins initialisieren
  pinMode(MOTOR_AIN1, OUTPUT);
  pinMode(MOTOR_AIN2, OUTPUT);
  pinMode(MOTOR_STBY, OUTPUT);
  digitalWrite(MOTOR_STBY, LOW);  // Standby bis MQTT-Befehl kommt

  // PWM für Motor konfigurieren
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(MOTOR_PWMA, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);
  Serial.println("TB6612FNG Motor-Treiber initialisiert.");

  // Motor im Standby starten
  motorStandby();
}
// ── Motor-Steuerung ────────────────────────────────────────────
void motorForward(int speed) {
  digitalWrite(MOTOR_STBY, HIGH);
  digitalWrite(MOTOR_AIN1, HIGH);
  digitalWrite(MOTOR_AIN2, LOW);
  ledcWrite(PWM_CHANNEL, speed);
  Serial.printf("Motor: VORWÄRTS, Speed=%d\n", speed);
}

void motorBackward(int speed) {
  digitalWrite(MOTOR_STBY, HIGH);
  digitalWrite(MOTOR_AIN1, LOW);
  digitalWrite(MOTOR_AIN2, HIGH);
  ledcWrite(PWM_CHANNEL, speed);
  Serial.printf("Motor: RÜCKWÄRTS, Speed=%d\n", speed);
}

void motorStop() {
  digitalWrite(MOTOR_AIN1, LOW);
  digitalWrite(MOTOR_AIN2, LOW);
  ledcWrite(PWM_CHANNEL, 0);
  Serial.println("Motor: STOPP");
}

void motorStandby() {
  digitalWrite(MOTOR_STBY, LOW);
  ledcWrite(PWM_CHANNEL, 0);
  Serial.println("Motor: STANDBY");
}