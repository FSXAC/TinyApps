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
#include "text.h"
#include "theory.h"

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
constexpr SDL_FRect kLogPanel = {kMargin, 64, kWidth - 2 * kMargin, 148};
constexpr float kLogPad = 14;
constexpr float kLogLineH = 28;
constexpr int kLogFontSize = 20;
constexpr float kStatusY = 222;
constexpr SDL_FRect kKeyboard = {kMargin, 252, kWidth - 2 * kMargin, 166};
constexpr float kHelpY = 428;

constexpr SDL_Color kBg{24, 25, 28, 255};
constexpr SDL_Color kPanel{34, 36, 41, 255};
constexpr SDL_Color kText{232, 232, 235, 255};
constexpr SDL_Color kDim{128, 132, 140, 255};
constexpr SDL_Color kAccent{104, 162, 255, 255};
constexpr SDL_Color kButton{44, 47, 54, 255};
constexpr SDL_Color kButtonHover{58, 62, 71, 255};
constexpr SDL_Color kKeyGap{8, 8, 10, 255};
constexpr SDL_Color kWhiteKey{244, 244, 246, 255};
constexpr SDL_Color kWhiteKeyDown{164, 198, 255, 255};
constexpr SDL_Color kWhiteLabel{96, 98, 106, 255};
constexpr SDL_Color kWhiteLabelC{28, 30, 34, 255};
constexpr SDL_Color kWhiteHint{178, 181, 188, 255};
constexpr SDL_Color kWhiteHintDown{92, 122, 180, 255};
constexpr SDL_Color kBlackKey{22, 23, 26, 255};
constexpr SDL_Color kBlackKeyTop{46, 48, 54, 255};
constexpr SDL_Color kBlackKeyDown{48, 98, 206, 255};
constexpr SDL_Color kBlackKeyDownTop{72, 128, 232, 255};
constexpr SDL_Color kBlackLabel{205, 208, 214, 255};
constexpr SDL_Color kBlackHint{118, 122, 130, 255};

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
  void updateTextScale();

  SDL_FRect keyRect(int offset) const;
  int keyAt(float x, float y) const;

  void draw();
  // Draws text with its top-left at (x, y); returns its width.
  float drawText(float x, float y, const std::string& s, Face face, int size, SDL_Color c);
  float textWidth(const std::string& s, Face face, int size);
  void fillRect(const SDL_FRect& r, SDL_Color c);
  // Per-corner radii: top-left, top-right, bottom-right, bottom-left.
  void fillRound(const SDL_FRect& r, std::array<float, 4> radii, SDL_Color c);
  void fillRound(const SDL_FRect& r, float radius, SDL_Color c) { fillRound(r, {radius, radius, radius, radius}, c); }
  void drawHeader();
  void drawLog();
  void drawKeyboard();

  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_AudioStream* stream_ = nullptr;
  std::vector<float> audioBuf_;
  TextCache text_;

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
  if (!text_.init(renderer_)) return false;
  updateTextScale();

  const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kSampleRate};
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audioCallback, this);
  if (stream_) {
    SDL_ResumeAudioStreamDevice(stream_);
  } else {
    SDL_Log("No audio (continuing silently): %s", SDL_GetError());
  }

  const float y = kHeaderY;
  buttons_ = {
      {{kMargin, y, 34, kHeaderH}, "‹", Action::OctaveDown},
      {{kMargin + 34 + 100, y, 34, kHeaderH}, "›", Action::OctaveUp},
      {{kWidth - kMargin - 3 * 76 - 16, y, 76, kHeaderH}, "Copy", Action::Copy},
      {{kWidth - kMargin - 2 * 76 - 8, y, 76, kHeaderH}, "Undo", Action::Undo},
      {{kWidth - kMargin - 76, y, 76, kHeaderH}, "Clear", Action::Clear},
  };
  return true;
}

void App::shutdown() {
  if (stream_) SDL_DestroyAudioStream(stream_);
  text_.shutdown();
  SDL_DestroyRenderer(renderer_);
  SDL_DestroyWindow(window_);
  SDL_Quit();
}

void App::updateTextScale() {
  int w = 0, h = 0;
  SDL_GetRenderOutputSize(renderer_, &w, &h);
  text_.setScale(std::max(1.0f, std::min(w / float(kWidth), h / float(kHeight))));
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

    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
      updateTextScale();
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

void App::fillRect(const SDL_FRect& r, SDL_Color c) {
  SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
  SDL_RenderFillRect(renderer_, &r);
}

void App::fillRound(const SDL_FRect& r, std::array<float, 4> radii, SDL_Color c) {
  constexpr int kSeg = 6;
  constexpr int kPerimeter = 4 * (kSeg + 1);
  constexpr float kPi = 3.14159265f;
  const SDL_FColor fc{c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f};
  // Corner arc centres, clockwise from top-left, with each arc's start angle.
  const std::array<SDL_FPoint, 4> centres = {{{r.x + radii[0], r.y + radii[0]},
                                              {r.x + r.w - radii[1], r.y + radii[1]},
                                              {r.x + r.w - radii[2], r.y + r.h - radii[2]},
                                              {r.x + radii[3], r.y + r.h - radii[3]}}};
  std::array<SDL_Vertex, kPerimeter + 1> v{};
  v[0] = {{r.x + r.w / 2, r.y + r.h / 2}, fc, {0, 0}};
  int n = 1;
  for (int corner = 0; corner < 4; ++corner) {
    const float start = kPi + corner * kPi / 2;
    for (int i = 0; i <= kSeg; ++i) {
      const float a = start + i * (kPi / 2) / kSeg;
      v[n++] = {{centres[corner].x + radii[corner] * std::cos(a),
                 centres[corner].y + radii[corner] * std::sin(a)},
                fc,
                {0, 0}};
    }
  }
  std::array<int, kPerimeter * 3> idx{};
  for (int i = 0; i < kPerimeter; ++i) {
    idx[i * 3] = 0;
    idx[i * 3 + 1] = 1 + i;
    idx[i * 3 + 2] = 1 + (i + 1) % kPerimeter;
  }
  SDL_RenderGeometry(renderer_, nullptr, v.data(), v.size(), idx.data(), idx.size());
}

float App::drawText(float x, float y, const std::string& s, Face face, int size, SDL_Color c) {
  const TextCache::Text& t = text_.get(s, face, size, c);
  if (t.tex) {
    const SDL_FRect dst{std::round(x), std::round(y), t.w, t.h};
    SDL_RenderTexture(renderer_, t.tex, nullptr, &dst);
  }
  return t.w;
}

float App::textWidth(const std::string& s, Face face, int size) {
  return text_.get(s, face, size, kText).w;
}

void App::drawHeader() {
  const bool copied = SDL_GetTicks() < copiedUntil_;
  for (const auto& b : buttons_) {
    const bool hover = SDL_PointInRectFloat(&mouse_, &b.rect);
    fillRound(b.rect, 6, hover ? kButtonHover : kButton);
    const bool isCopy = b.action == Action::Copy;
    const std::string label = isCopy && copied ? "Copied" : b.label;
    const TextCache::Text& t = text_.get(label, Face::Ui, 15, isCopy && copied ? kAccent : kText);
    drawText(b.rect.x + (b.rect.w - t.w) / 2, b.rect.y + (b.rect.h - t.h) / 2, label, Face::Ui, 15,
             isCopy && copied ? kAccent : kText);
  }

  // Octave range between the arrow buttons.
  const std::string range = theory::noteName(baseNote()) + " – " +
                            theory::noteName(baseNote() + kVisibleSemitones - 1);
  const TextCache::Text& t = text_.get(range, Face::Ui, 16, kText);
  const float mid = kMargin + 34 + 50;
  drawText(mid - t.w / 2, kHeaderY + (kHeaderH - t.h) / 2, range, Face::Ui, 16, kText);
}

void App::drawLog() {
  fillRound(kLogPanel, 8, kPanel);

  struct Token {
    const std::string* text;
    bool pending;
  };
  const std::string pending = log_.pendingLabel();
  std::vector<Token> tokens;
  for (const auto& e : log_.entries()) tokens.push_back({&e, false});
  if (!pending.empty()) tokens.push_back({&pending, true});

  if (tokens.empty()) {
    drawText(kLogPanel.x + kLogPad, kLogPanel.y + kLogPad, "Play something — notes show up here",
             Face::Ui, 17, kDim);
    return;
  }

  // Word-wrap, then show the most recent lines that fit.
  const float maxW = kLogPanel.w - 2 * kLogPad;
  const float space = textWidth(" ", Face::Mono, kLogFontSize);
  std::vector<std::vector<Token>> lines(1);
  float used = 0;
  for (const auto& t : tokens) {
    const float w = textWidth(*t.text, Face::Mono, kLogFontSize);
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
    for (const Token& t : lines[i])
      x += drawText(x, y, *t.text, Face::Mono, kLogFontSize, t.pending ? kAccent : kText) + space;
  }
}

void App::drawKeyboard() {
  fillRect({kKeyboard.x - 1, kKeyboard.y - 4, kKeyboard.w + 2, kKeyboard.h + 5}, kKeyGap);

  for (int pass = 0; pass < 2; ++pass) {  // whites first so blacks draw on top
    for (int off = 0; off < kVisibleSemitones; ++off) {
      const int pc = off % 12;
      if (isBlack(pc) != (pass == 1)) continue;
      const int midi = baseNote() + off;
      const bool down = midi <= 127 && holdCount_[midi] > 0;
      const std::string name = theory::noteName(midi);
      const char* hint = keyLabel(off);
      SDL_FRect r = keyRect(off);

      if (!isBlack(pc)) {
        r.x += 1;
        r.w -= 2;
        fillRound(r, {0, 0, 5, 5}, down ? kWhiteKeyDown : kWhiteKey);
        const float cx = r.x + r.w / 2;
        const SDL_Color nameColor = pc == 0 ? kWhiteLabelC : kWhiteLabel;
        const float nw = textWidth(name, Face::Ui, 14);
        drawText(cx - nw / 2, r.y + r.h - 26, name, Face::Ui, 14, nameColor);
        if (hint) {
          const float hw = textWidth(hint, Face::Ui, 11);
          drawText(cx - hw / 2, r.y + r.h - 44, hint, Face::Ui, 11, down ? kWhiteHintDown : kWhiteHint);
        }
      } else {
        fillRound(r, {0, 0, 3, 3}, down ? kBlackKeyDown : kBlackKey);
        // Lighter top face with a darker lip at the bottom reads as depth.
        fillRound({r.x + 3, r.y, r.w - 6, r.h - (down ? 4 : 8)}, {0, 0, 2, 2},
                  down ? kBlackKeyDownTop : kBlackKeyTop);
        const float cx = r.x + r.w / 2;
        const float nw = textWidth(name, Face::Ui, 11);
        drawText(cx - nw / 2, r.y + r.h - 36, name, Face::Ui, 11, kBlackLabel);
        if (hint) {
          const float hw = textWidth(hint, Face::Ui, 10);
          drawText(cx - hw / 2, r.y + r.h - 54, hint, Face::Ui, 10, kBlackHint);
        }
      }
    }
  }
}

void App::draw() {
  SDL_SetRenderDrawColor(renderer_, kBg.r, kBg.g, kBg.b, 255);
  SDL_RenderClear(renderer_);

  drawHeader();
  drawLog();

  const std::vector<int> held = log_.held();
  if (!held.empty()) {
    std::string status;
    for (int n : held) status += theory::noteName(n) + "  ";
    if (std::string chord = theory::chordName(held); !chord.empty()) status += "→  " + chord;
    drawText(kMargin + 2, kStatusY, status, Face::Ui, 15, kAccent);
  }

  drawKeyboard();

  drawText(kMargin, kHelpY,
           "Tab – \\  white keys   ·   number row  black keys   ·   Z / X  octave   "
           "·   Backspace  undo   ·   Esc  clear   ·   Cmd+C  copy",
           Face::Ui, 12, kDim);

  SDL_RenderPresent(renderer_);
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
