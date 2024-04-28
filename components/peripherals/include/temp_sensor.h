#ifndef TEMP_SENSOR_H_
#define TEMP_SENSOR_H_

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Initialize DS18S20 temperature sensor bus
 *
 */
void temp_sensor_init(void);

/**
 * @brief Get found sensor count
 * 
 * @return uint8_t 
 */
uint8_t temp_sensor_get_count(void);

/**
 * @brief Return current temperatures after temp_sensor_measure
 * 
 * @param temperature Output array of sensor count items, values in Celsius
 */
void temp_sensor_get_temperatures(int16_t* curr_temps);

/**
 * @brief Return lowest temperature after temp_sensor_measure
 * 
 * @return int16_t 
 */
int16_t temp_sensor_get_low(void);

/**
 * @brief Return highest temperature after temp_sensor_measure
 * 
 * @return int16_t 
 */
int16_t temp_sensor_get_high(void);

/**
 * @brief Return temperature sensor error
 * 
 * @return bool 
 */
bool temp_sensor_is_error(void);

/**
 * @brief Read ESP CPU temperature
 * 
 * @return float 
 */
float temp_sensor_read_cpu_temperature(void);

#endif /* TEMP_SENSOR_H_ */
