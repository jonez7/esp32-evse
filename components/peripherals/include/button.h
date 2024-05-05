#ifndef BUTTON_H_
#define BUTTON_H_

#include <stdint.h>

/**
 * @brief Button ID
 * 
 */
typedef enum
{
    BUTTON_ID_WIFI,
    BUTTON_ID_EVSE_ENABLE,
    BUTTON_ID_AUX1,
    BUTTON_ID_MAX
} button_id_t;

/**
 * @brief Button pressed handler func
 * 
 */
typedef void (*button_pressed_handler)(void);

/**
 * @brief Button released handler func
 *
 * @param The time (got from xTaskGetTickCount) when button was pressed
 */
typedef void (*button_released_handler)(TickType_t press_time);

/**
 * @brief Initialize button
 * 
 */
void button_init(void);

/**
 * @brief Set button pressed handler
 * 
 * @param button_id id of button
 * @param handler for button pressed activity
 */
void button_set_pressed_handler(button_id_t button_id, button_pressed_handler handler);

/**
 * @brief Set button released handler
 * 
 * @param button_id id of button
 * @param handler for button released activity
 */
void button_set_released_handler(button_id_t button_id, button_released_handler handler);

#endif /* BUTTON_H_ */
