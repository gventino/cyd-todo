idf_component_register(
    SRCS app_main.c settings_store.c
    INCLUDE_DIRS .
    REQUIRES cydui ui repo scenes model theme_quest settings provision netsetup netsync nvs_flash esp_timer esp_system
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wformat -Wformat-security) # CT-13
