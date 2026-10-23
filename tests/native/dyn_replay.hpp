#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Replays a telemetry file through the same LeanEstimator the firmware runs.
// Deterministic by construction: no clocks, no allocation-dependent ordering,
// and fixed-precision formatting.
std::string replay_to_csv(const std::vector<std::uint8_t>& file);
