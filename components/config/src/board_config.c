#include <string.h>
#include <ctype.h>
#include "esp_system.h"
#include "esp_err.h"

#include "board_config.h"

static const char* TAG = "board_config";

board_config_t board_config;

bool atob(const char* value)
{
    return value[0] == 'y';
}

board_config_energy_meter_t atoem(const char* value)
{
    if (!strcmp(value, "cur")) {
        return BOARD_CONFIG_ENERGY_METER_CUR;
    }
    if (!strcmp(value, "cur_vlt")) {
        return BOARD_CONFIG_ENERGY_METER_CUR_VLT;
    }
    return BOARD_CONFIG_ENERGY_METER_NONE;
}

board_config_serial_t atoser(const char* value)
{
    if (!strcmp(value, "uart")) {
        return BOARD_CONFIG_SERIAL_UART;
    }
    if (!strcmp(value, "rs485")) {
        return BOARD_CONFIG_SERIAL_RS485;
    }
    return BOARD_CONFIG_SERIAL_NONE;
}

#define SET_CONFIG_VALUE(name, prop, convert_fn)    \
    if (!strcmp(key, name)) {                       \
        board_config.prop = convert_fn(value);      \
        continue;                                   \
    }                                               \

#define SET_CONFIG_VALUE_STR(name, prop)            \
    if (!strcmp(key, name)) {                       \
        strcpy(board_config.prop, value);           \
        continue;                                   \
    }                                               \

void board_config_load()
{
    memset(&board_config, 0, sizeof(board_config_t));

    // Default to warning and all components
    board_config.log_level = ESP_LOG_WARN;
    board_config.log_component[0] = '*';

    FILE* file = fopen("/cfg/board.cfg", "r");
    if (file == NULL) {
        ESP_ERROR_CHECK(ESP_ERR_NOT_FOUND);
    }

    char buffer[256];

    while (fgets(buffer, 256, file)) {
        int buf_length = strlen(buffer);
        int buf_start = 0;
        while (buf_start < buf_length && isspace((unsigned char)buffer[buf_start])) {
            buf_start++;
        }
        int buf_end = buf_length;
        while (buf_end > 0 && !isgraph((unsigned char)buffer[buf_end - 1])) {
            buf_end--;
        }

        buffer[buf_end] = '\0';
        char* line = &buffer[buf_start];

        if (line[0] != '#') {
            char* saveptr;
            char* key = strtok_r(line, "=", &saveptr);
            if (key != NULL) {
                char* value = strtok_r(NULL, "=", &saveptr);
                if (value != NULL) {
                    SET_CONFIG_VALUE_STR("DEVICE_NAME", device_name);
                    SET_CONFIG_VALUE("LED_CHARGING", led_charging, atob);
                    SET_CONFIG_VALUE("LED_CHARGING_GPIO", led_charging_gpio, atoi);
                    SET_CONFIG_VALUE("LED_ERROR", led_error, atob);
                    SET_CONFIG_VALUE("LED_ERROR_GPIO", led_error_gpio, atoi);
                    SET_CONFIG_VALUE("LED_WIFI", led_wifi, atob);
                    SET_CONFIG_VALUE("LED_WIFI_GPIO", led_wifi_gpio, atoi);
                    SET_CONFIG_VALUE("LED_AUX1", led_aux1, atob);
                    SET_CONFIG_VALUE("LED_AUX1_GPIO", led_aux1_gpio, atoi);
                    SET_CONFIG_VALUE("BUTTON_WIFI_GPIO", button_wifi_gpio, atoi);
                    SET_CONFIG_VALUE("BUTTON_EVSE_ENABLE", button_evse_enable, atob);
                    SET_CONFIG_VALUE("BUTTON_EVSE_ENABLE_GPIO", button_evse_enable_gpio, atoi);
                    SET_CONFIG_VALUE("BUTTON_AUX1", button_aux1, atob);
                    SET_CONFIG_VALUE("BUTTON_AUX1_GPIO", button_aux1_gpio, atoi);
                    SET_CONFIG_VALUE("PILOT_PWM_GPIO", pilot_pwm_gpio, atoi);
                    SET_CONFIG_VALUE("PILOT_ADC_CHANNEL", pilot_adc_channel, atoi);
                    SET_CONFIG_VALUE("PILOT_DOWN_THRESHOLD_12", pilot_down_threshold_12, atoi);
                    SET_CONFIG_VALUE("PILOT_DOWN_THRESHOLD_9", pilot_down_threshold_9, atoi);
                    SET_CONFIG_VALUE("PILOT_DOWN_THRESHOLD_6", pilot_down_threshold_6, atoi);
                    SET_CONFIG_VALUE("PILOT_DOWN_THRESHOLD_3", pilot_down_threshold_3, atoi);
                    SET_CONFIG_VALUE("PILOT_DOWN_THRESHOLD_N12", pilot_down_threshold_n12, atoi);
                    SET_CONFIG_VALUE("PROXIMITY", proximity, atob);
                    SET_CONFIG_VALUE("PROXIMITY_ADC_CHANNEL", proximity_adc_channel, atoi);
                    SET_CONFIG_VALUE("PROXIMITY_DOWN_THRESHOLD_13", proximity_down_threshold_13, atoi);
                    SET_CONFIG_VALUE("PROXIMITY_DOWN_THRESHOLD_20", proximity_down_threshold_20, atoi);
                    SET_CONFIG_VALUE("PROXIMITY_DOWN_THRESHOLD_32", proximity_down_threshold_32, atoi);
                    SET_CONFIG_VALUE("AC_RELAY_GPIO", ac_relay_gpio, atoi);
                    SET_CONFIG_VALUE("AUX_RELAY", aux_relay, atob);
                    SET_CONFIG_VALUE("AUX_RELAY_GPIO", aux_relay_gpio, atoi);
                    SET_CONFIG_VALUE("POWER_OUTLET", power_outlet, atob);
                    SET_CONFIG_VALUE("SOCKET_LOCK", socket_lock, atob);
                    SET_CONFIG_VALUE("SOCKET_LOCK_A_GPIO", socket_lock_a_gpio, atoi);
                    SET_CONFIG_VALUE("SOCKET_LOCK_B_GPIO", socket_lock_b_gpio, atoi);
                    SET_CONFIG_VALUE("SOCKET_LOCK_DETECTION_GPIO", socket_lock_detection_gpio, atoi);
                    SET_CONFIG_VALUE("SOCKET_LOCK_DETECTION_DELAY", socket_lock_detection_delay, atoi);
                    SET_CONFIG_VALUE("SOCKET_LOCK_MIN_BREAK_TIME", socket_lock_min_break_time, atoi);
                    SET_CONFIG_VALUE("RCM", rcm, atob);
                    SET_CONFIG_VALUE("RCM_GPIO", rcm_gpio, atoi);
                    SET_CONFIG_VALUE("RCM_TEST_GPIO", rcm_test_gpio, atoi);
                    SET_CONFIG_VALUE("RCM_TEST", rcm_test, atob);
                    SET_CONFIG_VALUE("RCM_GPIO_PULLUP", rcm_gpio_pullup, atob);
                    SET_CONFIG_VALUE("RCM_GPIO_INVERTED", rcm_gpio_inverted, atob);
                    SET_CONFIG_VALUE("ENERGY_METER", energy_meter, atoem);
                    SET_CONFIG_VALUE("ENERGY_METER_THREE_PHASES", energy_meter_three_phases, atob);
                    SET_CONFIG_VALUE("ENERGY_METER_L1_CUR_ADC_CHANNEL", energy_meter_l1_cur_adc_channel, atoi);
                    SET_CONFIG_VALUE("ENERGY_METER_L2_CUR_ADC_CHANNEL", energy_meter_l2_cur_adc_channel, atoi);
                    SET_CONFIG_VALUE("ENERGY_METER_L3_CUR_ADC_CHANNEL", energy_meter_l3_cur_adc_channel, atoi);
                    SET_CONFIG_VALUE("ENERGY_METER_CUR_SCALE", energy_meter_cur_scale, atoff);
                    SET_CONFIG_VALUE("ENERGY_METER_L1_VLT_ADC_CHANNEL", energy_meter_l1_vlt_adc_channel, atoi);
                    SET_CONFIG_VALUE("ENERGY_METER_L2_VLT_ADC_CHANNEL", energy_meter_l2_vlt_adc_channel, atoi);
                    SET_CONFIG_VALUE("ENERGY_METER_L3_VLT_ADC_CHANNEL", energy_meter_l3_vlt_adc_channel, atoi);
                    SET_CONFIG_VALUE("ENERGY_METER_VLT_SCALE", energy_meter_vlt_scale, atoff);
                    SET_CONFIG_VALUE("AUX_IN_1", aux_in_1, atob);
                    SET_CONFIG_VALUE_STR("AUX_IN_1_NAME", aux_in_1_name);
                    SET_CONFIG_VALUE("AUX_IN_1_GPIO", aux_in_1_gpio, atoi);
                    SET_CONFIG_VALUE("AUX_IN_1_PULLUP", aux_in_1_pullup, atob);
                    SET_CONFIG_VALUE("AUX_IN_2", aux_in_2, atob);
                    SET_CONFIG_VALUE_STR("AUX_IN_2_NAME", aux_in_2_name);
                    SET_CONFIG_VALUE("AUX_IN_2_GPIO", aux_in_2_gpio, atoi);
                    SET_CONFIG_VALUE("AUX_IN_2_PULLUP", aux_in_2_pullup, atob);
                    SET_CONFIG_VALUE("AUX_IN_3", aux_in_3, atob);
                    SET_CONFIG_VALUE_STR("AUX_IN_3_NAME", aux_in_3_name);
                    SET_CONFIG_VALUE("AUX_IN_3_GPIO", aux_in_3_gpio, atoi);
                    SET_CONFIG_VALUE("AUX_IN_3_PULLUP", aux_in_3_pullup, atob);
                    SET_CONFIG_VALUE("AUX_IN_4", aux_in_4, atob);
                    SET_CONFIG_VALUE_STR("AUX_IN_4_NAME", aux_in_4_name);
                    SET_CONFIG_VALUE("AUX_IN_4_GPIO", aux_in_4_gpio, atoi);
                    SET_CONFIG_VALUE("AUX_IN_4_PULLUP", aux_in_4_pullup, atob);
                    SET_CONFIG_VALUE("AUX_IN_5", aux_in_5, atob);
                    SET_CONFIG_VALUE_STR("AUX_IN_5_NAME", aux_in_5_name);
                    SET_CONFIG_VALUE("AUX_IN_5_GPIO", aux_in_5_gpio, atoi);
                    SET_CONFIG_VALUE("AUX_IN_5_PULLUP", aux_in_5_pullup, atob);
                    SET_CONFIG_VALUE("AUX_IN_6", aux_in_6, atob);
                    SET_CONFIG_VALUE_STR("AUX_IN_6_NAME", aux_in_6_name);
                    SET_CONFIG_VALUE("AUX_IN_6_GPIO", aux_in_6_gpio, atoi);
                    SET_CONFIG_VALUE("AUX_IN_6_PULLUP", aux_in_6_pullup, atob);

                    SET_CONFIG_VALUE("AUX_OUT_1", aux_out_1, atob);
                    SET_CONFIG_VALUE_STR("AUX_OUT_1_NAME", aux_out_1_name);
                    SET_CONFIG_VALUE("AUX_OUT_1_GPIO", aux_out_1_gpio, atoi);
                    SET_CONFIG_VALUE("AUX_OUT_2", aux_out_2, atob);
                    SET_CONFIG_VALUE_STR("AUX_OUT_2_NAME", aux_out_2_name);
                    SET_CONFIG_VALUE("AUX_OUT_2_GPIO", aux_out_2_gpio, atoi);
                    SET_CONFIG_VALUE("AUX_OUT_3", aux_out_3, atob);
                    SET_CONFIG_VALUE_STR("AUX_OUT_3_NAME", aux_out_3_name);
                    SET_CONFIG_VALUE("AUX_OUT_3_GPIO", aux_out_3_gpio, atoi);
                    SET_CONFIG_VALUE("AUX_OUT_4", aux_out_4, atob);
                    SET_CONFIG_VALUE_STR("AUX_OUT_4_NAME", aux_out_4_name);
                    SET_CONFIG_VALUE("AUX_OUT_4_GPIO", aux_out_4_gpio, atoi);
                    SET_CONFIG_VALUE("AUX_OUT_5", aux_out_5, atob);
                    SET_CONFIG_VALUE_STR("AUX_OUT_5_NAME", aux_out_5_name);
                    SET_CONFIG_VALUE("AUX_OUT_5_GPIO", aux_out_5_gpio, atoi);
                    SET_CONFIG_VALUE("AUX_OUT_6", aux_out_6, atob);
                    SET_CONFIG_VALUE_STR("AUX_OUT_6_NAME", aux_out_6_name);
                    SET_CONFIG_VALUE("AUX_OUT_6_GPIO", aux_out_6_gpio, atoi);
                    SET_CONFIG_VALUE("AUX_AIN_1", aux_ain_1, atob);
                    SET_CONFIG_VALUE_STR("AUX_AIN_1_NAME", aux_ain_1_name);
                    SET_CONFIG_VALUE("AUX_AIN_1_ADC_CHANNEL", aux_ain_1_adc_channel, atoi);
                    SET_CONFIG_VALUE("AUX_AIN_2", aux_ain_2, atob);
                    SET_CONFIG_VALUE_STR("AUX_AIN_2_NAME", aux_ain_2_name);
                    SET_CONFIG_VALUE("AUX_AIN_2_ADC_CHANNEL", aux_ain_2_adc_channel, atoi);
#if CONFIG_ESP_CONSOLE_NONE || CONFIG_ESP_CONSOLE_UART_NUM != 0
                    SET_CONFIG_VALUE("SERIAL_1", serial_1, atoser);
                    SET_CONFIG_VALUE_STR("SERIAL_1_NAME", serial_1_name);
                    SET_CONFIG_VALUE("SERIAL_1_RXD_GPIO", serial_1_rxd_gpio, atoi);
                    SET_CONFIG_VALUE("SERIAL_1_TXD_GPIO", serial_1_txd_gpio, atoi);
                    SET_CONFIG_VALUE("SERIAL_1_RTS_GPIO", serial_1_rts_gpio, atoi);
#endif /* CONFIG_ESP_CONSOLE_NONE || CONFIG_ESP_CONSOLE_UART_NUM != 0 */
#if CONFIG_ESP_CONSOLE_NONE || CONFIG_ESP_CONSOLE_UART_NUM != 1
                    SET_CONFIG_VALUE("SERIAL_2", serial_2, atoser);
                    SET_CONFIG_VALUE_STR("SERIAL_2_NAME", serial_2_name);
                    SET_CONFIG_VALUE("SERIAL_2_RXD_GPIO", serial_2_rxd_gpio, atoi);
                    SET_CONFIG_VALUE("SERIAL_2_TXD_GPIO", serial_2_txd_gpio, atoi);
                    SET_CONFIG_VALUE("SERIAL_2_RTS_GPIO", serial_2_rts_gpio, atoi);
#endif /* CONFIG_ESP_CONSOLE_NONE || CONFIG_ESP_CONSOLE_UART_NUM != 1 */
#if SOC_UART_NUM > 2
#if CONFIG_ESP_CONSOLE_NONE || CONFIG_ESP_CONSOLE_UART_NUM != 2
                    SET_CONFIG_VALUE("SERIAL_3", serial_3, atoser);
                    SET_CONFIG_VALUE_STR("SERIAL_3_NAME", serial_3_name);
                    SET_CONFIG_VALUE("SERIAL_3_RXD_GPIO", serial_3_rxd_gpio, atoi);
                    SET_CONFIG_VALUE("SERIAL_3_TXD_GPIO", serial_3_txd_gpio, atoi);
                    SET_CONFIG_VALUE("SERIAL_3_RTS_GPIO", serial_3_rts_gpio, atoi);
#endif /* CONFIG_ESP_CONSOLE_NONE || CONFIG_ESP_CONSOLE_UART_NUM != 2 */
#endif /* SOC_UART_NUM > 2 */
                    SET_CONFIG_VALUE("ONEWIRE", onewire, atob);
                    SET_CONFIG_VALUE("ONEWIRE_GPIO", onewire_gpio, atoi);
                    SET_CONFIG_VALUE("ONEWIRE_TEMP_SENSOR", onewire_temp_sensor, atob);

                    SET_CONFIG_VALUE("WIFI_AP_AUTOSTART", wifi_ap_autostart, atob);
                    SET_CONFIG_VALUE_STR("WIFI_AP_SSID", wifi_ap_ssid);
                    SET_CONFIG_VALUE_STR("WIFI_AP_PASS", wifi_ap_pass);

                    SET_CONFIG_VALUE("MQTT", mqtt, atob);
                    SET_CONFIG_VALUE_STR("MQTT_URI", mqtt_uri);
                    SET_CONFIG_VALUE_STR("MQTT_MAIN_TOPIC", mqtt_main_topic);
                    SET_CONFIG_VALUE_STR("MQTT_CLIENT_ID", mqtt_client_id);
                    SET_CONFIG_VALUE_STR("MQTT_USER", mqtt_username);
                    SET_CONFIG_VALUE_STR("MQTT_PASSWORD", mqtt_password);
                    SET_CONFIG_VALUE("MQTT_HOMEASSISTANT_DISCOVERY", mqtt_homeassistant_discovery, atob);

                    SET_CONFIG_VALUE("ADDRESSABLE_LED", addressable_led, atob);
                    SET_CONFIG_VALUE("ADDRESSABLE_LED_GPIO", addressable_led_gpio, atoi);
                    SET_CONFIG_VALUE("ADDRESSABLE_LED_IS_GRB", addressable_led_is_grb, atob);

                    SET_CONFIG_VALUE("THERMISTOR", thermistor, atob);
                    SET_CONFIG_VALUE("THERMISTOR_ADC_CHANNEL", thermistor_adc_channel, atoi);
                    SET_CONFIG_VALUE("THERMISTOR_R1", thermistor_r1, atoi);
                    SET_CONFIG_VALUE("THERMISTOR_NOMINAL_R", thermistor_nominal_r, atoi);
                    SET_CONFIG_VALUE("THERMISTOR_BETA", thermistor_beta, atoi);

                    SET_CONFIG_VALUE("LOGGING_LEVEL", log_level, atoi);
                    SET_CONFIG_VALUE_STR("LOGGING_COMPONENT", log_component);

                    ESP_LOGE(TAG, "Unknown config value %s=%s", key, value);
                }
            }
        }
    }

    fclose(file);
}
