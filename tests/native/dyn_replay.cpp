#include "dyn_replay.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "dyn_record.hpp"
#include "lean_estimator.hpp"
#include "speed_fusion.hpp"

namespace {

using moto::device::DynRecord;
using moto::device::GnssRecord;
using moto::device::ImuStatsRecord;
using moto::device::MountRecord;
using moto::device::SessionMeta;

std::string format_dyn(double t_ms, const moto::motion::LeanOutput& out) {
  char line[256];
  if (std::isnan(out.lean_eff_deg)) {
    std::snprintf(line, sizeof(line),
                  "%.0f,%.3f,,%.3f,%.3f,%.3f,%d,%u,%u\n", t_ms,
                  static_cast<double>(out.lean_deg),
                  static_cast<double>(out.lean_rate_dps),
                  static_cast<double>(out.pitch_deg),
                  static_cast<double>(out.lateral_g),
                  static_cast<int>(out.source),
                  static_cast<unsigned>(out.quality),
                  static_cast<unsigned>(out.flags));
  } else {
    std::snprintf(line, sizeof(line),
                  "%.0f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%u,%u\n", t_ms,
                  static_cast<double>(out.lean_deg),
                  static_cast<double>(out.lean_eff_deg),
                  static_cast<double>(out.lean_rate_dps),
                  static_cast<double>(out.pitch_deg),
                  static_cast<double>(out.lateral_g),
                  static_cast<int>(out.source),
                  static_cast<unsigned>(out.quality),
                  static_cast<unsigned>(out.flags));
  }
  return line;
}

void set_mount(moto::motion::LeanEstimator& estimator,
               moto::motion::MountCalibration& mount,
               const MountRecord& record) {
  mount.set_basis(
      {static_cast<float>(record.forward[0]) / 10000.0F,
       static_cast<float>(record.forward[1]) / 10000.0F,
       static_cast<float>(record.forward[2]) / 10000.0F},
      {static_cast<float>(record.up[0]) / 10000.0F,
       static_cast<float>(record.up[1]) / 10000.0F,
       static_cast<float>(record.up[2]) / 10000.0F}, true);
  mount.set_trim_deg(static_cast<float>(record.trim_cdeg) / 100.0F);
  estimator.set_mount(mount);
}

}  // namespace

std::string replay_to_csv(const std::vector<std::uint8_t>& file) {
  std::string csv;
  csv += "# mt r1 blocks\n";
  csv += "t_ms,lean_deg,lean_eff_deg,lean_rate_dps,pitch_deg,lateral_g,"
         "source,quality,flags\n";

  moto::motion::LeanEstimator estimator;
  moto::motion::MountCalibration mount;
  mount.set_basis(moto::motion::kSensorIntoScreen,
                  moto::motion::kSensorScreenTop, false);
  estimator.set_mount(mount);

  // Speed goes through SpeedFusion exactly as on the device: a GNSS record is
  // written just before the sample it was applied to, so applying it when it
  // is read reproduces the firmware's order.
  moto::motion::SpeedFusion fusion;
  std::uint32_t session = 0;
  bool have_session = false;
  std::uint32_t previous_t = 0;
  bool have_previous = false;

  for (std::size_t base = 0; base + moto::device::kBlockSize <= file.size();
       base += moto::device::kBlockSize) {
    const std::uint8_t* block = file.data() + base;
    bool erased = true;
    for (std::size_t i = 0; i < moto::device::kBlockHeaderSize; ++i) {
      if (block[i] != 0xFFU) {
        erased = false;
        break;
      }
    }
    if (erased) continue;

    moto::device::BlockHeader header;
    if (!moto::device::decode_block_header(block, &header)) continue;
    if (header.crc32 == 0 ||
        moto::device::block_crc32(block) != header.crc32) {
      continue;  // torn or corrupt block: skip it, keep walking
    }
    if (!have_session || header.boot_id != session) {
      session = header.boot_id;
      have_session = true;
      estimator.reset();
      fusion.reset();
      have_previous = false;
      char line[160];
      std::snprintf(line, sizeof(line), "# session %u block_seq %u\n",
                    static_cast<unsigned>(session),
                    static_cast<unsigned>(header.block_seq));
      csv += line;
    }

    std::size_t offset = moto::device::kBlockHeaderSize;
    const std::size_t end =
        moto::device::kBlockHeaderSize + header.payload_len;
    while (offset + 1 <= end) {
      const std::uint8_t type = block[offset++];
      if (type == moto::device::kRecordDyn) {
        DynRecord record;
        if (offset + moto::device::kDynRecordSize > end ||
            !moto::device::decode_dyn(block + offset,
                                      moto::device::kDynRecordSize,
                                      &record)) {
          break;
        }
        offset += moto::device::kDynRecordSize;
        const float dt =
            have_previous
                ? std::clamp(
                      static_cast<float>(record.t_ms - previous_t) / 1000.0F,
                      0.002F, 0.05F)
                : 0.008F;
        previous_t = record.t_ms;
        have_previous = true;
        const moto::motion::Vec3f accel{
            static_cast<float>(record.ax_cms2) / 100.0F,
            static_cast<float>(record.ay_cms2) / 100.0F,
            static_cast<float>(record.az_cms2) / 100.0F};
        fusion.step(estimator.mount().to_body(accel), estimator.up_body(),
                    estimator.static_for_s() >=
                        moto::motion::SpeedFusion::kParkedForS,
                    dt, record.t_ms);
        estimator.step({static_cast<float>(record.gx_dds) / 10.0F,
                        static_cast<float>(record.gy_dds) / 10.0F,
                        static_cast<float>(record.gz_dds) / 10.0F},
                       accel, dt, fusion.sample(record.t_ms));
        csv += format_dyn(static_cast<double>(record.t_ms),
                          estimator.output());
        continue;
      }
      if (type == moto::device::kRecordGnss) {
        GnssRecord record;
        if (offset + moto::device::kGnssRecordSize > end ||
            !moto::device::decode_gnss(block + offset,
                                       moto::device::kGnssRecordSize,
                                       &record)) {
          break;
        }
        offset += moto::device::kGnssRecordSize;
        if ((record.flags & moto::device::kGnssHasSpeed) != 0U &&
            (record.flags & moto::device::kGnssRefused) == 0U) {
          moto::motion::SpeedFix fix;
          fix.mps = static_cast<float>(record.speed_cms) / 100.0F;
          fix.accuracy_mps =
              record.speed_acc_cms == 0xFFFFU
                  ? -1.0F
                  : static_cast<float>(record.speed_acc_cms) / 100.0F;
          fix.fix_ms = record.fix_ms;
          fix.rx_ms = record.t_rx_ms;
          fix.timestamped =
              (record.flags & moto::device::kGnssTimestamped) != 0U;
          fusion.add_fix(fix);
        }
        continue;
      }
      if (type == moto::device::kRecordMount) {
        MountRecord record;
        if (offset + moto::device::kMountRecordSize > end ||
            !moto::device::decode_mount_record(
                block + offset, moto::device::kMountRecordSize, &record)) {
          break;
        }
        offset += moto::device::kMountRecordSize;
        set_mount(estimator, mount, record);
        continue;
      }
      if (type == moto::device::kRecordSession) {
        SessionMeta meta;
        if (offset + moto::device::kSessionMetaSize > end ||
            !moto::device::decode_session_meta(
                block + offset, moto::device::kSessionMetaSize, &meta)) {
          break;
        }
        offset += moto::device::kSessionMetaSize;
        char line[192];
        std::snprintf(line, sizeof(line),
                      "# meta mode=%u odr=%u sample=%u h_mm=%d fw=%08x\n",
                      static_cast<unsigned>(meta.mode),
                      static_cast<unsigned>(meta.odr_hz),
                      static_cast<unsigned>(meta.sample_hz),
                      static_cast<int>(meta.lever_arm_mm),
                      static_cast<unsigned>(meta.firmware_hash));
        csv += line;
        set_mount(estimator, mount, meta.mount);
        continue;
      }
      if (type == moto::device::kRecordImuStats) {
        ImuStatsRecord record;
        if (offset + moto::device::kImuStatsRecordSize > end ||
            !moto::device::decode_imu_stats(
                block + offset, moto::device::kImuStatsRecordSize, &record)) {
          break;
        }
        offset += moto::device::kImuStatsRecordSize;
        char line[224];
        std::snprintf(
            line, sizeof(line),
            "# imu t=%u mode=%u samples=%u avail0=%u lockwait=%u i2c=%u "
            "missed=%u flash=%u flashgaps=%u dt_max=%u dt_avg=%u dirty=%u full=%u\n",
            static_cast<unsigned>(record.t_ms),
            static_cast<unsigned>(record.mode),
            static_cast<unsigned>(record.samples),
            static_cast<unsigned>(record.avail_zero),
            static_cast<unsigned>(record.lock_waits),
            static_cast<unsigned>(record.i2c_errors),
            static_cast<unsigned>(record.missed_reads),
            static_cast<unsigned>(record.flash_skips),
            static_cast<unsigned>(record.flash_gaps),
            static_cast<unsigned>(record.interval_max_us),
            static_cast<unsigned>(record.interval_avg_us),
            static_cast<unsigned>(record.flags & 1U),
            static_cast<unsigned>((record.flags >> 1U) & 1U));
        csv += line;
        continue;
      }
      break;  // unknown type: stop this block
    }
  }
  return csv;
}
