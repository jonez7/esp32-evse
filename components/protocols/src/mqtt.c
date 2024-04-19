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

#define NVS_NAMESPACE          "mqtt"
#define NVS_HA_DISCOVERY_DONE  "ha_discovery"

static const char* TAG = "mqtt";

static nvs_handle nvs;

static TaskHandle_t mqtt_task;

static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        int msg_id;
        ESP_LOGI(TAG, "MQTT Connected");
        msg_id = esp_mqtt_client_publish(client, "evse/qos1", "data_3", 0, 1, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        msg_id = esp_mqtt_client_subscribe(client, "evse/#", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT Disconnected");
        break;
    case MQTT_EVENT_DATA:
        //ESP_LOGI(TAG, "MQTT Data id=%d topic=%s", event->event_id, event->topic);
        ESP_LOGI(TAG, "MQTT Data TOPIC=%.*s\r\n", event->topic_len, event->topic);
        ESP_LOGI(TAG, "MQTT Data DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
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

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(50));
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
        ESP_LOGI(TAG, "Starting MQTT task");
        xTaskCreate(mqtt_task_func, "mqtt_task", 1024, NULL, 6, &mqtt_task);
    }
}


