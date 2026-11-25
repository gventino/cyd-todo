file(GLOB SRCS src/*.c)
idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include REQUIRES model)
# The pretend server is for the demo and the self-test: a small one unless the self-test needs the worst-case data.
if(NOT CONFIG_TODO_SELFTEST)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC TODO_MEM_QUEST_CAP=16 TODO_MEM_EVENT_CAP=16)
endif()
