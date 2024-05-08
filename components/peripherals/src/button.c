#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "button.h"
#include "board_config.h"

#define BIT_PRESSED             0
#define BIT_RELEASED            1
#define GPIO_STATE_BIT          BIT31

// The time to filter out activity
#define BOUNCHING_FILTER        250

static const char* TAG = "button";

static TaskHandle_t user_input_task;

static struct button_s
{
    gpio_num_t gpio;
    bool pressed;
    TickType_t prev_irq;
    TickType_t press_tick;
    button_handler_t activity_type;
    button_activity_handler handler;
} buttons[BUTTON_ID_MAX];

static void IRAM_ATTR button_isr_handler(void* arg)
{
    BaseType_t higher_task_woken = pdFALSE;

    uint32_t button_idx = (uint32_t)arg;

    if (gpio_get_level(buttons[button_idx].gpio)) {
        button_idx |= GPIO_STATE_BIT;
    } else {
        button_idx &= ~GPIO_STATE_BIT;
    }
    xTaskNotifyFromISR(user_input_task, button_idx, eSetValueWithOverwrite, &higher_task_woken);

    if (higher_task_woken) {
        portYIELD_FROM_ISR();
    }
}

static void user_input_task_func(void* param)
{
    uint32_t notification;

    while (true) {
        if (xTaskNotifyWait(0x00, 0xff, &notification, portMAX_DELAY)) {
            uint32_t button_idx = notification & ~GPIO_STATE_BIT;
            uint32_t state = (notification & GPIO_STATE_BIT? 1:0);

            if (xTaskGetTickCount() - buttons[button_idx].prev_irq > pdMS_TO_TICKS(BOUNCHING_FILTER)) {
                if (BIT_PRESSED == state) {
                    buttons[button_idx].press_tick = xTaskGetTickCount();
                    buttons[button_idx].pressed = true;
                    if ((buttons[button_idx].activity_type & BUTTON_HANDLER_PRESSED)
                        && buttons[button_idx].handler) {
                        buttons[button_idx].handler(buttons[button_idx].press_tick);
                    }
                }
                if (BIT_RELEASED == state) {
                    // sometimes after connect debug UART emit RELEASED_BIT
                    // without preceding PRESS_BIT
                    if (buttons[button_idx].pressed &&
                        (buttons[button_idx].activity_type & BUTTON_HANDLER_RELEASED) &&
                        buttons[button_idx].handler) {
                        buttons[button_idx].handler(buttons[button_idx].press_tick);
                    }
                    buttons[button_idx].pressed = false;
                }
                ESP_LOGD(TAG, "Button notfification=0x%08"PRIx32": button=%d state=%d pressed=%d", 
                    notification,
                    (int)button_idx,
                    (int)(state),
                    buttons[notification & ~GPIO_STATE_BIT].pressed);
            }
            buttons[button_idx].prev_irq = xTaskGetTickCount();
        }
    }
}

void button_init(void)
{
    for (int i = 0; i < BUTTON_ID_MAX; i++) {
        buttons[i].gpio = GPIO_NUM_NC;
        buttons[i].pressed = false;
        buttons[i].prev_irq = 0;
        buttons[i].press_tick = 0;
        buttons[i].activity_type = BUTTON_HANDLER_NONE;
        buttons[i].handler = NULL;
    }

    gpio_config_t io_conf =  {
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_ANYEDGE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pin_bit_mask = 0
    };

    // Wifi button always "mandatory"
    buttons[BUTTON_ID_WIFI].gpio = board_config.button_wifi_gpio;
    io_conf.pin_bit_mask |= BIT64(board_config.button_wifi_gpio);

    if (board_config.button_evse_enable) {
        buttons[BUTTON_ID_EVSE_ENABLE].gpio = board_config.button_evse_enable_gpio;
        io_conf.pin_bit_mask |= BIT64(board_config.button_evse_enable_gpio);
    }

    if (board_config.button_aux1) {
        buttons[BUTTON_ID_AUX1].gpio = board_config.button_aux1_gpio;
        io_conf.pin_bit_mask |= BIT64(board_config.button_aux1_gpio);
    }

    ESP_ERROR_CHECK(gpio_config(&io_conf));

    for (int i = 0; i < BUTTON_ID_MAX; i++) {
        if (GPIO_NUM_NC != buttons[i].gpio) {
            ESP_ERROR_CHECK(gpio_isr_handler_add(buttons[i].gpio, button_isr_handler, (void*)i));
            ESP_LOGD(TAG, "Set button ISR butt=%d, gpio=%d", i, (int)buttons[i].gpio);
        }
    }

    xTaskCreate(user_input_task_func, "user_input_task", 2 * 1024, NULL, 5, &user_input_task);
}

void button_set_handler(button_id_t button_id, button_activity_handler handler, button_handler_t type)
{
    if (buttons[button_id].gpio != GPIO_NUM_NC) {
        buttons[button_id].handler = handler;
        buttons[button_id].activity_type = type;
        ESP_LOGD(TAG, "Set pressed handler button %d handler %x type %d", button_id, (unsigned int)handler, type);
    }
    else {
        ESP_LOGE(TAG, "Cannot set pressed handler! Button %d not configured", button_id);
    }
}