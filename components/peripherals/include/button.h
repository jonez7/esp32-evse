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
    BUTTON_ID_AUX2,
    BUTTON_ID_TESLA,
    BUTTON_ID_MAX
} button_id_t;

/**
 * @brief Handler register types
 *
 */
typedef enum
{
    BUTTON_HANDLER_NONE = 0,
    BUTTON_HANDLER_PRESSED = 1,
    BUTTON_HANDLER_RELEASED = 2,
    BUTTON_HANDLER_BOTH = 3
} button_handler_t;

/**
 * @brief Button handler func
 *
 * @param time when button press was detected
 */
typedef void (*button_activity_handler)(TickType_t press_time);

/**
 * @brief Initialize button
 *
 */
void button_init(void);

/**
 * @brief Set button handler
 *
 * @param button_id id of button
 * @param handler for button pressed activity
 * @param type which kind of activity to trigger
 */
void button_set_handler(button_id_t button_id, button_activity_handler handler, button_handler_t type);

/**
 * @brief Set button state
 *
 * @param button_id id of button
 * @param enabled enable/disable button action
 */
void button_set_button_state(button_id_t button_id, bool enabled);

/**
 * @brief Get button state
 *
 * @param button_id id of button
 *
 * @return true if button is enable
 */
bool button_get_button_state(button_id_t button_id);

#endif /* BUTTON_H_ */
