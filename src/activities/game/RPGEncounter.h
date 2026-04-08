#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "RPGCharacter.h"

namespace rpg {

// Encounter types
enum class EncounterType : uint8_t {
  Story = 0,      // Text encounter with choices
  Combat = 1,     // Fight an enemy
  Merchant = 2,   // Buy/sell items
  RestSite = 3,   // Heal up
  Treasure = 4    // Find items/gold
};

// Enemy definitions for combat encounters
struct Enemy {
  uint8_t id;
  std::string name;
  uint8_t level;
  uint16_t maxHp;
  uint16_t hp;
  uint8_t armorClass;
  uint8_t damage;
  uint32_t experienceReward;
  uint32_t goldReward;
};

// Encounter choice
struct Choice {
  const char* text;
  uint8_t nextEncounterId;  // Which encounter to go to next
  uint32_t storyFlag;        // Story flag this choice sets
};

// Encounter definition
struct Encounter {
  uint8_t id;
  const char* title;
  EncounterType type;
  const char* description;

  // For story encounters
  const Choice* choices;
  int choiceCount;

  // For combat encounters
  Enemy* enemy;

  // Callback function if needed
  std::function<void(Character&)> onEnter;
  std::function<void(Character&)> onComplete;
};

// Get encounter by ID
const Encounter* getEncounter(uint8_t encounterId);

// Story encounters database
extern Encounter ENCOUNTERS[];
extern int ENCOUNTER_COUNT;

// Enemy database
extern Enemy ENEMIES[];
extern int ENEMY_COUNT;

Enemy* getEnemy(uint8_t enemyId);

}  // namespace rpg
