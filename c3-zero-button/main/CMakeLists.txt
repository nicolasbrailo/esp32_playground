file(GLOB_RECURSE srcs "*.c")
idf_component_register(SRCS "${srcs}"
                       PRIV_REQUIRES esp_driver_gpio esp_timer
                       INCLUDE_DIRS ".")
