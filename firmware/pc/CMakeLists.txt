# The PC build of the cyd-todo firmware: the same components that the ESP-IDF build will use, plus an SDL
# window and the tests. Configure from the repository root:  cmake -S firmware/pc -B firmware/pc/build -G Ninja
cmake_minimum_required(VERSION 3.20)
project(cyd_todo_pc LANGUAGES C)

option(CYDUI_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# The library, pinned as a submodule (or CYDUI_ROOT for a checkout of your own): only the code, not its tests.
set(CYDUI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/cydui" CACHE PATH "the cydui checkout to build against")
set(CYDUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CYDUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CYDUI_BUILD_FUZZ OFF CACHE BOOL "" FORCE)
add_subdirectory(${CYDUI_ROOT} ${CMAKE_BINARY_DIR}/cydui)

set(TODO_STRICT -std=c99 -pedantic -Wall -Wextra -Wshadow -Wcast-qual -Wconversion -Wno-sign-conversion -Werror)
set(COMP ${CMAKE_CURRENT_SOURCE_DIR}/../components)

function(todo_component name)
    file(GLOB srcs ${COMP}/${name}/src/*.c)
    add_library(todo_${name} STATIC ${srcs})
    target_include_directories(todo_${name} PUBLIC ${COMP}/${name}/include)
    target_compile_options(todo_${name} PRIVATE ${TODO_STRICT})
    if(CYDUI_SANITIZE)
        target_compile_options(todo_${name} PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
        target_link_options(todo_${name} PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined)
    endif()
endfunction()

todo_component(model)

# ---- tests (Unity, from the library) ----
enable_testing()
add_library(unity STATIC ${CYDUI_ROOT}/third_party/unity/unity.c)
target_include_directories(unity PUBLIC ${CYDUI_ROOT}/third_party/unity)

get_filename_component(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
function(todo_test name)
    add_executable(${name} tests/${name}.c)
    target_link_libraries(${name} PRIVATE unity ${ARGN})
    target_compile_options(${name} PRIVATE ${TODO_STRICT})
    target_compile_definitions(${name} PRIVATE REPO_ROOT="${REPO_ROOT}")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

todo_test(test_model todo_model)
todo_test(test_datetime todo_model)
