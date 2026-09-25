#include "keymap.h"

namespace {

struct Mapping {
  SDL_Scancode sc;
  int offset;
  const char* label;
};

// The 2nd octave's A# (22) would fall on Backspace, which is undo.
constexpr Mapping kMap[] = {
    {SDL_SCANCODE_TAB, 0, "Tab"},         {SDL_SCANCODE_1, 1, "1"},
    {SDL_SCANCODE_Q, 2, "Q"},             {SDL_SCANCODE_2, 3, "2"},
    {SDL_SCANCODE_W, 4, "W"},             {SDL_SCANCODE_E, 5, "E"},
    {SDL_SCANCODE_4, 6, "4"},             {SDL_SCANCODE_R, 7, "R"},
    {SDL_SCANCODE_5, 8, "5"},             {SDL_SCANCODE_T, 9, "T"},
    {SDL_SCANCODE_6, 10, "6"},            {SDL_SCANCODE_Y, 11, "Y"},
    {SDL_SCANCODE_U, 12, "U"},            {SDL_SCANCODE_8, 13, "8"},
    {SDL_SCANCODE_I, 14, "I"},            {SDL_SCANCODE_9, 15, "9"},
    {SDL_SCANCODE_O, 16, "O"},            {SDL_SCANCODE_P, 17, "P"},
    {SDL_SCANCODE_MINUS, 18, "-"},        {SDL_SCANCODE_LEFTBRACKET, 19, "["},
    {SDL_SCANCODE_EQUALS, 20, "="},       {SDL_SCANCODE_RIGHTBRACKET, 21, "]"},
    {SDL_SCANCODE_BACKSLASH, 23, "\\"},
};

}  // namespace

int keyOffset(SDL_Scancode sc) {
  for (const auto& m : kMap)
    if (m.sc == sc) return m.offset;
  return -1;
}

const char* keyLabel(int offset) {
  for (const auto& m : kMap)
    if (m.offset == offset) return m.label;
  return nullptr;
}
