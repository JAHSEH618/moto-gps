#include "tlog_logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "flash_guard.hpp"
#include "settings_store.h"

namespace moto::device {
namespace {

constexpr char kTag[] = "tlog";
constexpr std::uint32_t kFlushIntervalMs = 2000;
constexpr esp_partition_subtype_t kTlogSubtype =
    static_cast<esp_partition_subtype_t>(0x40);

std::int16_t to_i16(std::int64_t value) {
  return static_cast<std::int16_t>(
      std::clamp<std::int64_t>(value, -32768, 32767));
}

}  // namespace

bool TlogLogger::ensure_partition() {
  const esp_partition_t* found =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, kTlogSubtype, "tlog");
  if (found == nullptr) {
    ESP_LOGE(kTag, "no tlog partition; telemetry recording disabled");
    return false;
  }
  partition_ = found;
  capacity_ = found->size;
  return capacity_ >= kBlockSize * 2;
}

bool TlogLogger::erase_sector(std::uint32_t offset) {
  if (offset % kBlockSize != 0 || offset + kBlockSize > capacity_) return false;
  if (!flash_guard_take(500)) return false;
  const esp_err_t result = esp_partition_erase_range(
      static_cast<const esp_partition_t*>(partition_), offset, kBlockSize);
  flash_guard_give();
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "erase 0x%x failed: %s", static_cast<unsigned>(offset),
             esp_err_to_name(result));
    return false;
  }
  return true;
}

bool TlogLogger::sector_is_erased(std::uint32_t offset) {
  if (offset % kBlockSize != 0 || offset + kBlockSize > capacity_) return false;
  alignas(4) std::uint8_t chunk[256];
  for (std::uint32_t done = 0; done < kBlockSize; done += sizeof(chunk)) {
    if (esp_partition_read(static_cast<const esp_partition_t*>(partition_),
                           offset + done, chunk, sizeof(chunk)) != ESP_OK) {
      return false;
    }
    for (std::size_t i = 0; i < sizeof(chunk); ++i) {
      if (chunk[i] != 0xFFU) return false;
    }
  }
  return true;
}

bool TlogLogger::write_block() {
  if (!block_dirty_) return true;
  if (block_base_ + kBlockSize > capacity_) {
    full_ = true;
    return false;
  }
  BlockHeader header;
  header.block_seq = block_seq_;
  header.boot_id = boot_id_;
  header.first_sample_seq = first_sample_seq_;
  header.payload_len = payload_used_;
  header.crc32 = 0;
  const auto encoded = encode_block_header(header);
  std::memcpy(block_, encoded.data(), encoded.size());
  // Unused payload stays 0xFF so the CRC covers a deterministic block.
  std::memset(block_ + kBlockHeaderSize + payload_used_, 0xFF,
              kBlockSize - kBlockHeaderSize - payload_used_);
  const std::uint32_t crc = block_crc32(block_);
  block_[24] = static_cast<std::uint8_t>(crc & 0xFFU);
  block_[25] = static_cast<std::uint8_t>((crc >> 8U) & 0xFFU);
  block_[26] = static_cast<std::uint8_t>((crc >> 16U) & 0xFFU);
  block_[27] = static_cast<std::uint8_t>((crc >> 24U) & 0xFFU);

  // Never erase at runtime: the erase is tens of milliseconds with the cache
  // off, which used to freeze the IMU bus. Only write into a sector the host
  // tool already erased; if it is dirty, stop and let the status slot say so.
  if (!sector_is_erased(block_base_)) {
    ++write_errors_;
    ready_ = false;
    dirty_ = true;
    ESP_LOGE(kTag,
             "LOG DIRTY at 0x%x; recording stopped. Erase the tlog partition "
             "from the host (parttool.py erase_partition --partition-name "
             "tlog)",
             static_cast<unsigned>(block_base_));
    return false;
  }
  // A page write is sub-millisecond; the guard is still taken so an IMU read
  // cannot start in the middle of it.
  if (!flash_guard_take(500)) {
    ready_ = false;
    return false;
  }
  const esp_err_t result = esp_partition_write(
      static_cast<const esp_partition_t*>(partition_), block_base_, block_,
      kBlockSize);
  flash_guard_give();
  if (result != ESP_OK) {
    ++write_errors_;
    ready_ = false;
    ESP_LOGE(kTag, "block write at 0x%x failed: %s",
             static_cast<unsigned>(block_base_), esp_err_to_name(result));
    return false;
  }

  block_base_ += kBlockSize;
  ++block_seq_;
  first_sample_seq_ = seq_;
  payload_used_ = 0;
  block_dirty_ = false;
  return true;
}

bool TlogLogger::start_block() {
  if (block_base_ + kBlockSize > capacity_) {
    full_ = true;
    return false;
  }
  // The next block starts with a clean sector; the header is written at seal
  // time so a torn block is simply skipped by the reader.
  payload_used_ = 0;
  if (!wrote_session_meta_) {
    const auto meta = encode_session_meta(SessionMeta{
        session_.mode, session_.odr_hz, session_.sample_hz,
        session_.lever_arm_mm, 1500, 500, 3000, 300, session_.firmware_hash,
        session_.mount});
    block_[kBlockHeaderSize] = kRecordSession;
    std::memcpy(block_ + kBlockHeaderSize + 1, meta.data(), meta.size());
    payload_used_ = static_cast<std::uint16_t>(1 + meta.size());
    wrote_session_meta_ = true;
    block_dirty_ = true;
  }
  return true;
}

std::uint32_t TlogLogger::find_append_offset() {
  // Written blocks start with 'MTLG'; an erased sector starts with 0xFF.
  // Binary search is valid because blocks are appended strictly forward.
  const std::uint32_t sector_count = capacity_ / kBlockSize;
  std::uint32_t low = 0;
  std::uint32_t high = sector_count;
  std::uint8_t head[4] = {};
  while (low < high) {
    const std::uint32_t mid = low + (high - low) / 2;
    const esp_err_t result = esp_partition_read(
        static_cast<const esp_partition_t*>(partition_), mid * kBlockSize,
        head, sizeof(head));
    const bool erased =
        result != ESP_OK ||
        (head[0] == 0xFFU && head[1] == 0xFFU && head[2] == 0xFFU &&
         head[3] == 0xFFU);
    if (erased) {
      high = mid;
    } else {
      low = mid + 1;
    }
  }
  return low * kBlockSize;
}

bool TlogLogger::run_selftest() {
  if (tlog_selftest_done()) return true;
  const std::uint32_t offset = capacity_ - kBlockSize;
  if (!erase_sector(offset)) return false;
  alignas(4) std::uint8_t pattern[kBlockSize];
  for (std::size_t i = 0; i < sizeof(pattern); ++i) {
    pattern[i] = static_cast<std::uint8_t>((i * 31U + 7U) & 0xFFU);
  }
  if (!flash_guard_take(500)) return false;
  esp_err_t result = esp_partition_write(
      static_cast<const esp_partition_t*>(partition_), offset, pattern,
      sizeof(pattern));
  flash_guard_give();
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "above-16-MB write failed: %s", esp_err_to_name(result));
    return false;
  }
  alignas(4) std::uint8_t readback[kBlockSize];
  result = esp_partition_read(
      static_cast<const esp_partition_t*>(partition_), offset, readback,
      sizeof(readback));
  const bool ok =
      result == ESP_OK && std::memcmp(pattern, readback, sizeof(pattern)) == 0;
  erase_sector(offset);
  if (!ok) {
    ESP_LOGE(kTag, "above-16-MB erase/write/read self-test failed");
    return false;
  }
  mark_tlog_selftest_done();
  ESP_LOGI(kTag, "above-16-MB self-test passed at 0x%x",
           static_cast<unsigned>(offset));
  return true;
}

bool TlogLogger::erase_all() {
#ifdef CONFIG_MOTO_TLOG_DISABLE
  return false;
#else
  if (!ensure_partition()) return false;
  flash_guard_init();
  // Erase in 1 MiB chunks so the serial log shows progress and a partial
  // failure leaves a clear offset. A full-partition erase is slow on QIO
  // flash, and the user asked for a visible cleanup, not a silent hang.
  constexpr std::uint32_t kChunk = 1U << 20U;
  for (std::uint32_t offset = 0; offset < capacity_; offset += kChunk) {
    const std::uint32_t length = std::min(kChunk, capacity_ - offset);
    if (!flash_guard_take(5000)) return false;
    const esp_err_t result = esp_partition_erase_range(
        static_cast<const esp_partition_t*>(partition_), offset, length);
    flash_guard_give();
    if (result != ESP_OK) {
      ESP_LOGE(kTag, "erase 0x%x+0x%x failed: %s",
               static_cast<unsigned>(offset), static_cast<unsigned>(length),
               esp_err_to_name(result));
      return false;
    }
    ESP_LOGI(kTag, "erased 0x%x/0x%x", static_cast<unsigned>(offset + length),
             static_cast<unsigned>(capacity_));
  }
  block_base_ = 0;
  block_seq_ = 0;
  seq_ = 0;
  first_sample_seq_ = 0;
  payload_used_ = 0;
  wrote_session_meta_ = false;
  block_dirty_ = false;
  ready_ = false;
  full_ = false;
  dirty_ = false;
  return true;
#endif
}

bool TlogLogger::begin(const TlogSessionInfo& session) {
#ifdef CONFIG_MOTO_TLOG_DISABLE
  // Release build: leave the partition alone, so every append is a no-op.
  (void)session;
  ESP_LOGI(kTag, "tlog disabled in this build (CONFIG_MOTO_TLOG_DISABLE)");
  return false;
#else
  flash_guard_init();
  session_ = session;
  if (!ensure_partition()) return false;
  if (!run_selftest()) return false;
  boot_id_ = esp_random();
  block_seq_ = 0;
  seq_ = 0;
  first_sample_seq_ = 0;
  wrote_session_meta_ = false;
  block_dirty_ = false;
  full_ = false;
  dirty_ = false;
  block_base_ = find_append_offset();
  if (block_base_ + kBlockSize > capacity_) {
    full_ = true;
    ready_ = false;
    ESP_LOGW(kTag, "tlog partition is full");
    return false;
  }
  if (!sector_is_erased(block_base_)) {
    dirty_ = true;
    ready_ = false;
    ESP_LOGE(kTag,
             "LOG DIRTY at 0x%x; recording disabled until the host erases the "
             "tlog partition",
             static_cast<unsigned>(block_base_));
    return false;
  }
  if (!start_block()) return false;
  ready_ = true;
  last_flush_ms_ = 0;
  ESP_LOGI(kTag,
           "recording at 0x%x of 0x%x (mode=%u odr=%uHz sample=%uHz)",
           static_cast<unsigned>(block_base_), static_cast<unsigned>(capacity_),
           static_cast<unsigned>(session_.mode),
           static_cast<unsigned>(session_.odr_hz),
           static_cast<unsigned>(session_.sample_hz));
  return true;
#endif
}

void TlogLogger::append(std::uint64_t now_ms, const RawImuSample& raw,
                        const moto::motion::AttitudeEstimate& attitude) {
  if (!ready_ || full_) return;
  if (now_ms - last_flush_ms_ >= kFlushIntervalMs) {
    flush();
    last_flush_ms_ = now_ms;
  }
  if (!ready_) return;
  if (payload_used_ + 1 + kDynRecordSize >
      kBlockSize - kBlockHeaderSize) {
    if (!write_block()) return;
    if (!start_block()) {
      ready_ = false;
      return;
    }
  }
  DynRecord record;
  record.t_ms = static_cast<std::uint32_t>(now_ms);
  record.seq = static_cast<std::uint16_t>(seq_);
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
      attitude.has_lean_eff
          ? to_i16(std::lround(attitude.lean_eff_deg * 100.0F))
          : std::int16_t{-32768};
  record.lateral_g_cg = to_i16(std::lround(attitude.lateral_g * 100.0F));
  record.flags = static_cast<std::uint8_t>(attitude.flags & 0xFFU);
  record.quality = attitude.quality;

  const auto encoded = encode_dyn(record);
  block_[kBlockHeaderSize + payload_used_] = kRecordDyn;
  std::memcpy(block_ + kBlockHeaderSize + payload_used_ + 1, encoded.data(),
              encoded.size());
  payload_used_ = static_cast<std::uint16_t>(payload_used_ + 1 +
                                             encoded.size());
  block_dirty_ = true;
  ++seq_;
}

void TlogLogger::note_mode(std::uint8_t mode, std::uint16_t odr_hz,
                           std::uint16_t sample_hz) {
  if (!ready_ || full_) return;
  if (payload_used_ + 1 + kSessionMetaSize >
      kBlockSize - kBlockHeaderSize) {
    if (!write_block()) return;
    if (!start_block()) {
      ready_ = false;
      return;
    }
  }
  session_.mode = mode;
  session_.odr_hz = odr_hz;
  session_.sample_hz = sample_hz;
  const auto meta = encode_session_meta(SessionMeta{
      mode, odr_hz, sample_hz, session_.lever_arm_mm, 1500, 500, 3000, 300,
      session_.firmware_hash, session_.mount});
  block_[kBlockHeaderSize + payload_used_] = kRecordSession;
  std::memcpy(block_ + kBlockHeaderSize + payload_used_ + 1, meta.data(),
              meta.size());
  payload_used_ =
      static_cast<std::uint16_t>(payload_used_ + 1 + meta.size());
  block_dirty_ = true;
}

void TlogLogger::append_stats(const ImuStatsRecord& stats) {
  if (!ready_ || full_ || dirty_) return;
  if (payload_used_ + 1 + kImuStatsRecordSize >
      kBlockSize - kBlockHeaderSize) {
    if (!write_block()) return;
    if (!start_block()) {
      ready_ = false;
      return;
    }
  }
  const auto encoded = encode_imu_stats(stats);
  block_[kBlockHeaderSize + payload_used_] = kRecordImuStats;
  std::memcpy(block_ + kBlockHeaderSize + payload_used_ + 1, encoded.data(),
              encoded.size());
  payload_used_ =
      static_cast<std::uint16_t>(payload_used_ + 1 + encoded.size());
  block_dirty_ = true;
}

void TlogLogger::append_gnss(const GnssRecord& record) {
  if (!ready_ || full_ || dirty_) return;
  if (payload_used_ + 1 + kGnssRecordSize > kBlockSize - kBlockHeaderSize) {
    if (!write_block()) return;
    if (!start_block()) {
      ready_ = false;
      return;
    }
  }
  const auto encoded = encode_gnss(record);
  block_[kBlockHeaderSize + payload_used_] = kRecordGnss;
  std::memcpy(block_ + kBlockHeaderSize + payload_used_ + 1, encoded.data(),
              encoded.size());
  payload_used_ =
      static_cast<std::uint16_t>(payload_used_ + 1 + encoded.size());
  block_dirty_ = true;
}

void TlogLogger::flush() {
  if (!ready_ || !block_dirty_) return;
  if (!write_block()) return;
  if (!start_block()) ready_ = false;
}

}  // namespace moto::device
