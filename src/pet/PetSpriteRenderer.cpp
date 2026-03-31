#include "PetSpriteRenderer.h"

#include <HalStorage.h>

#include <string>

#include "PetSpriteData.h"

namespace {
std::string gLastMissingPetSpritePath;
std::string gLastMissingMiniSpritePath;
}

uint8_t PetSpriteRenderer::spriteBuffer[PetSpriteRenderer::SPRITE_BYTES];

const char* PetSpriteRenderer::stageName(PetStage stage) {
  switch (stage) {
    case PetStage::Egg:
      return "egg";
    case PetStage::Baby:
      return "hatchling";
    case PetStage::Child:
      return "youngster";
    case PetStage::Teen:
      return "companion";
    case PetStage::Adult:
      return "elder";
    case PetStage::Dead:
      return "dead";
    default:
      return "egg";
  }
}

const char* PetSpriteRenderer::moodName(PetMood mood) {
  switch (mood) {
    case PetMood::Happy:
      return "happy";
    case PetMood::Normal:
      return "neutral";
    case PetMood::Sad:
      return "sad";
    case PetMood::Sick:
      return "sick";
    case PetMood::Sleeping:
      return "sleeping";
    case PetMood::Dead:
      return "dead";
    case PetMood::Needy:
    case PetMood::Refusing:
      return "neutral";
    default:
      return "neutral";
  }
}

size_t PetSpriteRenderer::loadSprite(const char* path, size_t expectedBytes) {
  if (!Storage.exists(path)) {
    return 0;
  }
  return Storage.readFileToBuffer(path, reinterpret_cast<char*>(spriteBuffer), SPRITE_BYTES + 1, expectedBytes);
}

void PetSpriteRenderer::drawFallback(GfxRenderer& renderer,
                                     int x,
                                     int y,
                                     int scale,
                                     PetStage stage,
                                     uint8_t variant,
                                     uint8_t petType,
                                     uint8_t animFrame) {
  const int cell = 2 * scale;
  const uint32_t* rows = getSpriteRows(stage, variant, petType, animFrame);
  for (int row = 0; row < 24; ++row) {
    const uint32_t mask = rows[row];
    for (int col = 0; col < 24; ++col) {
      if (mask & (1u << (23 - col))) {
        renderer.fillRect(x + col * cell, y + row * cell, cell, cell);
      }
    }
  }
}

void PetSpriteRenderer::drawPet(GfxRenderer& renderer,
                                int x,
                                int y,
                                PetStage stage,
                                PetMood mood,
                                int scale,
                                uint8_t variant,
                                uint8_t petType,
                                uint8_t animFrame) {
  char path[80];

  if (variant > 0) {
    snprintf(path, sizeof(path), "/.crosspoint/pet/sprites/%s_v%d_%s.bin", stageName(stage), static_cast<int>(variant),
             moodName(mood));
    if (gLastMissingPetSpritePath != path && loadSprite(path, SPRITE_BYTES) == SPRITE_BYTES && scale == 1) {
      gLastMissingPetSpritePath.clear();
      renderer.drawImage(spriteBuffer, x, y, SPRITE_W, SPRITE_H);
      return;
    } else if (gLastMissingPetSpritePath != path) {
      gLastMissingPetSpritePath = path;
    }
  }

  snprintf(path, sizeof(path), "/.crosspoint/pet/sprites/%s_%s.bin", stageName(stage), moodName(mood));
  if (gLastMissingPetSpritePath != path && loadSprite(path, SPRITE_BYTES) == SPRITE_BYTES && scale == 1) {
    gLastMissingPetSpritePath.clear();
    renderer.drawImage(spriteBuffer, x, y, SPRITE_W, SPRITE_H);
  } else {
    if (gLastMissingPetSpritePath != path) {
      gLastMissingPetSpritePath = path;
    }
    drawFallback(renderer, x, y, scale, stage, variant, petType, animFrame);
  }
}

void PetSpriteRenderer::drawMini(GfxRenderer& renderer,
                                 int x,
                                 int y,
                                 PetStage stage,
                                 PetMood mood,
                                 uint8_t variant,
                                 uint8_t petType) {
  char path[88];

  if (variant > 0) {
    snprintf(path, sizeof(path), "/.crosspoint/pet/sprites/mini/%s_v%d_%s.bin", stageName(stage), static_cast<int>(variant),
             moodName(mood));
    if (gLastMissingMiniSpritePath != path && loadSprite(path, MINI_BYTES) == MINI_BYTES) {
      gLastMissingMiniSpritePath.clear();
      renderer.drawImage(spriteBuffer, x, y, MINI_W, MINI_H);
      return;
    } else if (gLastMissingMiniSpritePath != path) {
      gLastMissingMiniSpritePath = path;
    }
  }

  snprintf(path, sizeof(path), "/.crosspoint/pet/sprites/mini/%s_%s.bin", stageName(stage), moodName(mood));
  if (gLastMissingMiniSpritePath != path && loadSprite(path, MINI_BYTES) == MINI_BYTES) {
    gLastMissingMiniSpritePath.clear();
    renderer.drawImage(spriteBuffer, x, y, MINI_W, MINI_H);
  } else {
    if (gLastMissingMiniSpritePath != path) {
      gLastMissingMiniSpritePath = path;
    }
    drawFallback(renderer, x, y, 1, stage, variant, petType, 0);
  }
}

bool PetSpriteRenderer::loadSprite(const std::string& path) {
  FsFile f;
  if (!Storage.openFileForRead("PET", path, f)) {
    return false;
  }

  data_.clear();
  while (f.available()) {
    data_.push_back(static_cast<uint8_t>(f.read()));
  }
  f.close();
  return !data_.empty();
}

void PetSpriteRenderer::setFallbackStage(PetStage stage) {
  fallbackStage_ = stage;
}

void PetSpriteRenderer::setFallbackType(uint8_t type) {
  fallbackType_ = static_cast<uint8_t>(type % PetTypeNames::COUNT);
}

void PetSpriteRenderer::draw(GfxRenderer& renderer, int x, int y, int scale, bool color) const {
  if (data_.empty()) {
    const uint32_t* rows = getSpriteRows(fallbackStage_, 0, fallbackType_, 0);
    for (int row = 0; row < 24; ++row) {
      const uint32_t mask = rows[row];
      for (int col = 0; col < 24; ++col) {
        if (mask & (1u << (23 - col))) {
          renderer.fillRect(x + col * scale, y + row * scale, scale, scale, color);
        }
      }
    }
    return;
  }

  const int bytesPerRow = (width_ + 7) / 8;
  for (int row = 0; row < height_; ++row) {
    for (int col = 0; col < width_; ++col) {
      const int idx = row * bytesPerRow + (col / 8);
      if (idx >= static_cast<int>(data_.size())) {
        return;
      }
      const uint8_t mask = static_cast<uint8_t>(0x80 >> (col % 8));
      if (data_[idx] & mask) {
        renderer.fillRect(x + col * scale, y + row * scale, scale, scale, color);
      }
    }
  }
}
