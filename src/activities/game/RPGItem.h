#pragma once

#include <cstdint>
#include <string>

namespace rpg {

enum class ItemType : uint8_t {
  Weapon = 0,
  Armor = 1,
  Consumable = 2,
  Scroll = 3,
  Miscellaneous = 4
};

struct ItemDefinition {
  uint8_t id;
  const char* name;
  ItemType type;
  uint8_t rarity;  // 0=common, 1=uncommon, 2=rare, 3=legendary
  const char* description;
  uint16_t value;  // Gold coins worth
  uint8_t damage;  // For weapons
  uint8_t armor;   // For armor
};

// Item database
extern const ItemDefinition ITEMS[];
extern const int ITEM_COUNT;

// Special item IDs for starting gear
constexpr uint8_t ITEM_LONGSWORD = 0;
constexpr uint8_t ITEM_DAGGER = 1;
constexpr uint8_t ITEM_QUARTERSTAFF = 2;
constexpr uint8_t ITEM_LEATHER_ARMOR = 3;
constexpr uint8_t ITEM_PLATE_ARMOR = 4;
constexpr uint8_t ITEM_HEALING_POTION = 5;
constexpr uint8_t ITEM_MANA_POTION = 6;
constexpr uint8_t ITEM_SCROLL_FIREBALL = 7;
constexpr uint8_t ITEM_SCROLL_HEALING = 8;

const ItemDefinition* getItem(uint8_t itemId);
const char* itemTypeToString(ItemType type);

}  // namespace rpg
