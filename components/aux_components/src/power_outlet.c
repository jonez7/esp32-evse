#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "power_outlet.h"
#include "aux_relay.h"
#include "board_config.h"
#include "led.h"
#include "button.h"

static const char* TAG = "power_outlet";

static TaskHandle_t power_outlet_task;

static bool button_available = false;

static bool led_available = false;

static void power_outlet_button_press_handler(TickType_t press_time)
{
    static TickType_t prev_toggle_time = 0;
    // Previous togle needs to be second ago
    if ( press_time - prev_toggle_time > pdMS_TO_TICKS(1000)) {
        ESP_LOGD(TAG, "Button pressed, state: %d", power_outlet_get_state());
        power_outlet_set_state(!power_outlet_get_state());
        prev_toggle_time = press_time;
    }
}

static void power_outlet_task_func(void* param)
{
    uint32_t state;

    while (true) {
        if (xTaskNotifyWait(0x00, 0xff, &state, portMAX_DELAY)) {

            if (state) {
                aux_relay_set_state(true);
                led_set_on(LED_ID_AUX1);
            }
            else {
                aux_relay_set_state(false);
                led_set_off(LED_ID_AUX1);
            }
            ESP_LOGD(TAG, "Task, state: %"PRIu32, state);
        }
    }
}

void power_outlet_init(void)
{
    if (board_config.power_outlet) {
        aux_relay_init();

        // This needs maybe thinking, but now this is the way...
        // if BUTTON_AUX1 and/or LED_AUX1 are configured,
        // those are expected to be used by Power Outlet
        if (board_config.button_aux1) {
            button_available = true;
            // Initialize callback for button push
            button_set_handler(BUTTON_ID_AUX1, power_outlet_button_press_handler, BUTTON_HANDLER_BOTH);
        }
        if (board_config.led_aux1) {
            led_available = true;
            // And make sure that LED is off
            led_set_off(LED_ID_AUX1);
        }
        ESP_LOGD(TAG, "Avalability status button=%d led=%d", button_available, led_available);

        xTaskCreate(power_outlet_task_func, "power_outlet_task", 1 * 1024, NULL, 5, &power_outlet_task);
    }
}

void power_outlet_set_state(bool state)
{
    if (board_config.power_outlet) {
        ESP_LOGD(TAG, "Set, state: %d", state);
        xTaskNotify(power_outlet_task, (uint32_t)state, eSetValueWithoutOverwrite);
    }
}

bool power_outlet_get_state(void)
{
    return aux_relay_get_state();
}
