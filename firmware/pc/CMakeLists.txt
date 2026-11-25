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
todo_component(repo)
target_link_libraries(todo_repo PUBLIC todo_model)

# ---- tests (Unity, from the library) ----
enable_testing()
add_library(unity STATIC ${CYDUI_ROOT}/third_party/unity/unity.c)
target_include_directories(unity PUBLIC ${CYDUI_ROOT}/third_party/unity)

get_filename_component(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
add_library(todo_rig STATIC tests/rig.c tests/world.c)
target_link_libraries(todo_rig PUBLIC unity cydui cydui_mem todo_ui)
target_compile_options(todo_rig PRIVATE ${TODO_STRICT})
target_include_directories(todo_rig PUBLIC tests)
target_compile_definitions(todo_rig PUBLIC GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden" GOLDEN_OUT_DIR="${CMAKE_BINARY_DIR}/golden")
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/golden ${CMAKE_CURRENT_SOURCE_DIR}/golden)

function(todo_test name)
    add_executable(${name} tests/${name}.c)
    target_link_libraries(${name} PRIVATE unity ${ARGN})
    target_compile_options(${name} PRIVATE ${TODO_STRICT})
    target_compile_definitions(${name} PRIVATE REPO_ROOT="${REPO_ROOT}")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

file(GLOB QUEST_SRCS ${COMP}/theme_quest/src/*.c ${COMP}/theme_quest/fonts/*.c)
add_library(todo_theme_quest STATIC ${QUEST_SRCS})
target_include_directories(todo_theme_quest PUBLIC ${COMP}/theme_quest/include ${COMP}/theme_quest/fonts)
target_link_libraries(todo_theme_quest PUBLIC cydui todo_model)
target_compile_options(todo_theme_quest PRIVATE ${TODO_STRICT})
if(CYDUI_SANITIZE)
    target_compile_options(todo_theme_quest PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    target_link_options(todo_theme_quest PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined)
endif()

# the encoder is Nayuki's, unmodified: built with the warnings it was written for, not ours
add_library(todo_qr STATIC ${COMP}/qr/src/qr.c ${COMP}/qr/vendor/qrcodegen.c)
target_include_directories(todo_qr PUBLIC ${COMP}/qr/include PRIVATE ${COMP}/qr/vendor)
target_link_libraries(todo_qr PUBLIC cydui)
set_source_files_properties(${COMP}/qr/src/qr.c PROPERTIES COMPILE_OPTIONS "${TODO_STRICT}")
set_source_files_properties(${COMP}/qr/vendor/qrcodegen.c PROPERTIES COMPILE_OPTIONS "-std=c99;-Wall")
if(CYDUI_SANITIZE)
    target_compile_options(todo_qr PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    target_link_options(todo_qr PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined)
endif()

todo_component(ui)
target_link_libraries(todo_ui PUBLIC todo_model todo_repo todo_qr todo_theme_quest)

todo_test(test_model todo_model)
todo_test(test_datetime todo_model)
todo_test(test_memrepo todo_repo)
todo_test(test_theme_quest todo_theme_quest todo_rig)
todo_test(test_qr todo_qr todo_theme_quest todo_rig)
target_include_directories(test_qr PRIVATE ${COMP}/qr/vendor)
todo_test(test_qr_sheet todo_qr todo_theme_quest todo_rig)
add_test(NAME qr_decode COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/decode_qr.sh ${CMAKE_BINARY_DIR}/golden)
set_tests_properties(qr_decode PROPERTIES SKIP_RETURN_CODE 77 DEPENDS test_qr_sheet)
todo_test(test_screens todo_rig)
