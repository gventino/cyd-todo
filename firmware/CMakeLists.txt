cmake_minimum_required(VERSION 3.16)

# The library is a submodule; it is one component (the core and the ESP32 backend). The application's own components
# are the same directories the PC build compiles: pure C that does not know which of the two it is on.
get_filename_component(CYDUI_ROOT "${CMAKE_CURRENT_LIST_DIR}/third_party/cydui" ABSOLUTE)
set(EXTRA_COMPONENT_DIRS
    ${CYDUI_ROOT}
    ${CMAKE_CURRENT_LIST_DIR}/components/model
    ${CMAKE_CURRENT_LIST_DIR}/components/repo
    ${CMAKE_CURRENT_LIST_DIR}/components/qr
    ${CMAKE_CURRENT_LIST_DIR}/components/theme_quest
    ${CMAKE_CURRENT_LIST_DIR}/components/ui
    ${CMAKE_CURRENT_LIST_DIR}/components/scenes
    ${CMAKE_CURRENT_LIST_DIR}/components/provision
    ${CMAKE_CURRENT_LIST_DIR}/components/netsetup
    ${CMAKE_CURRENT_LIST_DIR}/components/jsonr
    ${CMAKE_CURRENT_LIST_DIR}/components/netrepo
    ${CMAKE_CURRENT_LIST_DIR}/components/netsync)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cyd-todo)
