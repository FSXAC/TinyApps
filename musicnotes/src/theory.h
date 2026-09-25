#pragma once

#include <string>
#include <vector>

namespace theory {

// Name for a pitch class 0-11 (sharps only): 0 -> "C", 1 -> "C#".
std::string pitchClassName(int pc);

// Scientific pitch name for a MIDI note: 60 -> "C4", 61 -> "C#4".
std::string noteName(int midi);

// Chord symbol for a set of MIDI notes, e.g. "Am7", or "C/E" when the bass
// isn't the root. Empty if fewer than 3 distinct pitch classes or no match.
std::string chordName(const std::vector<int>& midis);

// Log label for notes played together: a single note name ("C4"), a chord
// symbol ("Am7"), or the notes joined with '+' ("C4+E4").
std::string groupLabel(const std::vector<int>& midis);

}  // namespace theory
