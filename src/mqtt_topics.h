#ifndef MQTT_TOPICS_H
#define MQTT_TOPICS_H

namespace mqtt_topics {

static constexpr const char* ENGINE = "nano/esp32/engine";
static constexpr const char* ENGINE_SET = "nano/esp32/engine/set";
static constexpr const char* ENGINE_STATUS = "nano/esp32/engine/status";

static constexpr const char* SLEEP_MS = "nano/esp32/sleepms";
static constexpr const char* SLEEP_MS_STATUS = "nano/esp32/sleepms/status";
static constexpr const char* SLEEP_MS_WAKEUP_REASON = "nano/esp32/sleepms/wakeup_reason";

static constexpr const char* STATUS = "nano/esp32/status";
static constexpr const char* ALIVE_COUNTER = "nano/esp32/alive_counter";
static constexpr const char* IP = "nano/esp32/ip";
static constexpr const char* BOOT_COUNT = "nano/esp32/boot_count";
static constexpr const char* RESET_REASON = "nano/esp32/reset_reason";

static constexpr const char* BATTERY_MONITOR = "nano/esp32/battery/monitor";
static constexpr const char* BATTERY_VOLTAGE = "nano/esp32/battery/voltage";
static constexpr const char* BATTERY_PERCENT = "nano/esp32/battery/percent";
static constexpr const char* BATTERY_RATE = "nano/esp32/battery/rate";
static constexpr const char* BATTERY_CHARGING = "nano/esp32/battery/charging";

}  // namespace mqtt_topics

#endif // MQTT_TOPICS_H