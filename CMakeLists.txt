cmake_minimum_required(VERSION 3.16)

# Add TinyUSB config include path globally
include_directories("${CMAKE_SOURCE_DIR}/components/tinyusb_config/include")

list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(EvilDuck32)
