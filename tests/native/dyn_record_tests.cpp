// Telemetry format and replay determinism (DESIGN_V2 §11.2). The file the
// device records must be readable by a native tool with no firmware link, and
// the same bytes must produce byte-identical CSV twice.
#include "dyn_record.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "dyn_replay.hpp"

namespace {

using moto::device::DynRecord;
using moto::device::GnssRecord;
using moto::device::MountRecord;
using moto::device::TelemetryHeader;

int failures = 0;

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      std::cerr << __FILE__ << ':' << __LINE__                             \
                << " CHECK failed: " #condition << '\n';                   \
      ++failures;                                                          \
    }                                                                      \
  } while (false)

void test_record_round_trips() {
  DynRecord dyn;
  dyn.t_ms = 0xDEADBEEF;
  dyn.seq = 65535;
  dyn.ax_cms2 = -1234;
  dyn.ay_cms2 = 980;
  dyn.az_cms2 = -32768;
  dyn.gx_dds = 32767;
  dyn.gy_dds = -1;
  dyn.gz_dds = 42;
  dyn.temp_cdeg = -1500;
  dyn.lean_cdeg = -4200;
  dyn.lean_rate_dds = 999;
  dyn.lean_eff_cdeg = -32768;
  dyn.lateral_g_cg = 108;
  dyn.flags = 0xA5;
  dyn.quality = 87;

  const auto blob = moto::device::encode_dyn(dyn);
  DynRecord decoded;
  CHECK(moto::device::decode_dyn(blob.data(), blob.size(), &decoded));
  CHECK(decoded.t_ms == dyn.t_ms);
  CHECK(decoded.seq == dyn.seq);
  CHECK(decoded.ax_cms2 == dyn.ax_cms2);
  CHECK(decoded.ay_cms2 == dyn.ay_cms2);
  CHECK(decoded.az_cms2 == dyn.az_cms2);
  CHECK(decoded.gx_dds == dyn.gx_dds);
  CHECK(decoded.gy_dds == dyn.gy_dds);
  CHECK(decoded.temp_cdeg == dyn.temp_cdeg);
  CHECK(decoded.lean_cdeg == dyn.lean_cdeg);
  CHECK(decoded.lean_eff_cdeg == dyn.lean_eff_cdeg);
  CHECK(decoded.lateral_g_cg == dyn.lateral_g_cg);
  CHECK(decoded.flags == dyn.flags);
  CHECK(decoded.quality == dyn.quality);

  GnssRecord gnss;
  gnss.t_rx_ms = 123456;
  gnss.fix_ms = 123400;
  gnss.speed_cms = 2778;
  gnss.course_cdeg = 35999;
  gnss.speed_acc_cms = 15;
  gnss.flags = 0x03;
  const auto gnss_blob = moto::device::encode_gnss(gnss);
  GnssRecord gnss_decoded;
  CHECK(moto::device::decode_gnss(gnss_blob.data(), gnss_blob.size(),
                                  &gnss_decoded));
  CHECK(gnss_decoded.t_rx_ms == gnss.t_rx_ms);
  CHECK(gnss_decoded.fix_ms == gnss.fix_ms);
  CHECK(gnss_decoded.speed_cms == gnss.speed_cms);
  CHECK(gnss_decoded.course_cdeg == gnss.course_cdeg);
  CHECK(gnss_decoded.flags == gnss.flags);

  MountRecord mount;
  mount.forward[0] = 0;
  mount.forward[2] = -10000;
  mount.up[1] = 10000;
  mount.trim_cdeg = -125;
  const auto mount_blob = moto::device::encode_mount_record(mount);
  MountRecord mount_decoded;
  CHECK(moto::device::decode_mount_record(mount_blob.data(), mount_blob.size(),
                                          &mount_decoded));
  CHECK(mount_decoded.forward[2] == -10000);
  CHECK(mount_decoded.up[1] == 10000);
  CHECK(mount_decoded.trim_cdeg == -125);
}

void test_header_rejects_corruption() {
  TelemetryHeader header;
  header.session_epoch = 1000;
  header.start_ms = 5;
  header.sample_hz = 112;
  auto blob = moto::device::encode_header(header);
  TelemetryHeader decoded;
  CHECK(moto::device::decode_header(blob.data(), blob.size(), &decoded));
  CHECK(decoded.sample_hz == 112);

  auto corrupt = blob;
  corrupt[9] ^= 0x01U;
  CHECK(!moto::device::decode_header(corrupt.data(), corrupt.size(), &decoded));

  auto short_blob = blob;
  CHECK(!moto::device::decode_header(short_blob.data(), 10, &decoded));
}

std::vector<std::uint8_t> make_fixture(int samples) {
  std::vector<std::uint8_t> file;
  TelemetryHeader header;
  header.session_epoch = 20261023;
  header.start_ms = 1000;
  header.sample_hz = 112;
  const auto head = moto::device::encode_header(header);
  file.insert(file.end(), head.begin(), head.end());

  MountRecord mount;
  mount.forward[2] = -10000;
  mount.up[1] = 10000;
  const auto mount_blob = moto::device::encode_mount_record(mount);
  file.push_back(moto::device::kRecordMount);
  file.insert(file.end(), mount_blob.begin(), mount_blob.end());

  GnssRecord gnss;
  gnss.t_rx_ms = 1200;
  gnss.fix_ms = 1150;
  gnss.speed_cms = 2000;  // 72 km/h
  gnss.course_cdeg = 9000;
  gnss.flags = 0x01;
  const auto gnss_blob = moto::device::encode_gnss(gnss);
  file.push_back(moto::device::kRecordGnss);
  file.insert(file.end(), gnss_blob.begin(), gnss_blob.end());

  for (int i = 0; i < samples; ++i) {
    DynRecord record;
    record.t_ms = static_cast<std::uint32_t>(1200 + i * 1000 / 112);
    record.seq = static_cast<std::uint16_t>(i);
    record.ax_cms2 = 0;
    record.ay_cms2 = 980;
    record.az_cms2 = 0;
    // 20 deg/s about the sensor axis that is the bike's forward axis.
    record.gz_dds = -200;
    record.temp_cdeg = 2500;
    const auto blob = moto::device::encode_dyn(record);
    file.push_back(moto::device::kRecordDyn);
    file.insert(file.end(), blob.begin(), blob.end());
  }
  return file;
}

void test_replay_is_bit_identical() {
  const auto file = make_fixture(336);
  const std::string first = replay_to_csv(file);
  const std::string second = replay_to_csv(file);
  CHECK(first == second);
  CHECK(first.find("# mt r1") == 0);
  CHECK(first.find("# invalid") == std::string::npos);
  const auto lines = static_cast<int>(std::count(first.begin(), first.end(), '\n'));
  CHECK(lines == 2 + 336);

  // A different sample must produce different output; this guards against a
  // replay that silently ignores its input.
  auto changed = file;
  // The last record's first gyro byte: replay must react to raw input, not
  // to the recorded estimator output it deliberately recomputes.
  changed[changed.size() - 16] ^= 0x40U;  // last record's roll rate
  CHECK(replay_to_csv(changed) != first);
}

void test_short_file_is_rejected() {
  const std::vector<std::uint8_t> empty;
  CHECK(replay_to_csv(empty).rfind("# invalid", 0) == 0);
}

}  // namespace

int main() {
  test_record_round_trips();
  test_header_rejects_corruption();
  test_replay_is_bit_identical();
  test_short_file_is_rejected();

  if (failures != 0) {
    std::cerr << failures << " dyn_record checks failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All dyn_record checks passed\n";
  return EXIT_SUCCESS;
}
