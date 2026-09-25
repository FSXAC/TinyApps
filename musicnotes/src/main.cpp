#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "keymap.h"
#include "note_log.h"
#include "synth.h"
#include "theory.h"
#include "tiny.h"

namespace {

constexpr int kWidth = 1040;
constexpr int kHeight = 450;
constexpr int kSampleRate = 48000;
constexpr int kMinOctave = 0;
constexpr int kMaxOctave = 7;
constexpr int kDefaultOctave = 3;      // C3-B4, middle C in the centre
constexpr int kVisibleSemitones = 24;  // two octaves, C to B
constexpr int kWhiteKeys = 14;
constexpr float kMargin = 16;

// Layout, in logical pixels.
constexpr float kHeaderY = 16;
constexpr float kHeaderH = 34;
constexpr float kArrowW = 34;
constexpr float kRangeW = 120;  // octave range label between the arrows
constexpr float kButtonW = 96;
constexpr SDL_FRect kLogPanel = {kMargin, 64, kWidth - 2 * kMargin, 148};
constexpr float kLogPad = 14;
constexpr float kLogLineH = 30;
constexpr float kStatusY = 222;
constexpr SDL_FRect kKeyboard = {kMargin, 252, kWidth - 2 * kMargin, 166};
constexpr float kHelpY = 428;

// Logical pixels are points, i.e. one design unit (see common/DESIGN.md).
constexpr int kU = 1;
constexpr int kCaption = TINY_CAPTION * kU;
constexpr int kBody = TINY_BODY * kU;
constexpr int kTitle = TINY_TITLE * kU;

int ix(float v) { return static_cast<int>(std::lround(v)); }

bool isBlack(int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

enum class Action { OctaveDown, OctaveUp, Copy, Undo, Clear };

struct Button {
  SDL_FRect rect;
  const char* label;
  Action action;
};

class App {
 public:
  bool init();
  void run();
  void shutdown();

 private:
  static void SDLCALL audioCallback(void* userdata, SDL_AudioStream* stream, int additional, int total);

  int baseNote() const { return (octave_ + 1) * 12; }
  void press(int midi);
  void release(int midi);
  void releaseAll();
  void perform(Action a);
  void handleEvent(SDL_Event& e);

  SDL_FRect keyRect(int offset) const;
  int keyAt(float x, float y) const;

  void draw();
  // Draws text with its top-left at (x, y) in the given color; returns its width.
  int drawText(float x, float y, const std::string& s, int scale, TinyStyle st, TinyColor c);
  void drawHeader();
  void drawLog();
  void drawKeyboard();

  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_AudioStream* stream_ = nullptr;
  std::vector<float> audioBuf_;
  TinyFont font_{};

  Synth synth_{kSampleRate};
  NoteLog log_;
  bool running_ = true;
  int octave_ = kDefaultOctave;
  std::array<int, 128> holdCount_{};      // mouse and keyboard can hold the same note
  std::map<SDL_Scancode, int> keyNotes_;  // note each computer key started
  int mouseNote_ = -1;
  bool mouseDown_ = false;
  SDL_FPoint mouse_{-1, -1};
  std::vector<Button> buttons_;
  Uint64 copiedUntil_ = 0;
};

bool App::init() {
  // Small device buffer keeps key-to-sound latency around 5 ms.
  SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return false;
  }
  if (!SDL_CreateWindowAndRenderer("MusicNotes", kWidth, kHeight,
                                   SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &window_,
                                   &renderer_)) {
    SDL_Log("Window creation failed: %s", SDL_GetError());
    return false;
  }
  SDL_SetRenderLogicalPresentation(renderer_, kWidth, kHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
  SDL_SetRenderVSync(renderer_, 1);
  if (!tiny_font_init(&font_, renderer_)) return false;

  const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kSampleRate};
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audioCallback, this);
  if (stream_) {
    SDL_ResumeAudioStreamDevice(stream_);
  } else {
    SDL_Log("No audio (continuing silently): %s", SDL_GetError());
  }

  const float y = kHeaderY;
  constexpr float gap = TINY_PAD * kU;
  buttons_ = {
      {{kMargin, y, kArrowW, kHeaderH}, "‹", Action::OctaveDown},
      {{kMargin + kArrowW + kRangeW, y, kArrowW, kHeaderH}, "›", Action::OctaveUp},
      {{kWidth - kMargin - 3 * kButtonW - 2 * gap, y, kButtonW, kHeaderH}, "Copy", Action::Copy},
      {{kWidth - kMargin - 2 * kButtonW - gap, y, kButtonW, kHeaderH}, "Undo", Action::Undo},
      {{kWidth - kMargin - kButtonW, y, kButtonW, kHeaderH}, "Clear", Action::Clear},
  };
  return true;
}

void App::shutdown() {
  if (stream_) SDL_DestroyAudioStream(stream_);
  tiny_font_free(&font_);
  SDL_DestroyRenderer(renderer_);
  SDL_DestroyWindow(window_);
  SDL_Quit();
}

// Runs on the audio thread with the stream lock held.
void SDLCALL App::audioCallback(void* userdata, SDL_AudioStream* stream, int additional, int) {
  auto* app = static_cast<App*>(userdata);
  const int frames = additional / static_cast<int>(sizeof(float));
  if (frames <= 0) return;
  app->audioBuf_.resize(frames);
  app->synth_.render(app->audioBuf_.data(), frames);
  SDL_PutAudioStreamData(stream, app->audioBuf_.data(), frames * static_cast<int>(sizeof(float)));
}

void App::press(int midi) {
  if (midi < 0 || midi > 127) return;
  if (holdCount_[midi]++ > 0) return;
  if (stream_) SDL_LockAudioStream(stream_);
  synth_.noteOn(midi);
  if (stream_) SDL_UnlockAudioStream(stream_);
  log_.noteOn(midi);
}

void App::release(int midi) {
  if (midi < 0 || midi > 127 || holdCount_[midi] == 0) return;
  if (--holdCount_[midi] > 0) return;
  if (stream_) SDL_LockAudioStream(stream_);
  synth_.noteOff(midi);
  if (stream_) SDL_UnlockAudioStream(stream_);
  log_.noteOff(midi);
}

void App::releaseAll() {
  for (auto& [sc, midi] : keyNotes_) release(midi);
  keyNotes_.clear();
  if (mouseNote_ >= 0) release(mouseNote_);
  mouseNote_ = -1;
  mouseDown_ = false;
}

void App::perform(Action a) {
  switch (a) {
    case Action::OctaveDown: octave_ = std::max(kMinOctave, octave_ - 1); break;
    case Action::OctaveUp: octave_ = std::min(kMaxOctave, octave_ + 1); break;
    case Action::Copy:
      SDL_SetClipboardText(log_.text().c_str());
      copiedUntil_ = SDL_GetTicks() + 1200;
      break;
    case Action::Undo: log_.undo(); break;
    case Action::Clear: log_.clear(); break;
  }
}

void App::handleEvent(SDL_Event& e) {
  SDL_ConvertEventToRenderCoordinates(renderer_, &e);
  switch (e.type) {
    case SDL_EVENT_QUIT:
      running_ = false;
      break;

    case SDL_EVENT_KEY_DOWN: {
      if (e.key.repeat) break;
      if (e.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) {
        if (e.key.scancode == SDL_SCANCODE_C) perform(Action::Copy);
        if (e.key.scancode == SDL_SCANCODE_Z) perform(Action::Undo);
        break;
      }
      switch (e.key.scancode) {
        case SDL_SCANCODE_Z: perform(Action::OctaveDown); break;
        case SDL_SCANCODE_X: perform(Action::OctaveUp); break;
        case SDL_SCANCODE_BACKSPACE: perform(Action::Undo); break;
        case SDL_SCANCODE_ESCAPE: perform(Action::Clear); break;
        default: {
          const int off = keyOffset(e.key.scancode);
          if (off >= 0 && !keyNotes_.count(e.key.scancode)) {
            keyNotes_[e.key.scancode] = baseNote() + off;
            press(baseNote() + off);
          }
        }
      }
      break;
    }

    case SDL_EVENT_KEY_UP: {
      // Release the note the key started, even if the octave changed since.
      auto it = keyNotes_.find(e.key.scancode);
      if (it != keyNotes_.end()) {
        release(it->second);
        keyNotes_.erase(it);
      }
      break;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      if (e.button.button != SDL_BUTTON_LEFT) break;
      const SDL_FPoint p{e.button.x, e.button.y};
      for (const auto& b : buttons_) {
        if (SDL_PointInRectFloat(&p, &b.rect)) {
          perform(b.action);
          return;
        }
      }
      mouseDown_ = true;
      const int off = keyAt(p.x, p.y);
      if (off >= 0) {
        mouseNote_ = baseNote() + off;
        press(mouseNote_);
      }
      break;
    }

    case SDL_EVENT_MOUSE_MOTION: {
      mouse_ = {e.motion.x, e.motion.y};
      if (!mouseDown_) break;
      const int off = keyAt(e.motion.x, e.motion.y);
      const int note = off >= 0 ? baseNote() + off : -1;
      if (note != mouseNote_) {  // glissando across keys
        if (mouseNote_ >= 0) release(mouseNote_);
        mouseNote_ = note;
        if (note >= 0) press(note);
      }
      break;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (e.button.button != SDL_BUTTON_LEFT) break;
      if (mouseNote_ >= 0) release(mouseNote_);
      mouseNote_ = -1;
      mouseDown_ = false;
      break;

    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
      mouse_ = {-1, -1};
      break;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
      releaseAll();
      break;
  }
}

SDL_FRect App::keyRect(int offset) const {
  constexpr int kWhiteIndex[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
  const float whiteW = kKeyboard.w / kWhiteKeys;
  const int pc = offset % 12;
  const int white = (offset / 12) * 7 + kWhiteIndex[pc];
  if (!isBlack(pc)) return {kKeyboard.x + white * whiteW, kKeyboard.y, whiteW, kKeyboard.h};
  const float blackW = whiteW * 0.58f;
  return {kKeyboard.x + (white + 1) * whiteW - blackW / 2, kKeyboard.y, blackW, kKeyboard.h * 0.62f};
}

int App::keyAt(float x, float y) const {
  const SDL_FPoint p{x, y};
  for (int pass = 0; pass < 2; ++pass) {  // black keys sit on top, so test them first
    for (int off = 0; off < kVisibleSemitones; ++off) {
      if (isBlack(off % 12) != (pass == 0)) continue;
      const SDL_FRect r = keyRect(off);
      if (SDL_PointInRectFloat(&p, &r)) return off;
    }
  }
  return -1;
}

int App::drawText(float x, float y, const std::string& s, int scale, TinyStyle st, TinyColor c) {
  tiny_set_color(renderer_, c);
  return tiny_text(&font_, s.c_str(), ix(x), ix(y), scale, st);
}

void App::drawHeader() {
  const bool copied = SDL_GetTicks() < copiedUntil_;
  for (const auto& b : buttons_) {
    const bool isCopy = b.action == Action::Copy;
    const bool hover = SDL_PointInRectFloat(&mouse_, &b.rect);
    // "Copied" confirmation shows as a pressed (inverted) button.
    const TinyState st = isCopy && copied ? TINY_PRESSED : hover ? TINY_HOVER : TINY_NORMAL;
    const TinyColor ink = tiny_button(renderer_, ix(b.rect.x), ix(b.rect.y), ix(b.rect.w), ix(b.rect.h), kU, st);
    const std::string label = isCopy && copied ? "Copied" : b.label;
    const int w = tiny_text_width(label.c_str(), kBody, TINY_BOLD);
    drawText(b.rect.x + (b.rect.w - w) / 2, b.rect.y + (b.rect.h - tiny_text_height(kBody)) / 2, label, kBody,
             TINY_BOLD, ink);
  }

  // Octave range between the arrow buttons.
  const std::string range = theory::noteName(baseNote()) + " – " +
                            theory::noteName(baseNote() + kVisibleSemitones - 1);
  const int w = tiny_text_width(range.c_str(), kBody, TINY_BOLD);
  const float mid = kMargin + kArrowW + kRangeW / 2;
  drawText(mid - w / 2.0f, kHeaderY + (kHeaderH - tiny_text_height(kBody)) / 2, range, kBody, TINY_BOLD, TINY_INK);
}

void App::drawLog() {
  tiny_panel(renderer_, ix(kLogPanel.x), ix(kLogPanel.y), ix(kLogPanel.w), ix(kLogPanel.h), kU);

  struct Token {
    const std::string* text;
    bool pending;
  };
  const std::string pending = log_.pendingLabel();
  std::vector<Token> tokens;
  for (const auto& e : log_.entries()) tokens.push_back({&e, false});
  if (!pending.empty()) tokens.push_back({&pending, true});

  if (tokens.empty()) {
    drawText(kLogPanel.x + kLogPad, kLogPanel.y + kLogPad, "Play something — notes show up here", kBody,
             TINY_PLAIN, TINY_DIM);
    return;
  }

  // Word-wrap, then show the most recent lines that fit.
  const float maxW = kLogPanel.w - 2 * kLogPad;
  const float space = tiny_text_width(" ", kTitle, TINY_PLAIN) + kTitle;
  std::vector<std::vector<Token>> lines(1);
  float used = 0;
  for (const auto& t : tokens) {
    const float w = tiny_text_width(t.text->c_str(), kTitle, TINY_PLAIN);
    if (used > 0 && used + space + w > maxW) {
      lines.emplace_back();
      used = 0;
    }
    used += (used > 0 ? space : 0) + w;
    lines.back().push_back(t);
  }

  const size_t maxLines = static_cast<size_t>((kLogPanel.h - 2 * kLogPad) / kLogLineH);
  const size_t first = lines.size() > maxLines ? lines.size() - maxLines : 0;
  for (size_t i = first; i < lines.size(); ++i) {
    float x = kLogPanel.x + kLogPad;
    const float y = kLogPanel.y + kLogPad + (i - first) * kLogLineH;
    for (const Token& t : lines[i]) {
      if (t.pending) {
        // The group still being held is highlighted like a selection: inverted.
        const int w = tiny_text_width(t.text->c_str(), kTitle, TINY_PLAIN);
        const int pad = TINY_GAP * kU;
        tiny_set_color(renderer_, TINY_INK);
        tiny_fill_round(renderer_, ix(x) - pad, ix(y) - pad, w + 2 * pad, tiny_line_height(kTitle) - kTitle + 2 * pad,
                        TINY_RADIUS_SMALL, kU, TINY_CORNERS);
      }
      x += drawText(x, y, *t.text, kTitle, TINY_PLAIN, t.pending ? TINY_PAPER : TINY_INK) + space;
    }
  }
}

void App::drawKeyboard() {
  tiny_set_color(renderer_, TINY_INK);
  tiny_fill_rect(renderer_, ix(kKeyboard.x) - kU, ix(kKeyboard.y) - 4 * kU, ix(kKeyboard.w) + 2 * kU,
                 ix(kKeyboard.h) + 5 * kU);

  // Pressed keys invert: white keys go ink, black keys go paper.
  for (int pass = 0; pass < 2; ++pass) {  // whites first so blacks draw on top
    for (int off = 0; off < kVisibleSemitones; ++off) {
      const int pc = off % 12;
      if (isBlack(pc) != (pass == 1)) continue;
      const int midi = baseNote() + off;
      const bool down = midi <= 127 && holdCount_[midi] > 0;
      const std::string name = theory::noteName(midi);
      const char* hint = keyLabel(off);
      const SDL_FRect kr = keyRect(off);
      const int x = ix(kr.x), y = ix(kr.y), w = ix(kr.x + kr.w) - x, h = ix(kr.h);
      const float cx = kr.x + kr.w / 2;

      if (!isBlack(pc)) {
        tiny_set_color(renderer_, down ? TINY_INK : TINY_PAPER);
        tiny_fill_round(renderer_, x + kU, y, w - 2 * kU, h, TINY_RADIUS_SMALL, kU, TINY_BL | TINY_BR);
        const TinyStyle st = pc == 0 ? TINY_BOLD : TINY_PLAIN;  // C anchors each octave
        const TinyColor nameColor = down ? TINY_PAPER : pc == 0 ? TINY_INK : TINY_DIM;
        const int nameY = y + h - 12 * kU - tiny_text_height(kBody);
        drawText(cx - tiny_text_width(name.c_str(), kBody, st) / 2.0f, nameY, name, kBody, st, nameColor);
        if (hint)
          drawText(cx - tiny_text_width(hint, kCaption, TINY_PLAIN) / 2.0f, nameY - TINY_PAD * kU - tiny_text_height(kCaption),
                   hint, kCaption, TINY_PLAIN, down ? TINY_SHADE : TINY_GRAY);
      } else {
        tiny_frame_round(renderer_, x, y, w, h, TINY_RADIUS_SMALL, kU, TINY_BL | TINY_BR,
                         down ? TINY_PAPER : TINY_INK, TINY_INK);
        const int nameY = y + h - 10 * kU - tiny_text_height(kCaption);
        drawText(cx - tiny_text_width(name.c_str(), kCaption, TINY_PLAIN) / 2.0f, nameY, name, kCaption, TINY_PLAIN,
                 down ? TINY_INK : TINY_PAPER);
        if (hint)
          drawText(cx - tiny_text_width(hint, kCaption, TINY_PLAIN) / 2.0f, nameY - TINY_PAD * kU - tiny_text_height(kCaption),
                   hint, kCaption, TINY_PLAIN, down ? TINY_DIM : TINY_GRAY);
      }
    }
  }
}

void App::draw() {
  tiny_set_color(renderer_, TINY_PAPER);
  SDL_RenderClear(renderer_);

  drawHeader();
  drawLog();

  const std::vector<int> held = log_.held();
  if (!held.empty()) {
    std::string status;
    for (int n : held) status += theory::noteName(n) + "  ";
    if (std::string chord = theory::chordName(held); !chord.empty()) status += "→  " + chord;
    drawText(kMargin + 2, kStatusY, status, kBody, TINY_BOLD, TINY_INK);
  }

  drawKeyboard();

  drawText(kMargin, kHelpY,
           "Tab – \\  white keys   ·   number row  black keys   ·   Z / X  octave   "
           "·   Backspace  undo   ·   Esc  clear   ·   ⌘C  copy",
           kCaption, TINY_PLAIN, TINY_DIM);

  tiny_present(renderer_);
}

// Event-driven: redraws only after input (or when the "Copied" flash expires),
// so the app idles at ~0% CPU.
void App::run() {
  draw();
  while (running_) {
    Sint32 timeout = -1;
    if (copiedUntil_) {
      const Uint64 now = SDL_GetTicks();
      timeout = copiedUntil_ > now ? static_cast<Sint32>(copiedUntil_ - now) : 0;
    }
    SDL_Event e;
    if (SDL_WaitEventTimeout(&e, timeout)) {
      handleEvent(e);
      while (SDL_PollEvent(&e)) handleEvent(e);
    }
    if (copiedUntil_ && SDL_GetTicks() >= copiedUntil_) copiedUntil_ = 0;
    draw();
  }
}

}  // namespace

int main(int, char**) {
  App app;
  if (!app.init()) return 1;
  app.run();
  app.shutdown();
  return 0;
}
