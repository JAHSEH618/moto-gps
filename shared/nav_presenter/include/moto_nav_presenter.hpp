#pragma once

#include "moto_nav_ui.h"
#include "nav_core/nav_core.hpp"

#include <string>

namespace moto::nav {

// Deterministic bridge between the shared navigation model and the shared
// LVGL view. The char pointers in ui_state() remain valid until the next
// update() call or destruction of this presenter.
class NavPresenter {
 public:
  NavPresenter();
  NavPresenter(const NavPresenter&) = delete;
  NavPresenter& operator=(const NavPresenter&) = delete;
  NavPresenter(NavPresenter&&) = delete;
  NavPresenter& operator=(NavPresenter&&) = delete;

  void update(const NavSnapshot& snapshot);
  // A motion frame. The caller guarantees that only the heading and the
  // attitude differ from the snapshot of the last update(); the map is
  // re-projected only when the heading actually moved.
  void update_motion(const NavSnapshot& snapshot);
  [[nodiscard]] const moto_ui_state_t& ui_state() const;

  // Applies the last mapped state to the platform-independent LVGL UI API.
  // LVGL initialization remains the responsibility of the platform adapter.
  void apply_to_lvgl() const;

 private:
  void project_map(const NavSnapshot& snapshot);

  std::string road_name_;
  std::string next_road_name_;
  moto_ui_state_t ui_state_{};
  float projected_heading_deg_ = 0.0F;
};

}  // namespace moto::nav
