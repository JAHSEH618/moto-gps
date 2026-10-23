#pragma once

#include <string>

#include "moto_nav_ui.h"

namespace moto::test {

void reset_phone_nav_bridge_probe();
void emit_page_change(moto_ui_page_t page);
void emit_music_command(moto_music_command_t command);
void emit_demo_change(bool enabled);
void emit_home_shortcut();
int phone_nav_bridge_apply_count();
int phone_nav_bridge_home_error_count();
int phone_nav_bridge_board_lock_count();
void phone_nav_bridge_set_board_lock_available(bool available);
std::string phone_nav_bridge_last_road_name();
uint16_t phone_nav_bridge_last_heading_deg();
uint8_t phone_nav_bridge_last_has_attitude();
uint8_t phone_nav_bridge_last_has_home();
int16_t phone_nav_bridge_last_home_x();
int16_t phone_nav_bridge_last_home_y();
float phone_nav_bridge_last_lean_deg();
float phone_nav_bridge_last_pitch_deg();
float phone_nav_bridge_last_longitudinal_g();
float phone_nav_bridge_last_lateral_g();
uint8_t phone_nav_bridge_last_route_point_count();
uint8_t phone_nav_bridge_last_road_point_count();
uint8_t phone_nav_bridge_last_road_polyline_count();
uint8_t phone_nav_bridge_last_road_class();
uint8_t phone_nav_bridge_last_building_point_count();
uint8_t phone_nav_bridge_last_building_footprint_count();
uint8_t phone_nav_bridge_last_building_class();
moto_ui_phone_connection_t phone_nav_bridge_last_phone_connection();
bool phone_nav_bridge_media_connected();
bool phone_nav_bridge_media_playing();
bool phone_nav_bridge_media_like_available();
std::string phone_nav_bridge_media_source();
std::string phone_nav_bridge_media_title();
std::string phone_nav_bridge_media_artist();
bool phone_nav_bridge_last_demo_active();
moto_maneuver_t phone_nav_bridge_last_maneuver();
uint32_t phone_nav_bridge_last_distance_to_maneuver_m();
moto_ui_theme_t phone_nav_bridge_last_theme();
int phone_nav_bridge_theme_apply_count();
uint8_t phone_nav_bridge_last_ambient_night();
uint8_t phone_nav_bridge_last_show_buildings();
int phone_nav_bridge_buildings_apply_count();

}  // namespace moto::test
