#include "mqtt_client.h"
// ── MQTT Reconnect (ein Versuch) ──────────────────────────────
void mqtt_controller::mqttReconnect() {
  const char *mqtt_sub[2] = {"nano/esp32/engine", "nano/esp32/sleepms"};
  Serial.print("Versuche MQTT-Verbindung...");
  if (this->connect("NanoESP32-Client")) {
    Serial.println("verbunden");
    bool sub1 = this->subscribe(mqtt_sub[0]);
    bool sub2 = mqttClient.subscribe(mqtt_sub[1]);
    Serial.printf("Subscribe %s: %s\n", mqtt_sub[0], sub1 ? "OK" : "FAIL");
    Serial.printf("Subscribe %s: %s\n", mqtt_sub[1], sub2 ? "OK" : "FAIL");
  } else {
    Serial.print("fehlgeschlagen, RC=");
    Serial.println(mqttClient.state());
  }
}