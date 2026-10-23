#include "tlog_logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "esp_log.h"
#include "esp_partition.h"
#include "flash_guard.hpp"
#include "settings_store.h"

namespace moto::device {
namespace {

constexpr char kTag[] = "tlog";
constexpr std::uint32_t kSectorSize = 4096;
constexpr std::uint32_t kFlushIntervalMs = 2000;
// A telemetry partition is data subtype 0x40; there is no ESP-IDF subtype for
// a custom log ring, and reusing spiffs/nvs would mislead every tool that
// enumerates partitions.
constexpr esp_partition_subtype_t kTlogSubtype =
    static_cast<esp_partition_subtype_t>(0x40);

std::int16_t to_i16(std::int64_t value) {
  return static_cast<std::int16_t>(std::clamp<std::int64_t>(
      value, -32768, 32767));
}

}  // namespace

bool TlogLogger::ensure_partition() {
  const esp_partition_t* found = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, kTlogSubtype, "tlog");
  if (found == nullptr) {
    ESP_LOGE(kTag, "no tlog partition; telemetry recording disabled");
    return false;
  }
  partition_ = found;
  capacity_ = found->size;
  if (capacity_ <= kSectorSize) return false;
  return true;
}

bool TlogLogger::erase_sector(std::uint32_t offset) {
  if (offset % kSectorSize != 0) return false;
  if (offset + kSectorSize > capacity_) return false;
  if (!flash_guard_take(500)) return false;
  const esp_err_t result = esp_partition_erase_range(
      static_cast<const esp_partition_t*>(partition_), offset, kSectorSize);
  flash_guard_give();
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "erase at 0x%x failed: %s", static_cast<unsigned>(offset),
             esp_err_to_name(result));
    return false;
  }
  return true;
}

bool TlogLogger::write_at(std::uint32_t offset, const std::uint8_t* data,
                          std::size_t size) {
  if (offset + size > capacity_) return false;
  if (!flash_guard_take(500)) {
    ESP_LOGW(kTag, "flash guard timeout");
    return false;
  }
  const esp_err_t result = esp_partition_write(
      static_cast<const esp_partition_t*>(partition_), offset, data, size);
  flash_guard_give();
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "write at 0x%x failed: %s", static_cast<unsigned>(offset),
             esp_err_to_name(result));
    return false;
  }
  return true;
}

// The 1.75C has 32 MB of flash and the tlog partition starts at 16 MiB. Access
// above the first 16 MiB is the one thing that cannot be assumed from the
// datasheet, so prove erase/write/read once before trusting the region.
bool TlogLogger::run_selftest() {
  if (tlog_selftest_done()) return true;
  const std::uint32_t offset =
      ((capacity_ - kSectorSize) / kSectorSize) * kSectorSize;
  if (!erase_sector(offset)) return false;
  std::uint8_t pattern[kSectorSize];
  for (std::size_t i = 0; i < sizeof(pattern); ++i) {
    pattern[i] = static_cast<std::uint8_t>((i * 31U + 7U) & 0xFFU);
  }
  if (!write_at(offset, pattern, sizeof(pattern))) return false;
  std::uint8_t readback[kSectorSize];
  const esp_err_t result = esp_partition_read(
      static_cast<const esp_partition_t*>(partition_), offset, readback,
      sizeof(readback));
  if (result != ESP_OK ||
      std::memcmp(pattern, readback, sizeof(pattern)) != 0) {
    ESP_LOGE(kTag, "above-16-MB self-test failed at 0x%x",
             static_cast<unsigned>(offset));
    return false;
  }
  if (!erase_sector(offset)) return false;
  mark_tlog_selftest_done();
  ESP_LOGI(kTag, "above-16-MB erase/write/read self-test passed");
  return true;
}

bool TlogLogger::begin() {
  flash_guard_init();
  if (!ensure_partition()) return false;
  if (!run_selftest()) return false;

  std::uint32_t stored = 0;
  bool resumable = false;
  if (load_tlog_pos(&stored) && stored >= kTelemetryHeaderSize &&
      stored < capacity_) {
    std::uint8_t head[kTelemetryHeaderSize];
    TelemetryHeader header;
    resumable = esp_partition_read(
                    static_cast<const esp_partition_t*>(partition_), 0, head,
                    sizeof(head)) == ESP_OK &&
                decode_header(head, sizeof(head), &header);
    if (resumable) position_ = stored;
  }

  if (!resumable) {
    if (!erase_sector(0)) return false;
    TelemetryHeader header;
    header.session_epoch = 0;
    header.start_ms = 0;
    header.sample_hz = 125;  // polling path until the FIFO slice lands
    header.flags = 0x0001;   // development build
    const auto encoded = encode_header(header);
    if (!write_at(0, encoded.data(), encoded.size())) return false;
    position_ = kTelemetryHeaderSize;
    save_tlog_pos(position_);
  }

  ready_ = true;
  last_flush_ms_ = 0;
  ESP_LOGI(kTag, "recording to tlog at 0x%x of 0x%x",
           static_cast<unsigned>(position_), static_cast<unsigned>(capacity_));
  return true;
}

void TlogLogger::append(std::uint64_t now_ms, const RawImuSample& raw,
                        const moto::motion::AttitudeEstimate& attitude) {
  if (!ready_ || full_) return;
  if (now_ms - last_flush_ms_ >= kFlushIntervalMs) {
    flush();
    last_flush_ms_ = now_ms;
  }
  if (used_ + kDynRecordSize > kSectorSize) flush();
  if (position_ + used_ + kDynRecordSize > capacity_) {
    full_ = true;
    flush();
    ESP_LOGW(kTag, "telemetry partition full after %u records",
             static_cast<unsigned>(seq_));
    return;
  }
  // Entering a fresh sector: it must be erased before the first write, and the
  // sector holding the header is already erased.
  if (used_ == 0 && position_ % kSectorSize == 0 && position_ > 0) {
    if (!erase_sector(position_)) {
      ready_ = false;
      return;
    }
  }

  DynRecord record;
  record.t_ms = static_cast<std::uint32_t>(now_ms);
  record.seq = seq_++;
  record.ax_cms2 = to_i16(std::lround(raw.accel_mps2[0] * 100.0F));
  record.ay_cms2 = to_i16(std::lround(raw.accel_mps2[1] * 100.0F));
  record.az_cms2 = to_i16(std::lround(raw.accel_mps2[2] * 100.0F));
  record.gx_dds = to_i16(std::lround(raw.gyro_dps[0] * 10.0F));
  record.gy_dds = to_i16(std::lround(raw.gyro_dps[1] * 10.0F));
  record.gz_dds = to_i16(std::lround(raw.gyro_dps[2] * 10.0F));
  record.temp_cdeg = to_i16(std::lround(raw.temp_c * 100.0F));
  record.lean_cdeg = to_i16(std::lround(attitude.lean_deg * 100.0F));
  record.lean_rate_dds =
      to_i16(std::lround(attitude.lean_rate_dps * 10.0F));
  record.lean_eff_cdeg =
      attitude.has_lean_eff ? to_i16(std::lround(attitude.lean_eff_deg * 100.0F))
                            : std::int16_t{-32768};
  record.lateral_g_cg = to_i16(std::lround(attitude.lateral_g * 100.0F));
  record.flags = static_cast<std::uint8_t>(attitude.flags & 0xFFU);
  record.quality = attitude.quality;

  const auto encoded = encode_dyn(record);
  std::memcpy(buffer_ + used_, encoded.data(), encoded.size());
  used_ += encoded.size();
}

void TlogLogger::flush() {
  if (!ready_ || used_ == 0) return;
  if (!write_at(position_, buffer_, used_)) {
    ++write_errors_;
    ready_ = false;
    return;
  }
  position_ += static_cast<std::uint32_t>(used_);
  used_ = 0;
  save_tlog_pos(position_);
}

}  // namespace moto::device
