#include <string.h>
#include <math.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "nvs.h"
#include "mqtt_client.h"

#include "wifi.h"
#include "mqtt.h"
#include "evse.h"
#include "board_config.h"
#include "energy_meter.h"
#include "socket_lock.h"
#include "temp_sensor.h"
#include "proximity.h"

#define NVS_NAMESPACE          "mqtt"
#define NVS_HA_DISCOVERY_DONE  "ha_discovery"

#define LWT_TOPIC        "state"
#define LWT_CONNECTED    "online"
#define LWT_DISCONNECTED "offline"

#define ARRAY_SIZE(_x_) (sizeof(_x_)/sizeof((_x_)[0]))

static const char* TAG = "mqtt";

static nvs_handle nvs;

static TaskHandle_t mqtt_task;

static char lwt_topic[32];

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

#if 0
static bool read_holding_register(uint16_t addr, uint16_t* value)
{
    ESP_LOGD(TAG, "HR read %d", addr);
    switch (addr) {
      
    // sensor: modelStatus
        const char* state_str = evse_state_to_str(evse_get_state());
        *value = state_str[0] << 8 | state_str[1];
        
    // sensor: err
        evse_get_error();

    // sensor: ccs (own invention, current charger state)
        evse_is_enabled();

    // select: frc
        evse_set_enabled(bool enabled);

    // sensor: acs
    [   evse_is_require_auth();
        evse_is_pending_auth();
    ]

    // number: amp
        evse_get_charging_current();
        evse_set_charging_current();
    
    // Number: ate
        evse_get_consumption_limit();
        evse_set_consumption_limit();
   
    // number: att
        evse_get_charging_time_limit();
        evse_set_charging_time_limit();
    
    // sensor: lcc
        energy_meter_get_session_time();

    // sensor: cdi
        energy_meter_get_charging_time();

    // sensor: eto
        energy_meter_get_consumption();
        
    // status: nrg
    [ 
        /*Voltage L1 */ energy_meter_get_l1_voltage(),
        /*Voltage L2 */ energy_meter_get_l2_voltage(),
        /*Voltage L3 */ energy_meter_get_l3_voltage(),
        /*Voltage N  */ 0.0,
        /*Current L1 */ energy_meter_get_l1_current(),
        /*Current L2 */ energy_meter_get_l2_current(),
        /*Current L3 */ energy_meter_get_l3_current(),
        /*Power L1   */
        /*Power L2   */
        /*Power L3   */
        /*Power N    */
        /*Current power   */ energy_meter_get_power(),
        /*Power factor L1 */
        /*Power factor L2 */
        /*Power factor L3 */
        /*Power factor N  */


    // sensor: lck
        (uint8_t)evse_get_socket_outlet();
        
    // sensor: rcd
        evse_is_rcm();
    
    
    // sensor: cbl (max cable current)
        proximity_get_max_current();
    
    // number: amt
        evse_get_temp_threshold();
        evse_set_temp_threshold();

    // number: ama
        evse_get_max_charging_current();
        evse_set_max_charging_current();
 
    // number: ate
        evse_get_default_consumption_limit();
        evse_set_default_consumption_limit();

    // number: att
        evse_get_default_charging_time_limit();
        evse_set_default_charging_time_limit();

    // sensor: lck
        socket_lock_is_detection_high();
        
    // select: emm (Own invention, Energy Meter Mode)
       energy_meter_get_mode();
       energy_meter_set_mode();
    
    // sensor: rbt
      get_uptime();

    // sensor: tma
        temp_sensor_is_error();
        temp_sensor_get_low();
        temp_sensor_get_high();
        temp_sensor_get_count();
        temp_sensor_get_temperatures(int16_t* curr_temps);

}
#endif
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
        //"\"force_update\": \"true\","                       // 13
        "\"device\": "                                      // 12
          "{"                                               // 13
              "\"identifiers\": [\"%s\"],"                  // 14
              "\"name\": \"%s\","                           // 15
              "\"model\": \"ESP32 EVSE\","                  // 16
              "\"manufacturer\": \"OULWare\","              // 17
              "\"sw_version\": \"%s\","                     // 18
              "\"configuration_url\": \"http://%s\""        // 19
          "}",                                              // 20
        board_config.mqtt_main_topic,         // 1
        board_config.mqtt_main_topic, field,  // 2
        name,                                 // 3
        field,                                // 4
        LWT_TOPIC,                            // 7
        LWT_CONNECTED,                        // 8
        LWT_DISCONNECTED,                     // 9
        board_config.mqtt_main_topic,         // 14
        board_config.device_name,             // 15
        app_desc->version,                    // 18
        tmp);                                 // 19

    if (unique_nbr) {
        sprintf(tmp, ",\"unique_id\": \"%s-%s-%d\"", board_config.mqtt_main_topic, field, unique_nbr);
        strcat(payload, tmp);
              
    }
    else {
        sprintf(tmp ,",\"unique_id\": \"%s-%s\"", board_config.mqtt_main_topic, field );
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

static void mqtt_confg_component(
    esp_mqtt_client_handle_t client,
    char* component,
    char* field,
    char* name,
    char* icon,
    char* unit,
    char* device_class,
    char* state_class,
    char* entity_category)
{
    char discovery_topic[64];
    char payload[1000];
    char tmp[100];
    bool subscribe_topic = false;

    /* See https://www.home-assistant.io/docs/mqtt/discovery/ */
    sprintf(discovery_topic, "homeassistant/%s/%s/%s/config", component, board_config.mqtt_main_topic, field);

    mqtt_config_common_part(payload, field, name, icon, entity_category, 0);

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

    if ((strcmp(field, "switch") == 0) ||
        (strcmp(field, "select") == 0) ||
        (strcmp(field, "number") == 0)) {
        sprintf(tmp, ",\"command_topic\": \"~/%s/set\"", field);
        strcat(payload, tmp);
        subscribe_topic = true;
    }

    strcat(payload, "}");

    if (board_config.mqtt_homeassistant_discovery) {
      // One could say that it is bit unoptimal to build everything, but not use it...
      // so there is room for optimization, but expecting this flag always be true, so this
      // doesn't really matter ;)
      esp_mqtt_client_publish(client, discovery_topic, payload, 0, /*qos*/1, /*retain*/1);
    }

    if (subscribe_topic) {
        sprintf(tmp, "%s/%s/set", board_config.mqtt_main_topic, field);
        esp_mqtt_client_subscribe_single(client, tmp, /*qos*/0);
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
    char discovery_topic[64];
    char payload[1000];
    char tmp[100];

    /* See https://www.home-assistant.io/docs/mqtt/discovery/ */
    if (group) {
        sprintf(discovery_topic, "homeassistant/sensor/%s/%s_%d/config", board_config.mqtt_main_topic, field, group);
    }
    else {
        sprintf(discovery_topic, "homeassistant/sensor/%s/%s/config", board_config.mqtt_main_topic, field);
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

    if (board_config.mqtt_homeassistant_discovery) {
      // One could say that it is bit unoptimal to build everything, but not use it...
      // so there is room for optimization, but expecting this flag always be true, so this
      // doesn't really matter ;)
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
    char* mode)
{
    char discovery_topic[64];
    char payload[1000];
    char tmp[100];

    /* See https://www.home-assistant.io/docs/mqtt/discovery/ */
    sprintf(discovery_topic, "homeassistant/number/%s/%s/config", board_config.mqtt_main_topic, field);

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

    if (board_config.mqtt_homeassistant_discovery) {
      // One could say that it is bit unoptimal to build everything, but not use it...
      // so there is room for optimization, but expecting this flag always be true, so this
      // doesn't really matter ;)
      esp_mqtt_client_publish(client, discovery_topic, payload, 0, /*qos*/1, /*retain*/1);
    }

    sprintf(tmp, "%s/%s/set", board_config.mqtt_main_topic, field);
    esp_mqtt_client_subscribe_single(client, tmp, /*qos*/0);
}

static void mqtt_cfg_select(
    esp_mqtt_client_handle_t client,
    char* field,
    char* name,
    char* icon,
    char* options)
{
    char discovery_topic[64];
    char payload[1000];
    char tmp[100];

    /* See https://www.home-assistant.io/docs/mqtt/discovery/ */
    sprintf(discovery_topic, "homeassistant/select/%s/%s/config", board_config.mqtt_main_topic, field);

    mqtt_config_common_part(payload, field, name, icon, "config", 0);

    sprintf(tmp, ",\"command_topic\": \"~/%s/set\"", field);
    strcat(payload, tmp);

    sprintf(tmp, ",\"options\": [ %s ]", options);
    strcat(payload, tmp);

    strcat(payload, "}");

    if (board_config.mqtt_homeassistant_discovery) {
      // One could say that it is bit unoptimal to build everything, but not use it...
      // so there is room for optimization, but expecting this flag always be true, so this
      // doesn't really matter ;)
      esp_mqtt_client_publish(client, discovery_topic, payload, 0, /*qos*/1, /*retain*/1);
    }

    sprintf(tmp, "%s/%s/set", board_config.mqtt_main_topic, field);
    esp_mqtt_client_subscribe_single(client, tmp, /*qos*/0);
}

static void mqtt_cfg_switch(
    esp_mqtt_client_handle_t client,
    char* field,
    char* name,
    char* icon,
    char* device_class,
    char* state_on,
    char* state_off)
{
    char discovery_topic[64];
    char payload[1000];
    char tmp[100];

    /* See https://www.home-assistant.io/docs/mqtt/discovery/ */
    sprintf(discovery_topic, "homeassistant/switch/%s/%s/config", board_config.mqtt_main_topic, field);

    mqtt_config_common_part(payload, field, name, icon, "", 0);

    sprintf(tmp, ",\"command_topic\": \"~/%s/set\"", field);
    strcat(payload, tmp);

    if (strlen(device_class)) {
        sprintf(tmp, ",\"device_class\": \"%s\"", device_class);
        strcat(payload, tmp);
    }

    if (strlen(state_on)) {
        sprintf(tmp, ",\"state_on\": \"%s\"", state_on);
        strcat(payload, tmp);
    }

    if (strlen(state_off)) {
        sprintf(tmp, ",\"state_off\": \"%s\"", state_off);
        strcat(payload, tmp);
    }

    strcat(payload, "}");

    if (board_config.mqtt_homeassistant_discovery) {
      // One could say that it is bit unoptimal to build everything, but not use it...
      // so there is room for optimization, but expecting this flag always be true, so this
      // doesn't really matter ;)
      esp_mqtt_client_publish(client, discovery_topic, payload, 0, /*qos*/1, /*retain*/1);
    }

    sprintf(tmp, "%s/%s/set", board_config.mqtt_main_topic, field);
    esp_mqtt_client_subscribe_single(client, tmp, /*qos*/0);
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
    mqtt_cfg_sensor(client, 1, "tma"   , "Temperature sensor 1"       , ""                            , "°C" , "temperature"       , "measurement"     , "");
    mqtt_cfg_sensor(client, 2, "tma"   , "Temperature sensor 2"       , ""                            , "°C" , "temperature"       , "measurement"     , "");

    // Numbers:             Field| User Friendly Name         | Icon               | Unit | Device Class | Min| Max| Step| Mode
    mqtt_cfg_number(client, "ama", "Maximum charging current" , ""                 , "A"  , "current"    , 6  , 32 , 1   , "box"   );
    mqtt_cfg_number(client, "amp", "Charging current"         , ""                 , "A"  , "current"    , 6  , 32 , 0.1 , "slider");
    mqtt_cfg_number(client, "amt", "Temperature threshold"    , ""                 , "°C ", "temperature", 40 , 80 , 1   , "box"   );
    mqtt_cfg_number(client, "ate", "Consumption limit"        , ""                 , "kWh", "energy"     , 0  , 50 , 1   , "slider");
    mqtt_cfg_number(client, "att", "Charging time limit"      , "mdi:timer-outline", "min", ""           , 0  , 300, 5   , "slider");
    mqtt_cfg_number(client, "upl", "Under power limit"        , ""                 , "kWh", "energy"     , 0  , 10 , 0.01, "slider");
    mqtt_cfg_number(client, "acv", "AC Voltage"               , ""                 , "V"  , "voltage"    , 100, 300, 1   , "box"   );

    // Select:              Field | User Friendly Name | Icon                | Options
    mqtt_cfg_select(client, "emm" , "Energy meter mode", "mdi:meter-electric", "\"Dummy single phase\",\"Dummy three phase\",\"Current sensing\",\"Current and voltage sensing\"");

    // Switch:              Field| User Friendly Name           | Icon          | Device Class| State On | State Off
    mqtt_cfg_switch(client, "scs", "Set charger state"          , "mdi:auto-fix", ""          , "Charge" , "Don't Charge");
    mqtt_cfg_switch(client, "acs", "Card authorization required", ""            , "outlet"    , "Enabled", "Disbaled"    );
    mqtt_cfg_switch(client, "lck", "Socket lock"                , ""            , "switch"    , "Enabled", "Disbaled"    );

}

static void mqtt_publish_static_data(esp_mqtt_client_handle_t client) {


  char topic[64];
  char payload[64];

  // Availability
  esp_mqtt_client_publish(client, lwt_topic, LWT_CONNECTED, 0, /*qos*/1, /*retain*/1);

  // MAC
  sprintf(topic, "%s/mac", board_config.mqtt_main_topic);
  wifi_get_mac(payload);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // IP
  sprintf(topic, "%s/ip", board_config.mqtt_main_topic);
  wifi_get_ip(payload);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
}

static void mqtt_publish_system_data(esp_mqtt_client_handle_t client) {


  char topic[64];
  char payload[64];

  // Uptime
  sprintf(topic, "%s/rbt", board_config.mqtt_main_topic);
  sprintf(payload, "%lld", esp_timer_get_time() / 1000000);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Free Mem
  multi_heap_info_t heap_info;
  heap_caps_get_info(&heap_info, MALLOC_CAP_INTERNAL);
  sprintf(topic, "%s/fme", board_config.mqtt_main_topic);
  sprintf(payload, "%d", heap_info.total_free_bytes);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Wifi RSSI
  sprintf(topic, "%s/rssi", board_config.mqtt_main_topic);
  sprintf(payload, "%d", wifi_get_rssi());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // CPU Temperature
  sprintf(topic, "%s/tmc", board_config.mqtt_main_topic);
  sprintf(payload, "%.1f", temp_sensor_read_cpu_temperature());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
}

static void mqtt_publish_evse_sensor_data(esp_mqtt_client_handle_t client) {

  char topic[64];
  char payload[512];
  char tmp[64];

ESP_LOGI(TAG, "<START");
  // Cable maximum current
  sprintf(topic, "%s/cbl", board_config.mqtt_main_topic);
  sprintf(payload, "%d", proximity_get_max_current());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
ESP_LOGI(TAG, "1");
  // Current charger state
  sprintf(topic, "%s/ccs", board_config.mqtt_main_topic);
  sprintf(payload, "%s", (evse_is_enabled()? "Charging enabled":"Charging Disabled") );
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
ESP_LOGI(TAG, "2");
  // Charging duration
  sprintf(topic, "%s/cdi", board_config.mqtt_main_topic);
  sprintf(payload, "%ld", energy_meter_get_charging_time());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
ESP_LOGI(TAG, "3");
  // Error code
  sprintf(topic, "%s/err", board_config.mqtt_main_topic);
  sprintf(payload, "%ld", evse_get_error());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
ESP_LOGI(TAG, "4");
  // Total energy
  sprintf(topic, "%s/eto", board_config.mqtt_main_topic);
  sprintf(payload, "%ld", energy_meter_get_consumption());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
ESP_LOGI(TAG, "5");
  // Last session time
  sprintf(topic, "%s/lst", board_config.mqtt_main_topic);
  sprintf(payload, "%ld", energy_meter_get_session_time());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
ESP_LOGI(TAG, "6");
  // Measuremnt statistics
  sprintf(topic, "%s/nrg", board_config.mqtt_main_topic);
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
  sprintf(topic, "%s/rcd", board_config.mqtt_main_topic);
  sprintf(payload, "%s", (evse_is_rcm()?"Detected":"Not detected"));
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1); ///=>> Binary_Sensor!!!!

  // Status
  sprintf(topic, "%s/status", board_config.mqtt_main_topic);
  sprintf(payload, "%s", evse_state_to_str(evse_get_state()));
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Temperature sensors
  sprintf(topic, "%s/tma", board_config.mqtt_main_topic);
  sprintf(payload, "{");
  int16_t curr_temps[2] = { 0 };
  uint8_t i;
  temp_sensor_get_temperatures(curr_temps);
  for (i=1 ; i < ARRAY_SIZE(curr_temps); i++)
  {
    sprintf(tmp, "\"temperature_sensor_%d\":%d,", i, curr_temps[i-1]);
    strcat(payload, tmp);
  }
  sprintf(tmp, "\"temperature_sensor_%d\":%d}", i, curr_temps[i-1]);
  strcat(payload, tmp);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, ">END");
}

static void mqtt_publish_evse_number_data(esp_mqtt_client_handle_t client) {

  char topic[64];
  char payload[512];

  // Maximum current limit
  sprintf(topic, "%s/ama", board_config.mqtt_main_topic);
  sprintf(payload, "%d", evse_get_max_charging_current());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Requested current
  sprintf(topic, "%s/amp", board_config.mqtt_main_topic);
  sprintf(payload, "%f", evse_get_charging_current()/10.0);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Current temperature limit
  sprintf(topic, "%s/amt", board_config.mqtt_main_topic);
  sprintf(payload, "%d", evse_get_temp_threshold());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Automatic stop energy
  sprintf(topic, "%s/ate", board_config.mqtt_main_topic);
  sprintf(payload, "%f", evse_get_default_consumption_limit()/1000.0);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Automatic stop time
  sprintf(topic, "%s/att", board_config.mqtt_main_topic);
  sprintf(payload, "%f", evse_get_default_charging_time_limit()/60.0);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Under power limit
  sprintf(topic, "%s/upl", board_config.mqtt_main_topic);
  sprintf(payload, "%f", evse_get_default_under_power_limit()/1000.0);
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // AC Voltage
  sprintf(topic, "%s/acv", board_config.mqtt_main_topic);
  sprintf(payload, "%d", energy_meter_get_ac_voltage());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
}

static void mqtt_publish_evse_select_data(esp_mqtt_client_handle_t client) {

  char topic[64];
  char payload[512];

  // Energy meter mode
  sprintf(topic, "%s/emm", board_config.mqtt_main_topic);
  sprintf(payload, "%s", energy_meter_mode_to_str_mqtt(energy_meter_get_mode()));
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
}

static void mqtt_publish_evse_switch_data(esp_mqtt_client_handle_t client) {

  char topic[64];
  char payload[512];

  // Set charger state
  sprintf(topic, "%s/scs", board_config.mqtt_main_topic);
  sprintf(payload, "%d", evse_is_enabled());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Card authorization required
  sprintf(topic, "%s/acs", board_config.mqtt_main_topic);
  sprintf(payload, "%d", evse_is_pending_auth());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);

  // Effective lock setting
  sprintf(topic, "%s/lck", board_config.mqtt_main_topic);
  sprintf(payload, "%d", socket_lock_is_detection_high());
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
        //ESP_LOGI(TAG, "MQTT Data id=%d topic=%s", event->event_id, event->topic);
        //ESP_LOGI(TAG, "MQTT Data TOPIC=%.*s\r\n", event->topic_len, event->topic);
        //ESP_LOGI(TAG, "MQTT Data DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    case MQTT_EVENT_PUBLISHED:
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_task_func(void* param)
{
    uint8_t ha_discovery_done = 1;

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = board_config.mqtt_uri,
        .credentials.username = board_config.mqtt_username,
        .credentials.client_id = board_config.mqtt_client_id,
        .credentials.authentication.password = board_config.mqtt_password,
        .session.last_will.topic = lwt_topic,
        .session.last_will.msg = LWT_DISCONNECTED,
        .session.last_will.msg_len = strlen(LWT_DISCONNECTED),
        .session.last_will.qos = 0,
        .session.last_will.retain = 1,
    };


    if (!xEventGroupWaitBits(wifi_event_group, WIFI_STA_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY)) {
        ESP_LOGI(TAG, "Waiting Wifi to be ready before starting MQTT service");
        do {
        } while (!xEventGroupWaitBits(wifi_event_group, WIFI_STA_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY));
    }
  
    ESP_LOGI(TAG, "Starting MQTT service");
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

    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs));
    nvs_get_u8(nvs, NVS_HA_DISCOVERY_DONE, &ha_discovery_done);

    if (!ha_discovery_done) {
        ha_discovery_done = 1;
        nvs_set_u8(nvs, NVS_HA_DISCOVERY_DONE, ha_discovery_done);
        nvs_commit(nvs);
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
        sprintf(lwt_topic, "%s/%s", board_config.mqtt_main_topic, LWT_TOPIC);
        ESP_LOGI(TAG, "Starting MQTT task");
        xTaskCreate(mqtt_task_func, "mqtt_task", 4*1024, NULL, 11, &mqtt_task);
    }
}