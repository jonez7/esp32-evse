/*
 * Author: Fred Larsen
 * Github: www.github.com/fredilarsen, TeslaChargeDoorOpener
 github.com/fredilarsen/TeslaChargeDoorOpener
 */
 
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <rom/ets_sys.h>

#include "board_config.h"
#include "button.h"

#define HIGH            1
#define LOW             0

#define ARRAY_SIZE(_x_) (sizeof(_x_)/sizeof((_x_)[0]))

static const char* TAG = "tesla_button";

static TaskHandle_t tesla_button_task;

static void tesla_button_send_sequence(void)
{
    // The signal to send
    const uint16_t pulse_width = 400;             // Microseconds
    const uint16_t message_distance = 23;         // Millis
    const uint8_t transmissions = 5;              // Number of repeated transmissions
    const uint8_t sequence[] = { 
      0x02,0xAA,0xAA,0xAA,  // Preamble of 26 bits by repeating 1010
      0x2B,                 // Sync byte
      0x2C,0xCB,0x33,0x33,0x2D,0x34,0xB5,0x2B,0x4D,0x32,0xAD,0x2C,0x56,0x59,0x96,0x66,
      0x66,0x5A,0x69,0x6A,0x56,0x9A,0x65,0x5A,0x58,0xAC,0xB3,0x2C,0xCC,0xCC,0xB4,0xD2,
      0xD4,0xAD,0x34,0xCA,0xB4,0xA0};

    for (uint8_t t = 0; t < transmissions; t++) {
        for (uint8_t j = 0; j < ARRAY_SIZE(sequence); j++) {
            for (int8_t bit = 7; bit >= 0; bit--) { // MSB
                gpio_set_level(board_config.tesla_button_transmitter_gpio, (sequence[j] & (1 << bit)) != 0 ? HIGH : LOW);
                ets_delay_us(pulse_width);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(message_distance));
    }
}

static void tesla_button_button_press_handler(TickType_t press_time)
{
    static TickType_t prev_toggle_time = 0;
    // Previous togle needs to be second ago
    if ( press_time - prev_toggle_time > pdMS_TO_TICKS(1000)) {
        xTaskNotify(tesla_button_task, 1, eSetValueWithoutOverwrite);
        prev_toggle_time = press_time;
    }
}

static void tesla_button_task_func(void* param)
{
    uint32_t state;

    ESP_LOGI/*D*/(TAG, "Task created and running");
    while (true) {
        if (xTaskNotifyWait(0x00, 0xff, &state, portMAX_DELAY)) {
            tesla_button_send_sequence();
            ESP_LOGI/*D*/(TAG, "Sequence send");
        }
    }
}

void tesla_button_init(void)
{
    if (board_config.tesla_button) {

        gpio_config_t conf = {
            .pin_bit_mask = BIT64(board_config.tesla_button_transmitter_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&conf));

        // Initialize callback for button push
        button_set_handler(
            (button_id_t)board_config.tesla_button_button_id,
            tesla_button_button_press_handler,
            BUTTON_HANDLER_BOTH);

        xTaskCreate(tesla_button_task_func, "tesla_button_task", 1 * 1024, NULL, 5, &tesla_button_task);
    }
}
