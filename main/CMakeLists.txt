idf_component_register(SRCS "flow_control.c" "wifi.c" "main.c" "web_server.c"
                    INCLUDE_DIRS ".")

set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../user_interface")
if(EXISTS ${WEB_SRC_DIR})
    spiffs_create_partition_image(www ${WEB_SRC_DIR} FLASH_IN_PROJECT)
else()
    message(FATAL_ERROR "${WEB_SRC_DIR}/dist doesn't exit. Please run 'npm run build' in ${WEB_SRC_DIR}")
endif()