#ifndef AUX_RELAY_H_
#define AUX_RELAY_H_

#include <stdbool.h>

/**
 * @brief Initialize aux relay
 * 
 */
void aux_relay_init(void);

/**
 * @brief Set state of aux relay
 * 
 * @param state 
 */
void aux_relay_set_state(bool state);

/**
 * @brief Get state of aux relay
 * 
 * @return state 
 */
bool aux_relay_get_state(void);

#endif /* AUX_RELAY_H_ */
