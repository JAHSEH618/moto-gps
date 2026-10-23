// Native telemetry replay: same bytes, same estimator, deterministic CSV.
// Usage: moto_replay <file>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "dyn_replay.hpp"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <telemetry-file>\n", argv[0]);
    return 2;
  }
  std::ifstream input(argv[1], std::ios::binary);
  if (!input) {
    std::fprintf(stderr, "cannot open %s\n", argv[1]);
    return 1;
  }
  const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(input)),
                                       std::istreambuf_iterator<char>());
  const std::string csv = replay_to_csv(file);
  std::fwrite(csv.data(), 1, csv.size(), stdout);
  return csv.rfind("# invalid", 0) == 0 ? 1 : 0;
}
