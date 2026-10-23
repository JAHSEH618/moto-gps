#include "dyn_replay.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "dyn_record.hpp"
#include "lean_estimator.hpp"

namespace {

using moto::device::DynRecord;
using moto::device::GnssRecord;
using moto::device::MountRecord;
using moto::device::TelemetryHeader;

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

}  // namespace

std::string replay_to_csv(const std::vector<std::uint8_t>& file) {
  TelemetryHeader header;
  if (!moto::device::decode_header(file.data(), file.size(), &header)) {
    return "# invalid telemetry header\n";
  }
  std::string csv;
  char head[128];
  std::snprintf(head, sizeof(head), "# mt r1 hz=%u epoch=%u\n",
                static_cast<unsigned>(header.sample_hz),
                static_cast<unsigned>(header.session_epoch));
  csv += head;
  csv += "t_ms,lean_deg,lean_eff_deg,lean_rate_dps,pitch_deg,lateral_g,"
         "source,quality,flags\n";

  moto::motion::LeanEstimator estimator;
  moto::motion::MountCalibration mount;
  mount.set_basis({0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F}, false);
  estimator.set_mount(mount);

  bool have_speed = false;
  std::uint32_t last_gnss_rx = 0;
  std::uint16_t last_speed_cms = 0;
  std::uint32_t previous_t = 0;
  bool have_previous = false;

  std::size_t offset = moto::device::kTelemetryHeaderSize;
  while (offset < file.size()) {
    const std::uint8_t type = file[offset++];
    if (type == moto::device::kRecordMount) {
      MountRecord record;
      if (!moto::device::decode_mount_record(file.data() + offset,
                                             file.size() - offset, &record)) {
        csv += "# truncated mount record\n";
        break;
      }
      mount.set_basis(
          {static_cast<float>(record.forward[0]) / 10000.0F,
           static_cast<float>(record.forward[1]) / 10000.0F,
           static_cast<float>(record.forward[2]) / 10000.0F},
          {static_cast<float>(record.up[0]) / 10000.0F,
           static_cast<float>(record.up[1]) / 10000.0F,
           static_cast<float>(record.up[2]) / 10000.0F},
          true);
      mount.set_trim_deg(static_cast<float>(record.trim_cdeg) / 100.0F);
      estimator.set_mount(mount);
      offset += moto::device::kMountRecordSize;
      continue;
    }
    if (type == moto::device::kRecordGnss) {
      GnssRecord record;
      if (!moto::device::decode_gnss(file.data() + offset,
                                     file.size() - offset, &record)) {
        csv += "# truncated gnss record\n";
        break;
      }
      have_speed = (record.flags & 0x01U) != 0U;
      last_gnss_rx = record.t_rx_ms;
      last_speed_cms = record.speed_cms;
      offset += moto::device::kGnssRecordSize;
      continue;
    }
    if (type != moto::device::kRecordDyn) {
      csv += "# unknown record type\n";
      break;
    }
    DynRecord record;
    if (!moto::device::decode_dyn(file.data() + offset, file.size() - offset,
                                  &record)) {
      csv += "# truncated dyn record\n";
      break;
    }
    const float dt =
        have_previous
            ? std::clamp(static_cast<float>(record.t_ms - previous_t) / 1000.0F,
                         0.002F, 0.05F)
            : 1.0F / static_cast<float>(header.sample_hz);
    previous_t = record.t_ms;
    have_previous = true;

    moto::motion::SpeedSample speed;
    if (have_speed) {
      speed.valid = true;
      speed.mps = static_cast<float>(last_speed_cms) / 100.0F;
      speed.age_s =
          static_cast<float>(record.t_ms - last_gnss_rx) / 1000.0F;
      if (speed.age_s < 0.0F) speed.age_s = 0.0F;
    }
    estimator.step({static_cast<float>(record.gx_dds) / 10.0F,
                    static_cast<float>(record.gy_dds) / 10.0F,
                    static_cast<float>(record.gz_dds) / 10.0F},
                   {static_cast<float>(record.ax_cms2) / 100.0F,
                    static_cast<float>(record.ay_cms2) / 100.0F,
                    static_cast<float>(record.az_cms2) / 100.0F},
                   dt, speed);
    csv += format_dyn(static_cast<double>(record.t_ms), estimator.output());
    offset += moto::device::kDynRecordSize;
  }
  return csv;
}
