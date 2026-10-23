if(NOT TARGET lvgl)
    add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/lvgl ${CMAKE_BINARY_DIR}/third_party/lvgl)
endif()

if(EMSCRIPTEN)
    # The SDL headers are supplied by Emscripten only when the port is enabled
    # for every translation unit that includes the LVGL SDL driver.
    target_compile_options(lvgl PUBLIC "-sUSE_SDL=2")
endif()

# Every generated face is a source. Listing them by hand is what broke the
# xtensa link three times (attitude_estimator.cpp, device_settings.cpp, the
# display faces), so the directory is globbed here and in the ESP-IDF
# component. Re-run cmake after generating a new face.
file(GLOB MOTO_NAV_UI_FONT_SOURCES "${CMAKE_CURRENT_LIST_DIR}/assets/*.c")

add_library(moto_nav_ui STATIC
    ${MOTO_NAV_UI_FONT_SOURCES}
    src/moto_nav_ui.cpp
)

target_include_directories(moto_nav_ui PUBLIC include)
target_link_libraries(moto_nav_ui PUBLIC lvgl)
# The 40 px CJK face has a bitmap larger than 1 MB, which needs 20+ bit glyph
# offsets. The flag changes the glyph descriptor layout, so it must reach every
# translation unit that includes lvgl.h, not just this target.
target_compile_definitions(lvgl PUBLIC LV_FONT_FMT_TXT_LARGE=1)
target_compile_features(moto_nav_ui PUBLIC cxx_std_17)
target_compile_options(moto_nav_ui PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
    $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)
