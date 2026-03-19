#pragma once

#include <string>
#include <vector>

#include <GfxRenderer.h>

#include "PetState.h"

class PetSpriteRenderer {
 public:
  static constexpr int SPRITE_W = 48;
  static constexpr int SPRITE_H = 48;
  static constexpr int MINI_W = 24;
  static constexpr int MINI_H = 24;
  static constexpr int SPRITE_BYTES = SPRITE_W * SPRITE_H / 8;
  static constexpr int MINI_BYTES = MINI_W * MINI_H / 8;

  static constexpr int BUILTIN_GRID = 24;
  static constexpr int displaySize(int scale = 1) { return BUILTIN_GRID * 2 * scale; }

  static void drawPet(GfxRenderer& renderer,
                      int x,
                      int y,
                      PetStage stage,
                      PetMood mood,
                      int scale = 1,
                      uint8_t variant = 0,
                      uint8_t petType = 0,
                      uint8_t animFrame = 0);

  static void drawMini(GfxRenderer& renderer,
                       int x,
                       int y,
                       PetStage stage,
                       PetMood mood,
                       uint8_t variant = 0,
                       uint8_t petType = 0);

  bool loadSprite(const std::string& path);
  void setFallbackStage(PetStage stage);
  void setFallbackType(uint8_t type);
  void draw(GfxRenderer& renderer, int x, int y, int scale, bool color = true) const;

 private:
  static uint8_t spriteBuffer[SPRITE_BYTES];

  static const char* stageName(PetStage stage);
  static const char* moodName(PetMood mood);
  static size_t loadSprite(const char* path, size_t expectedBytes);
  static void drawFallback(GfxRenderer& renderer,
                           int x,
                           int y,
                           int scale,
                           PetStage stage,
                           uint8_t variant = 0,
                           uint8_t petType = 0,
                           uint8_t animFrame = 0);

  std::vector<uint8_t> data_;
  PetStage fallbackStage_ = PetStage::Egg;
  uint8_t fallbackType_ = 0;
  int width_ = 12;
  int height_ = 12;
};
