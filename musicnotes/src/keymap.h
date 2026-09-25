#pragma once

#include <SDL3/SDL_scancode.h>

// Two-octave layout by physical key position (white keys on the Tab row,
// black keys on the number row):
//      1  2     4  5  6     8  9     -  =
//   Tab Q  W  E  R  T  Y  U  I  O  P  [  ]  \
//   C   D  E  F  G  A  B  C  D  E  F  G  A  B
// Returns the semitone offset from the current octave's C, or -1 if unmapped.
int keyOffset(SDL_Scancode sc);

// Keyboard label for a semitone offset ("Tab" for 0), or nullptr if unmapped.
const char* keyLabel(int offset);
