#pragma once

#include <cstdint>

#include "GameTypes.h"

// Handles per-level state persistence.
// Stores deltas from seed-generated state: fog of war, surviving monsters,
// and items remaining/dropped on each level.
class GameSave {
 public:
  // Save current level state to /.crosspoint/game/level_NN.bin
  static bool saveLevel(uint8_t depth, const uint8_t* fogOfWar, const game::Monster* monsters, uint8_t monsterCount,
                        const game::Item* items, uint8_t itemCount);

  // Load level state from file. Returns false if no save exists for this depth.
  static bool loadLevel(uint8_t depth, uint8_t* fogOfWar, game::Monster* monsters, uint8_t& monsterCount,
                        game::Item* items, uint8_t& itemCount);

  // Check if a saved level file exists
  static bool hasLevel(uint8_t depth);

  // Delete a specific level file
  static void deleteLevel(uint8_t depth);

  // Delete all game save data (save.bin + all level files)
  static void deleteAll();
};
