#pragma once

#include "moto_nav_ui.h"

namespace moto::test {

void reset_nav_ui_probe();
int nav_ui_apply_count();
int nav_ui_home_error_count();
const moto_ui_state_t& last_applied_nav_ui_state();
moto_ui_theme_t last_nav_ui_theme();
int nav_ui_theme_apply_count();
uint8_t last_nav_ui_ambient_night();
uint8_t last_nav_ui_show_buildings();
int nav_ui_buildings_apply_count();

}  // namespace moto::test
