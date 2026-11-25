idf_component_register(
    SRCS app_main.c
    INCLUDE_DIRS .
    REQUIRES cydui ui repo scenes model theme_quest provision nvs_flash esp_timer esp_system
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wformat -Wformat-security) # CT-13
