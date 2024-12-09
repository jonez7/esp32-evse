#include "temp_sensor.h"

#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <rom/ets_sys.h>
#include <string.h>
#include <sys/param.h>

#include "adc.h"
#include "board_config.h"
#include "ds18x20.h"

#define MAX_SENSORS           5
#define MEASURE_PERIOD        10000  // 10s
#define MEASURE_ERR_THRESHOLD 3
#define ADC_MAX               ((1 << 13) - 1)

static const char* TAG = "temp_sensor";

static ds18x20_addr_t sensor_addrs[MAX_SENSORS];

static int16_t temps[MAX_SENSORS];

static uint8_t sensor_count = 0;

static int16_t low_temp = 0;

static int16_t high_temp = 0;

static uint8_t measure_err_count = 0;

static adc_oneshot_unit_handle_t m_handle;

#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
#include "driver/temperature_sensor.h"

static temperature_sensor_handle_t temp_handle = NULL;
static temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 50);
#endif

bool thermistor_init(void)
{
    adc_oneshot_unit_init_cfg_t conf = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&conf, &m_handle);
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "ADC one shot already configured... err: %d", err);
        m_handle = adc_handle;
    } else if (err != ESP_OK) {
        ESP_LOGI(TAG, "ADC one shot configured fail... err: %d", err);
        return false;
    }

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    err = adc_oneshot_config_channel(m_handle, board_config.thermistor_adc_channel, &config);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "ADC init error");
    }
    return err == ESP_OK;
}

static void thermistor_task_func(void* param)
{
#define Kelvin 273.15
    const float R1 = board_config.thermistor_r1;
    const float Beta = board_config.thermistor_beta;
    const float NTC_R = board_config.thermistor_nominal_r;
    const float NTC_C = (Kelvin + 25.0);

    const adc_channel_t adc_channel = board_config.thermistor_adc_channel;

    while (true) {
        int adc = 0;
        for (int i = 0; i < 10; i++) {
            adc_oneshot_read(m_handle, adc_channel, &adc);
            ets_delay_us(20);
        }
        adc /= 8;

        float Rt = R1 * (((float)ADC_MAX / adc) - 1);
        float K = (Beta * NTC_C) / (Beta + (NTC_C * logf(Rt / NTC_R)));
        float C = K - Kelvin;  // convert to Celsius
        low_temp = high_temp = (int16_t)(C * 100);

        vTaskDelay(pdMS_TO_TICKS(MEASURE_PERIOD));
    }
}

static void temp_sensor_task_func(void* param)
{
    while (true) {
        esp_err_t err = ds18x20_measure_and_read_multi(board_config.onewire_gpio, sensor_addrs, sensor_count, temps);
        if (err == ESP_OK) {
            int16_t low = INT16_MAX;
            int16_t high = INT16_MIN;

            for (int i = 0; i < sensor_count; i++) {
                low = MIN(low, temps[i]);
                high = MAX(high, temps[i]);
            }

            low_temp = low;
            high_temp = high;
            measure_err_count = 0;
        } else {
            ESP_LOGW(TAG, "Measure error %d (%s)", err, esp_err_to_name(err));
            measure_err_count++;
        }
        vTaskDelay(pdMS_TO_TICKS(MEASURE_PERIOD));
    }
}

void temp_sensor_init(void)
{
    if (board_config.onewire && board_config.onewire_temp_sensor) {
        gpio_reset_pin(board_config.onewire_gpio);
        gpio_set_pull_mode(board_config.onewire_gpio, GPIO_PULLUP_ONLY);

        size_t found = 0;
        ESP_ERROR_CHECK(ds18x20_scan_devices(board_config.onewire_gpio, sensor_addrs, MAX_SENSORS, &found));

        if (found > MAX_SENSORS) {
            ESP_LOGW(TAG, "Found %d sensors, but can handle max %d", found, MAX_SENSORS);
        } else {
            ESP_LOGI(TAG, "Found %d sensors", found);
        }
        sensor_count = MIN(found, MAX_SENSORS);

        if (sensor_count > 0) {
            xTaskCreate(temp_sensor_task_func, "temp_sensor_task", 2 * 1024, NULL, 5, NULL);
        }
    } else if (board_config.thermistor) {
        if (thermistor_init()) {
            xTaskCreate(thermistor_task_func, "temp_sensor_task", 2 * 1024, NULL, 5, NULL);
            sensor_count = 1;
        }
    }
#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
    ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_handle));
#endif
}

uint8_t temp_sensor_get_count(void)
{
    return sensor_count;
}

void temp_sensor_get_temperatures(int16_t* curr_temps)
{
    if (sensor_count) {
        memcpy(curr_temps, temps, sensor_count * sizeof(temps[0]));
    }
}

int16_t temp_sensor_get_low(void)
{
    return low_temp;
}

int16_t temp_sensor_get_high(void)
{
    return high_temp;
}

bool temp_sensor_is_error(void)
{
    return sensor_count == 0 || measure_err_count > MEASURE_ERR_THRESHOLD;
}

// CPU Temp
#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
float temp_sensor_read_cpu_temperature(void)
{
    // Enable temperature sensor
    if (ESP_OK != temperature_sensor_enable(temp_handle)) return -99;
    // Get converted sensor data
    float tsens_out;
    if (ESP_OK != temperature_sensor_get_celsius(temp_handle, &tsens_out)) return -99;
    // Disable the temperature sensor if it is not needed and save the power
    temperature_sensor_disable(temp_handle);
    return tsens_out;
}
#else
extern uint8_t temprature_sens_read();

float temp_sensor_read_cpu_temperature(void)
{
    return (temprature_sens_read() - 32) / 1.8;
}
#endif
