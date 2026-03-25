#include "RPGItem.h"

namespace rpg {

const ItemDefinition ITEMS[] = {
    {ITEM_LONGSWORD,      "Longsword",          ItemType::Weapon,     0, "A knight's blade forged in the Solamnic tradition. 1d8 slashing.", 50,  8, 0},
    {ITEM_DAGGER,         "Dagger",             ItemType::Weapon,     0, "A finesse blade favored by rogues and rangers. 1d4 piercing.",     10,  4, 0},
    {ITEM_QUARTERSTAFF,   "Quarterstaff",       ItemType::Weapon,     0, "An ash staff carved with runic glyphs. Channel arcane power. 1d6.",  5,  6, 0},
    {ITEM_LEATHER_ARMOR,  "Leather Armor",      ItemType::Armor,      0, "Cured hide armor, light enough for swift movement. AC +2.",         30,  0, 2},
    {ITEM_PLATE_ARMOR,    "Plate Armor",        ItemType::Armor,      1, "Solamnic knight's full plate, forged for Ansalon's wars. AC +5.",   150, 0, 5},
    {ITEM_HEALING_POTION, "Healing Potion",     ItemType::Consumable, 0, "Brewed by healers of Mishakal. Restores 20 HP on use.",             25,  0, 0},
    {ITEM_MANA_POTION,    "Clarity Draught",    ItemType::Consumable, 0, "A tonic that clears the arcane mind. Removes stun effect.",         20,  0, 0},
    {ITEM_SCROLL_FIREBALL,"Scroll of Fireball", ItemType::Scroll,     1, "Raistlin's notation blazes with red fire. Deals 8-14 fire damage.", 100, 6, 0},
    {ITEM_SCROLL_HEALING, "Scroll of Healing",  ItemType::Scroll,     0, "Goldmoon's prayer in silver ink. Restores 35 HP, cures ailments.", 50,  0, 0},
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
