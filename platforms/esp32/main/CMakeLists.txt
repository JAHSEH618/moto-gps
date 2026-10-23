set(MOTO_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(MOTO_SHARED_UI_SOURCE
    "${MOTO_REPOSITORY_ROOT}/shared/nav_ui/src/moto_nav_ui.cpp")
# Every generated font is a source in its own right, and listing them one by
# one is how the xtensa link lost attitude_estimator.cpp, device_settings.cpp
# and the 2026-10-23 display faces. Glob the directory instead: a new face
# lands in the firmware the moment it is generated.
file(GLOB MOTO_SHARED_UI_FONTS
    "${MOTO_REPOSITORY_ROOT}/shared/nav_ui/assets/*.c")
# Adding a face needs one reconfigure (`idf.py reconfigure`) because the glob
# is evaluated at configure time; it still cannot be forgotten the way an
# explicit file list can.
set(MOTO_SHARED_UI_INCLUDE
    "${MOTO_REPOSITORY_ROOT}/shared/nav_ui/include")
set(MOTO_SHARED_NAV_PRESENTER_SOURCE
    "${MOTO_REPOSITORY_ROOT}/shared/nav_presenter/src/moto_nav_presenter.cpp")
set(MOTO_SHARED_BLE_PROTOCOL_SOURCE
    "${MOTO_REPOSITORY_ROOT}/shared/ble_protocol/src/ble_protocol.cpp")

# The shared UI source is compiled directly into this ESP-IDF component. Do not
# copy it into platforms/esp32: Web and firmware must execute the same code.
idf_component_register(
    SRCS
        "app_main.cpp"
        "attitude_estimator.cpp"
        "ble_nav_transport_nimble.cpp"
        "board_port_waveshare_1_75c.cpp"
        "device_settings.cpp"
        "motion_heading_sensor.cpp"
        "phone_nav_bridge.cpp"
        "settings_store.cpp"
        "${MOTO_SHARED_BLE_PROTOCOL_SOURCE}"
        "${MOTO_SHARED_NAV_PRESENTER_SOURCE}"
        ${MOTO_SHARED_UI_FONTS}
        "${MOTO_SHARED_UI_SOURCE}"
    INCLUDE_DIRS
        "."
        "${MOTO_REPOSITORY_ROOT}/shared"
        "${MOTO_REPOSITORY_ROOT}/shared/ble_protocol/include"
        "${MOTO_REPOSITORY_ROOT}/shared/nav_presenter/include"
        "${MOTO_SHARED_UI_INCLUDE}"
    REQUIRES
        esp_timer
        esp_driver_gpio
        esp_driver_i2c
        esp_hw_support
        freertos
        log
        lvgl
        esp_lcd_co5300
        nvs_flash
        bt
        esp32_s3_touch_amoled_1_75c
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)
