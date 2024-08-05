#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "rcm.h"
#include "board_config.h"
#include "evse.h"

// static bool do_test = false;

// static bool triggered = false;

// static bool test_triggered = false;

// static void IRAM_ATTR rcm_isr_handler(void* arg)
// {
//     if (!do_test) {
//         triggered = true;
//     } else {
//         test_triggered = true;
//     }
// }

void rcm_init(void)
{
    if (board_config.rcm) {
        gpio_config_t io_conf = {};

        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pin_bit_mask = BIT64(board_config.rcm_test_gpio);
        ESP_ERROR_CHECK(gpio_config(&io_conf));

        io_conf.mode = GPIO_MODE_INPUT;
        if (board_config.rcm_gpio_pullup)
            io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        else
            io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
        // io_conf.intr_type = GPIO_INTR_POSEDGE;
        io_conf.pin_bit_mask = BIT64(board_config.rcm_gpio);
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        //ESP_ERROR_CHECK(gpio_isr_handler_add(board_config.rcm_gpio, rcm_isr_handler, NULL));
    }
}

static bool read_rcm_gpio_state(void)
{
    bool const state = !!gpio_get_level(board_config.rcm_gpio);
    return state ^ board_config.rcm_gpio_inverted;
}

bool rcm_test(void)
{
    if (!board_config.rcm) {
        return true;
    }

    gpio_config_t io_conf = {0};
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pin_bit_mask = BIT64(board_config.rcm_test_gpio);
    io_conf.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&io_conf));


    gpio_set_level(board_config.rcm_test_gpio, !board_config.rcm_gpio_inverted);
    vTaskDelay(pdMS_TO_TICKS(100));
    bool const success = read_rcm_gpio_state();
    gpio_set_level(board_config.rcm_test_gpio, board_config.rcm_gpio_inverted);

    io_conf.mode = GPIO_MODE_INPUT;
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    return success;
}

bool rcm_is_triggered(void)
{
    if (read_rcm_gpio_state()) {
        vTaskDelay(pdMS_TO_TICKS(1));
        return read_rcm_gpio_state();
    }
    return false;
}
