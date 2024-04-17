#ifndef ADDRESSABLE_LED_H_
#define ADDRESSABLE_LED_H_

#include <stdint.h>

/**
 * @brief Initialize addressable led(s)
 *
 */
void addressable_led_init(void);


/**
 * @brief Set addressable led color
 *
 */
void addressable_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief Clear addressable leds (turn off)
 *
 */
void addressable_led_clear(void);

#endif
