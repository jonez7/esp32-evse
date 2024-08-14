#include <string.h>
#include <stdbool.h>
#include <sys/param.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_spiffs.h"
#include "driver/gpio.h"

#include "evse.h"
#include "peripherals.h"
#include "led.h"
#include "button.h"
#include "modbus.h"
#include "mqtt.h"
#include "protocols.h"
#include "serial.h"
#include "board_config.h"
#include "wifi.h"
#include "script.h"
#include "logger.h"
#include "addressable_led.h"
#include "power_outlet.h"
#include "tesla_button.h"

#define AP_CONNECTION_TIMEOUT   60000 // 60sec
#define RESET_HOLD_TIME         5000  // 5sec

static const char* TAG = "app_main";

static evse_state_t led_state = -1;

static bool evse_enabled = false;

static void reset_and_reboot(void)
{
    ESP_LOGW(TAG, "All settings will be erased...");
    ESP_ERROR_CHECK(nvs_flash_erase());

    ESP_LOGW(TAG, "Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(500));

    esp_restart();
}

static void wifi_event_task_func(void* param)
{
    EventBits_t mode_bits;
    TickType_t const wait_sta = board_config.wifi_ap_autostart ? pdMS_TO_TICKS(10000) : portMAX_DELAY;

    while (true) {
        led_set_off(LED_ID_WIFI);
        mode_bits = xEventGroupWaitBits(wifi_event_group, WIFI_AP_MODE_BIT | WIFI_STA_MODE_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
        if (mode_bits & WIFI_AP_MODE_BIT) {
            led_set_state(LED_ID_WIFI, 100, 900);

            if (xEventGroupWaitBits(wifi_event_group, WIFI_AP_CONNECTED_BIT | WIFI_STA_MODE_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(AP_CONNECTION_TIMEOUT)) & WIFI_AP_CONNECTED_BIT) {
                led_set_state(LED_ID_WIFI, 1900, 100);
                do {
                } while (!(xEventGroupWaitBits(wifi_event_group, WIFI_AP_DISCONNECTED_BIT | WIFI_STA_MODE_BIT, pdFALSE, pdFALSE, portMAX_DELAY) & WIFI_AP_DISCONNECTED_BIT));
            } else {
                if (xEventGroupGetBits(wifi_event_group) & WIFI_AP_MODE_BIT) {
                    wifi_ap_stop();
                }
            }
        } else if (mode_bits & WIFI_STA_MODE_BIT) {
            led_set_state(LED_ID_WIFI, 500, 500);

            if (xEventGroupWaitBits(wifi_event_group, WIFI_STA_CONNECTED_BIT | WIFI_AP_MODE_BIT, pdFALSE, pdFALSE, wait_sta) & WIFI_STA_CONNECTED_BIT) {
                led_set_on(LED_ID_WIFI);
                do {
                } while (!(xEventGroupWaitBits(wifi_event_group, WIFI_STA_DISCONNECTED_BIT | WIFI_AP_MODE_BIT, pdFALSE, pdFALSE, portMAX_DELAY) & WIFI_STA_DISCONNECTED_BIT));
            } else {
                // No STA found... start AP
                wifi_ap_start();
            }
        }
    }
}

void wifi_button_release_handler(TickType_t press_time)
{
    if (xTaskGetTickCount() - press_time >= pdMS_TO_TICKS(RESET_HOLD_TIME)) {
        ESP_LOGD(TAG, "REBOOT the SYSTEM.");
        evse_set_available(false);
        reset_and_reboot();
    } else {
        if (!(xEventGroupGetBits(wifi_event_group) & WIFI_AP_MODE_BIT)) {
            ESP_LOGD(TAG, "START WIFI AP");
            wifi_ap_start();
        }
    }
}

void evse_enable_button_press_handler(TickType_t press_time)
{
    static TickType_t prev_toggle_time = 0;
    // Previous togle needs to be second ago
    if ( press_time - prev_toggle_time > pdMS_TO_TICKS(1000)) {
        ESP_LOGD(TAG, "EVSE_ENABLE: state=%d", evse_is_enabled());
        evse_set_enabled(!evse_is_enabled());
        prev_toggle_time = press_time;
    }
}

void set_button_callbacks(void)
{
    button_set_handler(BUTTON_ID_WIFI, wifi_button_release_handler, BUTTON_HANDLER_RELEASED);
    if (board_config.button_evse_enable) {
        button_set_handler(BUTTON_ID_EVSE_ENABLE,  evse_enable_button_press_handler, BUTTON_HANDLER_BOTH);
    }
}

static void fs_info(esp_vfs_spiffs_conf_t* conf)
{
    size_t total = 0, used = 0;
    esp_err_t ret = esp_spiffs_info(conf->partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get partition %s information %s", conf->partition_label, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition %s size: total: %d, used: %d", conf->partition_label, total, used);
    }
}

static void fs_init(void)
{
    esp_vfs_spiffs_conf_t cfg_conf = {
        .base_path = "/cfg",
        .partition_label = "cfg",
        .max_files = 1,
        .format_if_mount_failed = false
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&cfg_conf));

    esp_vfs_spiffs_conf_t data_conf = {
        .base_path = "/data",
        .partition_label = "data",
        .max_files = 5,
        .format_if_mount_failed = true
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&data_conf));

    fs_info(&cfg_conf);
    fs_info(&data_conf);
}

static bool ota_diagnostic(void)
{
    //TODO diagnostic after ota
    return true;
}

#define ADDRESSABLE_LED_READY           0,128,0
#define ADDRESSABLE_LED_EV_CONNECTED    128,90,0
#define ADDRESSABLE_LED_CHARGING        0,0,128
#define ADDRESSABLE_LED_ERROR           128,0,0

static void update_leds(void)
{
    if ((led_state != evse_get_state()) || (evse_enabled != evse_is_enabled())) {
        led_state = evse_get_state();
        evse_enabled = evse_is_enabled();

        switch (led_state) {
        case EVSE_STATE_A:
            if (evse_enabled && !board_config.led_error) {
                led_set_on(LED_ID_CHARGING);
            }
            else {
                led_set_off(LED_ID_CHARGING);
            }
            led_set_off(LED_ID_ERROR);
            addressable_led_set_rgb(ADDRESSABLE_LED_READY);
            break;
        case EVSE_STATE_B1:
        case EVSE_STATE_B2:
            led_set_state(LED_ID_CHARGING, 500, 500);
            led_set_off(LED_ID_ERROR);
            addressable_led_set_rgb(ADDRESSABLE_LED_EV_CONNECTED);
            break;
        case EVSE_STATE_C1:
        case EVSE_STATE_D1:
            led_set_state(LED_ID_CHARGING, 1900, 100);
            led_set_off(LED_ID_ERROR);
            addressable_led_set_rgb(ADDRESSABLE_LED_CHARGING);
            break;
        case EVSE_STATE_C2:
        case EVSE_STATE_D2:
            led_set_on(LED_ID_CHARGING);
            led_set_off(LED_ID_ERROR);
            addressable_led_set_rgb(ADDRESSABLE_LED_CHARGING);
            break;
        case EVSE_STATE_E:
            if (evse_enabled && !board_config.led_error) {
                led_set_state(LED_ID_CHARGING,100,100);
            }
            else {
                led_set_off(LED_ID_CHARGING);
            }
            led_set_on(LED_ID_ERROR);
            addressable_led_set_rgb(ADDRESSABLE_LED_ERROR);
            break;
        case EVSE_STATE_F:
            led_set_off(LED_ID_CHARGING);
            led_set_state(LED_ID_ERROR, 500, 500);
            addressable_led_set_rgb(ADDRESSABLE_LED_ERROR);
            break;
        }
    }
}

void app_main(void)
{
    logger_init();
    esp_log_set_vprintf(logger_vprintf);

    const esp_partition_t* running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Running partition: %s", running->label);

    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "OTA pending verify");
            if (ota_diagnostic()) {
                ESP_LOGI(TAG, "Diagnostics completed successfully! Continuing execution ...");
                esp_ota_mark_app_valid_cancel_rollback();
            } else {
                ESP_LOGE(TAG, "Diagnostics failed! Start rollback to the previous version ...");
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }
        }
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    fs_init();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    board_config_load();

    // Configure logging level
    esp_log_level_set(board_config.log_component, board_config.log_level);

    wifi_init();
    peripherals_init();
    modbus_init();
    mqtt_init();
    serial_init();
    protocols_init();
    evse_init();
    button_init();
    script_init();
    addressable_led_init();
    power_outlet_init();
    tesla_button_init();

    set_button_callbacks();

    xTaskCreate(wifi_event_task_func, "wifi_event_task", 4 * 1024, NULL, 5, NULL);

    while (true) {
        evse_process();
        update_leds();

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
