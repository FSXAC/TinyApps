#include "text.h"

#include <cmath>

namespace {

constexpr const char* kUiFonts[] = {"/System/Library/Fonts/SFNS.ttf",
                                    "/System/Library/Fonts/Helvetica.ttc"};
constexpr const char* kMonoFonts[] = {"/System/Library/Fonts/SFNSMono.ttf",
                                      "/System/Library/Fonts/Menlo.ttc"};
constexpr size_t kMaxCached = 1024;

}  // namespace

bool TextCache::init(SDL_Renderer* renderer) {
  renderer_ = renderer;
  if (!TTF_Init()) {
    SDL_Log("TTF_Init failed: %s", SDL_GetError());
    return false;
  }
  return true;
}

void TextCache::shutdown() {
  flush();
  TTF_Quit();
}

void TextCache::setScale(float scale) {
  if (scale == scale_) return;
  scale_ = scale;
  flush();
}

void TextCache::flush() {
  for (auto& [key, t] : cache_) SDL_DestroyTexture(t.tex);
  cache_.clear();
  for (auto& [key, f] : fonts_)
    if (f) TTF_CloseFont(f);
  fonts_.clear();
}

TTF_Font* TextCache::font(Face face, int size) {
  auto [it, inserted] = fonts_.try_emplace({face, size}, nullptr);
  if (inserted) {
    for (const char* path : face == Face::Mono ? kMonoFonts : kUiFonts) {
      if ((it->second = TTF_OpenFont(path, size * scale_))) break;
    }
    if (!it->second) SDL_Log("No font for size %d: %s", size, SDL_GetError());
  }
  return it->second;
}

const TextCache::Text& TextCache::get(const std::string& s, Face face, int size,
                                      SDL_Color c) {
  std::string key;
  key.reserve(s.size() + 16);
  key += static_cast<char>(face);
  key += static_cast<char>(size);
  key += static_cast<char>(c.r);
  key += static_cast<char>(c.g);
  key += static_cast<char>(c.b);
  key += s;
  if (auto it = cache_.find(key); it != cache_.end()) return it->second;

  if (cache_.size() >= kMaxCached) {
    for (auto& [k, t] : cache_) SDL_DestroyTexture(t.tex);
    cache_.clear();
  }

  Text t;
  if (TTF_Font* f = font(face, size); f && !s.empty()) {
    if (SDL_Surface* surf = TTF_RenderText_Blended(f, s.c_str(), 0, c)) {
      t.tex = SDL_CreateTextureFromSurface(renderer_, surf);
      t.w = surf->w / scale_;
      t.h = surf->h / scale_;
      SDL_DestroySurface(surf);
    }
  }
  return cache_.emplace(std::move(key), t).first->second;
}
