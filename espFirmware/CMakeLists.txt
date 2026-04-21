# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
# cmake_minimum_required(VERSION 3.22)

cmake_minimum_required(VERSION 3.16)

list(APPEND EXTRA_COMPONENT_DIRS "C:/esp/v6.0/esp-zigbee-sdk/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rfidZigbee)