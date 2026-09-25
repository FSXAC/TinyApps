#include "synth.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kTwoPi = 6.283185307179586;
constexpr float kAttackSeconds = 0.004f;
constexpr float kReleaseSeconds = 0.08f;
constexpr float kSilent = 1e-4f;
constexpr float kMasterGain = 0.2f;
}  // namespace

void Synth::noteOn(int midi) {
  // Retrigger the same note, else take a free voice, else steal the oldest.
  Voice* v = nullptr;
  for (auto& cand : voices_)
    if (cand.active && cand.midi == midi) v = &cand;
  if (!v)
    for (auto& cand : voices_)
      if (!cand.active) { v = &cand; break; }
  if (!v)
    v = &*std::min_element(voices_.begin(), voices_.end(),
                           [](const Voice& a, const Voice& b) { return a.startedAt < b.startedAt; });

  const double freq = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
  // Low notes ring longer than high ones; upper harmonics die faster.
  const double tau = std::clamp(3.0 * std::pow(2.0, -(midi - 60) / 24.0), 0.4, 6.0);

  *v = Voice{};
  v->active = true;
  v->midi = midi;
  v->phaseInc = freq / sampleRate_;
  v->startedAt = ++counter_;
  for (int k = 0; k < kHarmonics; ++k) {
    const int h = k + 1;
    if (freq * h > sampleRate_ * 0.45) break;  // avoid aliasing
    v->gain[k] = 1.0f / std::pow(static_cast<float>(h), 1.5f);
    const double tauK = tau / (1.0 + 0.6 * k);
    v->decay[k] = static_cast<float>(std::exp(-1.0 / (tauK * sampleRate_)));
  }
}

void Synth::noteOff(int midi) {
  for (auto& v : voices_)
    if (v.active && v.midi == midi) v.releasing = true;
}

void Synth::allOff() {
  for (auto& v : voices_) v.releasing = true;
}

void Synth::render(float* out, int frames) {
  std::fill(out, out + frames, 0.0f);
  const float attackStep = 1.0f / (kAttackSeconds * sampleRate_);
  const float releaseMul = std::exp(-1.0f / (kReleaseSeconds * sampleRate_));

  for (auto& v : voices_) {
    if (!v.active) continue;
    for (int i = 0; i < frames; ++i) {
      float s = 0;
      for (int k = 0; k < kHarmonics; ++k) {
        if (v.gain[k] == 0) continue;
        s += v.gain[k] * static_cast<float>(std::sin(kTwoPi * (k + 1) * v.phase));
        v.gain[k] *= v.decay[k];
      }
      out[i] += s * v.attack * v.release;

      v.phase += v.phaseInc;
      if (v.phase >= 1.0) v.phase -= 1.0;
      if (v.attack < 1.0f) v.attack = std::min(1.0f, v.attack + attackStep);
      if (v.releasing) v.release *= releaseMul;
    }
    if (v.release < kSilent || v.gain[0] < kSilent) v.active = false;
  }

  for (int i = 0; i < frames; ++i) out[i] = std::tanh(out[i] * kMasterGain);
}
