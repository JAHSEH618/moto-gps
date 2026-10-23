#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "attitude_estimator.hpp"
#include "device_settings.hpp"
#include "moto/ble_protocol/ble_protocol.hpp"
#include "moto_nav_presenter.hpp"
#include "motion_heading_fusion.hpp"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

class PhoneNavBridge {
 public:
  using SendCallback = bool (*)(const moto::ble::Message& message,
                                std::uint8_t frame_flags,
                                void* context);

  explicit PhoneNavBridge(moto::nav::NavPresenter& presenter);

  void set_sender(SendCallback callback, void* context) noexcept;
  void install_ui_callbacks();
  // Starts the sole background owner of NavPresenter and all LVGL writes.
  // BLE/IMU callbacks only retain state and signal this low-priority task.
  bool start_renderer();
#ifdef ESP_PLATFORM
  // For the `sys tasks` bench command. Null before start_renderer().
  TaskHandle_t render_task_handle() const {
    return render_task_handle_.load(std::memory_order_acquire);
  }
#endif
#ifndef ESP_PLATFORM
  // Deterministic pump for native tests. Production firmware always uses the
  // task created by start_renderer().
  void render_pending_for_test();
#endif
  void on_link_state(bool active);
  // Persists an applied settings set. The bridge never touches NVS itself:
  // the firmware installs moto::device::save_settings, and the host tests
  // install a recorder so they can assert that a wire change was stored.
  using SettingsStoreCallback =
      bool (*)(const moto::device::DeviceSettings& settings);
  void set_settings_store(SettingsStoreCallback callback);
  // Applies settings the device already owns (boot replay). Must run on the
  // task that holds the LVGL lock, after moto_nav_ui_create(), because a tree
  // reset clears the theme and the building visibility.
  void apply_settings(const moto::device::DeviceSettings& settings,
                      bool ambient_night);
  // Echoes the applied settings with the revision that was applied. Public so
  // the host tests can assert the echo without a second bridge instance.
  void send_settings_state();
  void on_imu_sample(
      float heading_rate_dps,
      const moto::motion::AttitudeEstimate& attitude,
      std::uint64_t sample_ms);
  void update_demo(std::uint64_t now_ms);
  moto::ble::AckStatus on_message(
      const moto::ble::ReassembledMessage& message);

 private:
  struct GeometryAssembly {
    bool active = false;
    bool complete = false;
    std::uint32_t route_token = 0;
    std::uint32_t route_generation = 0;
    std::uint16_t chunk_count = 0;
    std::uint16_t next_chunk = 0;
    std::uint16_t total_point_count = 0;
    std::uint16_t point_count = 0;
    moto::ble::GeoPointE6 origin_e6{};
    moto::nav::Gcj02Point origin{};
    std::array<moto::nav::Gcj02Point,
               moto::nav::kRouteViewPointCapacity>
        points{};
  };

  static void page_changed(moto_ui_page_t page, void* context);
  static void music_command(moto_music_command_t command, void* context);
  static void demo_changed(uint8_t enabled, void* context);
  static void home_shortcut(void* context);

  void consume_navigation(const moto::ble::NavigationSnapshot& input);
  moto::ble::AckStatus consume_geometry(
      const moto::ble::RouteGeometry& input);
  void consume_traffic(const moto::ble::TrafficDeviation& input);
  void consume_media(const moto::ble::MediaState& input);
  moto::ble::AckStatus consume_map_scene(const moto::ble::MapScene& input);
  void consume_home_location(const moto::ble::HomeLocation& input);
  moto::ble::AckStatus consume_settings(
      const moto::ble::SettingsState& input);
  // Requires state_mutex_. This helper never calls LVGL or the BLE sender.
  void apply_geometry_locked(moto::nav::NavSnapshot& target);
  moto::nav::NavSnapshot& phone_snapshot_locked() noexcept;
  void present_navigation();
  void present_motion();
  void request_render(std::uint32_t flags) noexcept;
  void render_pending();
#ifdef ESP_PLATFORM
  static void render_task(void* context);
  void run_renderer();
#endif
  void send_page_command(moto_ui_page_t page);
  void send_music_command(moto_music_command_t command);
  // Returns the command id written to the wire, or 0 when nothing was sent.
  std::uint16_t send_device_command(moto::ble::DeviceCommand command);
  // The device asked for a route home. Public so the host tests can exercise
  // the send path without synthesising an LVGL tap.
  void home_command();
  void set_demo_active(bool active);
  // Fill an existing member in place. NavSnapshot contains the bounded map
  // window and is intentionally several kilobytes; returning one by value can
  // overflow the small FreeRTOS demo-task stack as road density grows.
  static void fill_demo_snapshot(moto::nav::NavSnapshot& output,
                                 std::uint64_t elapsed_ms);

  moto::nav::NavPresenter& presenter_;
  moto::nav::NavSnapshot snapshot_{};
  // Newest accepted HomeLocation revision for this session.
  std::uint32_t home_revision_ = 0;
  // Newest applied settings revision. Deliberately not reset on link loss or
  // reboot: the device is the authority, and replaying an older revision would
  // resurrect a theme the rider already left.
  moto::device::DeviceSettings settings_{};
  std::uint32_t settings_revision_ = 0;
  bool ambient_night_ = false;
  SettingsStoreCallback settings_store_ = nullptr;
  // Command id of the last HomeShortcut. The phone answers InvalidState when no
  // home is set; the device cannot know that on its own, so the refusal has to
  // come back as a visible hint instead of a silent no-op.
  std::uint16_t pending_home_command_id_ = 0;
  // Only the render task writes these retained copies. They let state_mutex_
  // be released before waiting for LVGL or running the relatively expensive
  // road/building projection.
  moto::nav::NavSnapshot render_snapshot_{};
  GeometryAssembly geometry_{};
  SendCallback sender_ = nullptr;
  void* sender_context_ = nullptr;
  // BLE decoding runs on moto_ble_rx while touch callbacks run on the LVGL
  // worker. Protect every access to the shared navigation/session/command
  // state, and release this mutex before calling LVGL or the BLE sender.
  mutable std::mutex state_mutex_;
  std::uint32_t phone_session_id_ = 0;
  std::uint16_t next_command_id_ = 1;
  bool link_active_ = false;
  moto_ui_phone_connection_t ui_phone_connection_ = MOTO_UI_PHONE_OFFLINE;
  moto::ble::MediaState media_state_{};
  moto::ble::MediaState render_media_state_{};
  moto_ui_phone_connection_t render_phone_connection_ =
      MOTO_UI_PHONE_OFFLINE;
  bool render_demo_active_ = false;
  moto::device::DeviceSettings render_settings_{};
  bool render_ambient_night_ = false;
  enum RenderFlag : std::uint32_t {
    RenderNavigation = 1U << 0U,
    RenderMotion = 1U << 1U,
    RenderMedia = 1U << 2U,
    RenderHomeError = 1U << 3U,
    RenderDisplay = 1U << 4U,
  };
  std::atomic<std::uint32_t> pending_render_flags_{0};
#ifdef ESP_PLATFORM
  std::atomic<TaskHandle_t> render_task_handle_{nullptr};
#endif
  MotionHeadingFusion heading_fusion_;
  std::uint64_t last_motion_present_ms_ = 0;
  moto::nav::NavSnapshot snapshot_before_demo_{};
  std::uint64_t demo_started_ms_ = 0;
  bool demo_active_ = false;
};
