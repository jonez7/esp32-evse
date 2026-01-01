#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

#include "led.h"
#include "board_config.h"

#define BLOCK_TIME              10

static const char* TAG = "led";

static struct led_s
{
    gpio_num_t gpio;
    bool on : 1;
    bool control_inverted : 1;
    uint16_t ontime, offtime;
    TimerHandle_t timer;
} leds[LED_ID_MAX];

static void led_set_gpio_level(struct led_s* led, bool on)
{
    if (led->control_inverted) {
        gpio_set_level(led->gpio, !on);
    } else {
        gpio_set_level(led->gpio, on);
    }
}

void led_init(void)
{
    for (int i = 0; i < LED_ID_MAX; i++) {
        leds[i].timer = NULL;
        leds[i].gpio = GPIO_NUM_NC;
    }

    gpio_config_t io_conf =  {
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pin_bit_mask = 0
    };
    
    if (board_config.led_wifi) {
        leds[LED_ID_WIFI].gpio = board_config.led_wifi_gpio;
        leds[LED_ID_WIFI].control_inverted = board_config.led_wifi_control_inverted;
        io_conf.pull_up_en = (leds[LED_ID_WIFI].control_inverted ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE);
        io_conf.pin_bit_mask = BIT64(board_config.led_wifi_gpio);
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }

    if (board_config.led_charging) {
        leds[LED_ID_CHARGING].gpio = board_config.led_charging_gpio;
        leds[LED_ID_CHARGING].control_inverted = board_config.led_charging_control_inverted;
        io_conf.pull_up_en = (leds[LED_ID_CHARGING].control_inverted ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE);
        io_conf.pin_bit_mask = BIT64(board_config.led_charging_gpio);
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }

    if (board_config.led_error) {
        leds[LED_ID_ERROR].gpio = board_config.led_error_gpio;
        leds[LED_ID_ERROR].control_inverted = board_config.led_error_control_inverted;
        io_conf.pull_up_en = (leds[LED_ID_ERROR].control_inverted ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE);
        io_conf.pin_bit_mask = BIT64(board_config.led_error_gpio);
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }

    if (board_config.led_aux1) {
        leds[LED_ID_AUX1].gpio = board_config.led_aux1_gpio;
        leds[LED_ID_AUX1].control_inverted = board_config.led_aux1_control_inverted;
        io_conf.pull_up_en = (leds[LED_ID_AUX1].control_inverted ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE);
        io_conf.pin_bit_mask = BIT64(board_config.led_aux1_gpio);
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }

    for (int i = 0; i < LED_ID_MAX; i++) {
        if (leds[i].gpio != GPIO_NUM_NC) {
            led_set_gpio_level(&leds[i], false);
        }
    }
}



static void timer_callback(TimerHandle_t xTimer)
{
    struct led_s* led = (struct led_s*)pvTimerGetTimerID(xTimer);

    led->on = !led->on;
    led_set_gpio_level(led, led->on);

    xTimerChangePeriod(xTimer, pdMS_TO_TICKS(led->on ? led->ontime : led->offtime), BLOCK_TIME);
}

void led_set_state(led_id_t led_id, uint16_t ontime, uint16_t offtime)
{
    struct led_s* led = &leds[led_id];
    if (led->gpio != GPIO_NUM_NC) {
        if (led->timer != NULL) {
            xTimerStop(led->timer, BLOCK_TIME);
            xTimerDelete(led->timer, BLOCK_TIME);
            led->timer = NULL;
        }

        led->ontime = ontime;
        led->offtime = offtime;

        if (ontime == 0) {
            ESP_LOGD(TAG, "Set led %d off", led_id);
            led->on = false;
            led_set_gpio_level(led, led->on);
        } else if (offtime == 0) {
            ESP_LOGD(TAG, "Set led %d on", led_id);
            led->on = true;
            led_set_gpio_level(led, led->on);
        } else {
            ESP_LOGD(TAG, "Set led %d blink (on: %d off: %d)", led_id, ontime, offtime);

            led->on = true;
            led_set_gpio_level(led, led->on);

            if (led->timer == NULL) {
                led->timer = xTimerCreate("led_timer", pdMS_TO_TICKS(ontime), pdFALSE, (void*)led, timer_callback);
            }
            xTimerStart(led->timer, BLOCK_TIME);
        }
    }
}

