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
#include "power_outlet.h"
#include "button.h"

#define LWT_TOPIC        "state"
#define LWT_CONNECTED    "online"
#define LWT_DISCONNECTED "offline"

#define FORCE_UPDATE_SECONDS  (10*60) // 10 minutes

#define ARRAY_SIZE(_x_) (sizeof(_x_)/sizeof((_x_)[0]))


typedef void (*mqtt_value_set_handler)(char *data);

struct mqtt_value_set_handlers_funcs
{
  char command_topic[64];
  mqtt_value_set_handler handler;

};

static const char* TAG = "mqtt";

static TaskHandle_t mqtt_task;

static bool mqtt_connected;

static char mqtt_main_topic[32];

static char mqtt_main_id[32];

static char mqtt_lwt_topic[64];

static uint8_t mqtt_value_set_handler_count;

static struct mqtt_value_set_handlers_funcs mqtt_value_set_handlers[18];

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
        int msg_id = esp_mqtt_client_subscribe_single(
                client, mqtt_value_set_handlers[mqtt_value_set_handler_count].command_topic, /*qos*/1);
        if (msg_id < 0) {
            ESP_LOGE(TAG, "Unable to subscribe topic %s. error %d", topic, msg_id);
        } else {
            ESP_LOGD(TAG, "Subscribed to topic %s. msg_id %d", topic, msg_id);
        }
        mqtt_value_set_handler_count++;
    } else {
        ESP_LOGE(TAG, "Handler table overflow! Cannot set handler for topic %s", topic);
    }
}

static char* mqtt_config_common_part(
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

    payload += sprintf(payload,
        "{"                                                 // 0
        "\"~\":\"%s\","                                     // 1
        "\"object_id\":\"%s_%s\","                          // 2
        "\"name\":\"%s\","                                  // 3
        "\"state_topic\":\"~/%s\","                         // 4
        "\"availability\":["                                // 5
           "{"                                              // 6
              "\"topic\":\"~/%s\","                         // 7
              "\"payload_available\":\"%s\","               // 8
              "\"payload_not_available\":\"%s\""            // 9
           "}"                                              // 10
          "],"                                              // 11
        "\"device\":"                                       // 12
          "{"                                               // 13
              "\"identifiers\":[\"%s\"],"                   // 14
              "\"name\":\"%s\","                            // 15
              "\"model\":\"ESP32 EVSE\","                   // 16
              "\"manufacturer\":\"OULWare\","               // 17
              "\"sw_version\":\"%s\","                      // 18
              "\"configuration_url\":\"http://%s\""         // 19
          "},"                                              // 20
        "\"qos\":1",
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
        payload += sprintf(payload, ",\"unique_id\":\"%s-%s-%d\"",
                           mqtt_main_id, field, unique_nbr);

    }
    else {
        payload += sprintf(payload, ",\"unique_id\":\"%s-%s\"", mqtt_main_id, field);
    }

    if (strlen(icon)) {
        payload += sprintf(payload, ",\"icon\":\"%s\"", icon);
    }

    if (strlen(entity_category)) {
        payload += sprintf(payload, ",\"entity_category\":\"%s\"", entity_category);
    }

    return payload;
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
    char* entity_category,
    bool isfloat)
{
    if (board_config.mqtt_homeassistant_discovery) {

        char discovery_topic[100];
        char payload[1000];
        char * p_out;

        /* See https://www.home-assistant.io/docs/mqtt/discovery/ and
         *     https://www.home-assistant.io/integrations/sensor.mqtt/ */
        if (group) {
            sprintf(discovery_topic, "homeassistant/sensor/%s/%s_%d/config", mqtt_main_topic, field, group);
        }
        else {
            sprintf(discovery_topic, "homeassistant/sensor/%s/%s/config", mqtt_main_topic, field);
        }

        p_out = mqtt_config_common_part(payload, field, name, icon, entity_category, group);

        if (strlen(unit)) {
            p_out += sprintf(p_out, ",\"unit_of_meas\":\"%s\"", unit);
        }

        if (strlen(device_class)) {
            p_out += sprintf(p_out, ",\"device_class\":\"%s\"", device_class);
        }

        if (strlen(state_class)) {
            p_out += sprintf(p_out, ",\"state_class\":\"%s\"", state_class);
        }

        if (group) {
            char name_mod[32];
            strcpy(name_mod, name);
            strlwr(name_mod);
            replacechar(name_mod, ' ', '_');
            p_out += sprintf(p_out, ",\"value_template\": \"{{ value_json.%s%s }}\"",
                             name_mod, (isfloat ? " | float" : ""));
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
        char * p_out;

        /* See https://www.home-assistant.io/docs/mqtt/discovery/ and
         *     https://www.home-assistant.io/integrations/number.mqtt/ */
        sprintf(discovery_topic, "homeassistant/number/%s/%s/config", mqtt_main_topic, field);

        p_out = mqtt_config_common_part(payload, field, name, icon, "config", 0);
        p_out += sprintf(p_out, ",\"command_topic\":\"~/%s/set\"", field);
        p_out += sprintf(p_out, ",\"min\":\"%f\"", min);
        p_out += sprintf(p_out, ",\"max\":\"%f\"", max);
        p_out += sprintf(p_out, ",\"step\":\"%f\"", step);

        if (strlen(mode)) {
            p_out += sprintf(p_out, ",\"mode\":\"%s\"", mode);
        }

        if (strlen(unit)) {
            p_out += sprintf(p_out, ",\"unit_of_meas\":\"%s\"", unit);
        }

        if (strlen(device_class)) {
            p_out += sprintf(p_out, ",\"device_class\":\"%s\"", device_class);
        }

        strcat(payload, "}");
        esp_mqtt_client_publish(client, discovery_topic, payload, 0, /*qos*/1, /*retain*/1);
    }

    if (handler) {
        mqtt_subscribe_set_function(client, field, handler);
    }
}

static void mqtt_cfg_select_range(
    esp_mqtt_client_handle_t client,
    char* field,
    char* name,
    char* icon,
    uint8_t min,
    uint8_t max,
    uint8_t step,
    mqtt_value_set_handler handler)
{
    if (board_config.mqtt_homeassistant_discovery) {
        char discovery_topic[100];
        char payload[1000];
        char * p_out;

        /* See https://www.home-assistant.io/docs/mqtt/discovery/ and
         *     https://www.home-assistant.io/integrations/select.mqtt/ */
        sprintf(discovery_topic, "homeassistant/select/%s/%s/config", mqtt_main_topic, field);

        p_out = mqtt_config_common_part(payload, field, name, icon, "config", 0);
        p_out += sprintf(p_out, ",\"command_topic\":\"~/%s/set\"", field);
        p_out += sprintf(p_out, ",\"options\":[");

        while (min <= max) {
            p_out += sprintf(p_out, "\"%uA\",", min);
            min += step;
        }
        // note: replaces last comma after the loop
        sprintf((p_out - 1), "]}");
        esp_mqtt_client_publish(client, discovery_topic, payload, 0, /*qos*/1, /*retain*/1);
    }

    if (handler) {
        mqtt_subscribe_set_function(client, field, handler);
    }
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
        char * p_out;

        /* See https://www.home-assistant.io/docs/mqtt/discovery/ and
         *     hhttps://www.home-assistant.io/integrations/select.mqtt/ */
        sprintf(discovery_topic, "homeassistant/select/%s/%s/config", mqtt_main_topic, field);

        p_out = mqtt_config_common_part(payload, field, name, icon, "config", 0);
        p_out += sprintf(p_out, ",\"command_topic\":\"~/%s/set\"", field);
        p_out += sprintf(p_out, ",\"options\":[%s]", options);

        strcat(payload, "}");
        esp_mqtt_client_publish(client, discovery_topic, payload, 0, /*qos*/1, /*retain*/1);
    }

    if (handler) {
        mqtt_subscribe_set_function(client, field, handler);
    }
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
        char * p_out;

        /* See https://www.home-assistant.io/docs/mqtt/discovery/ and
         *     https://www.home-assistant.io/integrations/switch.mqtt/ */
        sprintf(discovery_topic, "homeassistant/switch/%s/%s/config", mqtt_main_topic, field);

        p_out = mqtt_config_common_part(payload, field, name, icon, "config", 0);
        p_out += sprintf(p_out, ",\"command_topic\":\"~/%s/set\"", field);

        if (strlen(device_class)) {
            p_out += sprintf(p_out, ",\"device_class\": \"%s\"", device_class);
        }

        strcat(payload, "}");
        esp_mqtt_client_publish(client, discovery_topic, payload, 0, /*qos*/1, /*retain*/1);
    }

    if (handler) {
        mqtt_subscribe_set_function(client, field, handler);
    }
}

static void mqtt_cfg_button(
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
        char * p_out;

        /* See https://www.home-assistant.io/docs/mqtt/discovery/ and
         *     https://www.home-assistant.io/integrations/button.mqtt/ */
        sprintf(discovery_topic, "homeassistant/button/%s/%s/config", mqtt_main_topic, field);

        p_out = mqtt_config_common_part(payload, field, name, icon, "config", 0);
        p_out += sprintf(p_out, ",\"command_topic\":\"~/%s/set\"", field);

        if (strlen(device_class)) {
            p_out += sprintf(p_out, ",\"device_class\": \"%s\"", device_class);
        }

        strcat(payload, "}");
        esp_mqtt_client_publish(client, discovery_topic, payload, 0, /*qos*/1, /*retain*/1);
    }

    if (handler) {
        mqtt_subscribe_set_function(client, field, handler);
    }
}

// Maximum charging current / ama
static void mqtt_evse_set_max_charging_current(char* data) {
    evse_set_max_charging_current((uint8_t)(atoi(data)));
}

// Charging current / amp
static void mqtt_evse_set_charging_current(char* data) {
    evse_set_charging_current((uint16_t)(atoi(data) * 10));
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

// Aux power outlet / pol
static void mqtt_aux_set_power_outlet(char* data) {
    power_outlet_set_state(strcmp(data, "ON")==0 ? true : false);
}

// Disable/enable buttos / enb
static void mqtt_enable_disable_buttons(char* data) {
    bool enabled = strcmp(data, "ON")==0 ? true : false;

    if (board_config.button_evse_enable) {
        button_set_button_state(BUTTON_ID_EVSE_ENABLE, enabled);
    }
    if (board_config.button_aux1) {
        button_set_button_state(BUTTON_ID_AUX1, enabled);
    }
}

// Restart the device
static void mqtt_evse_reboot(char* data) {
    evse_set_available(false);
    esp_restart();
}

static void mqtt_subscribe_send_ha_discovery(esp_mqtt_client_handle_t client)
{
    // Clear subscribe handler buffer
    mqtt_value_set_handler_count = 0;

    uint8_t const tma_cnt = temp_sensor_get_count();

    // System Sensors:  Group| Field   | User Friendly Name           | Icon                          | Unit | Device Class        | State Class       | Entity category
    mqtt_cfg_sensor(client, 0, "rbt"   , "Uptime"                     , "mdi:clock-time-eight-outline", "s"  , ""                  , ""                , "diagnostic", false);
    mqtt_cfg_sensor(client, 0, "mac"   , "MAC Address"                , "mdi:network-outline"         , ""   , ""                  , ""                , "diagnostic", false);
    mqtt_cfg_sensor(client, 0, "ip"    , "IP Adress"                  , "mdi:network-outline"         , ""   , ""                  , ""                , "diagnostic", false);
    mqtt_cfg_sensor(client, 0, "fme"   , "Free Heap Memory"           , "mdi:memory"                  , "B"  , ""                  , "measurement"     , "diagnostic", false);
    mqtt_cfg_sensor(client, 0, "rssi"  , "Wi-Fi RSSI"                 , "mdi:wifi"                    , "dBm", "signal_strength"   , "measurement"     , "diagnostic", false);
    mqtt_cfg_sensor(client, 0, "tmc"   , "CPU Temperature"            , "mdi:thermometer"             , "°C" , "temperature"       , "measurement"     , "diagnostic", false);

    // EVSE Sensors:    Group| Field   | User Friendly Name           | Icon                          | Unit | Device Class        | State Class       | Entity category
    mqtt_cfg_sensor(client, 0, "cbl"   , "Cable maximum current"      , ""                            , "A"  , "current"           , "measurement"     , "", false);
    mqtt_cfg_sensor(client, 0, "ccs"   , "Current charger state"      , "mdi:auto-fix"                , ""   , ""                  , ""                , "", false);
    mqtt_cfg_sensor(client, 0, "cdi"   , "Charging duration"          , "mdi:timer-outline"           , "s"  , ""                  , "measurement"     , "", false);
    mqtt_cfg_sensor(client, 0, "err"   , "Error code"                 , "mdi:alert-circle-outline"    , ""   , ""                  , ""                , "", false);
    mqtt_cfg_sensor(client, 0, "eto"   , "Total energy"               , ""                            , "Wh" , "energy"            , "total_increasing", "", false);
    mqtt_cfg_sensor(client, 0, "lst"   , "Last session time"          , "mdi:counter"                 , "s"  , ""                  , "measurement"     , "", false);
    mqtt_cfg_sensor(client, 1, "nrg"   , "Voltage L1"                 , ""                            , "V"  , "voltage"           , "measurement"     , "", false);
    mqtt_cfg_sensor(client, 2, "nrg"   , "Voltage L2"                 , ""                            , "V"  , "voltage"           , "measurement"     , "", false);
    mqtt_cfg_sensor(client, 3, "nrg"   , "Voltage L3"                 , ""                            , "V"  , "voltage"           , "measurement"     , "", false);
    mqtt_cfg_sensor(client, 4, "nrg"   , "Current L1"                 , ""                            , "A"  , "current"           , "measurement"     , "", false);
    mqtt_cfg_sensor(client, 5, "nrg"   , "Current L2"                 , ""                            , "A"  , "current"           , "measurement"     , "", false);
    mqtt_cfg_sensor(client, 6, "nrg"   , "Current L3"                 , ""                            , "A"  , "current"           , "measurement"     , "", false);
    mqtt_cfg_sensor(client, 7, "nrg"   , "Current power"              , ""                            , "W"  , "power"             , "measurement"     , "", false);
    mqtt_cfg_sensor(client, 0, "rcd"   , "Residual current detection" , "mdi:current-dc"              , ""   , ""                  , ""                , "", false);
    mqtt_cfg_sensor(client, 0, "status", "Status"                     , "mdi:heart-pulse"             , ""   , ""                  , ""                , "", false);
    mqtt_cfg_sensor(client, 1, "tma"   , "Temperature sensor error"   , ""                            , ""   , ""                  , ""                , "", false);
    mqtt_cfg_sensor(client, 2, "tma"   , "Temperature sensor low"     , ""                            , "°C" , "temperature"       , "measurement"     , "", true);
    mqtt_cfg_sensor(client, 3, "tma"   , "Temperature sensor high"    , ""                            , "°C" , "temperature"       , "measurement"     , "", true);
    mqtt_cfg_sensor(client, 4, "tma"   , "Temperature sensor count"   , ""                            , ""   , ""                  , ""                , "", false);
    if (tma_cnt)
        mqtt_cfg_sensor(client, 5, "tma"   , "Temperature sensor 1"       , ""                            , "°C" , "temperature"       , "measurement"     , "", true);
    if (1 < tma_cnt)
        mqtt_cfg_sensor(client, 6, "tma"   , "Temperature sensor 2"       , ""                            , "°C" , "temperature"       , "measurement"     , "", true);
    if (2 < tma_cnt)
        mqtt_cfg_sensor(client, 7, "tma"   , "Temperature sensor 3"       , ""                            , "°C" , "temperature"       , "measurement"     , "", true);
    if (3 < tma_cnt)
        mqtt_cfg_sensor(client, 8, "tma"   , "Temperature sensor 4"       , ""                            , "°C" , "temperature"       , "measurement"     , "", true);
    if (4 < tma_cnt)
        mqtt_cfg_sensor(client, 9, "tma"   , "Temperature sensor 5"       , ""                            , "°C" , "temperature"       , "measurement"     , "", true);
    mqtt_cfg_sensor(client, 0, "lck"   , "Socket lock status"         , "mdi:lock-question"           , ""   , ""                  , ""                , "", false);

    // Numbers:             Field| User Friendly Name           | Icon               | Unit | Device Class | Min| Max| Step| Mode    | Value Set Handler
    mqtt_cfg_number(client, "amt", "Temperature threshold"      , ""                 , "°C ", "temperature", 40 , 80 , 1   , "box"   , mqtt_evse_set_temp_threshold);
    mqtt_cfg_number(client, "ate", "Consumption limit"          , ""                 , "kWh", "energy"     , 0  , 50 , 1   , "slider", mqtt_evse_set_consumption_limit);
    mqtt_cfg_number(client, "att", "Charging time limit"        , "mdi:timer-outline", "min", ""           , 0  , 300, 5   , "slider", mqtt_evse_set_charging_time_limit);
    mqtt_cfg_number(client, "upl", "Under power limit"          , ""                 , "kWh", "energy"     , 0  , 10 , 0.01, "slider", mqtt_evse_set_under_power_limit);
    mqtt_cfg_number(client, "date","Default consumption limit"  , ""                 , "kWh", "energy"     , 0  , 50 , 1   , "slider", mqtt_evse_set_default_consumption_limit);
    mqtt_cfg_number(client, "datt","Default charging time limit", "mdi:timer-outline", "min", ""           , 0  , 300, 5   , "slider", mqtt_evse_set_default_charging_time_limit);
    mqtt_cfg_number(client, "dupl","Default under power limit"  , ""                 , "kWh", "energy"     , 0  , 10 , 0.01, "slider", mqtt_evse_set_default_under_power_limit);
    mqtt_cfg_number(client, "acv", "AC Voltage"                 , ""                 , "V"  , "voltage"    , 100, 300, 1   , "box"   , mqtt_evse_set_ac_voltage);

    // Selects range:             Field| User Friendly Name           | Icon               | Min| Max| Step| Value Set Handler
    mqtt_cfg_select_range(client, "ama", "Maximum charging current"   , "mdi:current-ac"   , 6  , 32 , 1   , mqtt_evse_set_max_charging_current);
    mqtt_cfg_select_range(client, "amp", "Charging current"           , "mdi:current-ac"   , 6  , 32 , 1   , mqtt_evse_set_charging_current);

    // Select:              Field | User Friendly Name | Icon                | Options
    mqtt_cfg_select(client, "emm" , "Energy meter mode", "mdi:meter-electric", "\"Dummy\",\"Current sensing\",\"Current and voltage sensing\"", mqtt_evse_set_energy_meter_mode);

    // Switch:              Field| User Friendly Name           | Icon                 | Device Class| Value Set Handler
    mqtt_cfg_switch(client, "scs", "Set charger state"          , "mdi:auto-fix"       , "switch"    , mqtt_evse_set_charger_state);
    mqtt_cfg_switch(client, "acs", "Card authorization required", ""                   , "switch"    , mqtt_evse_set_require_auth);
    mqtt_cfg_switch(client, "sol", "Socket outlet"              , "mdi:ev-plug-type2"  , "switch"    , mqtt_evse_set_socket_outlet);
    mqtt_cfg_switch(client, "pol", "Aux power outlet"           , "mdi:power-socket-eu", "switch"    , mqtt_aux_set_power_outlet);
    mqtt_cfg_switch(client, "enb", "Enable/Disable buttons"     , ""                   , "switch"    , mqtt_enable_disable_buttons);

    // Button:                  Field| User Friendly Name  | Icon| Device Class| Value Set Handler
    mqtt_cfg_button(client, "restart", "Restart the device", ""  , "restart"   , mqtt_evse_reboot);

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

static void mqtt_publish_system_data(esp_mqtt_client_handle_t client, bool force) {

  char topic[64];
  char payload[512];

  // Uptime
  sprintf(topic, "%s/rbt", mqtt_main_topic);
  sprintf(payload, "%lld", esp_timer_get_time() / 1000000);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);

  // Free Mem
  static size_t prev_fme = 0;
  multi_heap_info_t heap_info;
  heap_caps_get_info(&heap_info, MALLOC_CAP_INTERNAL);
  size_t fme = heap_info.total_free_bytes;
  if (force || (fme != prev_fme)) {
      sprintf(topic, "%s/fme", mqtt_main_topic);
      sprintf(payload, "%d", fme);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_fme = fme;
  }

  // Wifi RSSI
  static int8_t prev_rssi = 0;
  int8_t rssi = wifi_get_rssi();
  if (force || (rssi != prev_rssi)) {
      sprintf(topic, "%s/rssi", mqtt_main_topic);
      sprintf(payload, "%d", rssi);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_rssi = rssi;
  }

  // CPU Temperature
  static float prev_tmc = 0.0;
  float tmc = temp_sensor_read_cpu_temperature();
  // this might be bit stupid becuase comparing floats, but...
  if (force || (tmc != prev_tmc)) {
      sprintf(topic, "%s/tmc", mqtt_main_topic);
      sprintf(payload, "%.1f", tmc);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_tmc = tmc;
  }
}

static void mqtt_publish_evse_sensor_data(esp_mqtt_client_handle_t client, bool force) {

  char topic[64];
  char payload[512];
  char tmp[64];

  // Cable maximum current
  static uint8_t prev_cbl = 0;
  uint8_t cbl = proximity_get_max_current();
  if (force || (cbl != prev_cbl)) {
      sprintf(topic, "%s/cbl", mqtt_main_topic);
      sprintf(payload, "%d", cbl);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_cbl = cbl;
  }

  // Current charger state
  static bool prev_ccs = 0;
  bool ccs = evse_is_enabled();
  if (force || (ccs != prev_ccs)) {
      sprintf(topic, "%s/ccs", mqtt_main_topic);
      sprintf(payload, "%s", (ccs? "Charging enabled":"Charging Disabled") );
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_ccs = ccs;
  }

  // Charging duration
  static uint32_t prev_cdi = 0;
  uint32_t cdi = energy_meter_get_charging_time();
  if (force || (cdi != prev_cdi)) {
      sprintf(topic, "%s/cdi", mqtt_main_topic);
      sprintf(payload, "%ld", cdi);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_cdi = cdi;
  }

  // Error code
  static uint32_t prev_err = 0;
  uint32_t err = evse_get_error();
  if (force || (err != prev_err)) {
      sprintf(topic, "%s/err", mqtt_main_topic);
      sprintf(payload, "%s", evse_error_to_str(err));
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
      prev_err = err;
  }

  // Total energy
  static uint32_t prev_eto = 0;
  uint32_t eto = energy_meter_get_consumption();
  if (force || (eto != prev_eto)) {
      sprintf(topic, "%s/eto", mqtt_main_topic);
      sprintf(payload, "%ld", eto);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_eto = eto;
  }

  // Last session time
  static uint32_t prev_lst = 0;
  uint32_t lst = energy_meter_get_session_time();
  if (force || (lst != prev_lst)) {
      sprintf(topic, "%s/lst", mqtt_main_topic);
      sprintf(payload, "%ld", lst);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_lst = lst;
  }

  // Measuremnt statistics
  static float prev_l1v = 0.0;
  static float prev_l2v = 0.0;
  static float prev_l3v = 0.0;
  static float prev_l1c = 0.0;
  static float prev_l2c = 0.0;
  static float prev_l3c = 0.0;
  static uint16_t prev_pwr = 0;
  float l1v = energy_meter_get_l1_voltage();
  float l2v = energy_meter_get_l2_voltage();
  float l3v = energy_meter_get_l3_voltage();
  float l1c = energy_meter_get_l1_current();
  float l2c = energy_meter_get_l2_current();
  float l3c = energy_meter_get_l3_current();
  uint16_t pwr = energy_meter_get_power();
  if (force ||
      ((l1v != prev_l1v) || (l2v != prev_l2v) || (l3v != prev_l3v) ||
       (l1c != prev_l1c) || (l2c != prev_l2c) || (l3c != prev_l3c) ||
       (pwr != prev_pwr))) {
      sprintf(topic, "%s/nrg", mqtt_main_topic);
      sprintf(payload, "{");
      // Voltage L1 / L2 / L3
      sprintf(tmp, "\"voltage_l1\":%f,", l1v);
      strcat(payload, tmp);
      sprintf(tmp, "\"voltage_l2\":%f,", l2v);
      strcat(payload, tmp);
      sprintf(tmp, "\"voltage_l3\":%f,", l3v);
      strcat(payload, tmp);
      // Current L1 / L2 /L3
      sprintf(tmp, "\"current_l1\":%f,", l1c);
      strcat(payload, tmp);
      sprintf(tmp, "\"current_l2\":%f,", l2c);
      strcat(payload, tmp);
      sprintf(tmp, "\"current_l3\":%f,", l3c);
      strcat(payload, tmp);
      // Current power
      sprintf(tmp, "\"current_power\":%d}", pwr);
      strcat(payload, tmp);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_l1v = l1v;
      prev_l2v = l2v;
      prev_l3v = l3v;
      prev_l1c = l1c;
      prev_l2c = l2c;
      prev_l3c = l3c;
      prev_pwr = pwr;
  }

  // Residual current detection
  static bool prev_rcd = 0;
  bool rcd = evse_is_rcm();
  if (force || (rcd != prev_rcd)) {
      sprintf(topic, "%s/rcd", mqtt_main_topic);
      sprintf(payload, "%s", (rcd?"Detected":"Not detected"));
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_rcd = rcd;
  }

  // Status
  static evse_state_t prev_status = EVSE_STATE_A;
  evse_state_t status = evse_get_state();
  if (force || (status != prev_status)) {
      sprintf(topic, "%s/status", mqtt_main_topic);
      sprintf(payload, "%s", evse_state_to_str_long(status));
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
      prev_status = status;
  }

  // Temperature sensors
  static bool prev_tma_err = 0;
  static int16_t prev_tma_low = 0;
  static int16_t prev_tma_high = 0;
  static uint8_t prev_tma_cnt = 0;
  static int16_t prev_tma_temps[2] = { 0 };
  bool tma_err = temp_sensor_is_error();
  int16_t const tma_low = temp_sensor_get_low();
  int16_t const tma_high = temp_sensor_get_high();
  uint8_t const tma_cnt = temp_sensor_get_count();
  int16_t tma_temps[tma_cnt];
  memset(&tma_temps, 0, sizeof(tma_temps));
  temp_sensor_get_temperatures(tma_temps);
  bool temp_changed = false;
  for (uint8_t i = 0 ; i < tma_cnt; i++) {
      if (tma_temps[i] != prev_tma_temps[i]) {
          temp_changed = true;
          break;
      }
  }
  if (force ||
      ((tma_err != prev_tma_err) ||
       (tma_low != prev_tma_low) || (tma_high != prev_tma_high) ||
       (tma_cnt != prev_tma_cnt) || temp_changed)) {
      sprintf(topic, "%s/tma", mqtt_main_topic);
      sprintf(payload, "{");
      sprintf(tmp, "\"temperature_sensor_error\":%d,", tma_err);
      strcat(payload, tmp);
      sprintf(tmp, "\"temperature_sensor_low\":\"%.1f\",", tma_low / 100.0);
      strcat(payload, tmp);
      sprintf(tmp, "\"temperature_sensor_high\":\"%.1f\",", tma_high / 100.0);
      strcat(payload, tmp);
      sprintf(tmp, "\"temperature_sensor_count\":%d", tma_cnt);
      strcat(payload, tmp);
      for (uint8_t i = 0 ; i < tma_cnt; i++) {
        sprintf(tmp, ",\"temperature_sensor_%d\":\"%.1f\"", i + 1, tma_temps[i] / 100.0);
        strcat(payload, tmp);
        prev_tma_temps[i] = tma_temps[i];
      };
      strcat(payload, "}");
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_tma_err = tma_err;
      prev_tma_low = tma_low;
      prev_tma_high = tma_high;
      prev_tma_cnt = tma_cnt;
  }

  // Socket lock status
  static socket_lock_status_t prev_lck = 0;
  socket_lock_status_t lck = socket_lock_get_status();
  if (force || (lck != prev_lck)) {
      sprintf(topic, "%s/lck", mqtt_main_topic);
      sprintf(payload, "%s", socket_lock_status_to_str(lck));
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_lck = lck;
  }

}

static void mqtt_publish_evse_number_data(esp_mqtt_client_handle_t client, bool force) {

  char topic[64];
  char payload[512];

  // Maximum current limit
  static uint8_t prev_ama = 0;
  uint8_t ama = evse_get_max_charging_current();
  if (force || (ama != prev_ama)) {
      sprintf(topic, "%s/ama", mqtt_main_topic);
      sprintf(payload, "%uA", ama);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_ama = ama;
  }

  // Requested current
  static uint16_t prev_amp = 0;
  uint16_t amp = evse_get_charging_current() / 10;
  if (force || (amp != prev_amp)) {
      sprintf(topic, "%s/amp", mqtt_main_topic);
      sprintf(payload, "%uA", amp);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_amp = amp;
  }

  // Current temperature limit
  static uint8_t prev_amt = 0;
  uint8_t amt = evse_get_temp_threshold();
  if (force || (amt != prev_amt)) {
      sprintf(topic, "%s/amt", mqtt_main_topic);
      sprintf(payload, "%d", amt);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_amt = amt;
  }

  // Automatic stop energy
  static uint32_t prev_ate = 0;
  uint32_t ate = evse_get_consumption_limit();
  if (force || (ate != prev_ate)) {
      sprintf(topic, "%s/ate", mqtt_main_topic);
      sprintf(payload, "%f", ate/1000.0);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_ate = ate;
  }

  // Automatic stop time
  static uint32_t prev_att = 0;
  uint32_t att = evse_get_charging_time_limit();
  if (force || (att != prev_att)) {
      sprintf(topic, "%s/att", mqtt_main_topic);
      sprintf(payload, "%f", att/60.0);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_att = att;
  }

  // Under power limit
  static uint16_t prev_upl = 0;
  uint16_t upl = evse_get_under_power_limit();
  if (force || (upl != prev_upl)) {
      sprintf(topic, "%s/upl", mqtt_main_topic);
      sprintf(payload, "%f", upl/1000.0);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_upl = upl;
  }

  // Default automatic stop energy
  static uint32_t prev_date = 0;
  uint32_t date = evse_get_default_consumption_limit();
  if (force || (date != prev_date)) {
      sprintf(topic, "%s/date", mqtt_main_topic);
      sprintf(payload, "%f", date/1000.0);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_date = date;
  }

  // Default automatic stop time
  static uint32_t prev_datt = 0;
  uint32_t datt = evse_get_default_charging_time_limit();
  if (force || (datt != prev_datt)) {
      sprintf(topic, "%s/datt", mqtt_main_topic);
      sprintf(payload, "%f", datt/60.0);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_datt = datt;
  }

  // Default under power limit
  static uint16_t prev_dupl = 0;
  uint16_t dupl = evse_get_default_under_power_limit();
  if (force || (dupl != prev_dupl)) {
      sprintf(topic, "%s/dupl", mqtt_main_topic);
      sprintf(payload, "%f", dupl/1000.0);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_dupl = dupl;
  }

  // AC Voltage
  static uint16_t prev_acv = 0;
  uint16_t acv = energy_meter_get_ac_voltage();
  if (force || (acv != prev_acv)) {
      sprintf(topic, "%s/acv", mqtt_main_topic);
      sprintf(payload, "%d", acv);
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_acv = acv;
  }
}

static void mqtt_publish_evse_select_data(esp_mqtt_client_handle_t client, bool force) {

  char topic[64];
  char payload[512];

  // Energy meter mode
  static energy_meter_mode_t prev_emm = 0;
  energy_meter_mode_t emm = energy_meter_get_mode();
  if (force || (emm != prev_emm)) {
      sprintf(topic, "%s/emm", mqtt_main_topic);
      sprintf(payload, "%s", energy_meter_mode_to_str_mqtt(emm));
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_emm = emm;
  }
}

static void mqtt_publish_evse_switch_data(esp_mqtt_client_handle_t client, bool force) {

  char topic[64];
  char payload[512];

  // Set charger state
  static bool prev_scs = 0;
  bool scs = evse_is_enabled();
  if (force || (scs != prev_scs)) {
      sprintf(topic, "%s/scs", mqtt_main_topic);
      sprintf(payload, "%s", evse_is_enabled()? "ON": "OFF");
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/0);
      prev_scs = scs;
  }

  // Card authorization required
  static bool prev_acs = 0;
  bool acs = evse_is_require_auth();
  if (force || (acs != prev_acs)) {
      sprintf(topic, "%s/acs", mqtt_main_topic);
      sprintf(payload, "%s", acs? "ON": "OFF");
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_acs = acs;
  }

  // Socket outlet
  static bool prev_sol = 0;
  bool sol = evse_get_socket_outlet();
  if (force || (sol != prev_sol)) {
      sprintf(topic, "%s/sol", mqtt_main_topic);
      sprintf(payload, "%s", sol? "ON": "OFF");
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_sol = sol;
  }

  // Power outlet
  static bool prev_pol = 0;
  bool pol = power_outlet_get_state();
  if (force || (pol != prev_pol)) {
      sprintf(topic, "%s/pol", mqtt_main_topic);
      sprintf(payload, "%s", pol? "ON": "OFF");
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_pol = pol;
  }

  // Button state
  static bool prev_enb = 0;
  bool const enb = (button_get_button_state(BUTTON_ID_EVSE_ENABLE)
                    || button_get_button_state(BUTTON_ID_AUX1));
  if (force || (enb != prev_enb)) {
      sprintf(topic, "%s/enb", mqtt_main_topic);
      sprintf(payload, "%s", enb ? "ON": "OFF");
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);

  // Disable/enable buttos / enb
  static bool prev_enb = 0;
  bool enb = false;
  if (board_config.button_evse_enable) {
      enb = button_get_button_state(BUTTON_ID_EVSE_ENABLE);
  }
  if (board_config.button_aux1) {
      enb |= button_get_button_state(BUTTON_ID_AUX1);
  }
  if (force || (enb != prev_enb)) {
      sprintf(topic, "%s/enb", mqtt_main_topic);
      sprintf(payload, "%s", enb? "ON": "OFF");
      esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/0, /*retain*/0);
      prev_enb = enb;
  }
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
        mqtt_publish_system_data(client, true);
        mqtt_publish_evse_sensor_data(client, true);
        mqtt_publish_evse_number_data(client, true);
        mqtt_publish_evse_select_data(client, true);
        mqtt_publish_evse_switch_data(client, true);
        mqtt_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT Disconnected");
        mqtt_connected = false;
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
                    mqtt_publish_evse_switch_data(client, true);
                }
                break;
            }
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        break;
    case MQTT_EVENT_PUBLISHED:
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGD(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGD(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    default:
        ESP_LOGD(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_task_func(void* param)
{
    mqtt_connected = false;

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
        return;
    }
    if (esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "client register failed!");
        return;
    }
    if (esp_mqtt_client_start(client) != ESP_OK) {
        ESP_LOGE(TAG, "client start failed!");
        return;
    }
    ESP_LOGI(TAG, "MQTT service running");

    uint32_t static_data_publish_counter = 0;
    bool force = false;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!mqtt_connected) {
            // Wait until MQTT is connected correctly
            static_data_publish_counter = 0;
            continue;
        }

        static_data_publish_counter++;

        int64_t start = esp_timer_get_time();
        if ((static_data_publish_counter % FORCE_UPDATE_SECONDS) == 0) {
            force = true;
        }
        if ((static_data_publish_counter % 10) == 0) {
            mqtt_publish_system_data(client, force);
            mqtt_publish_evse_sensor_data(client, force);
            mqtt_publish_evse_number_data(client, force);
            mqtt_publish_evse_select_data(client, force);
            mqtt_publish_evse_switch_data(client, force);
            ESP_LOGD(TAG, "update time=%lld", (esp_timer_get_time() - start));
            force = false;
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
        ESP_LOGD(TAG, "Creating MQTT task");
        xTaskCreate(mqtt_task_func, "mqtt_task", 5*1024, NULL, 11, &mqtt_task);
    }
}
