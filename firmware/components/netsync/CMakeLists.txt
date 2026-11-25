idf_component_register(
    SRCS src/netsync.c src/http_transport.c
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS src
    REQUIRES netrepo repo model
    PRIV_REQUIRES esp_http_client esp_timer esp_netif lwip freertos log esp_hw_support provision
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wformat -Wformat-security)
