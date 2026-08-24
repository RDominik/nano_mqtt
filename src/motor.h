#ifndef MOTOR_H
#define MOTOR_H

#include <Arduino.h>
#include "mqtt_client.h"
#include "pins.h"

/**
 * @brief Motor commands that can be queued from other execution contexts.
 * @details
 * The command queue is consumed in the main loop context by run_motor().
 */
enum MotorCommand : uint8_t {
	MOTOR_CMD_NONE = 0,
	MOTOR_CMD_FORWARD,
	MOTOR_CMD_BACKWARD,
	MOTOR_CMD_STOP,
	MOTOR_CMD_STANDBY
};

// PWM configuration (ESP32 LEDC)
const int PWM_CHANNEL  = 0;
const int PWM_FREQ     = 20000;  // 20 kHz
const int PWM_RES      = 8;     // 8 Bit → 0-255

// Current motor speed (0-255)
extern int motorSpeed;

/**
 * @brief Initialize the DRV8838 driver and PWM output.
 * @details
 * Configures LEDC PWM on ENABLE and sets the driver to sleep as a safe default.
 */
void setup_motor();
/**
 * @brief Drive the motor in forward direction.
 * @param[in] speed PWM duty cycle in the range 0..255.
 */
void motorForward(int speed);
/**
 * @brief Drive the motor in backward direction.
 * @param[in] speed PWM duty cycle in the range 0..255.
 */
void motorBackward(int speed);
/**
 * @brief Stop the motor.
 * @details
 * Disables both direction inputs and sets PWM duty to 0.
 */
void motorStop();
/**
 * @brief Put motor driver into standby mode.
 * @details
 * Standby mode lowers power consumption when no motion is needed.
 */
void motorStandby();
/**
 * @brief Queue a motor command for deferred processing.
 * @param[in] command Motor command to store in the single-slot queue.
 */
void request_motor_command(MotorCommand command);
/**
 * @brief Run motor control state machine and publish state transitions.
 * @param[in,out] mqtt MQTT controller used for status publishes.
 * @details
 * Applies queued commands, evaluates button/reed input logic, updates motor
 * outputs, and publishes state changes only on transitions.
 */
void run_motor(mqtt_controller& mqtt);

#endif // MOTOR_H