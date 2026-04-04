#pragma once

#include "GameTypes.h"

// Generates a dungeon level deterministically from a seed.
// The same seed + depth always produces the same layout.
// Caller provides output arrays; generator fills them in-place.
class DungeonGenerator {
 public:
  struct Result {
    int16_t stairsUpX, stairsUpY;
    int16_t stairsDownX, stairsDownY;
    uint8_t monsterCount;
    uint8_t itemCount;
  };

  // Generate a level into the provided tile grid, monster and item arrays.
  // tiles must be game::MAP_SIZE bytes. monsters/items must be sized to MAX_MONSTERS/MAX_ITEMS_PER_LEVEL.
  static Result generate(uint32_t gameSeed, uint8_t depth, game::Tile* tiles, game::Monster* monsters,
                         game::Item* items);
};
