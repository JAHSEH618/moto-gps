#include "qmi8658_fifo.hpp"

#include <algorithm>
#include <cmath>

#include "esp_log.h"
#include "flash_guard.hpp"
#include "esp_timer.h"

namespace moto::motion {
namespace {

constexpr char kTag[] = "qmi_fifo";
constexpr std::uint8_t kCtrl1 = 0x02;
constexpr std::uint8_t kCtrl2 = 0x03;
constexpr std::uint8_t kCtrl3 = 0x04;
constexpr std::uint8_t kCtrl5 = 0x06;
constexpr std::uint8_t kCtrl7 = 0x08;
constexpr std::uint8_t kCtrl8 = 0x09;
constexpr std::uint8_t kCtrl9 = 0x0A;
constexpr std::uint8_t kFifoWtm = 0x13;
constexpr std::uint8_t kFifoCtrl = 0x14;
constexpr std::uint8_t kFifoCount = 0x15;
constexpr std::uint8_t kFifoStatus = 0x16;
constexpr std::uint8_t kFifoData = 0x17;
constexpr std::uint8_t kStatusInt = 0x2D;
constexpr std::uint8_t kTimestampLow = 0x30;

constexpr std::uint8_t kCmdAck = 0x00;
constexpr std::uint8_t kCmdReqFifo = 0x05;
constexpr std::uint8_t kFifoStream128 = 0x0E;  // Stream, 128 samples, RD_MODE=0
constexpr float kAccelLsbPerG = 4096.0F;       // +-8 g
constexpr float kGyroLsbPerDps = 64.0F;        // +-512 dps
constexpr float kGravity = 9.80665F;
constexpr std::uint32_t kHandshakeTimeoutUs = 3000;
constexpr std::uint32_t kWindowLimitUs = 2000;

std::uint16_t decode16(const std::uint8_t* data, bool big_endian) {
  return big_endian
             ? static_cast<std::uint16_t>(
                   (static_cast<std::uint16_t>(data[0]) << 8U) | data[1])
             : static_cast<std::uint16_t>(
                   (static_cast<std::uint16_t>(data[1]) << 8U) | data[0]);
}

std::int16_t to_signed(std::uint16_t value) {
  return static_cast<std::int16_t>(value);
}

}  // namespace

bool Qmi8658Fifo::read_register(std::uint8_t reg, std::uint8_t* out,
                               std::size_t len) {
  return qmi8658_read_register(device_, reg, out, static_cast<std::uint8_t>(len)) ==
         ESP_OK;
}

bool Qmi8658Fifo::write_register(std::uint8_t reg, std::uint8_t value) {
  return qmi8658_write_register(device_, reg, value) == ESP_OK;
}

bool Qmi8658Fifo::control9(std::uint8_t command) {
  if (!write_register(kCtrl9, command)) return false;
  const std::int64_t deadline = esp_timer_get_time() + kHandshakeTimeoutUs;
  while (esp_timer_get_time() < deadline) {
    std::uint8_t status = 0;
    if (read_register(kStatusInt, &status, 1) &&
        (status & 0x80U) != 0U) {
      return write_register(kCtrl9, kCmdAck);
    }
  }
  return false;
}

void Qmi8658Fifo::fall_back(const char* reason) {
  if (active_ || fallback_reason_ == nullptr) {
    fallback_reason_ = reason;
  }
  active_ = false;
  stats_.active = false;
  ESP_LOGW(kTag, "FIFO disabled: %s", reason);
}

void Qmi8658Fifo::shutdown() { active_ = false; }

bool Qmi8658Fifo::begin(qmi8658_dev_t* device) {
  device_ = device;
  if (device_ == nullptr) return false;
  // CTRL7: synchronous sampling must be off; enable accel + gyro.
  if (!write_register(kCtrl7, 0x03)) {
    fall_back("CTRL7 write failed");
    return false;
  }
  // CTRL1: address auto-increment, big-endian field preserved, INT2 enabled,
  // FIFO interrupt routed to INT2.
  if (!write_register(kCtrl1, 0x70)) {
    fall_back("CTRL1 write failed");
    return false;
  }
  // CTRL2/3: +-8 g, +-512 dps, 448.4 Hz in 6DOF (ODR code 0100).
  if (!write_register(kCtrl2, 0x24) || !write_register(kCtrl3, 0x54)) {
    fall_back("ODR/range write failed");
    return false;
  }
  // CTRL5: both LPFs on, mode 10 (~5.4% of ODR). Bit positions per Rev D;
  // read back and log so the bench can confirm the layout.
  if (!write_register(kCtrl5, 0x99)) {
    fall_back("CTRL5 write failed");
    return false;
  }
  std::uint8_t readback = 0;
  read_register(kCtrl5, &readback, 1);
  ESP_LOGI(kTag, "CTRL5=0x%02x (expect 0x99)", readback);
  // CTRL8: CTRL9 handshake via STATUSINT polling (INT1 sits behind the
  // expander and is too slow).
  if (!write_register(kCtrl8, 0x80)) {
    fall_back("CTRL8 write failed");
    return false;
  }
  if (!control9(0x04)) {  // reset FIFO
    fall_back("FIFO reset handshake timed out");
    return false;
  }
  if (!write_register(kFifoWtm, watermark_) ||
      !write_register(kFifoCtrl, kFifoStream128)) {
    fall_back("FIFO config write failed");
    return false;
  }
  active_ = true;
  stats_ = FifoStats{};
  stats_.active = true;
  have_timestamp_ = false;
  endian_checked_ = false;
  consecutive_loss_ = 0;
  slow_windows_ = 0;
  watermark_reduced_ = false;
  ESP_LOGI(kTag, "FIFO stream/128, watermark %u", watermark_);
  return true;
}

void Qmi8658Fifo::set_watermark(std::uint8_t samples) {
  watermark_ = std::clamp<std::uint8_t>(samples, 1, 64);
  if (active_) write_register(kFifoWtm, watermark_);
}

bool Qmi8658Fifo::validate_endianness(const std::uint8_t* raw,
                                      std::size_t frames) {
  // A stationary board reads ~1 g on its gravity axis whatever the mounting.
  // Try both byte orders and accept the one whose dominant axis is near 4096.
  const auto plausible = [&](bool big_endian) {
    float best = 0.0F;
    for (std::size_t frame = 0; frame < frames; ++frame) {
      const std::uint8_t* p = raw + frame * 12;
      for (int axis = 0; axis < 3; ++axis) {
        const float lsb =
            std::fabs(static_cast<float>(to_signed(
                decode16(p + axis * 2, big_endian))));
        best = std::max(best, lsb);
      }
    }
    return best > 0.7F * kAccelLsbPerG && best < 1.3F * kAccelLsbPerG;
  };
  if (plausible(true)) {
    big_endian_ = true;
    return true;
  }
  if (plausible(false)) {
    big_endian_ = false;
    ESP_LOGW(kTag, "FIFO data is little-endian (BE field off)");
    return true;
  }
  return false;
}

std::size_t Qmi8658Fifo::read_batch(FifoFrame* frames, std::size_t capacity) {
  std::uint8_t count = 0;
  std::uint8_t status = 0;
  if (!read_register(kFifoCount, &count, 1) ||
      !read_register(kFifoStatus, &status, 1)) {
    return 0;
  }
  if ((status & 0x20U) != 0U) ++stats_.overflow;
  const std::uint32_t words =
      (static_cast<std::uint32_t>(status & 0x03U) << 8U) | count;
  const std::uint32_t bytes = words * 2U;
  if (bytes == 0) return 0;
  if (bytes > capacity * 12U) {
    fall_back("FIFO batch larger than the decode buffer");
    return 0;
  }

  // Read the sample counter before the batch to reconstruct produced samples.
  std::uint8_t ts[3] = {};
  std::uint32_t timestamp = 0;
  if (read_register(kTimestampLow, ts, 3)) {
    timestamp = static_cast<std::uint32_t>(ts[0]) |
                (static_cast<std::uint32_t>(ts[1]) << 8U) |
                (static_cast<std::uint32_t>(ts[2]) << 16U);
  }

  // The whole read-mode window must finish inside one ODR period. The caller
  // (sensor task) already holds flash_guard around this call.
  if (!moto::device::flash_guard_take(200)) return 0;
  const std::int64_t started = esp_timer_get_time();
  if (!control9(kCmdReqFifo)) {
    moto::device::flash_guard_give();
    fall_back("REQ_FIFO handshake timed out");
    return 0;
  }
  std::uint8_t raw[64 * 12];
  // The vendor helper takes a uint8_t length, so a batch larger than 255 B is
  // read in chunks. Bytes beyond the decode buffer are still consumed, because
  // leaving them in the FIFO would desynchronise the stream.
  std::size_t remaining = bytes;
  std::size_t copied = 0;
  bool read_ok = true;
  while (remaining > 0) {
    const std::size_t chunk = std::min<std::size_t>(remaining, 240);
    std::uint8_t scratch[240];
    std::uint8_t* destination = scratch;
    if (copied < sizeof(raw)) {
      destination = raw + copied;
    }
    if (!read_register(kFifoData, destination,
                       static_cast<std::uint8_t>(chunk))) {
      read_ok = false;
      break;
    }
    if (copied < sizeof(raw)) {
      copied = std::min<std::size_t>(sizeof(raw), copied + chunk);
    }
    remaining -= chunk;
  }
  write_register(kFifoCtrl, kFifoStream128);  // leave read mode
  const std::int64_t window_us = esp_timer_get_time() - started;
  moto::device::flash_guard_give();
  stats_.window_samples++;
  stats_.window_us_sum += static_cast<std::uint64_t>(window_us);
  stats_.window_us_max =
      std::max<std::uint32_t>(stats_.window_us_max,
                              static_cast<std::uint32_t>(window_us));
  if (!read_ok) {
    fall_back("FIFO data read failed");
    return 0;
  }
  const std::size_t frame_count = std::min<std::size_t>(bytes, sizeof(raw)) / 12U;
  if (!endian_checked_) {
    if (!validate_endianness(raw, frame_count)) {
      fall_back("FIFO byte order validation failed");
      return 0;
    }
    endian_checked_ = true;
  }

  const std::size_t n = std::min<std::size_t>(frame_count, capacity);
  for (std::size_t frame = 0; frame < n; ++frame) {
    const std::uint8_t* p = raw + frame * 12;
    for (int axis = 0; axis < 3; ++axis) {
      const float accel_lsb =
          static_cast<float>(to_signed(decode16(p + axis * 2, big_endian_)));
      const float gyro_lsb = static_cast<float>(
          to_signed(decode16(p + 6 + axis * 2, big_endian_)));
      frames[frame].accel_mps2[axis] =
          accel_lsb / kAccelLsbPerG * kGravity;
      frames[frame].gyro_dps[axis] = gyro_lsb / kGyroLsbPerDps;
    }
  }
  ++stats_.batches;
  stats_.received += static_cast<std::uint32_t>(n);
  if (have_timestamp_) {
    const std::uint32_t produced = (timestamp - last_timestamp_) & 0xFFFFFFU;
    stats_.produced += produced;
    if (produced > n) {
      stats_.lost += produced - static_cast<std::uint32_t>(n);
      ++consecutive_loss_;
    } else {
      consecutive_loss_ = 0;
    }
  }
  last_timestamp_ = timestamp;
  have_timestamp_ = true;

  if (window_us > static_cast<std::int64_t>(kWindowLimitUs)) {
    ++slow_windows_;
    if (slow_windows_ >= 3 && !watermark_reduced_) {
      set_watermark(3);
      watermark_reduced_ = true;
      slow_windows_ = 0;
      ESP_LOGW(kTag, "read window > 2.0 ms; watermark reduced to 3");
    } else if (slow_windows_ >= 3 && watermark_reduced_) {
      fall_back("read window stayed above 2.0 ms after lowering watermark");
      return n;
    }
  } else {
    slow_windows_ = 0;
  }
  if (consecutive_loss_ >= 3) {
    fall_back("three consecutive batches lost samples");
  }
  return n;
}

std::size_t Qmi8658Fifo::poll(FifoFrame* frames, std::size_t capacity) {
  if (!active_ || frames == nullptr || capacity == 0) return 0;
  std::uint8_t status = 0;
  if (!read_register(kFifoStatus, &status, 1)) return 0;
  const bool ready = (status & 0x40U) != 0U || (status & 0x80U) != 0U ||
                     (status & 0x20U) != 0U;
  if (!ready) return 0;
  return read_batch(frames, capacity);
}

}  // namespace moto::motion
