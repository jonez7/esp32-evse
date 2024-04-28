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

#define LWT_TOPIC        "connection"
#define LWT_CONNECTED    "connected"
#define LWT_DISCONNECTED "connection lost"

static const char* TAG = "mqtt";

static nvs_handle nvs;

static TaskHandle_t mqtt_task;

static char lwt_topic[32];

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
static void mqtt_config_component(
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
    const esp_app_desc_t* app_desc = esp_app_get_description();

    wifi_get_ip(tmp);

    /* See https://www.home-assistant.io/docs/mqtt/discovery/ */
    sprintf(discovery_topic, "homeassistant/%s/%s/%s/config", component, board_config.mqtt_main_topic, field);

    sprintf(payload, 
        "{"                                                 // 0
        "\"~\": \"%s\","                                    // 1
        "\"unique_id\": \"%s-%s\","                         // 2
        "\"object_id\": \"%s_%s\","                         // 3
        "\"name\": \"%s\","                                 // 4
        "\"icon\": \"%s\","                                 // 5
        "\"state_topic\": \"~/%s\","                        // 6
        "\"availability_topic\": \"~/%s\","                 // 7
        "\"payload_available\": \"%s\","                    // 8
        "\"payload_not_available\": \"%s\","                // 9
        "\"force_update\": \"true\","                       // -
        "\"device\": "                                      // 10
          "{"                                               // 11
              "\"identifiers\": [\"%s\"],"                  // 12
              "\"name\": \"%s\","                           // 13
              "\"model\": \"ESP32 EVSE\","                  // 14
              "\"manufacturer\": \"OULWare\","              // 15
              "\"sw_version\": \"%s\","                     // 16
              "\"configuration_url\": \"http://%s\""        // 17
          "}",                                              // 18
        board_config.mqtt_main_topic,         // 1
        board_config.mqtt_main_topic, field,  // 2
        board_config.mqtt_main_topic, field,  // 3
        name,                                 // 4
        icon,                                 // 5
        field,                                // 6
        LWT_TOPIC,                            // 7
        LWT_CONNECTED,                        // 8
        LWT_DISCONNECTED,                     // 9
        board_config.mqtt_main_topic,         // 12
        board_config.device_name,             // 13
        app_desc->version,                    // 16
        tmp);                                 // 17

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

    if (strlen(entity_category)) {
        sprintf(tmp, ",\"entity_category\": \"%s\"", entity_category);
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

static void mqtt_subscribe_send_ha_discovery(esp_mqtt_client_handle_t client) {

    //                          component | Field   | User Friendly Name           | Icon                          | Unit | Device Class        | State Class       | Entity Category
    mqtt_config_component(client, "sensor", "rbt"   , "Uptime"                     , "mdi:clock-time-eight-outline", "s"  , ""                  , ""                , "diagnostic");
    mqtt_config_component(client, "sensor", "mac"   , "MAC Address"                , "mdi:network-outline"         , ""   , ""                  , ""                , "diagnostic");
    mqtt_config_component(client, "sensor", "ip"    , "IP Adress"                  , "mdi:network-outline"         , ""   , ""                  , ""                , "diagnostic");
    mqtt_config_component(client, "sensor", "fme"   , "Free Heap Memory"           , "mdi:memory"                  , "B"  , ""                  , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "rssi"  , "Wi-Fi RSSI"                 , "mdi:wifi"                    , "dBm", "signal_strength"   , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "tmc"   , "CPU Temperature"            , "mdi:thermometer"             , "°C" , "temperature"       , "measurement"     , "diagnostic");


    mqtt_config_component(client, "sensor", "acs"   , "Card authorization required", ""                            , ""   , ""                  , ""                , "diagnostic");
#if 0
    mqtt_config_component(client, "number", "ama"   , "Maximum current limit"      , ""                            , "A"  , "current"           , "measurement"     , ""          );
    mqtt_config_component(client, "number", "amp"   , "Requested current"          , ""                            , "A"  , "current"           , ""                , "config"    );
    mqtt_config_component(client, "number", "amt"   , "Current temperature limit"  , ""                            , "°C" , "temperature"       , "measurement"     , "diagnostic");
    mqtt_config_component(client, "number", "ate"   , "Automatic stop energy"      , ""                            , "Wh" , "energy"            , "total_increasing", ""          );
    mqtt_config_component(client, "number", "att"   , "Automatic stop time"        , "mdi:timer-outline"           , "s"  , ""                  , "measurement"     , ""          );
#endif
#if 0
    mqtt_config_component(client, "sensor", "cbl"   , "Cable maximum current"      , ""                            , "A"  , "current"           , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "ccs"   , "Current charger state"      , "mdi:auto-fix"                , ""   , ""                  , ""                , "diagnostic");
    mqtt_config_component(client, "sensor", "cdi"   , "Charging duration"          , "mdi:timer-outline"           , "s"  , ""                  , "measurement"     , "diagnostic");
#endif
#if 0
    mqtt_config_component(client, "select", "emm"   , "Energy meter mode"          , "mdi:meter-electric"          , ""   , ""                  , ""                , "config"    );
    mqtt_config_component(client, "sensor", "err"   , "Error code"                 , "mdi:alert-circle-outline"    , ""   , ""                  , ""                , "diagnostic");
    mqtt_config_component(client, "sensor", "eto"   , "Total energy"               , ""                            , "Wh" , "energy"            , "total_increasing", "diagnostic");
#endif
#if 0
    mqtt_config_component(client, "sensor", "lck"   , "Effective lock setting"     , ""                            , ""   , ""                  , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "lst"   , "Last session time"          , "mdi:counter"                 , "s"  , ""                  , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "nrg_0" , "Voltage L1"                 , ""                            , "V"  , "voltage"           , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "nrg_1" , "Voltage L2"                 , ""                            , "V"  , "voltage"           , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "nrg_2" , "Voltage L3"                 , ""                            , "V"  , "voltage"           , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "nrg_3" , "Voltage N"                  , ""                            , "V"  , "voltage"           , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "nrg_4" , "Current L1"                 , ""                            , "A"  , "current"           , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "nrg_5" , "Current L2"                 , ""                            , "A"  , "current"           , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "nrg_6" , "Current L3"                 , ""                            , "A"  , "current"           , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "nrg_7" , "Power L1"                   , ""                            , "W"  , "power"             , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "nrg_8" , "Power L2"                   , ""                            , "W"  , "power"             , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "nrg_9" , "Power L3"                   , ""                            , "W"  , "power"             , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "nrg_10", "Power N"                    , ""                            , "W"  , "power"             , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "nrg_11", "Current power"              , ""                            , "W"  , "power"             , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "nrg_12", "Power factor L1"            , ""                            , "%"  , "power_factor"      , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "nrg_13", "Power factor L2"            , ""                            , "%"  , "power_factor"      , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "nrg_14", "Power factor L3"            , ""                            , "%"  , "power_factor"      , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "nrg_15", "Power factor N"             , ""                            , "%"  , "power_factor"      , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "rcd"   , "Residual current detection" , ""                            , ""   , ""                  , "measurement"     , "diagnostic");
#endif
#if 0
    mqtt_config_component(client, "select", "scs"   , "Set charger state"          , "mdi:auto-fix"                , ""   , ""                  , ""                , "config"    );
    mqtt_config_component(client, "sensor", "status", "Status"                     , "mdi:heart-pulse"             , ""   , ""                  , ""                , "diagnostic");
    mqtt_config_component(client, "sensor", "tma_0" , "Temperature sensor 1"       , ""                            , "°C" , "temperature"       , "measurement"     , "diagnostic");
    mqtt_config_component(client, "sensor", "tma_1" , "Temperature sensor 2"       , ""                            , "°C" , "temperature"       , "measurement"     , "diagnostic");
#endif
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

static void mqtt_publish_sensor_data(esp_mqtt_client_handle_t client) {

  char topic[64];
  char payload[64];
  static uint8_t foo=0;

  // Card authorization required
  sprintf(topic, "%s/acs", board_config.mqtt_main_topic);
  sprintf(payload, "%d", foo++);//evse_is_pending_auth());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "1");
#if 0
  // Maximum current limit
  sprintf(topic, "%s/ama", board_config.mqtt_main_topic);
  sprintf(payload, "%d", evse_get_max_charging_current());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "2");
  // Requested current
  sprintf(topic, "%s/amp", board_config.mqtt_main_topic);
  sprintf(payload, "%d", evse_get_charging_current());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "3");
  // Current temperature limit
  sprintf(topic, "%s/amt", board_config.mqtt_main_topic);
  sprintf(payload, "%d", evse_get_temp_threshold());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "4");
  // Automatic stop energy
  sprintf(topic, "%s/ate", board_config.mqtt_main_topic);
  sprintf(payload, "%ld", evse_get_default_consumption_limit());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "5");
  // Automatic stop time
  sprintf(topic, "%s/att", board_config.mqtt_main_topic);
  sprintf(payload, "%ld", evse_get_default_charging_time_limit());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "6");
  // Cable maximum current
  sprintf(topic, "%s/cbl", board_config.mqtt_main_topic);
  sprintf(payload, "%d", 0);//proximity_get_max_current());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "7");
  // Current charger state
  sprintf(topic, "%s/ccs", board_config.mqtt_main_topic);
  sprintf(payload, "%d", evse_is_enabled());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "8");
  // Charging duration
  sprintf(topic, "%s/cdi", board_config.mqtt_main_topic);
  sprintf(payload, "%ld", energy_meter_get_charging_time());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "9");
  // Energy meter mode
  sprintf(topic, "%s/emm", board_config.mqtt_main_topic);
  sprintf(payload, "%d", energy_meter_get_mode());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "10");
  // Error code
  sprintf(topic, "%s/err", board_config.mqtt_main_topic);
  sprintf(payload, "%ld", evse_get_error());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "11");
  // Total energy
  sprintf(topic, "%s/eto", board_config.mqtt_main_topic);
  sprintf(payload, "%ld", energy_meter_get_consumption());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "12");
  // Effective lock setting
  sprintf(topic, "%s/lck", board_config.mqtt_main_topic);
  sprintf(payload, "%d", socket_lock_is_detection_high());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "13");
  // Last session time
  sprintf(topic, "%s/lst", board_config.mqtt_main_topic);
  sprintf(payload, "%ld", energy_meter_get_session_time());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "14");
  // Voltage L1
  sprintf(topic, "%s/nrg_0", board_config.mqtt_main_topic);
  sprintf(payload, "%f", energy_meter_get_l1_voltage());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "15");
  // Voltage L2
  sprintf(topic, "%s/nrg_1", board_config.mqtt_main_topic);
  sprintf(payload, "%f", energy_meter_get_l2_voltage());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "16");
  // Voltage L3
  sprintf(topic, "%s/nrg_2", board_config.mqtt_main_topic);
  sprintf(payload, "%f", energy_meter_get_l3_voltage());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "17");
  // Current L1
  sprintf(topic, "%s/nrg_4", board_config.mqtt_main_topic);
  sprintf(payload, "%f", energy_meter_get_l1_current());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "18");
  // Current L2
  sprintf(topic, "%s/nrg_5", board_config.mqtt_main_topic);
  sprintf(payload, "%f", energy_meter_get_l2_current());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "19");
  // Current L3
  sprintf(topic, "%s/nrg_6", board_config.mqtt_main_topic);
  sprintf(payload, "%f", energy_meter_get_l3_current());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "20");
  // Current power
  sprintf(topic, "%s/nrg_11", board_config.mqtt_main_topic);
  sprintf(payload, "%d", energy_meter_get_power());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "21");
  // Residual current detection
  sprintf(topic, "%s/rcd", board_config.mqtt_main_topic);
  sprintf(payload, "%d", evse_is_rcm());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "22");
  // Set charger state
  sprintf(topic, "%s/scs", board_config.mqtt_main_topic);
  sprintf(payload, "%d", evse_is_enabled());
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "23");
  // Status
  sprintf(topic, "%s/status", board_config.mqtt_main_topic);
  sprintf(payload, "%s", evse_state_to_str(evse_get_state()));
  esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
  ESP_LOGI(TAG, "24");
  //sprintf(topic, "%s/tma", board_config.mqtt_main_topic);
  //sprintf(payload, "%d", temp_sensor_get_temperatures(int16_t* curr_temps););
  //esp_mqtt_client_publish(client, topic, payload, 0, /*qos*/1, /*retain*/1);
#endif
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
            mqtt_publish_sensor_data(client);
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
        xTaskCreate(mqtt_task_func, "mqtt_task", 1024*2, NULL, 6, &mqtt_task);
    }
}


