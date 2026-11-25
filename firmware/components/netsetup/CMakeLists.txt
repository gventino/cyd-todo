idf_component_register(
    SRCS src/netsetup.c src/portal.c src/creds.c
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS src
    REQUIRES provision
    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server esp_timer esp_hw_support lwip freertos log
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wformat -Wformat-security)
