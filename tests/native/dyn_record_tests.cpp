// Telemetry block format and deterministic replay (v2 r1 §11.2).
#include "dyn_record.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include "dyn_replay.hpp"

namespace {

using moto::device::DynRecord;
using moto::device::GnssRecord;
using moto::device::MountRecord;
using moto::device::SessionMeta;

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
  CHECK(decoded.az_cms2 == dyn.az_cms2);
  CHECK(decoded.gx_dds == dyn.gx_dds);
  CHECK(decoded.temp_cdeg == dyn.temp_cdeg);
  CHECK(decoded.lean_eff_cdeg == dyn.lean_eff_cdeg);
  CHECK(decoded.flags == dyn.flags);

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

  MountRecord mount;
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

  SessionMeta meta;
  meta.mode = 1;
  meta.odr_hz = 448;
  meta.sample_hz = 112;
  meta.lever_arm_mm = 900;
  meta.firmware_hash = 0x12345678;
  meta.mount = mount;
  const auto meta_blob = moto::device::encode_session_meta(meta);
  SessionMeta meta_decoded;
  CHECK(moto::device::decode_session_meta(meta_blob.data(), meta_blob.size(),
                                          &meta_decoded));
  CHECK(meta_decoded.mode == 1);
  CHECK(meta_decoded.odr_hz == 448);
  CHECK(meta_decoded.sample_hz == 112);
  CHECK(meta_decoded.lever_arm_mm == 900);
  CHECK(meta_decoded.firmware_hash == 0x12345678);
  CHECK(meta_decoded.mount.up[1] == 10000);

  moto::device::ImuStatsRecord stats;
  stats.t_ms = 123456;
  stats.samples = 112;
  stats.avail_zero = 3;
  stats.lock_waits = 7;
  stats.i2c_errors = 1;
  stats.missed_reads = 2;
  stats.flash_skips = 4;
  stats.interval_max_us = 8930;
  stats.interval_avg_us = 4466;
  stats.mode = 2;
  stats.flags = 1;
  const auto stats_blob = moto::device::encode_imu_stats(stats);
  moto::device::ImuStatsRecord stats_decoded;
  CHECK(moto::device::decode_imu_stats(stats_blob.data(), stats_blob.size(),
                                       &stats_decoded));
  CHECK(stats_decoded.t_ms == 123456);
  CHECK(stats_decoded.samples == 112);
  CHECK(stats_decoded.avail_zero == 3);
  CHECK(stats_decoded.lock_waits == 7);
  CHECK(stats_decoded.i2c_errors == 1);
  CHECK(stats_decoded.missed_reads == 2);
  CHECK(stats_decoded.flash_skips == 4);
  CHECK(stats_decoded.interval_max_us == 8930);
  CHECK(stats_decoded.interval_avg_us == 4466);
  CHECK(stats_decoded.mode == 2);
  CHECK(stats_decoded.flags == 1);
}

void test_block_crc_detects_corruption() {
  std::vector<std::uint8_t> block(moto::device::kBlockSize, 0xFF);
  moto::device::BlockHeader header;
  header.block_seq = 7;
  header.boot_id = 42;
  header.first_sample_seq = 3;
  header.payload_len = 8;
  const auto encoded = moto::device::encode_block_header(header);
  std::memcpy(block.data(), encoded.data(), encoded.size());
  std::memcpy(block.data() + 32, "12345678", 8);
  const std::uint32_t crc = moto::device::block_crc32(block.data());
  block[24] = static_cast<std::uint8_t>(crc & 0xFFU);
  block[25] = static_cast<std::uint8_t>((crc >> 8U) & 0xFFU);
  block[26] = static_cast<std::uint8_t>((crc >> 16U) & 0xFFU);
  block[27] = static_cast<std::uint8_t>((crc >> 24U) & 0xFFU);

  moto::device::BlockHeader decoded;
  CHECK(moto::device::decode_block_header(block.data(), &decoded));
  CHECK(decoded.block_seq == 7);
  CHECK(moto::device::block_crc32(block.data()) ==
        moto::device::block_crc32(block.data()));
  block[100] ^= 0x01U;
  CHECK(moto::device::block_crc32(block.data()) != crc);
}

void append_payload_record(std::vector<std::uint8_t>& payload,
                           std::uint8_t type,
                           const std::vector<std::uint8_t>& body) {
  payload.push_back(type);
  payload.insert(payload.end(), body.begin(), body.end());
}

void append_block(std::vector<std::uint8_t>& file, std::uint32_t boot_id,
                  std::uint32_t block_seq, std::uint32_t first_sample,
                  const std::vector<std::uint8_t>& payload,
                  bool corrupt_crc = false) {
  std::vector<std::uint8_t> block(moto::device::kBlockSize, 0xFF);
  std::memcpy(block.data() + 32, payload.data(), payload.size());
  moto::device::BlockHeader header;
  header.block_seq = block_seq;
  header.boot_id = boot_id;
  header.first_sample_seq = first_sample;
  header.payload_len = static_cast<std::uint16_t>(payload.size());
  const auto encoded = moto::device::encode_block_header(header);
  std::memcpy(block.data(), encoded.data(), encoded.size());
  std::uint32_t crc = moto::device::block_crc32(block.data());
  if (corrupt_crc) crc ^= 0xFFFFFFFFU;
  block[24] = static_cast<std::uint8_t>(crc & 0xFFU);
  block[25] = static_cast<std::uint8_t>((crc >> 8U) & 0xFFU);
  block[26] = static_cast<std::uint8_t>((crc >> 16U) & 0xFFU);
  block[27] = static_cast<std::uint8_t>((crc >> 24U) & 0xFFU);
  file.insert(file.end(), block.begin(), block.end());
}

std::vector<std::uint8_t> session_payload(int samples, bool include_meta) {
  std::vector<std::uint8_t> payload;
  if (include_meta) {
    SessionMeta meta;
    meta.mode = 1;
    meta.odr_hz = 448;
    meta.sample_hz = 112;
    meta.lever_arm_mm = 900;
    meta.firmware_hash = 0xC0FFEE;
    MountRecord mount;
    mount.forward[2] = -10000;
    mount.up[1] = 10000;
    meta.mount = mount;
    const auto meta_blob = moto::device::encode_session_meta(meta);
    append_payload_record(payload, moto::device::kRecordSession,
                          {meta_blob.begin(), meta_blob.end()});
  }
  GnssRecord gnss;
  gnss.t_rx_ms = 100;
  gnss.fix_ms = 50;
  gnss.speed_cms = 2000;
  gnss.flags = 0x01;
  const auto gnss_blob = moto::device::encode_gnss(gnss);
  append_payload_record(payload, moto::device::kRecordGnss,
                        {gnss_blob.begin(), gnss_blob.end()});

  for (int i = 0; i < samples; ++i) {
    DynRecord record;
    record.t_ms = static_cast<std::uint32_t>(100 + i * 1000 / 112);
    record.seq = static_cast<std::uint16_t>(i);
    record.ay_cms2 = 980;
    record.gz_dds = -200;
    record.temp_cdeg = 2500;
    const auto blob = moto::device::encode_dyn(record);
    append_payload_record(payload, moto::device::kRecordDyn,
                          {blob.begin(), blob.end()});
  }
  return payload;
}

std::vector<std::uint8_t> make_fixture() {
  std::vector<std::uint8_t> file;
  append_block(file, 0xAAAA, 0, 0, session_payload(120, true));
  append_block(file, 0xAAAA, 1, 120, session_payload(80, false));
  // A torn block must be skipped, not abort the file.
  append_block(file, 0xAAAA, 2, 200, session_payload(40, false), true);
  // Erased sectors between blocks are legal after a skipped write.
  file.insert(file.end(), moto::device::kBlockSize, 0xFF);
  append_block(file, 0xBBBB, 3, 0, session_payload(60, true));
  return file;
}

void test_replay_is_bit_identical_and_session_aware() {
  const auto file = make_fixture();
  const std::string first = replay_to_csv(file);
  const std::string second = replay_to_csv(file);
  CHECK(first == second);
  CHECK(first.find("# mt r1 blocks") == 0);
  CHECK(first.find("# meta mode=1 odr=448 sample=112 h_mm=900") !=
        std::string::npos);
  CHECK(first.find("# session 43690") != std::string::npos);
  CHECK(first.find("# session 48059") != std::string::npos);
  // 120 + 80 valid records; the corrupt block (40) and the erased block add
  // no data lines.
  const int lines =
      static_cast<int>(std::count(first.begin(), first.end(), '\n'));
  CHECK(lines == 2 + 2 + 2 + 120 + 80 + 60);

  auto changed = file;
  changed[moto::device::kBlockSize + 32 + 20] ^= 0x10U;
  CHECK(replay_to_csv(changed) != first);
}

void test_empty_file_is_header_only() {
  const std::vector<std::uint8_t> empty;
  const std::string csv = replay_to_csv(empty);
  const int lines = static_cast<int>(std::count(csv.begin(), csv.end(), '\n'));
  CHECK(lines == 2);
}

}  // namespace

int main() {
  test_record_round_trips();
  test_block_crc_detects_corruption();
  test_replay_is_bit_identical_and_session_aware();
  test_empty_file_is_header_only();

  if (failures != 0) {
    std::cerr << failures << " dyn_record checks failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All dyn_record checks passed\n";
  return EXIT_SUCCESS;
}
