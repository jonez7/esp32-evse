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

#endif /* AUX_RELAY_H_ */
