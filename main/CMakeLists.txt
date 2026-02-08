#idf_component_register(SRCS "blink_example_main.c"
#                      INCLUDE_DIRS ".")
# Choose which application to build
# Comment/uncomment the SRCS line you want

# Option 1: Blink example
# idf_component_register(SRCS "blink_example_main.c"
#                     INCLUDE_DIRS ".")

# Option 2: Camera streaming (ACTIVE)
idf_component_register(SRCS "camera_streamer.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi esp_http_server nvs_flash esp32-camera esp_timer)
