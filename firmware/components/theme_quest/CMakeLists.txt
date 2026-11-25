file(GLOB SRCS src/*.c fonts/*.c)
idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include fonts REQUIRES cydui model)
