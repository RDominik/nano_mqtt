#include "motor.h"
#include "mqtt_topics.h"

#include <freertos/FreeRTOS.h>

/**
 * @file motor.cpp
 * @brief Motor driver control, input handling, command queue consumption, and state publishing.
 */

#define RUN_STATE_FORWARD 1
#define RUN_STATE_BACKWARD 2
#define RUN_STATE_STOP 3
#define RUN_STATE_STANDBY 4

int motorSpeed = 100;

/**
 * @brief Apply PWM duty cycle with 8-bit safety clamping.
 * @param[in] speed Requested duty cycle.
 */
static void applyMotorPwm(int speed) {
  int duty = constrain(speed, 0, 255);
  ledcWrite(PWM_CHANNEL, duty);
}

static uint8_t running_state = 0;
static volatile MotorCommand pending_command = MOTOR_CMD_NONE;
static portMUX_TYPE motorCommandMux = portMUX_INITIALIZER_UNLOCKED;

/**
 * @brief Atomically fetch and clear the pending queued motor command.
 * @return Last queued command or MOTOR_CMD_NONE.
 */
static MotorCommand fetch_motor_command() {
  MotorCommand cmd;
  portENTER_CRITICAL(&motorCommandMux);
  cmd = pending_command;
  pending_command = MOTOR_CMD_NONE;
  portEXIT_CRITICAL(&motorCommandMux);
  return cmd;
}

/**
 * @brief Queue one motor command to be processed in loop context.
 * @param[in] command Desired motor command.
 */
void request_motor_command(MotorCommand command) {
  portENTER_CRITICAL(&motorCommandMux);
  pending_command = command;
  portEXIT_CRITICAL(&motorCommandMux);
}

/**
 * @brief Initialize PWM output and put motor driver into sleep mode.
 */
void setup_motor() {

  // configure PWM for motor
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(MOTOR_ENABLE, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);
  Serial.println("DRV8838 motor driver initialized.");

  // start motor in standby
  motorStandby();
}
// ── Motor Control ──────────────────────────────────────────────
/**
 * @brief Drive motor forward.
 * @param[in] speed PWM duty cycle in range 0..255.
 */
void motorForward(int speed) {
  digitalWrite(MOTOR_SLEEP, HIGH);
  digitalWrite(MOTOR_PHASE, HIGH);
  applyMotorPwm(speed);
  Serial.printf("Motor FORWARD: pwm=%d\n", constrain(speed, 0, 255));
  running_state = RUN_STATE_FORWARD;
}

/**
 * @brief Drive motor backward.
 * @param[in] speed PWM duty cycle in range 0..255.
 */
void motorBackward(int speed) {
  digitalWrite(MOTOR_SLEEP, HIGH);
  digitalWrite(MOTOR_PHASE, LOW);
  applyMotorPwm(speed);
  Serial.printf("Motor BACKWARD: pwm=%d\n", constrain(speed, 0, 255));
  running_state = RUN_STATE_BACKWARD;
}

/**
 * @brief Stop motor motion by clearing direction pins and PWM.
 */
void motorStop() {
  digitalWrite(MOTOR_SLEEP, HIGH);
  ledcWrite(PWM_CHANNEL, 0);
  Serial.println("Motor STOP");
  running_state = RUN_STATE_STOP;
}

/**
 * @brief Set motor driver to standby mode.
 */
void motorStandby() {
  ledcWrite(PWM_CHANNEL, 0);
  digitalWrite(MOTOR_SLEEP, LOW);
  running_state = RUN_STATE_STANDBY;
  Serial.println("Motor: STANDBY");
}

/**
 * @brief Process queued commands, local inputs, and MQTT state publishing.
 * @param[in,out] mqtt MQTT controller used for transition publishes.
 */
void run_motor(mqtt_controller& mqtt) {
  static unsigned long lastButtonPress = 0;
  static uint8_t lastPublishedState = 0;
  // ── button handler (debounce 300 ms) ──
  static uint8_t state_button = 0U;
  static boolean button_flag = false;
  bool mqttCommandApplied = false;

  // Execute queued command first so remote control is reflected promptly.
  switch (fetch_motor_command()) {
    case MOTOR_CMD_FORWARD:
      Serial.println("MQTT command: FORWARD");
      motorForward(motorSpeed);
      mqttCommandApplied = true;
      break;
    case MOTOR_CMD_BACKWARD:
      Serial.println("MQTT command: BACKWARD");
      motorBackward(motorSpeed);
      mqttCommandApplied = true;
      break;
    case MOTOR_CMD_STANDBY:
      Serial.println("MQTT command: STANDBY");
      state_button = 0;
      motorStandby();
      mqttCommandApplied = true;
      break;
    case MOTOR_CMD_STOP:
      Serial.println("MQTT command: STOP");
      state_button = 0;
      motorStop();
      mqttCommandApplied = true;
      break;
    default:
      break;
  }

  // Reed switch is treated as a hard stop condition.
  // if (!mqttCommandApplied && digitalRead(REED1_PIN) == LOW) {
  //   state_button = 0;
  //   motorStop();
  // }
  if ((digitalRead(BUTTON_PIN) == LOW) && (millis() - lastButtonPress > 300) && !button_flag) {
    lastButtonPress = millis();
    button_flag = true;

    switch(state_button) {
      case 0:
        state_button = 1;
        motorForward(motorSpeed);
        break;
      case 1:
        state_button = 2;
        motorBackward(motorSpeed);
        break;
      default:
        state_button = 0;
        motorStop();
        break;
    }
  }
  if (digitalRead(BUTTON_PIN) == HIGH) {
      button_flag = false;
  }

  if (!mqttCommandApplied && running_state == lastPublishedState) {
    return;
  }

  // Publish only state transitions to keep MQTT traffic minimal.
  switch(running_state) {
    case RUN_STATE_FORWARD:
      mqtt.publishSafe(mqtt_topics::ENGINE_SET, "open");
      break;
    case RUN_STATE_BACKWARD:
      mqtt.publishSafe(mqtt_topics::ENGINE_SET, "close");
      break;
    case RUN_STATE_STOP:
      mqtt.publishSafe(mqtt_topics::ENGINE_SET, "stop");
      break;
    case RUN_STATE_STANDBY:
      mqtt.publishSafe(mqtt_topics::ENGINE_SET, "standby");
      break;
    default:
      break;
  }
  lastPublishedState = running_state;
}