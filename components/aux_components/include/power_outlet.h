#ifndef POWER_OUTLET_H_
#define POWER_OUTLET_H_

#include <stdbool.h>

/**
 * @brief Initialize power outlet
 * 
 */
void power_outlet_init(void);

/**
 * @brief Set state of power outlet
 * 
 * @param state 
 */
void power_outlet_set_state(bool state);

/**
 * @brief Get state of power outlet
 * 
 * @return state 
 */
bool power_outlet_get_state(void);

#endif /* POWER_OUTLET_H_ */
