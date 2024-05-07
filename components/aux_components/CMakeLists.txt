set(srcs
    "src/power_outlet.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES nvs_flash driver esp_timer 
                    REQUIRES config evse)