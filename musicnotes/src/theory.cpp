#include "theory.h"

#include <algorithm>
#include <array>

namespace theory {
namespace {

constexpr std::array<const char*, 12> kNames = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

struct ChordType {
  const char* suffix;
  std::vector<int> intervals;
};

// Earlier entries win when the same notes can be read as two chords and
// neither root is the bass (e.g. prefer Am7/E over C6/E).
const std::vector<ChordType> kChords = {
    {"", {0, 4, 7}},          {"m", {0, 3, 7}},
    {"dim", {0, 3, 6}},       {"aug", {0, 4, 8}},
    {"sus2", {0, 2, 7}},      {"sus4", {0, 5, 7}},
    {"7", {0, 4, 7, 10}},     {"maj7", {0, 4, 7, 11}},
    {"m7", {0, 3, 7, 10}},    {"m7b5", {0, 3, 6, 10}},
    {"dim7", {0, 3, 6, 9}},   {"mMaj7", {0, 3, 7, 11}},
    {"6", {0, 4, 7, 9}},      {"m6", {0, 3, 7, 9}},
    {"7sus4", {0, 5, 7, 10}}, {"add9", {0, 2, 4, 7}},
    {"madd9", {0, 2, 3, 7}},  {"9", {0, 2, 4, 7, 10}},
    {"maj9", {0, 2, 4, 7, 11}}, {"m9", {0, 2, 3, 7, 10}},
};

int mod12(int n) { return ((n % 12) + 12) % 12; }

}  // namespace

std::string pitchClassName(int pc) { return kNames[mod12(pc)]; }

std::string noteName(int midi) {
  return pitchClassName(midi) + std::to_string(midi / 12 - 1);
}

std::string chordName(const std::vector<int>& midis) {
  if (midis.empty()) return {};
  unsigned mask = 0;
  for (int m : midis) mask |= 1u << mod12(m);
  if (__builtin_popcount(mask) < 3) return {};

  const int bass = mod12(*std::min_element(midis.begin(), midis.end()));
  std::string best;
  size_t bestIndex = kChords.size();
  for (int root = 0; root < 12; ++root) {
    if (!(mask >> root & 1u)) continue;
    for (size_t i = 0; i < kChords.size(); ++i) {
      unsigned tmpl = 0;
      for (int iv : kChords[i].intervals) tmpl |= 1u << mod12(root + iv);
      if (tmpl != mask) continue;
      std::string name = pitchClassName(root) + kChords[i].suffix;
      if (root == bass) return name;  // root position always wins
      if (i < bestIndex) {
        best = name + "/" + pitchClassName(bass);
        bestIndex = i;
      }
      break;
    }
  }
  return best;
}

std::string groupLabel(const std::vector<int>& midis) {
  std::vector<int> notes = midis;
  std::sort(notes.begin(), notes.end());
  notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
  if (notes.empty()) return {};
  if (notes.size() == 1) return noteName(notes[0]);
  if (std::string chord = chordName(notes); !chord.empty()) return chord;
  std::string out;
  for (int n : notes) {
    if (!out.empty()) out += '+';
    out += noteName(n);
  }
  return out;
}

}  // namespace theory
