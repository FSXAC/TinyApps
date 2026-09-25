// Minimal test runner for the SDL-free core. Run via `ctest` or directly.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "note_log.h"
#include "synth.h"
#include "theory.h"

static int failures = 0;

#define CHECK_EQ(actual, expected)                                                    \
  do {                                                                                \
    const std::string a_ = (actual), e_ = (expected);                                 \
    if (a_ != e_) {                                                                   \
      std::printf("%s:%d: %s\n  got      '%s'\n  expected '%s'\n", __FILE__, __LINE__, \
                  #actual, a_.c_str(), e_.c_str());                                   \
      ++failures;                                                                     \
    }                                                                                 \
  } while (0)

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static void testNoteNames() {
  using theory::noteName;
  CHECK_EQ(noteName(60), "C4");
  CHECK_EQ(noteName(61), "C#4");
  CHECK_EQ(noteName(69), "A4");
  CHECK_EQ(noteName(21), "A0");
  CHECK_EQ(noteName(71), "B4");
  CHECK_EQ(noteName(72), "C5");
  CHECK_EQ(noteName(127), "G9");
}

static void testChords() {
  using theory::chordName;
  CHECK_EQ(chordName({60, 64, 67}), "C");
  CHECK_EQ(chordName({57, 60, 64}), "Am");
  CHECK_EQ(chordName({57, 60, 64, 67}), "Am7");
  CHECK_EQ(chordName({60, 64, 67, 71}), "Cmaj7");
  CHECK_EQ(chordName({55, 59, 62, 65}), "G7");
  CHECK_EQ(chordName({64, 67, 72}), "C/E");                 // first inversion
  CHECK_EQ(chordName({60, 64, 67, 69}), "C6");              // bass decides C6 vs Am7
  CHECK_EQ(chordName({64, 67, 69, 72}), "Am7/E");           // neither root in bass
  CHECK_EQ(chordName({60, 63, 66, 69}), "Cdim7");
  CHECK_EQ(chordName({60, 62, 67}), "Csus2");
  CHECK_EQ(chordName({67, 72, 74}), "Gsus4");
  CHECK_EQ(chordName({48, 60, 64, 67, 76}), "C");           // doubled notes
  CHECK_EQ(chordName({60, 64}), "");
  CHECK_EQ(chordName({60, 61, 62}), "");
}

static void testGroupLabel() {
  using theory::groupLabel;
  CHECK_EQ(groupLabel({60}), "C4");
  CHECK_EQ(groupLabel({64, 60}), "C4+E4");
  CHECK_EQ(groupLabel({60, 72}), "C4+C5");
  CHECK_EQ(groupLabel({67, 60, 64}), "C");
}

static void testNoteLog() {
  NoteLog log;
  log.noteOn(60);
  CHECK_EQ(log.pendingLabel(), "C4");
  log.noteOff(60);
  log.noteOn(62);
  log.noteOff(62);
  CHECK_EQ(log.text(), "C4 D4");

  // Chord: stays pending until every note is released.
  log.noteOn(57);
  log.noteOn(60);
  log.noteOn(64);
  CHECK_EQ(log.pendingLabel(), "Am");
  log.noteOff(60);
  log.noteOff(57);
  CHECK_EQ(log.text(), "C4 D4");
  log.noteOff(64);
  CHECK_EQ(log.text(), "C4 D4 Am");
  CHECK_EQ(log.pendingLabel(), "");

  log.noteOff(99);  // stray release is ignored
  CHECK_EQ(log.text(), "C4 D4 Am");

  log.undo();
  CHECK_EQ(log.text(), "C4 D4");
  log.clear();
  CHECK_EQ(log.text(), "");
}

static void testSynth() {
  Synth synth(48000);
  std::vector<float> buf(4800);
  synth.render(buf.data(), 4800);
  CHECK(buf[100] == 0.0f);

  synth.noteOn(69);
  synth.render(buf.data(), 4800);
  float peak = 0;
  for (float s : buf) peak = std::fmax(peak, std::fabs(s));
  CHECK(peak > 0.05f && peak <= 1.0f);

  synth.noteOff(69);
  for (int i = 0; i < 10; ++i) synth.render(buf.data(), 4800);
  CHECK(std::fabs(buf.back()) < 1e-3f);
}

int main() {
  testNoteNames();
  testChords();
  testGroupLabel();
  testNoteLog();
  testSynth();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}
