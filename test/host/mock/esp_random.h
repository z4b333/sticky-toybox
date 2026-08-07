#pragma once
#include <cstdint>
#include <random>
inline std::mt19937& hostRng() {
  static std::mt19937 rng(42);
  return rng;
}
inline uint32_t esp_random() { return hostRng()(); }
// The harness opens every app dozens of times while checking hub routing, and
// each of those draws random numbers. Reseeding before the screenshots keeps
// the puzzles, fleets and dice rolls in the images stable from run to run.
inline void hostReseed(uint32_t seed) { hostRng().seed(seed); }
