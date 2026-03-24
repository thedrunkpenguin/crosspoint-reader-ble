#include "RPGItem.h"

namespace rpg {

const ItemDefinition ITEMS[] = {
    {ITEM_LONGSWORD, "Longsword", ItemType::Weapon, 0, "A classic steel sword", 50, 8, 0},
    {ITEM_DAGGER, "Dagger", ItemType::Weapon, 0, "A short blade", 10, 4, 0},
    {ITEM_QUARTERSTAFF, "Quarterstaff", ItemType::Weapon, 0, "A sturdy wooden staff", 5, 6, 0},
    {ITEM_LEATHER_ARMOR, "Leather Armor", ItemType::Armor, 0, "Light protective gear", 30, 0, 2},
    {ITEM_PLATE_ARMOR, "Plate Armor", ItemType::Armor, 1, "Heavy but protective", 150, 0, 5},
    {ITEM_HEALING_POTION, "Healing Potion", ItemType::Consumable, 0, "Restores 20 HP", 25, 0, 0},
    {ITEM_MANA_POTION, "Mana Potion", ItemType::Consumable, 0, "Restores 15 mana points", 20, 0, 0},
    {ITEM_SCROLL_FIREBALL, "Scroll of Fireball", ItemType::Scroll, 1, "A powerful spell scroll", 100, 6, 0},
    {ITEM_SCROLL_HEALING, "Scroll of Healing", ItemType::Scroll, 0, "Heals wounds", 50, 0, 0},
    // Add more items as needed
};

const int ITEM_COUNT = sizeof(ITEMS) / sizeof(ITEMS[0]);

const ItemDefinition* getItem(uint8_t itemId) {
  for (int i = 0; i < ITEM_COUNT; i++) {
    if (ITEMS[i].id == itemId) {
      return &ITEMS[i];
    }
  }
  return nullptr;
}

const char* itemTypeToString(ItemType type) {
  switch (type) {
    case ItemType::Weapon:
      return "Weapon";
    case ItemType::Armor:
      return "Armor";
    case ItemType::Consumable:
      return "Consumable";
    case ItemType::Scroll:
      return "Scroll";
    case ItemType::Miscellaneous:
      return "Misc";
    default:
      return "Unknown";
  }
}

}  // namespace rpg
