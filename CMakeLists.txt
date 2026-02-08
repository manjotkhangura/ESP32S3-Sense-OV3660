# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

#include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
#idf_build_set_property(MINIMAL_BUILD ON)
#project(ESPIDF-example)

# Include XIAO board definition
include(boards/xiao_esp32s3_sense/board.cmake)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ESPIDF-example)


