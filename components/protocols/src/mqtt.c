#include <string.h>
#include <math.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "mqtt_client.h"

#include "wifi.h"
#include "mqtt.h"
#include "evse.h"
#include "board_config.h"
#include "energy_meter.h"
#include "socket_lock.h"
#include "temp_sensor.h"
#include "proximity.h"

#define LWT_TOPIC        "state"
#define LWT_CONNECTED    "online"
#define LWT_DISCONNECTED "offline"

#define ARRAY_SIZE(_x_) (sizeof(_x_)/sizeof((_x_)[0]))


typedef void (*mqtt_value_set_handler)(char *data);

struct mqtt_value_set_handlers_funcs
{
  char command_topic[64];
  mqtt_value_set_handler handler;

};

static const char* TAG = "mqtt";

static TaskHandle_t mqtt_task;

static char mqtt_main_topic[32];

static char mqtt_main_id[32];

static char mqtt_lwt_topic[64];

static uint8_t mqtt_value_set_handler_count = 0;

static struct mqtt_value_set_handlers_funcs mqtt_value_set_handlers[15];

static int replacechar(char *str, char orig, char rep)
{
    char *ix = str;
    int n = 0;
    while((ix = strchr(ix, orig)) != NULL) {
        *ix++ = rep;
        n++;
    }
    return n;
}

static void mqtt_subscribe_set_function(
    esp_mqtt_client_handle_t client,
    char* topic,
    mqtt_value_set_handler handler)
{
    if (mqtt_value_set_handler_count < ARRAY_SIZE(mqtt_value_set_handlers)) {
        sprintf(mqtt_value_set_handlers[mqtt_value_set_handler_count].command_topic,
            "%s/%s/set", mqtt_main_topic, topic);
        mqtt_value_set_handlers[mqtt_value_set_handler_count].handler = handler;
        esp_mqtt_client_subscribe_single(
            client, mqtt_value_set_handlers[mqtt_value_set_handler_count].command_topic, /*qos*/0);
        mqtt_value_set_handler_count++;
    }
    else {
        ESP_LOGI(TAG, "Handler table overflow! Cannot set handler for topic %s", topic);
    }
}

static void mqtt_config_common_part(
    char* payload,
    char* field,
    char* name,
    char* icon,
    char* entity_category,
    uint8_t unique_nbr)
{
    char tmp[64];
    const esp_app_desc_t* app_desc = esp_app_get_description();

    wifi_get_ip(tmp);

    sprintf(payload, 
        "{"                                                 // 0
        "\"~\": \"%s\","                                    // 1
        "\"object_id\": \"%s_%s\","                         // 2
        "\"name\": \"%s\","                                 // 3
        "\"state_topic\": \"~/%s\","                        // 4
        "\"availability\": ["                               // 5
           "{"                                              // 6
              "\"topic\": \"~/%s\","                        // 7
              "\"payload_available\": \"%s\","              // 8
              "\"payload_not_available\": \"%s\""           // 9
           "}"                                              // 10
          "],"                                              // 11
        "\"device\": "                                      // 12
          "{"                                               // 13
              "\"identifiers\": [\"%s\"],"                  // 14
              "\"name\": \"%s\","                           // 15
              "\"model\": \"ESP32 EVSE\","                  // 16
              "\"manufacturer\": \"OULWare\","              // 17
              "\"sw_version\": \"%s\","                     // 18
              "\"configuration_url\": \"http://%s\""        // 19
          "}",                                              // 20
        mqtt_main_topic,          // 1
        mqtt_main_topic, field,   // 2
        name,                     // 3
        field,                    // 4
        LWT_TOPIC,                // 7
        LWT_CONNECTED,            // 8
        LWT_DISCONNECTED,         // 9
        mqtt_main_id,             // 14
        board_config.device_name, // 15
        app_desc->version,        // 18
        tmp);                     // 19

    if (unique_nbr) {
        sprintf(tmp, ",\"unique_id\": \"%s-%s-%d\"", mqtt_main_id, field, unique_nbr);
        strcat(payload, tmp);
              
    }
    else {
        sprintf(tmp ,",\"unique_id\": \"%s-%s\"", mqtt_main_id, field );
        strcat(payload, tmp);
    }

    if (strlen(icon)) {
        sprintf(tmp, ",\"icon\": \"%s\"", icon);
        strcat(payload, tmp);
    }

    if (strlen(entity_category)) {
        sprintf(tmp, ",\"entity_category\": \"%s\"", entity_category);
        strcat(payload, tmp);
    }
}

static void mqtt_cfg_sensor(
    esp_mqtt_client_handle_t client,
    uint8_t group,
    char* field,
    char* name,
    char* icon,
    char* unit,
    char* device_class,
    char* state_class,
    char* entity_category)
{
    if (board_config.mqtt_homeassistant_discovery) {

        char discovery_topic[100];
        char payload[1000];
        char tmp[100];

        /* See https://www.home-assistant.io/docs/mqtt/discovery/ and
         *     https://www.home-assistant.io/integrations/sensor.mqtt/ */
        if (group) {
            sprintf(discovery_topic, "homeassistant/sensor/%s/%s_%d/config", mqtt_main_topic, field, group);
        }
        else {
            sprintf(discovery_topic, "homeassistant/sensor/%s/%s/config", mqtt_main_topic, field);
        }

        mqtt_config_common_part(payload, field, name, icon, entity_category, group);

        if (strlen(unit)) {
            sprintf(tmp, ",\"unit_of_meas\": \"%s\"", unit);
            strcat(payload, tmp);
        }

        if (strlen(device_class)) {
            sprintf(tmp, ",\"device_class\": \"%s\"", device_class);
            strcat(payload, tmp);
        }

        if (strlen(state_class)) {
            sprintf(tmp, ",\"state_class\": \"%s\"", state_class);
            strcat(payload, tmp);
        }

        if (group) {
            char name_mod[32];
            strcpy(tmp, name);
            strcpy(name_mod, strlwr(tmp));
            replacechar(name_mod, ' ', '_');
            sprintf(tmp, ",\"value_template\": \"{{ value_json.%s }}\"", name_mod);
            strcat(payload, tmp);
        } 

        strcat(payload, "}");

        esp_mqtt_client_publish(client, discovery_topic, payload, 0, /*qos*/1, /*retain*/1);
    }
}

static void mqtt_cfg_number(
    esp_mqtt_client_handle_t client,
    char* field,
    char* name,
    char* icon,
    char* unit,
    char* device_class,
    float min,
    float max,
    float step,
    char* mode,
    mqtt_value_set_handler handler)
{
    if (board_config.mqtt_homeassistant_discovery) {
        char discovery_topic[100];
        char payload[1000];
        char tmp[100];

        /* See https://www.home-assistant.io/docs/mqtt/discovery/ and
         *     https://www.home-assistant.io/integrations/number.mqtt/ */
        sprintf(discovery_topic, "homeassistant/number/%s/%s/config", mqtt_main_topic, field);

        mqtt_config_common_part(payload, field, name, icon, "config", 0);

        sprintf(tmp, ",\"command_topic\": \"~/%s/set\"", field);
        strcat(payload, tmp);

        sprintf(tmp, ",\"min\": \"%f\"", min);
        strcat(payload, tmp);

        sprintf(tmp, ",\"max\": \"%f\"", max);
        strcat(payload, tmp);

        sprintf(tmp, ",\"step\": \"%f\"", step);
        strcat(payload, tmp);

        if (strlen(mode)) {
            sprintf(tmp, ",\"mode\": \"%s\"", mode);
            strcat(payload, tmp);
        }

        if (strlen(unit)) {
            sprintf(tmp, ",\"unit_of_meas\": \"%s\"", unit);
            strcat(payload, tmp);
        }

        if (strlen(device_class)) {
            sprintf(tmp, ",\"device_class\": \"%s\"", device_class);
            strcat(payload, tmp);
        }

        strcat(payload, "}");

        esp_mqtt_client_publish(client, discovery_topic, payload, 0, /*qos*/1, /*retain*/1);
    }

    mqtt_subscribe_set_function(client, field, handler);
}

static void mqtt_cfg_select(
    esp_mqtt_client_handle_t client,
    char* field,
    char* name,
    char* icon,
    char* options,
    mqtt_value_set_handler handler)
{
    if (board_config.mqtt_homeassistant_discovery) {
        char discovery_topic[100];
        char payload[1000];
        char tmp[512];

        /* See https://www.home-assistant.io/docs/mqtt/discovery/ and
         *     hhttps://www.home-assistant.io/integrations/select.mqtt/ */
        sprintf(discovery_topic, "homeassistant/select/%s/%s/config", mqtt_main_topic, field);

        mqtt_config_common_part(payload, field, name, icon, "config", 0);

        sprintf(tmp, ",\"command_topic\": \"~/%s/set\"", field);
        strcat(payload, tmp);

        sprintf(tmp, ",\"options\": [ %s ]", options);
        strcat(payload, tmp);

        strcat(payload, "}");

        esp_mqtt_client_publish(client, discovery_topic, payload, 0, /*qos*/1, /*retain*/1);
    }

    mqtt_subscribe_set_function(client, field, handler);
}

static void mqtt_cfg_switch(
    esp_mqtt_client_handle_t client,
    char* field,
    char* name,
    char* icon,
    char* device_class,
    mqtt_value_set_handler handler)
{
    if (board_config.mqtt_homeassistant_discovery) {
        char discovery_topic[100];
        char payload[1000];
        char tmp[100];

        /* See https://www.home-assistant.io/docs/mqtt/discovery/ and 
         *     https://www.home-assistant.io/integrations/switch.mqtt/ */
        sprintf(discovery_topic, "homeassistant/switch/%s/%s/config", mqtt_main_topic, field);

        mqtt_config_common_part(payload, field, name, icon, "config", 0);

        sprintf(tmp, ",\"command_topic\": \"~/%s/set\"", field);
        strcat(payload, tmp);

        if (strlen(device_class)) {
            sprintf(tmp, ",\"device_class\": \"%s\"", device_class);
            strcat(payload, tmp);
        }

        strcat(payload, "}");

        esp_mqtt_client_publish(client, discovery_topic, payload, 0, /*qos*/1, /*retain*/1);
    }

    mqtt_subscribe_set_function(client, field, handler);
}


// Maximum charging current / ama
static void mqtt_evse_set_max_charging_current(char* data) {
  evse_set_max_charging_current((uint8_t)(atoi(data)));
}

// Charging current / amp
static void mqtt_evse_set_charging_current(char* data) {
  evse_set_charging_current((uint16_t)(atoi(data)*10));
}

// Temperature threshold / amt
static void mqtt_evse_set_temp_threshold(char* data) {
  evse_set_temp_threshold((uint8_t)(atoi(data)));
}

// Default consumption limit / ate
static void mqtt_evse_set_consumption_limit(char* data) {
  evse_set_consumption_limit((uint32_t)(atoi(data)*1000));
}

// Default consumption limit / ate
static void mqtt_evse_set_default_consumption_limit(char* data) {
  evse_set_default_consumption_limit((uint32_t)(atoi(data)*1000));
}

// Charging time limit / att
static void mqtt_evse_set_charging_time_limit(char* data) {
  evse_set_charging_time_limit((uint32_t)(atoi(data)*1000));
}

// Default charging time limit
static void mqtt_evse_set_default_charging_time_limit(char* data) {
  evse_set_default_charging_time_limit((uint32_t)(atoi(data)*1000));
}

// Under power limit / upl
static void mqtt_evse_set_under_power_limit(char* data) {
  evse_set_under_power_limit((uint32_t)(atoi(data)*1000));
}

// Default under power limit / upl
static void mqtt_evse_set_default_under_power_limit(char* data) {
  evse_set_default_under_power_limit((uint32_t)(atoi(data)*1000));
}

// AC Voltage /acv
static void mqtt_evse_set_ac_voltage(char* data) {
  energy_meter_set_ac_voltage((uint16_t)(atoi(data)));
}
  // Energy meter mode / emm
static void mqtt_evse_set_energy_meter_mode(char* data) {
  energy_meter_set_mode(energy_meter_str_to_mode_mqtt(data));
}

// Set charger state / scs
static void mqtt_evse_set_charger_state(char* data) {
  evse_set_enabled(strcmp(data, "ON")==0 ? true : false);
}

// Card authorization required / acs
static void mqtt_evse_set_require_auth(char* data) {
  evse_set_require_auth(strcmp(data, "ON")==0 ? true : false);
}

// Socket outlet / sol
static void mqtt_evse_set_socket_outlet(char* data) {
  evse_set_socket_outlet(strcmp(data, "ON")==0 ? true : false);
}

static void mqtt_subscribe_send_ha_discovery(esp_mqtt_client_handle_t client) {

    // System Sensors:  Group| Field   | User Friendly Name           | Icon                          | Unit | Device Class        | State Class       | Entity category
    mqtt_cfg_sensor(client, 0, "rbt"   , "Uptime"                     , "mdi:clock-time-eight-outline", "s"  , ""                  , ""                , "diagnostic");
    mqtt_cfg_sensor(client, 0, "mac"   , "MAC Address"                , "mdi:network-outline"         , ""   , ""                  , ""                , "diagnostic");
    mqtt_cfg_sensor(client, 0, "ip"    , "IP Adress"                  , "mdi:network-outline"         , ""   , ""                  , ""                , "diagnostic");
    mqtt_cfg_sensor(client, 0, "fme"   , "Free Heap Memory"           , "mdi:memory"                  , "B"  , ""                  , "measurement"     , "diagnostic");
    mqtt_cfg_sensor(client, 0, "rssi"  , "Wi-Fi RSSI"                 , "mdi:wifi"                    , "dBm", "signal_strength"   , "measurement"     , "diagnostic");
    mqtt_cfg_sensor(client, 0, "tmc"   , "CPU Temperature"            , "mdi:thermometer"             , "°C" , "temperature"       , "measurement"     , "diagnostic");

    // EVSE Sensors:    Group| Field   | User Friendly Name           | Icon                          | Unit | Device Class        | State Class       | Entity category
    mqtt_cfg_sensor(client, 0, "cbl"   , "Cable maximum current"      , ""                            , "A"  , "current"           , "measurement"     , "");
    mqtt_cfg_sensor(client, 0, "ccs"   , "Current charger state"      , "mdi:auto-fix"                , ""   , ""                  , ""                , "");
    mqtt_cfg_sensor(client, 0, "cdi"   , "Charging duration"          , "mdi:timer-outline"           , "s"  , ""                  , "measurement"     , "");
    mqtt_cfg_sensor(client, 0, "err"   , "Error code"                 , "mdi:alert-circle-outline"    , ""   , ""                  , ""                , "");
    mqtt_cfg_sensor(client, 0, "eto"   , "Total energy"               , ""                            , "Wh" , "energy"            , "total_increasing", "");
    mqtt_cfg_sensor(client, 0, "lst"   , "Last session time"          , "mdi:counter"                 , "s"  , ""                  , "measurement"     , "");
    mqtt_cfg_sensor(client, 1, "nrg"   , "Voltage L1"                 , ""                            , "V"  , "voltage"           , "measurement"     , "");
    mqtt_cfg_sensor(client, 2, "nrg"   , "Voltage L2"                 , ""                            , "V"  , "voltage"           , "measurement"     , "");
    mqtt_cfg_sensor(client, 3, "nrg"   , "Voltage L3"                 , ""                            , "V"  , "voltage"           , "measurement"     , "");
    mqtt_cfg_sensor(client, 4, "nrg"   , "Current L1"                 , ""                            , "A"  , "current"           , "measurement"     , "");
    mqtt_cfg_sensor(client, 5, "nrg"   , "Current L2"                 , ""                            , "A"  , "current"           , "measurement"     , "");
    mqtt_cfg_sensor(client, 6, "nrg"   , "Current L3"                 , ""                            , "A"  , "current"           , "measurement"     , "");
    mqtt_cfg_sensor(client, 7, "nrg"   , "Current power"              , ""                            , "W"  , "power"             , "measurement"     , "");
    mqtt_cfg_sensor(client, 0, "rcd"   , "Residual current detection" , "mdi:current-dc"              , ""   , ""                  , ""                , "");
    mqtt_cfg_sensor(client, 0, "status", "Status"                     , "mdi:heart-pulse"             , ""   , ""                  , ""                , "");
    mqtt_cfg_sensor(client, 1, "tma"   , "Temperature sensor error"   , ""                            , ""   , ""                  , ""                , "");
    mqtt_cfg_sensor(client, 2, "tma"   , "Temperature sensor low"     , ""                            , "°C" , "temperature"       , "measurement"     , "");
    mqtt_cfg_sensor(client, 3, "tma"   , "Temperature sensor high"    , ""                            , "°C" , "temperature"       , "measurement"     , "");
    mqtt_cfg_sensor(client, 4, "tma"   , "Temperature sensor count"   , ""                            , ""   , ""                  , ""                , "");
    mqtt_cfg_sensor(client, 5, "tma"   , "Temperature sensor 1"       , ""                            , "°C" , "temperature"       , "measurement"     , "");
    mqtt_cfg_sensor(client, 6, "tma"   , "Temperature sensor 2"       , ""                            , "°C" , "temperature"       , "measurement"     , "");
    mqtt_cfg_sensor(client, 0, "lck"   , "Socket lock status"         , "mdi:lock-question"           , ""   , ""                  , ""                , "");

    // Numbers:             Field| User Friendly Name           | Icon               | Unit | Device Class | Min| Max| Step| Mode    | Value Set Handler
    mqtt_cfg_number(client, "ama", "Maximum charging current"   , ""                 , "A"  , "current"    , 6  , 32 , 1   , "box"   , mqtt_evse_set_max_charging_current);
    mqtt_cfg_number(client, "amp", "Charging current"           , ""                 , "A"  , "current"    , 6  , 32 , 0.1 , "slider", mqtt_evse_set_charging_current);
    mqtt_cfg_number(client, "amt", "Temperature threshold"      , ""                 , "°C ", "temperature", 40 , 80 , 1   , "box"   , mqtt_evse_set_temp_threshold);
    mqtt_cfg_number(client, "ate", "Consumption limit"          , ""                 , "kWh", "energy"     , 0  , 50 , 1   , "slider", mqtt_evse_set_consumption_limit);
    mqtt_cfg_number(client, "att", "Charging time limit"        , "mdi:timer-outline", "min", ""           , 0  , 300, 5   , "slider", mqtt_evse_set_charging_time_limit);
    mqtt_cfg_number(client, "upl", "Under power limit"          , ""                 , "kWh", "energy"     , 0  , 10 , 0.01, "slider", mqtt_evse_set_under_power_limit);
    mqtt_cfg_number(client, "date","Default consumption limit"  , ""                 , "kWh", "energy"     , 0  , 50 , 1   , "slider", mqtt_evse_set_default_consumption_limit);
    mqtt_cfg_number(client, "datt","Default charging time limit", "mdi:timer-outline", "min", ""           , 0  , 300, 5   , "slider", mqtt_evse_set_default_charging_time_limit);
    mqtt_cfg_number(client, "dupl","Default under power limit"  , ""                 , "kWh", "energy"     , 0  , 10 , 0.01, "slider", mqtt_evse_set_default_under_power_limit);
    mqtt_cfg_number(client, "acv", "AC Voltage"                 , ""                 , "V"  , "voltage"    , 100, 300, 1   , "box"   , mqtt_evse_set_ac_voltage);

    // Select:              Field | User Friendly Name | Icon                | Options
    mqtt_cfg_select(client, "emm" , "Energy meter mode", "mdi:meter-electric", "\"Dummy single phase\",\"Dummy three phase\",\"Current sensing\",\"Current and voltage sensing\"", mqtt_evse_set_energy_meter_mode);

    // Switch:              Field| User Friendly Name           | Icon          | Device Class| Value Set Handler
    mqtt_cfg_switch(client, "scs", "Set charger state"          , "mdi:auto-fix", "switch"    , mqtt_evse_set_charger_state);
    mqtt_cfg_switch(client, "acs", "Card authorization required", ""            , "switch"    , mqtt_evse_set_require_auth);
    mqtt_cfg_switch(client, "sol", "Socket outlet"              , ""            , "switch"    , mqtt_evse_set_socket_outlet);

    for (uint8_t i=0; i < mqtt_value_set_handler_count; i++) {
        ESP_LOGD(TAG, 
            "Handler index=%d topic=%s handler=%x",
            i,
            mqtt_value_set_handlers[i].command_topic,
            (unsigned int)mqtt_value_set_handlers[i].handler);
    }
}

static void mqtt_publish_static_data(esp_mqtt_client_handle_t client) {

  char topic[64];
  char payload[512];

  // Availability
  esp_mqtt_client_publish(client, mqtt_lwt_topic, LWT_CONNECTED, 0, /*qos*/1, /*retain*/1);

  // MAC
  sprintf(topic, "%s/mac", mqtt_main_topic);
  wifi_get_mac(payload, ":");
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // IP
  sprintf(topic, "%s/ip", mqtt_main_topic);
  wifi_get_ip(payload);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
}

static void mqtt_publish_system_data(esp_mqtt_client_handle_t client) {

  char topic[64];
  char payload[512];

  // Uptime
  sprintf(topic, "%s/rbt", mqtt_main_topic);
  sprintf(payload, "%lld", esp_timer_get_time() / 1000000);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Free Mem
  multi_heap_info_t heap_info;
  heap_caps_get_info(&heap_info, MALLOC_CAP_INTERNAL);
  sprintf(topic, "%s/fme", mqtt_main_topic);
  sprintf(payload, "%d", heap_info.total_free_bytes);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Wifi RSSI
  sprintf(topic, "%s/rssi", mqtt_main_topic);
  sprintf(payload, "%d", wifi_get_rssi());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // CPU Temperature
  sprintf(topic, "%s/tmc", mqtt_main_topic);
  sprintf(payload, "%.1f", temp_sensor_read_cpu_temperature());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
}

static void mqtt_publish_evse_sensor_data(esp_mqtt_client_handle_t client) {

  char topic[64];
  char payload[512];
  char tmp[64];

  // Cable maximum current
  sprintf(topic, "%s/cbl", mqtt_main_topic);
  sprintf(payload, "%d", proximity_get_max_current());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Current charger state
  sprintf(topic, "%s/ccs", mqtt_main_topic);
  sprintf(payload, "%s", (evse_is_enabled()? "Charging enabled":"Charging Disabled") );
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Charging duration
  sprintf(topic, "%s/cdi", mqtt_main_topic);
  sprintf(payload, "%ld", energy_meter_get_charging_time());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Error code
  sprintf(topic, "%s/err", mqtt_main_topic);
  sprintf(payload, "%s", evse_error_to_str(evse_get_error()));
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Total energy
  sprintf(topic, "%s/eto", mqtt_main_topic);
  sprintf(payload, "%ld", energy_meter_get_consumption());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Last session time
  sprintf(topic, "%s/lst", mqtt_main_topic);
  sprintf(payload, "%ld", energy_meter_get_session_time());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Measuremnt statistics
  sprintf(topic, "%s/nrg", mqtt_main_topic);
  sprintf(payload, "{");
  // Voltage L1 / L2 / L3
  sprintf(tmp, "\"voltage_l1\":%f,", energy_meter_get_l1_voltage());
  strcat(payload, tmp);
  sprintf(tmp, "\"voltage_l2\":%f,", energy_meter_get_l2_voltage());
  strcat(payload, tmp);
  sprintf(tmp, "\"voltage_l3\":%f,", energy_meter_get_l3_voltage());
  strcat(payload, tmp);
  // Current L1 / L2 /L3
  sprintf(tmp, "\"current_l1\":%f,", energy_meter_get_l1_current());
  strcat(payload, tmp);
  sprintf(tmp, "\"current_l2\":%f,", energy_meter_get_l2_current());
  strcat(payload, tmp);
  sprintf(tmp, "\"current_l3\":%f,", energy_meter_get_l3_current());
  strcat(payload, tmp);
  // Current power
  sprintf(tmp, "\"current_power\":%f}", energy_meter_get_l3_current());
  strcat(payload, tmp);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Residual current detection
  sprintf(topic, "%s/rcd", mqtt_main_topic);
  sprintf(payload, "%s", (evse_is_rcm()?"Detected":"Not detected"));
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Status
  sprintf(topic, "%s/status", mqtt_main_topic);
  sprintf(payload, "%s", evse_state_to_str(evse_get_state()));
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Temperature sensors
  sprintf(topic, "%s/tma", mqtt_main_topic);
  sprintf(payload, "{");
  sprintf(tmp, "\"temperature_sensor_error\":%d,", temp_sensor_is_error());
  strcat(payload, tmp);
  sprintf(tmp, "\"temperature_sensor_low\":%.1f,", temp_sensor_get_low()/100.0);
  strcat(payload, tmp);
  sprintf(tmp, "\"temperature_sensor_high\":%.1f,", temp_sensor_get_high()/100.0);
  strcat(payload, tmp);
  sprintf(tmp, "\"temperature_sensor_count\":%d,", temp_sensor_get_count());
  strcat(payload, tmp);
  int16_t curr_temps[2] = { 0 };
  uint8_t i;
  temp_sensor_get_temperatures(curr_temps);
  for (i=1 ; i < ARRAY_SIZE(curr_temps); i++)
  {
    sprintf(tmp, "\"temperature_sensor_%d\":%.1f,", i, curr_temps[i-1]/100.0);
    strcat(payload, tmp);
  }
  sprintf(tmp, "\"temperature_sensor_%d\":%.1f}", i, curr_temps[i-1]/100.0);
  strcat(payload, tmp);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Socket lock status
  sprintf(topic, "%s/lck", mqtt_main_topic);
  sprintf(payload, "%s", socket_lock_status_to_str(socket_lock_get_status()));
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

}

static void mqtt_publish_evse_number_data(esp_mqtt_client_handle_t client) {

  char topic[64];
  char payload[512];

  // Maximum current limit
  sprintf(topic, "%s/ama", mqtt_main_topic);
  sprintf(payload, "%d", evse_get_max_charging_current());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Requested current
  sprintf(topic, "%s/amp", mqtt_main_topic);
  sprintf(payload, "%f", evse_get_charging_current()/10.0);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Current temperature limit
  sprintf(topic, "%s/amt", mqtt_main_topic);
  sprintf(payload, "%d", evse_get_temp_threshold());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Automatic stop energy
  sprintf(topic, "%s/ate", mqtt_main_topic);
  sprintf(payload, "%f", evse_get_consumption_limit()/1000.0);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Automatic stop time
  sprintf(topic, "%s/att", mqtt_main_topic);
  sprintf(payload, "%f", evse_get_charging_time_limit()/60.0);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Under power limit
  sprintf(topic, "%s/upl", mqtt_main_topic);
  sprintf(payload, "%f", evse_get_under_power_limit()/1000.0);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Default automatic stop energy
  sprintf(topic, "%s/date", mqtt_main_topic);
  sprintf(payload, "%f", evse_get_default_consumption_limit()/1000.0);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Default automatic stop time
  sprintf(topic, "%s/datt", mqtt_main_topic);
  sprintf(payload, "%f", evse_get_default_charging_time_limit()/60.0);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Default under power limit
  sprintf(topic, "%s/dupl", mqtt_main_topic);
  sprintf(payload, "%f", evse_get_default_under_power_limit()/1000.0);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // AC Voltage
  sprintf(topic, "%s/acv", mqtt_main_topic);
  sprintf(payload, "%d", energy_meter_get_ac_voltage());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
}

static void mqtt_publish_evse_select_data(esp_mqtt_client_handle_t client) {

  char topic[64];
  char payload[512];

  // Energy meter mode
  sprintf(topic, "%s/emm", mqtt_main_topic);
  sprintf(payload, "%s", energy_meter_mode_to_str_mqtt(energy_meter_get_mode()));
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
}

static void mqtt_publish_evse_switch_data(esp_mqtt_client_handle_t client) {

  char topic[64];
  char payload[512];

  // Set charger state
  sprintf(topic, "%s/scs", mqtt_main_topic);
  sprintf(payload, "%s", evse_is_enabled()? "ON": "OFF");
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Card authorization required
  sprintf(topic, "%s/acs", mqtt_main_topic);
  sprintf(payload, "%s", evse_is_require_auth()? "ON": "OFF");
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Socket outlet
  sprintf(topic, "%s/sol", mqtt_main_topic);
  sprintf(payload, "%s", evse_get_socket_outlet()? "ON": "OFF");
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
}

static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT Connected");
        mqtt_subscribe_send_ha_discovery(client);
        mqtt_publish_static_data(client);
        mqtt_publish_system_data(client);
        mqtt_publish_evse_sensor_data(client);
        mqtt_publish_evse_number_data(client);
        mqtt_publish_evse_select_data(client);
        mqtt_publish_evse_switch_data(client);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT Disconnected");
        break;
    case MQTT_EVENT_DATA:
        char topic[32];
        char data[64];
        memcpy(topic, event->topic, event->topic_len);
        topic[event->topic_len] = '\0';
        memcpy(data, event->data, event->data_len);
        data[event->data_len] = '\0';
        ESP_LOGD(TAG, "MQTT Data event=%d topic=%s msg_id=%d DATA=%s", event->event_id, topic, event->msg_id, data);
        for (uint8_t i=0; i < mqtt_value_set_handler_count; i++) {
            if (strcmp(topic, mqtt_value_set_handlers[i].command_topic) == 0) {

            if (mqtt_value_set_handlers[i].handler) {
                    mqtt_value_set_handlers[i].handler(data);
                    mqtt_publish_evse_switch_data(client);
                }
                break;
            }
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    case MQTT_EVENT_PUBLISHED:
        break;
    case MQTT_EVENT_SUBSCRIBED:
        break;
    default:
        ESP_LOGD(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_task_func(void* param)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = board_config.mqtt_uri,
        .credentials.client_id = mqtt_main_id,
        .session.last_will.topic = mqtt_lwt_topic,
        .session.last_will.msg = LWT_DISCONNECTED,
        .session.last_will.msg_len = strlen(LWT_DISCONNECTED),
        .session.last_will.qos = 0,
        .session.last_will.retain = 1,
    };

    if (strlen(board_config.mqtt_username)) {
        mqtt_cfg.credentials.username = board_config.mqtt_username;
    }

    if (strlen(board_config.mqtt_password)) {
        mqtt_cfg.credentials.authentication.password = board_config.mqtt_password;
    }

    if (!xEventGroupWaitBits(wifi_event_group, WIFI_STA_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY)) {
        ESP_LOGI(TAG, "Waiting Wifi to be ready before starting MQTT service");
        do {
        } while (!xEventGroupWaitBits(wifi_event_group, WIFI_STA_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY));
    }
  
    ESP_LOGD(TAG, "Starting MQTT service");
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "client init failed!");
    }
    if (esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "client register failed!");
    }
    if (esp_mqtt_client_start(client) != ESP_OK) {
        ESP_LOGE(TAG, "client start failed!");
    }

    uint8_t static_data_publish_counter = 10;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        static_data_publish_counter--;
        if (!static_data_publish_counter) {
            mqtt_publish_system_data(client);
            static_data_publish_counter = 10;
            mqtt_publish_evse_sensor_data(client);
            mqtt_publish_evse_number_data(client);
            mqtt_publish_evse_select_data(client);
            mqtt_publish_evse_switch_data(client);
        }
    }
}

void mqtt_init(void)
{
    ESP_LOGD(TAG, "MQTT=%d", board_config.mqtt);
    ESP_LOGD(TAG, "MQTT_URI=%s", board_config.mqtt_uri);
    ESP_LOGD(TAG, "MQTT_MAIN_TOPIC=%s", board_config.mqtt_main_topic);
    ESP_LOGD(TAG, "MQTT_CLIENT_ID=%s", board_config.mqtt_client_id);
    ESP_LOGD(TAG, "MQTT_USER=%s", board_config.mqtt_username);
    ESP_LOGD(TAG, "MQTT_PASSWORD=%s", board_config.mqtt_password);
    ESP_LOGD(TAG, "MQTT_HOMEASSISTANT_DISCOVERY=%d", board_config.mqtt_homeassistant_discovery);

    if (board_config.mqtt) {
        char mac[15];
        wifi_get_mac(mac, "");
        sprintf(mqtt_main_id, "%s-%s", board_config.mqtt_client_id, &mac[6]);
        sprintf(mqtt_main_topic, "%s_%s", board_config.mqtt_main_topic, &mac[6]);
        sprintf(mqtt_lwt_topic, "%s/%s", mqtt_main_topic, LWT_TOPIC);
        ESP_LOGD(TAG, "Starting MQTT task");
        xTaskCreate(mqtt_task_func, "mqtt_task", 5*1024, NULL, 11, &mqtt_task);
    }
}
