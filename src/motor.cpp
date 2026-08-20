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

static constexpr unsigned long MOTOR_HARD_OFF_TIMEOUT_MS = 30000UL;

int motorSpeed = 250;

/**
 * @brief Apply PWM duty cycle with 8-bit safety clamping.
 * @param[in] speed Requested duty cycle.
 */
static void applyMotorPwm(int speed) {
  int duty = constrain(speed, 0, 255);
  ledcWrite(PWM_CHANNEL, duty);
}

static uint8_t running_state = 0;
static unsigned long motor_run_started_ms = 0;
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
 * @brief Handle one queued MQTT motor command.
 * @param[in,out] state_button Button state machine state.
 * @retval true A command was applied.
 * @retval false No queued command was available.
 */
static bool process_mqtt_motor_command(uint8_t& state_button) {
  switch (fetch_motor_command()) {
    case MOTOR_CMD_FORWARD:
      Serial.println("MQTT command: FORWARD");
      motorForward(motorSpeed);
      return true;
    case MOTOR_CMD_BACKWARD:
      Serial.println("MQTT command: BACKWARD");
      motorBackward(motorSpeed);
      return true;
    case MOTOR_CMD_STANDBY:
      Serial.println("MQTT command: STANDBY");
      state_button = 0;
      motorStandby();
      return true;
    case MOTOR_CMD_STOP:
      Serial.println("MQTT command: STOP");
      state_button = 0;
      motorStop();
      return true;
    default:
      return false;
  }
}

/**
 * @brief Handle local button-driven motor state changes.
 * @param[in,out] lastButtonPress Timestamp of the last accepted press.
 * @param[in,out] button_flag Debounce latch.
 * @param[in,out] state_button Button state machine state.
 * @retval true The button started a new motor action.
 * @retval false No button action was taken.
 */
static bool process_button_motor_state(unsigned long& lastButtonPress, boolean& button_flag, uint8_t& state_button) {
  if ((digitalRead(BUTTON_PIN) == LOW) && (millis() - lastButtonPress > 300) && !button_flag) {
    lastButtonPress = millis();
    button_flag = true;

    switch (state_button) {
      case 0:
        state_button = 1;
        motorForward(motorSpeed);
        return true;
      case 1:
        state_button = 2;
        motorBackward(motorSpeed);
        return true;
      default:
        state_button = 0;
        motorStop();
        return true;
    }
  }

  if (digitalRead(BUTTON_PIN) == HIGH) {
    button_flag = false;
  }

  return false;
}

/**
 * @brief Enforce the hard timeout for the motor.
 * @param[in,out] state_button Button state machine state.
 * @retval true The timeout forced the motor into standby.
 * @retval false The timeout was not reached.
 */
static bool process_motor_timeout(uint8_t& state_button) {
  if ((running_state == RUN_STATE_FORWARD || running_state == RUN_STATE_BACKWARD) &&
      (motor_run_started_ms != 0) &&
      (millis() - motor_run_started_ms >= MOTOR_HARD_OFF_TIMEOUT_MS)) {
    Serial.println("Motor timeout reached -> hard off");
    state_button = 0;
    motorStandby();
    return true;
  }

  return false;
}

/**
 * @brief Publish the current motor state to MQTT.
 * @param[in,out] mqtt MQTT controller used for state publishing.
 */
static void publish_motor_state(mqtt_controller& mqtt) {
  switch (running_state) {
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
  motor_run_started_ms = millis();
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
  motor_run_started_ms = millis();
  running_state = RUN_STATE_BACKWARD;
}

/**
 * @brief Stop motor motion by clearing direction pins and PWM.
 */
void motorStop() {
  digitalWrite(MOTOR_SLEEP, HIGH);
  ledcWrite(PWM_CHANNEL, 0);
  Serial.println("Motor STOP");
  motor_run_started_ms = 0;
  running_state = RUN_STATE_STOP;
}

/**
 * @brief Set motor driver to standby mode.
 */
void motorStandby() {
  ledcWrite(PWM_CHANNEL, 0);
  digitalWrite(MOTOR_SLEEP, LOW);
  motor_run_started_ms = 0;
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
  static uint8_t state_button = 0U;
  static boolean button_flag = false;
  bool motorStateChanged = false;

  motorStateChanged = process_mqtt_motor_command(state_button);
  motorStateChanged = process_button_motor_state(lastButtonPress, button_flag, state_button) || motorStateChanged;
  motorStateChanged = process_motor_timeout(state_button) || motorStateChanged;

  if (!motorStateChanged && running_state == lastPublishedState) {
    return;
  }

  publish_motor_state(mqtt);
  lastPublishedState = running_state;
}