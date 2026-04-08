#include "RPGCharacter.h"

#include <algorithm>
#include <cstring>
#include <esp_random.h>

namespace rpg {

Character::Character(CharacterClass charClass, const std::string& name)
    : charClass(charClass), name(name), level(1), experience(0) {
  stats = getClassStats(charClass);

  // Calculate AC and HP based on class
  // HP = max hit die value + CON modifier per D&D 5e (at 1st level, max die)
  switch (charClass) {
    case CharacterClass::Warrior:   // Fighter d10
      armorClass = 16;
      maxHp = static_cast<uint16_t>(10 + stats.getModifier(stats.constitution));
      break;
    case CharacterClass::Barbarian: // Barbarian d12
      armorClass = 14;
      maxHp = static_cast<uint16_t>(12 + stats.getModifier(stats.constitution));
      break;
    case CharacterClass::Ranger:    // Ranger d10
      armorClass = 14;
      maxHp = static_cast<uint16_t>(10 + stats.getModifier(stats.constitution));
      break;
    case CharacterClass::Rogue:     // Rogue d8
      armorClass = 15;
      maxHp = static_cast<uint16_t>(8 + stats.getModifier(stats.constitution));
      break;
    case CharacterClass::Cleric:    // Cleric d8
      armorClass = 16;
      maxHp = static_cast<uint16_t>(8 + stats.getModifier(stats.constitution));
      break;
    case CharacterClass::Mage:      // Wizard d6
      armorClass = 12;
      maxHp = static_cast<uint16_t>(6 + stats.getModifier(stats.constitution));
      break;
    default:
      armorClass = 12;
      maxHp = 8;
      break;
  }

  if (maxHp < 1) maxHp = 1;
  hp = maxHp;
  baseArmorClass = armorClass;
  equippedWeaponId = NO_EQUIPPED_ITEM;
  equippedArmorId = NO_EQUIPPED_ITEM;
  goldCoins = 50 + (esp_random() % 50);
  inventoryCount = 0;
}

void Character::addExperience(uint32_t amount) {
  experience += amount;
  while (levelUp()) {
    // Keep leveling up if experience crosses multiple thresholds
  }
}

bool Character::levelUp() {
  uint32_t nextLvl = experienceForNextLevel();
  if (experience >= nextLvl) {
    level++;
    stats.strength += (level % 2 == 0) ? 1 : 0;
    stats.dexterity += (level % 3 == 0) ? 1 : 0;
    stats.constitution += 1;
    stats.intelligence += (level % 4 == 0) ? 1 : 0;
    stats.wisdom += (level % 3 == 1) ? 1 : 0;
    stats.charisma += (level % 5 == 0) ? 1 : 0;

    // Increase max HP
    // HP per level: average of class hit die + CON modifier (D&D 5e)
    uint8_t hpGain = 1;
    switch (charClass) {
      case CharacterClass::Barbarian:  // d12 avg ~7
        hpGain = static_cast<uint8_t>(7 + stats.getModifier(stats.constitution));
        break;
      case CharacterClass::Warrior:    // Fighter d10 avg ~6
      case CharacterClass::Ranger:     // Ranger d10 avg ~6
        hpGain = static_cast<uint8_t>(6 + stats.getModifier(stats.constitution));
        break;
      case CharacterClass::Cleric:     // Cleric d8 avg ~5
      case CharacterClass::Rogue:      // Rogue d8 avg ~5
        hpGain = static_cast<uint8_t>(5 + stats.getModifier(stats.constitution));
        break;
      case CharacterClass::Mage:       // Wizard d6 avg ~4
        hpGain = static_cast<uint8_t>(4 + stats.getModifier(stats.constitution));
        break;
      default:
        hpGain = 5;
        break;
    }
    if (hpGain < 1) hpGain = 1;
    maxHp += hpGain;
    hp = maxHp;
    return true;
  }
  return false;
}

uint32_t Character::experienceForNextLevel() const {
  // D&D 5e XP thresholds (index = current level, value = XP needed to advance)
  static const uint32_t XP_THRESHOLDS[] = {
    0,       // unused
    300,     // level 1 -> 2
    900,     // level 2 -> 3
    2700,    // level 3 -> 4
    6500,    // level 4 -> 5
    14000,   // level 5 -> 6
    23000,   // level 6 -> 7
    34000,   // level 7 -> 8
    48000,   // level 8 -> 9
    64000,   // level 9 -> 10
    85000,   // level 10 -> 11
    100000,  // level 11 -> 12
    120000,  // level 12 -> 13
    140000,  // level 13 -> 14
    165000,  // level 14 -> 15
    195000,  // level 15 -> 16
    225000,  // level 16 -> 17
    265000,  // level 17 -> 18
    305000,  // level 18 -> 19
    355000,  // level 19 -> 20
  };
  if (level >= 20) return UINT32_MAX;
  const uint8_t idx = (level < 20) ? level : 19u;
  return XP_THRESHOLDS[idx];
}

uint8_t Character::proficiencyBonus() const {
  // D&D 5e proficiency bonus by level
  if (level <= 4)  return 2;
  if (level <= 8)  return 3;
  if (level <= 12) return 4;
  if (level <= 16) return 5;
  return 6;
}

const char* classToString(CharacterClass charClass) {
  switch (charClass) {
    case CharacterClass::Warrior:
      return "Fighter";     // Solamnic Knight tradition
    case CharacterClass::Rogue:
      return "Rogue";       // Kender-cunning thief
    case CharacterClass::Mage:
      return "Wizard";      // Red-robed arcane scholar
    case CharacterClass::Cleric:
      return "Cleric";      // Disciple of Mishakal
    case CharacterClass::Ranger:
      return "Ranger";      // Wildwood tracker
    case CharacterClass::Barbarian:
      return "Barbarian";   // Plains warrior of Abanasinia
    default:
      return "Adventurer";
  }
}

CharacterStats getClassStats(CharacterClass charClass) {
  CharacterStats stats;
  switch (charClass) {
    case CharacterClass::Warrior:
      stats.strength = 15;
      stats.dexterity = 11;
      stats.constitution = 14;
      stats.intelligence = 10;
      stats.wisdom = 12;
      stats.charisma = 13;
      break;
    case CharacterClass::Barbarian:
      stats.strength = 16;
      stats.dexterity = 10;
      stats.constitution = 16;
      stats.intelligence = 8;
      stats.wisdom = 11;
      stats.charisma = 10;
      break;
    case CharacterClass::Ranger:
      stats.strength = 13;
      stats.dexterity = 15;
      stats.constitution = 12;
      stats.intelligence = 11;
      stats.wisdom = 14;
      stats.charisma = 10;
      break;
    case CharacterClass::Rogue:
      stats.strength = 11;
      stats.dexterity = 16;
      stats.constitution = 10;
      stats.intelligence = 12;
      stats.wisdom = 11;
      stats.charisma = 13;
      break;
    case CharacterClass::Cleric:
      stats.strength = 12;
      stats.dexterity = 10;
      stats.constitution = 13;
      stats.intelligence = 11;
      stats.wisdom = 16;
      stats.charisma = 14;
      break;
    case CharacterClass::Mage:
      stats.strength = 8;
      stats.dexterity = 12;
      stats.constitution = 10;
      stats.intelligence = 16;
      stats.wisdom = 13;
      stats.charisma = 11;
      break;
    default:
      stats.strength = 10;
      stats.dexterity = 10;
      stats.constitution = 10;
      stats.intelligence = 10;
      stats.wisdom = 10;
      stats.charisma = 10;
      break;
  }
  return stats;
}

}  // namespace rpg
