file(GLOB SRCS src/*.c)
idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include REQUIRES cydui model repo qr theme_quest settings)
