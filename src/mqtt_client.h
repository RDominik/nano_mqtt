#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <PubSubClient.h>

/**
 * @brief Handle incoming MQTT messages from subscribed topics.
 * @param[in] topic Zero-terminated topic string.
 * @param[in] payload Pointer to raw payload bytes.
 * @param[in] length Number of payload bytes.
 */
void mqttCallback(char* topic, byte* payload, unsigned int length);
/**
 * @brief Read and clear the pending deep-sleep request flag.
 * @retval true A sleep request was pending.
 * @retval false No sleep request was pending.
 */
bool get_sleepRequested();
/**
 * @brief Get configured deep-sleep duration in milliseconds.
 * @return Sleep duration in milliseconds.
 */
uint64_t get_sleepTimeMs();
/**
 * @brief Reset configured deep-sleep duration to zero.
 */
void reset_sleepTimeMs();
/**
 * @brief Initialize MQTT synchronization primitives and shared state.
 */
void setup_mqtt();

/**
 * @brief Thread-safe wrapper around PubSubClient used by this firmware.
 */
class mqtt_controller : public PubSubClient {
public:
    /**
     * @brief Construct the MQTT controller wrapper.
     * @param[in,out] client Underlying network client used by PubSubClient.
     */
    mqtt_controller(Client& client) : PubSubClient(client) {
        setServer("192.168.188.97", 1883);
        setCallback(mqttCallback);
    };
    /**
     * @brief Publish under mutex protection.
     * @param[in] topic Topic name.
     * @param[in] payload Message payload.
      * @param[in] retained When true, broker stores payload as retained message.
     * @retval true Publish accepted by PubSubClient.
     * @retval false Publish not sent (mutex timeout or disconnected state).
     */
     bool publishSafe(const char* topic, const char* payload, bool retained = false);
    /**
     * @brief Disconnect MQTT client under mutex protection.
     */
    void disconnect();
    /**
     * @brief Execute one MQTT processing cycle.
     * @details
     * Performs reconnect attempt if needed, then runs client loop and
     * periodic alive publications.
     */
    void mqttRun();
    /**
     * @brief Attempt one MQTT reconnect cycle.
     */
    void mqttReconnect();
    /**
     * @brief Publish sleep status and disconnect cleanly.
     * @param[in] topic Topic name.
     * @param[in] message Payload.
     */
    void sleep(const char* topic, const char* message);


private:
    char alive_counter[2] = {'0', '\0'};
};

#endif // MQTT_CLIENT_H
