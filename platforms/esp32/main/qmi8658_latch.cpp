#include "qmi8658_latch.hpp"

#include <cmath>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "flash_guard.hpp"

namespace moto::motion {
namespace {

constexpr char kTag[] = "qmi_latch";
constexpr std::uint8_t kCtrl1 = 0x02;
constexpr std::uint8_t kCtrl2 = 0x03;
constexpr std::uint8_t kCtrl3 = 0x04;
constexpr std::uint8_t kCtrl5 = 0x06;
constexpr std::uint8_t kCtrl7 = 0x08;
constexpr std::uint8_t kCtrl8 = 0x09;
constexpr std::uint8_t kCtrl9 = 0x0A;
constexpr std::uint8_t kCal1Low = 0x0B;
constexpr std::uint8_t kStatusInt = 0x2D;
constexpr std::uint8_t kTempLow = 0x33;
constexpr std::uint8_t kCmdAck = 0x00;
constexpr std::uint8_t kCmdAhbClockGating = 0x12;

// QMI8658B Rev D §5.4: aFS=010 (±8 g), aODR=0100 (448.4 Hz in 6DOF).
// INT2_EN is set even though the board does not route INT2: the CTRL9
// completion path on this die has only been observed with the interrupt
// enabled (the FIFO path writes 0x70 and its handshake works). Byte order
// stays little-endian (BE=0).
constexpr std::uint8_t kCtrl1AutoIncrementLittleEndian = 0x40;
constexpr std::uint8_t kCtrl2Accel8g448 = 0x24;
constexpr std::uint8_t kCtrl3Gyro512dps448 = 0x54;
// Both LPFs on, mode 10 = 5.39% of ODR (~24.2 Hz at 448.4 Hz). The layout is
// bit6:5 gyro mode, bit4 gyro enable, bit2:1 accel mode, bit0 accel enable.
constexpr std::uint8_t kCtrl5BothLpfMode10 = 0x55;
constexpr std::uint8_t kCtrl7SyncAccelGyro = 0x83;

constexpr float kAccelLsbPerG = 4096.0F;  // ±8 g
constexpr float kGyroLsbPerDps = 64.0F;   // ±512 dps
constexpr float kGravity = 9.80665F;
constexpr std::uint32_t kHandshakeTimeoutUs = 3000;
constexpr std::uint32_t kStatusRetryUs = 50;
// AHB clock gating switches a clock domain and is much slower than a FIFO
// reset. 50 ms with a 200 us poll is still bounded and has been observed to
// complete on this unit.
constexpr std::uint32_t kAhbTimeoutUs = 50'000;
constexpr std::uint32_t kAhbPollUs = 200;
constexpr int kConsecutiveI2cLimit = 8;
constexpr int kConsecutiveLockLimit = 20;

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

bool Qmi8658Latch::read_register(std::uint8_t reg, std::uint8_t* out,
                                 std::size_t len) {
  return qmi8658_read_register(device_, reg, out,
                               static_cast<std::uint8_t>(len)) == ESP_OK;
}

bool Qmi8658Latch::write_register(std::uint8_t reg, std::uint8_t value) {
  return qmi8658_write_register(device_, reg, value) == ESP_OK;
}

bool Qmi8658Latch::disable_ahb_clock_gating() {
  if (!write_register(kCal1Low, 0x01)) return false;
  if (!write_register(kCtrl9, kCmdAhbClockGating)) return false;
  esp_rom_delay_us(500);
  const std::int64_t deadline = esp_timer_get_time() + kAhbTimeoutUs;
  while (esp_timer_get_time() < deadline) {
    std::uint8_t status = 0;
    if (!read_register(kStatusInt, &status, 1)) return false;
    if ((status & 0x80U) != 0U) {
      return write_register(kCtrl9, kCmdAck);
    }
    esp_rom_delay_us(kAhbPollUs);
  }
  return false;
}

bool Qmi8658Latch::control9(std::uint8_t command) {
  if (!write_register(kCtrl9, command)) return false;
  const std::int64_t deadline = esp_timer_get_time() + kHandshakeTimeoutUs;
  while (esp_timer_get_time() < deadline) {
    std::uint8_t status = 0;
    if (read_register(kStatusInt, &status, 1) && (status & 0x80U) != 0U) {
      // Reading STATUSINT clears CmdDone; the ACK closes the transaction.
      return write_register(kCtrl9, kCmdAck);
    }
    esp_rom_delay_us(kStatusRetryUs);
  }
  return false;
}

void Qmi8658Latch::fall_back(const char* reason) {
  if (!active_ || fallback_reason_ == nullptr) fallback_reason_ = reason;
  active_ = false;
  stats_.active = false;
  ESP_LOGW(kTag, "latch polling disabled: %s", reason);
}

void Qmi8658Latch::count_i2c_error() {
  ++stats_.i2c_errors;
  if (++consecutive_i2c_errors_ >= kConsecutiveI2cLimit) {
    fall_back("consecutive I2C read errors");
  }
}

void Qmi8658Latch::note_flash_blocking() {
  flash_gap_pending_ = true;
}

void Qmi8658Latch::reset_stats() {
  const bool was_active = stats_.active;
  stats_ = LatchStats{};
  stats_.active = was_active;
  have_last_timestamp_ = false;
  last_timestamp_us_ = 0;
}

bool Qmi8658Latch::begin(qmi8658_dev_t* device) {
  device_ = device;
  if (device_ == nullptr) return false;
  stats_ = LatchStats{};
  fallback_reason_ = nullptr;
  consecutive_i2c_errors_ = 0;
  endian_checked_ = false;
  big_endian_ = false;
  have_last_timestamp_ = false;
  last_timestamp_us_ = 0;

  // The IMU is powered by the PMIC and survives an ESP32 reset; a previous
  // FIFO session can leave FIFO_CTRL in Stream mode, which hides the output
  // registers and the sync status. Reset the chip before configuring.
  write_register(0x60, 0xB0);
  esp_rom_delay_us(20'000);
  std::uint8_t reset_status = 0;
  read_register(0x4D, &reset_status, 1);
  ESP_LOGI(kTag, "soft reset: RESET_STATUS=0x%02x", reset_status);

  std::uint8_t who = 0;
  std::uint8_t revision = 0;
  read_register(0x00, &who, 1);
  read_register(0x01, &revision, 1);
  ESP_LOGI(kTag, "WHO_AM_I=0x%02x REVISION_ID=0x%02x", who, revision);

  // Stop the sensor before changing the output configuration. The lock is off
  // while the registers are rewritten, so no sample can be half-configured.
  if (!write_register(kCtrl7, 0x00)) {
    fall_back("CTRL7 disable failed");
    return false;
  }
  esp_rom_delay_us(1000);

  // Auto-increment is required for the 14-byte burst; BE=0 because the driver
  // (and the bench) read little-endian on this board.
  if (!write_register(kCtrl1, kCtrl1AutoIncrementLittleEndian)) {
    fall_back("CTRL1 write failed");
    return false;
  }
  // Go through the vendored helpers so the driver's LSB scale stays in sync
  // with CTRL2/CTRL3; calibration still reads through qmi8658_read_sensor_data.
  if (qmi8658_set_accel_range(device_, QMI8658_ACCEL_RANGE_8G) != ESP_OK ||
      qmi8658_set_accel_odr(
          device_, static_cast<qmi8658_accel_odr_t>(0x04)) != ESP_OK ||
      qmi8658_set_gyro_range(device_, QMI8658_GYRO_RANGE_512DPS) != ESP_OK ||
      qmi8658_set_gyro_odr(
          device_, static_cast<qmi8658_gyro_odr_t>(0x04)) != ESP_OK) {
    fall_back("range/ODR configuration failed");
    return false;
  }
  if (!write_register(kCtrl2, kCtrl2Accel8g448) ||
      !write_register(kCtrl3, kCtrl3Gyro512dps448)) {
    fall_back("CTRL2/CTRL3 write failed");
    return false;
  }
  if (!write_register(kCtrl5, kCtrl5BothLpfMode10)) {
    fall_back("CTRL5 write failed");
    return false;
  }
  // CTRL9 completion must arrive through STATUSINT, not INT1 (the IMU INT net
  // is not on this board).
  if (!write_register(kCtrl8, 0x80)) {
    fall_back("CTRL8 write failed");
    return false;
  }
  // Run the CTRL9 command with the sensors on but syncSmpl OFF. Polling
  // STATUSINT while syncSmpl is enabled can take the data lock (Avail=1 starts
  // the lock), which is unnecessary while the command is in flight.
  if (!write_register(kCtrl7, 0x03)) {
    fall_back("CTRL7 streaming enable failed");
    return false;
  }
  esp_rom_delay_us(2000);
  // §11.2.1: I2C + syncSmpl needs AHB clock gating off. CAL1_L=0x01 is the
  // documented selector for that CTRL9 command, not a calibration value.
  bool ahb_ok = disable_ahb_clock_gating();
  if (!ahb_ok) {
    esp_rom_delay_us(2000);
    ahb_ok = disable_ahb_clock_gating();
  }
  if (ahb_ok && !write_register(kCtrl7, kCtrl7SyncAccelGyro)) {
    fall_back("CTRL7 sync enable failed");
    return false;
  }

  std::uint8_t ctrl1 = 0;
  std::uint8_t ctrl5 = 0;
  std::uint8_t ctrl7 = 0;
  std::uint8_t ctrl8 = 0;
  std::uint8_t statusint = 0;
  read_register(kCtrl1, &ctrl1, 1);
  read_register(kCtrl5, &ctrl5, 1);
  read_register(kCtrl7, &ctrl7, 1);
  read_register(kCtrl8, &ctrl8, 1);
  read_register(kStatusInt, &statusint, 1);
  if (!ahb_ok) {
    ESP_LOGW(kTag,
             "AHB gating handshake failed: CTRL8=0x%02x STATUSINT=0x%02x",
             ctrl8, statusint);
    write_register(kCtrl7, 0x00);
    fall_back("AHB clock gating handshake timed out");
    return false;
  }
  ESP_LOGI(kTag,
           "latch ready: CTRL1=0x%02x (expect 0x40) CTRL5=0x%02x (expect "
           "0x55) CTRL7=0x%02x (expect 0x83) CTRL8=0x%02x, 448.4 Hz, LPF "
           "5.39%%, read %u us",
           ctrl1, ctrl5, ctrl7, ctrl8,
           static_cast<unsigned>(kPeriodUs));

  active_ = true;
  stats_.active = true;
  return true;
}

void Qmi8658Latch::log_status(const char* when) {
  if (device_ == nullptr) return;
  std::uint8_t ctrl1 = 0;
  std::uint8_t ctrl2 = 0;
  std::uint8_t ctrl3 = 0;
  std::uint8_t ctrl5 = 0;
  std::uint8_t ctrl7 = 0;
  std::uint8_t status0 = 0;
  std::uint8_t statusint = 0;
  read_register(kCtrl1, &ctrl1, 1);
  read_register(kCtrl2, &ctrl2, 1);
  read_register(kCtrl3, &ctrl3, 1);
  read_register(kCtrl5, &ctrl5, 1);
  read_register(kCtrl7, &ctrl7, 1);
  read_register(0x2E, &status0, 1);
  read_register(kStatusInt, &statusint, 1);
  ESP_LOGI(kTag,
           "%s: C1=0x%02x C2=0x%02x C3=0x%02x C5=0x%02x C7=0x%02x "
           "STATUS0=0x%02x STATUSINT=0x%02x samples=%u avail0=%u",
           when, ctrl1, ctrl2, ctrl3, ctrl5, ctrl7, status0, statusint,
           static_cast<unsigned>(stats_.samples),
           static_cast<unsigned>(stats_.avail_zero));
}

void Qmi8658Latch::shutdown() {
  if (device_ != nullptr && active_) {
    write_register(kCtrl7, 0x00);
    // §11.5: restore clock gating after leaving the locking mechanism.
    write_register(kCal1Low, 0x00);
    control9(kCmdAhbClockGating);
  }
  active_ = false;
  stats_.active = false;
}

bool Qmi8658Latch::plausible_accel(const float accel_mps2[3]) const {
  const float magnitude = std::sqrt(
      accel_mps2[0] * accel_mps2[0] + accel_mps2[1] * accel_mps2[1] +
      accel_mps2[2] * accel_mps2[2]);
  return magnitude > 0.1F * kGravity && magnitude < 8.0F * kGravity;
}

bool Qmi8658Latch::decode_sample(const std::uint8_t* raw,
                                 LatchSample* sample) {
  // raw[0..1] = TEMP, raw[2..7] = AX/AY/AZ, raw[8..13] = GX/GY/GZ.
  const auto axis = [&](int offset) {
    return static_cast<float>(
        to_signed(decode16(raw + offset, big_endian_)));
  };
  for (int i = 0; i < 3; ++i) {
    sample->accel_mps2[i] = axis(2 + i * 2) / kAccelLsbPerG * kGravity;
    sample->gyro_dps[i] = axis(8 + i * 2) / kGyroLsbPerDps;
  }
  sample->temp_c =
      static_cast<float>(to_signed(decode16(raw, big_endian_))) / 256.0F;

  if (!endian_checked_) {
    // One stationary sample is enough to pin the byte order. If the board is
    // already moving, keep the documented little-endian default rather than
    // guessing from a deceleration spike.
    const auto stationary = [&](bool big_endian) {
      float values[3];
      for (int i = 0; i < 3; ++i) {
        values[i] = static_cast<float>(
                        to_signed(decode16(raw + 2 + i * 2, big_endian))) /
                    kAccelLsbPerG;
      }
      const float magnitude = std::sqrt(values[0] * values[0] +
                                        values[1] * values[1] +
                                        values[2] * values[2]);
      return magnitude > 0.7F && magnitude < 1.3F;
    };
    if (stationary(false)) {
      endian_checked_ = true;
    } else if (stationary(true)) {
      big_endian_ = true;
      endian_checked_ = true;
      ESP_LOGW(kTag, "latch data is big-endian on this unit");
      return decode_sample(raw, sample);  // re-decode with the chosen order
    } else if (stats_.samples > 8) {
      endian_checked_ = true;
    }
  }

  if (!plausible_accel(sample->accel_mps2)) {
    ++stats_.invalid_samples;
    return false;
  }
  return true;
}

bool Qmi8658Latch::read(LatchSample* sample) {
  if (!active_ || sample == nullptr) return false;
  // A page write can hold the guard for a few hundred microseconds. Skipping
  // this tick is cheaper than starting a burst that the write would stretch;
  // the miss is counted separately from an I2C error.
  if (!moto::device::flash_guard_take(0)) {
    ++stats_.flash_skips;
    return false;
  }
  ++stats_.cycles;

  std::uint8_t status = 0;
  if (!read_register(kStatusInt, &status, 1)) {
    count_i2c_error();
    moto::device::flash_guard_give();
    return false;
  }
  if ((status & 0x01U) == 0U) {
    ++stats_.avail_zero;
    moto::device::flash_guard_give();
    return false;
  }
  if ((status & 0x02U) == 0U) {
    // Avail=1 starts the lock; Locked is set within 12 us at 448.4 Hz.
    ++stats_.lock_waits;
    bool locked = false;
    for (int attempt = 0; attempt < 3 && !locked; ++attempt) {
      esp_rom_delay_us(kLockDelayUs);
      std::uint8_t confirm = 0;
      if (!read_register(kStatusInt, &confirm, 1)) {
        count_i2c_error();
        moto::device::flash_guard_give();
        return false;
      }
      locked = (confirm & 0x02U) != 0U;
    }
    if (!locked) {
      ++stats_.lock_timeouts;
      moto::device::flash_guard_give();
      return false;
    }
  }

  std::uint8_t raw[14] = {};
  const bool ok = read_register(kTempLow, raw, sizeof(raw));
  const std::int64_t timestamp_us = esp_timer_get_time();
  moto::device::flash_guard_give();
  if (!ok) {
    count_i2c_error();
    return false;
  }
  consecutive_i2c_errors_ = 0;
  ++stats_.samples;

  if (have_last_timestamp_) {
    const std::int64_t delta = timestamp_us - last_timestamp_us_;
    if (delta > 0) {
      const auto delta_us = static_cast<std::uint32_t>(delta);
      stats_.interval_us_sum += delta_us;
      ++stats_.intervals;
      if (delta_us > stats_.interval_us_max) stats_.interval_us_max = delta_us;
      if (delta_us > 2U * kPeriodUs) {
        if (flash_gap_pending_) {
          ++stats_.flash_gaps;
        } else {
          ++stats_.missed_reads;
        }
      }
      flash_gap_pending_ = false;
    }
  }
  last_timestamp_us_ = timestamp_us;
  have_last_timestamp_ = true;
  sample->timestamp_us = timestamp_us;

  if (!decode_sample(raw, sample)) return false;
  return true;
}

}  // namespace moto::motion
