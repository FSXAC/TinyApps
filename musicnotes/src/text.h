#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <map>
#include <string>
#include <unordered_map>
#include <utility>

enum class Face { Ui, Mono };

// Renders strings with system fonts and caches one texture per
// (string, face, size, color). Text is rasterized at the output pixel scale
// so it stays sharp under the renderer's logical presentation.
class TextCache {
 public:
  struct Text {
    SDL_Texture* tex = nullptr;
    float w = 0, h = 0;  // logical pixels
  };

  bool init(SDL_Renderer* renderer);
  void shutdown();
  // Pixels per logical pixel; flushes the cache when it changes.
  void setScale(float scale);

  const Text& get(const std::string& s, Face face, int size, SDL_Color color);

 private:
  TTF_Font* font(Face face, int size);
  void flush();

  SDL_Renderer* renderer_ = nullptr;
  float scale_ = 1;
  std::map<std::pair<Face, int>, TTF_Font*> fonts_;
  std::unordered_map<std::string, Text> cache_;
};
