set(srcs
    "src/adc.c"
    "src/peripherals.c"
    "src/led.c"
    "src/button.c"
    "src/pilot.c"
    "src/proximity.c"
    "src/ac_relay.c"
    "src/aux_relay.c"
    "src/energy_meter.c"
    "src/socket_lock.c"
    "src/rcm.c"
    "src/aux_io.c"
    "src/onewire.c"
    "src/ds18x20.c"
    "src/temp_sensor.c"
    "src/addressable_led.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES nvs_flash driver esp_adc esp_timer espressif__led_strip
                    REQUIRES config evse)
