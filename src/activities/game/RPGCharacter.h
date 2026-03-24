#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace rpg {

enum class CharacterClass : uint8_t {
  Warrior = 0,
  Rogue = 1,
  Mage = 2,
  Cleric = 3,
  Ranger = 4,
  Barbarian = 5
};

struct CharacterStats {
  uint8_t strength;
  uint8_t dexterity;
  uint8_t constitution;
  uint8_t intelligence;
  uint8_t wisdom;
  uint8_t charisma;

  uint8_t getModifier(uint8_t score) const {
    return (score - 10) / 2;
  }
};

struct Character {
  CharacterClass charClass;
  std::string name;
  uint8_t level;
  uint32_t experience;
  uint16_t hp;
  uint16_t maxHp;
  uint16_t goldCoins;

  CharacterStats stats;

  uint16_t armorClass;

  // Inventory (simplified - max 12 items)
  static constexpr int MAX_INVENTORY = 12;
  struct InventoryItem {
    uint8_t itemId;
    uint8_t quantity;
  };
  std::array<InventoryItem, MAX_INVENTORY> inventory{};
  uint8_t inventoryCount = 0;

  // Story progress
  uint32_t storyFlags = 0;  // Bitfield for story choices

  Character() = default;
  explicit Character(CharacterClass charClass, const std::string& name);

  void addExperience(uint32_t amount);
  bool levelUp();
  uint32_t experienceForNextLevel() const;
};

const char* classToString(CharacterClass charClass);
CharacterStats getClassStats(CharacterClass charClass);

}  // namespace rpg
