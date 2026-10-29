# main/CMakeLists.txt - Dynamic Incremental Registration
set(COMPONENT_SRCS "main.c")
set(COMPONENT_INCLUDES "include")

# State management
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/hud_state.c")
    list(APPEND COMPONENT_SRCS "hud_state.c")
endif()

# Milestone 2: Display & UI
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/display")
    list(APPEND COMPONENT_INCLUDES "display")
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/display/pcd8544.c")
        list(APPEND COMPONENT_SRCS "display/pcd8544.c" "display/hud_ui.c")
    endif()
endif()

# Milestone 3: NimBLE Chronos BLE
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/ble")
    list(APPEND COMPONENT_INCLUDES "ble")
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/ble/chronos_ble.c")
        list(APPEND COMPONENT_SRCS "ble/chronos_ble.c" "ble/chronos_packet.c")
    endif()
endif()

# Milestone 4: Hardware Sensors
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sensors")
    list(APPEND COMPONENT_INCLUDES "sensors")
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sensors/voltmeter.c")
        list(APPEND COMPONENT_SRCS
            "sensors/voltmeter.c"
            "sensors/speedometer.c"
            "sensors/dht_sensor.c"
            "sensors/ldr_sensor.c"
            "sensors/buzzer.c"
        )
    endif()
endif()

# Milestone 5: Web Portal & OTA
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/web")
    list(APPEND COMPONENT_INCLUDES "web")
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/web/web_portal.c")
        list(APPEND COMPONENT_SRCS "web/web_portal.c")
    endif()
endif()

idf_component_register(
    SRCS
        ${COMPONENT_SRCS}
    INCLUDE_DIRS
        ${COMPONENT_INCLUDES}
    REQUIRES
        esp_adc
        esp_http_server
        app_update
        nvs_flash
        driver
        bt
        esp_timer
        esp_wifi
        esp_event
        esp_netif
        esp_partition
        freertos
        log
        json
)
