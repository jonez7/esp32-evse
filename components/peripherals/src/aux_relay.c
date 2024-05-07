#include "esp_log.h"
#include "driver/gpio.h"

#include "aux_relay.h"
#include "board_config.h"

static const char* TAG = "aux_relay";

static bool aux_relay_state;

void aux_relay_init(void)
{
    if (board_config.aux_relay) {
        gpio_config_t conf = {
            .pin_bit_mask = BIT64(board_config.aux_relay_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&conf));

        // Initalize the relay to known state
        // TODO: Shoud this be stored to NVS?
        aux_relay_set_state(false);
    }
}

void aux_relay_set_state(bool state)
{
    if (board_config.aux_relay) {
        ESP_LOGI(TAG, "Set relay: %d", state);
        gpio_set_level(board_config.aux_relay_gpio, state);
        aux_relay_state = state;
    }
}

bool aux_relay_get_state(void)
{
    return aux_relay_state;
}