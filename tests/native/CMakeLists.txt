cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(moto_nav_native_tests LANGUAGES CXX)
endif()

enable_testing()

if(NOT TARGET moto_nav_core)
  add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../../shared/nav_core"
    "${CMAKE_CURRENT_BINARY_DIR}/nav_core"
  )
endif()

if(NOT TARGET moto_coordinates)
  add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../../shared/coordinates"
    "${CMAKE_CURRENT_BINARY_DIR}/coordinates"
  )
endif()

if(NOT TARGET moto_nav_app)
  add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../../shared/nav_app"
    "${CMAKE_CURRENT_BINARY_DIR}/nav_app"
  )
endif()

set(MOTO_USING_NAV_UI_TEST_STUB OFF)
if(NOT TARGET moto_nav_ui)
  # Mapping tests do not need an LVGL display. This ABI-compatible stub keeps
  # the standalone native test build small and lets apply_to_lvgl() be probed.
  add_library(moto_nav_ui STATIC moto_nav_ui_stub.cpp)
  target_include_directories(
    moto_nav_ui PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/../../shared/nav_ui/include"
  )
  target_compile_features(moto_nav_ui PUBLIC cxx_std_17)
  set(MOTO_USING_NAV_UI_TEST_STUB ON)
endif()

if(NOT TARGET moto_nav_presenter)
  add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../../shared/nav_presenter"
    "${CMAKE_CURRENT_BINARY_DIR}/nav_presenter"
  )
endif()

if(NOT TARGET moto_ble_protocol)
  add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../../shared/ble_protocol"
    "${CMAKE_CURRENT_BINARY_DIR}/ble_protocol"
  )
endif()

add_executable(nav_core_tests nav_core_tests.cpp)
target_link_libraries(nav_core_tests PRIVATE moto_nav_core moto_coordinates)
target_compile_features(nav_core_tests PRIVATE cxx_std_17)

# The full LVGL target exists in the project build, but not when this test
# directory is configured standalone with its lightweight UI stub.
if(TARGET lvgl)
  # The board does not run on this one pool: board_port_init() adds a 2 MiB
  # PSRAM pool on top of the 64 KiB internal default (see
  # platforms/esp32/main/board_port_waveshare_1_75c.cpp). A suite with a single
  # 8 MiB pool models nothing the firmware does, so the high-water mark it
  # reports says nothing about the device. Run in the board's shape instead.
  set(MOTO_NATIVE_LVGL_HEAP_BYTES "65536" CACHE STRING
      "LVGL internal heap for native render tests; 65536 matches the board")
  # A cached value survives the default changing, and an 8 MiB pool is how the
  # last heap "defect" was measured against a shape the firmware never has.
  if(NOT MOTO_NATIVE_LVGL_HEAP_BYTES STREQUAL "65536")
    message(WARNING
      "MOTO_NATIVE_LVGL_HEAP_BYTES=${MOTO_NATIVE_LVGL_HEAP_BYTES} is not the "
      "board's 64 KiB pool; the render suite no longer models the device")
  endif()
  # LV_USE_TEST compiles lv_test_indev.c, the only way a test can send a real
  # LV_EVENT_GESTURE. It must be PUBLIC: the flag also adds members to
  # lv_global_t, so every translation unit that includes lvgl.h has to agree.
  target_compile_definitions(lvgl PUBLIC LV_USE_TEST=1)
  target_compile_definitions(lvgl PRIVATE
    CONFIG_LV_MEM_SIZE=${MOTO_NATIVE_LVGL_HEAP_BYTES}
    # LV_ASSERT_HANDLER defaults to `while(1);`, which turns a null-object bug
    # into a silent 100% CPU spin that ctest can only report as a timeout.
    # Trap instead, so the failure arrives as a stack in the test log. Only the
    # native build gets this; the firmware keeps the spin on purpose.
    "LV_ASSERT_HANDLER=__builtin_trap()\\;")
  add_executable(nav_ui_render_tests nav_ui_render_tests.cpp)
  target_link_libraries(nav_ui_render_tests PRIVATE moto_nav_ui lvgl)
  target_compile_features(nav_ui_render_tests PRIVATE cxx_std_17)
endif()

add_executable(nav_core_fixture_demo nav_core_fixture_demo.cpp)
target_link_libraries(nav_core_fixture_demo PRIVATE moto_nav_core moto_coordinates)
target_compile_features(nav_core_fixture_demo PRIVATE cxx_std_17)

add_executable(nav_app_tests nav_app_tests.cpp)
target_link_libraries(nav_app_tests PRIVATE moto_nav_app)
target_compile_features(nav_app_tests PRIVATE cxx_std_17)

add_executable(nav_presenter_tests nav_presenter_tests.cpp)
target_link_libraries(nav_presenter_tests PRIVATE moto_nav_presenter)
target_compile_features(nav_presenter_tests PRIVATE cxx_std_17)
if(MOTO_USING_NAV_UI_TEST_STUB)
  target_compile_definitions(nav_presenter_tests PRIVATE
    MOTO_NAV_UI_TEST_STUB=1
  )
endif()

add_executable(ble_protocol_tests ble_protocol_tests.cpp)
target_link_libraries(ble_protocol_tests PRIVATE moto_ble_protocol)
target_compile_features(ble_protocol_tests PRIVATE cxx_std_17)
target_compile_definitions(ble_protocol_tests PRIVATE
  "MOTO_BLE_GOLDEN_FIXTURE=\"${CMAKE_CURRENT_LIST_DIR}/../../shared/protocol/fixtures/ble-navigation-v1.golden.txt\""
)

add_executable(connection_epoch_gate_tests connection_epoch_gate_tests.cpp)
target_include_directories(connection_epoch_gate_tests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main"
)
target_compile_features(connection_epoch_gate_tests PRIVATE cxx_std_17)

add_executable(motion_heading_fusion_tests motion_heading_fusion_tests.cpp)
target_include_directories(motion_heading_fusion_tests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main"
)
target_compile_features(motion_heading_fusion_tests PRIVATE cxx_std_17)

add_executable(device_settings_tests
  device_settings_tests.cpp
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main/device_settings.cpp"
)
target_include_directories(device_settings_tests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main"
)
target_link_libraries(device_settings_tests PRIVATE moto_ble_protocol)
target_compile_features(device_settings_tests PRIVATE cxx_std_17)

add_executable(attitude_estimator_tests
  attitude_estimator_tests.cpp
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main/attitude_estimator.cpp"
)
target_include_directories(attitude_estimator_tests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main"
)
target_compile_features(attitude_estimator_tests PRIVATE cxx_std_17)

add_executable(lean_estimator_tests
  lean_estimator_tests.cpp
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main/lean_estimator.cpp"
)
target_include_directories(lean_estimator_tests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main"
)
target_compile_features(lean_estimator_tests PRIVATE cxx_std_17)

find_package(Threads REQUIRED)
add_executable(phone_nav_bridge_tests
  phone_nav_bridge_tests.cpp
  esp32_host_stubs/phone_nav_bridge_platform_stubs.cpp
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main/phone_nav_bridge.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main/device_settings.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../shared/nav_presenter/src/moto_nav_presenter.cpp"
)
target_include_directories(phone_nav_bridge_tests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/esp32_host_stubs"
  "${CMAKE_CURRENT_LIST_DIR}"
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main"
  "${CMAKE_CURRENT_LIST_DIR}/../../shared/nav_presenter/include"
  "${CMAKE_CURRENT_LIST_DIR}/../../shared/nav_ui/include"
)
target_link_libraries(phone_nav_bridge_tests PRIVATE
  moto_ble_protocol
  moto_nav_core
  Threads::Threads
)
target_compile_features(phone_nav_bridge_tests PRIVATE cxx_std_17)

add_test(NAME nav_core_tests COMMAND nav_core_tests)
if(TARGET nav_ui_render_tests)
  # argv: partial buffer rows, extra pool bytes. The pool matches the board's
  # 2 MiB PSRAM layer, so the suite runs with 64 KiB + 2 MiB like the device.
  add_test(NAME nav_ui_render_tests COMMAND nav_ui_render_tests 40 2097152)
  # A trap is instant, but a deadlock or an accidental infinite loop in a
  # timer is not. Fail in three minutes instead of ctest's 1500 s default.
  set_tests_properties(nav_ui_render_tests PROPERTIES TIMEOUT 180)
endif()
add_test(NAME nav_app_tests COMMAND nav_app_tests)
add_test(NAME nav_presenter_tests COMMAND nav_presenter_tests)
add_test(NAME ble_protocol_tests COMMAND ble_protocol_tests)
add_test(NAME connection_epoch_gate_tests COMMAND connection_epoch_gate_tests)
add_test(NAME motion_heading_fusion_tests COMMAND motion_heading_fusion_tests)
add_test(NAME device_settings_tests COMMAND device_settings_tests)
add_test(NAME attitude_estimator_tests COMMAND attitude_estimator_tests)
add_test(NAME lean_estimator_tests COMMAND lean_estimator_tests)

add_executable(mount_basis_tests
  mount_basis_tests.cpp
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main/mount_basis.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main/lean_estimator.cpp"
)
target_include_directories(mount_basis_tests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main"
)
target_compile_features(mount_basis_tests PRIVATE cxx_std_17)
add_test(NAME mount_basis_tests COMMAND mount_basis_tests)

add_executable(dyn_record_tests
  dyn_record_tests.cpp
  dyn_replay.cpp
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main/dyn_record.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main/lean_estimator.cpp"
)
target_include_directories(dyn_record_tests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main"
  "${CMAKE_CURRENT_LIST_DIR}"
)
target_compile_features(dyn_record_tests PRIVATE cxx_std_17)
add_test(NAME dyn_record_tests COMMAND dyn_record_tests)

add_executable(moto_replay
  moto_replay.cpp
  dyn_replay.cpp
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main/dyn_record.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main/lean_estimator.cpp"
)
target_include_directories(moto_replay PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../platforms/esp32/main"
  "${CMAKE_CURRENT_LIST_DIR}"
)
target_compile_features(moto_replay PRIVATE cxx_std_17)
add_test(NAME phone_nav_bridge_tests COMMAND phone_nav_bridge_tests)
