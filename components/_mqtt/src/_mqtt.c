#include <string.h>
#include <math.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "nvs.h"

#include "_mqtt.h"
#include "evse.h"
#include "board_config.h"
#include "energy_meter.h"
#include "socket_lock.h"
#include "temp_sensor.h"

static const char* TAG = "mqtt";

//static nvs_handle nvs;

//static uint8_t unit_id = 1;

//static void restart_func(void* arg)
//{
//    vTaskDelay(pdMS_TO_TICKS(5000));
//    esp_restart();
//    vTaskDelete(NULL);
//}

//static void timeout_restart()
//{
//    xTaskCreate(restart_func, "restart_task", 2 * 1024, NULL, 10, NULL);
//}

void mqtt_init(void)
{
    //ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs));

    //nvs_get_u8(nvs, NVS_UNIT_ID, &unit_id);

    ESP_LOGI(TAG, "MQTT_URI=%s", board_config.mqtt_uri);
    ESP_LOGI(TAG, "MQTT_MAIN_TOPIC=%s", board_config.mqtt_main_topic);
    ESP_LOGI(TAG, "MQTT_CLIENT_ID=%s", board_config.mqtt_client_id);
    ESP_LOGI(TAG, "MQTT_USER=%s", board_config.mqtt_username);
    ESP_LOGI(TAG, "MQTT_PASSWORD=%s", board_config.mqtt_password);
    ESP_LOGI(TAG, "MQTT_HOMEASSISTANT_DISCOVERY=%d", board_config.mqtt_homeassistant_discovery);
}

