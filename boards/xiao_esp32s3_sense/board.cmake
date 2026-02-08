#
# XIAO ESP32S3 Sense Board Definition
#

set(IDF_TARGET "esp32s3")

# Add board config to defaults
set(SDKCONFIG_DEFAULTS
    "${CMAKE_SOURCE_DIR}/sdkconfig.defaults"
    "${CMAKE_SOURCE_DIR}/boards/xiao_esp32s3_sense/sdkconfig.board"
)

# Add board pins to include path
include_directories("${CMAKE_SOURCE_DIR}/boards/xiao_esp32s3_sense")

message(STATUS "Board: XIAO ESP32S3 Sense")
message(STATUS "Flash: 8MB, PSRAM: 8MB, Camera: OV3640")
message(STATUS "SDKCONFIG_DEFAULTS: ${SDKCONFIG_DEFAULTS}")