#pragma once

#include <array>
#include <cstdint>

// Small polyphonic additive synth with a decaying, piano-like envelope.
// Not thread-safe: the caller serializes noteOn/noteOff with render().
class Synth {
 public:
  explicit Synth(int sampleRate) : sampleRate_(sampleRate) {}

  void noteOn(int midi);
  void noteOff(int midi);
  void allOff();
  // Writes `frames` mono float samples.
  void render(float* out, int frames);

 private:
  static constexpr int kVoices = 32;
  static constexpr int kHarmonics = 6;

  struct Voice {
    bool active = false;
    bool releasing = false;
    int midi = -1;
    double phase = 0;
    double phaseInc = 0;
    float attack = 0;
    float release = 1;
    std::array<float, kHarmonics> gain{};
    std::array<float, kHarmonics> decay{};
    uint64_t startedAt = 0;
  };

  int sampleRate_;
  uint64_t counter_ = 0;
  std::array<Voice, kVoices> voices_{};
};
