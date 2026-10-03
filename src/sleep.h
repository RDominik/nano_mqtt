#ifndef SLEEP_H
#define SLEEP_H

#include <Arduino.h>
#include "mqtt_client.h"

/**
 * @brief Store a deep-sleep request and its duration.
 * @param[in] requested True to request deep sleep.
 * @param[in] time_in_ms Sleep duration in milliseconds.
 */
void set_sleepRequested(bool requested, uint64_t time_in_ms = 0UL);

/**
 * @brief Read and clear the pending deep-sleep request.
 * @retval true A sleep request was pending.
 * @retval false No sleep request was pending.
 */
bool get_sleepRequested();

/**
 * @brief Get the configured deep-sleep duration.
 * @return Sleep duration in milliseconds.
 */
uint64_t get_sleepTimeMs();

/**
 * @brief Reset the configured deep-sleep duration to zero.
 */
void reset_sleepTimeMs();

/**
 * @brief Stop runtime services and enter deep sleep.
 * @param[in,out] mqtt MQTT controller used for final status publishing.
 * @param[in,out] mqttTaskHandle Handle of the MQTT task to stop.
 */
void deepSleep_handling(mqtt_controller& mqtt, TaskHandle_t& mqttTaskHandle);

#endif // SLEEP_H
