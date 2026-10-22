#include "moto_nav_ui_test_probe.hpp"

#include <string>

namespace {

int apply_count = 0;
int home_error_count = 0;
moto_ui_state_t applied_state{};
std::string applied_road_name;
std::string applied_next_road_name;
moto_ui_theme_t applied_theme = MOTO_UI_THEME_OFFICIAL;
int theme_apply_count = 0;
uint8_t applied_ambient_night = 0;
uint8_t applied_show_buildings = 0;
int buildings_apply_count = 0;

}  // namespace

extern "C" void moto_nav_ui_create(void) {}
extern "C" void moto_nav_ui_show_boot_screen(void) {}

extern "C" void moto_nav_ui_set_state(const moto_ui_state_t* state) {
  if (state == nullptr) {
    return;
  }
  ++apply_count;
  applied_state = *state;
  applied_road_name = state->road_name == nullptr ? "" : state->road_name;
  applied_next_road_name =
      state->next_road_name == nullptr ? "" : state->next_road_name;
  applied_state.road_name =
      state->road_name == nullptr ? nullptr : applied_road_name.c_str();
  applied_state.next_road_name = state->next_road_name == nullptr
                                     ? nullptr
                                     : applied_next_road_name.c_str();
}

extern "C" void moto_nav_ui_set_motion_state(const moto_ui_state_t*) {}
extern "C" void moto_nav_ui_set_phone_connection(
    moto_ui_phone_connection_t) {}

extern "C" void moto_nav_ui_set_theme(moto_ui_theme_t theme) {
  applied_theme = theme;
  ++theme_apply_count;
}
extern "C" void moto_nav_ui_set_ambient_night(uint8_t is_night) {
  applied_ambient_night = is_night;
}
extern "C" void moto_nav_ui_set_show_buildings(uint8_t visible) {
  applied_show_buildings = visible;
  ++buildings_apply_count;
}
extern "C" void moto_nav_ui_set_reduce_motion(uint8_t) {}
extern "C" void moto_nav_ui_set_page(moto_ui_page_t) {}
extern "C" moto_ui_page_t moto_nav_ui_get_page(void) {
  return MOTO_UI_PAGE_NAVIGATION;
}
extern "C" void moto_nav_ui_set_page_change_callback(
    moto_page_change_callback_t,
    void*) {}
extern "C" void moto_nav_ui_set_music_state(const moto_music_state_t*) {}
extern "C" void moto_nav_ui_set_music_page_enabled(uint8_t) {}
extern "C" void moto_nav_ui_set_music_command_callback(
    moto_music_command_callback_t,
    void*) {}
extern "C" void moto_nav_ui_set_demo_active(uint8_t) {}
extern "C" void moto_nav_ui_set_demo_change_callback(
    moto_demo_change_callback_t,
    void*) {}
extern "C" void moto_nav_ui_set_home_callback(
    moto_home_shortcut_callback_t,
    void*) {}
extern "C" void moto_nav_ui_show_home_error(void) { ++home_error_count; }

namespace moto::test {

void reset_nav_ui_probe() {
  apply_count = 0;
  home_error_count = 0;
  applied_state = {};
  applied_road_name.clear();
  applied_next_road_name.clear();
  applied_theme = MOTO_UI_THEME_OFFICIAL;
  theme_apply_count = 0;
  applied_ambient_night = 0;
  applied_show_buildings = 0;
  buildings_apply_count = 0;
}

int nav_ui_apply_count() { return apply_count; }

int nav_ui_home_error_count() { return home_error_count; }

moto_ui_theme_t last_nav_ui_theme() { return applied_theme; }
int nav_ui_theme_apply_count() { return theme_apply_count; }
uint8_t last_nav_ui_ambient_night() { return applied_ambient_night; }
uint8_t last_nav_ui_show_buildings() { return applied_show_buildings; }
int nav_ui_buildings_apply_count() { return buildings_apply_count; }

const moto_ui_state_t& last_applied_nav_ui_state() {
  return applied_state;
}

}  // namespace moto::test
