#include "esp_log.h"
#include <led_strip.h>

#include "board_config.h"
#include "addressable_led.h"

#include <stdint.h>

static const char * TAG = "a_led";

static led_strip_handle_t led_strip;

// Selection of control method
#define RMT
//#define SPI

/**
 * @brief Initialize addressable led(s)
 *
 */
void addressable_led_init(void) {
    if (board_config.addressable_led) {
#ifdef RMT
        /* LED strip initialization with the GPIO and pixels number*/
        led_strip_config_t strip_config = {
            .strip_gpio_num = board_config.addressable_led_gpio,
            .max_leds = 1,                              // The number of LEDs in the strip,
            .led_pixel_format = LED_PIXEL_FORMAT_GRB,   // Pixel format of your LED strip
            .led_model = LED_MODEL_WS2812,              // LED strip model
            .flags.invert_out = false,                  // whether to invert the output signal (useful when your hardware has a level inverter)
        };

        led_strip_rmt_config_t rmt_config = {
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
            .rmt_channel = 0,
#else
            .clk_src = RMT_CLK_SRC_DEFAULT, // different clock source can lead to different power consumption
            .resolution_hz = 10 * 1000 * 1000, // 10MHz
            .flags.with_dma = false, // whether to enable the DMA feature
#endif
        };
        esp_err_t esp_err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
#endif // RMT
#ifdef SPI
        /* LED strip initialization with the GPIO and pixels number*/
        led_strip_config_t strip_config = {
            .strip_gpio_num = board_config.addressable_led_gpio, // The GPIO that connected to the LED strip's data line
            .max_leds = 1,                                       // The number of LEDs in the strip,
            .led_pixel_format = LED_PIXEL_FORMAT_GRB,            // Pixel format of your LED strip
            .led_model = LED_MODEL_WS2812,                       // LED strip model
            .flags.invert_out = false,                           // whether to invert the output signal (useful when your hardware has a level inverter)
        };

        led_strip_spi_config_t spi_config = {
            .clk_src = SPI_CLK_SRC_DEFAULT, // different clock source can lead to different power consumption
            .flags.with_dma = true, // Using DMA can improve performance and help drive more LEDs
            .spi_bus = SPI2_HOST,   // SPI bus ID
        };
        esp_err_t esp_err = led_strip_new_spi_device(&strip_config, &spi_config, &led_strip);
#endif //SPI
        if (esp_err == ESP_OK) {
            ESP_LOGI(TAG, "Addressable led initialized to pin %d", strip_config.strip_gpio_num);
        } else {
            ESP_LOGE(TAG, "Addressable led failed to initialize to pin %d", strip_config.strip_gpio_num);
        }
        ESP_ERROR_CHECK(led_strip_clear(led_strip));
    }
}


/**
 * @brief Set addressable led color
 *
 */
void addressable_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue) {
    if (board_config.addressable_led) {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, red, green, blue));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    }
}

void addressable_led_clear(void) {
    if (board_config.addressable_led) {
        ESP_ERROR_CHECK(led_strip_clear(led_strip));
    }
}
