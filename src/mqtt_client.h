#include <PubSubClient.h>
#include "secrets.h"  // ssid, password, mqtt_server, ota_password

class mqtt_controller : public PubSubClient {
public:
    mqtt_controller(Client& client) : PubSubClient(client) {
        setServer(mqtt_server, 1883);
        setCallback(mqttCallback);
        // Hier können Sie zusätzliche Initialisierungen vornehmen, z.B.:
        // - Standard-Callback setzen
        // - Verbindungsinformationen speichern
    };
    
    void mqttTask(void* parameter);
    void mqttReconnect();
    static void mqttCallback(char* topic, byte* payload, unsigned int length);
private:
    const char *mqtt_sub[2] = {"nano/esp32/engine", "nano/esp32/sleepms"};
};
