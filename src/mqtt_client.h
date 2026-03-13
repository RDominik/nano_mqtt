#include <PubSubClient.h>



void mqttCallback(char* topic, byte* payload, unsigned int length);
bool getSleepRequested();
uint64_t getSleepTimeUs();
void setup_mqtt();

class mqtt_controller : public PubSubClient {
public:
    mqtt_controller(Client& client) : PubSubClient(client) {
        setServer("192.168.188.97", 1883);
        setCallback(mqttCallback);
        // Hier können Sie zusätzliche Initialisierungen vornehmen, z.B.:
        // - Standard-Callback setzen
        // - Verbindungsinformationen speichern
    };
    void disconnect();
    void mqttRun();
    void mqttReconnect();
    void sleep(const char* topic, const char* message);


private:
    const char *mqtt_sub[2] = {"nano/esp32/engine", "nano/esp32/sleepms"};
    char alive_counter[2] = {'0', '\0'};
};

