#include "RPGCharacter.h"

#include <algorithm>
#include <esp_system.h>
#include <cstring>

namespace rpg {

Character::Character(CharacterClass charClass, const std::string& name)
    : charClass(charClass), name(name), level(1), experience(0) {
  stats = getClassStats(charClass);

  // Calculate AC and HP based on class
  switch (charClass) {
    case CharacterClass::Warrior:
      armorClass = 16;  // Assumes armor
      maxHp = 12 + stats.getModifier(stats.constitution);
      break;
    case CharacterClass::Barbarian:
      armorClass = 14;
      maxHp = 14 + stats.getModifier(stats.constitution);
      break;
    case CharacterClass::Ranger:
      armorClass = 14;
      maxHp = 10 + stats.getModifier(stats.constitution);
      break;
    case CharacterClass::Rogue:
      armorClass = 15;
      maxHp = 8 + stats.getModifier(stats.constitution);
      break;
    case CharacterClass::Cleric:
      armorClass = 16;
      maxHp = 10 + stats.getModifier(stats.constitution);
      break;
    case CharacterClass::Mage:
      armorClass = 12;
      maxHp = 6 + stats.getModifier(stats.constitution);
      break;
    default:
      armorClass = 12;
      maxHp = 8;
      break;
  }

  if (maxHp < 1) maxHp = 1;
  hp = maxHp;
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
    uint8_t hpGain = 1;
    switch (charClass) {
      case CharacterClass::Warrior:
      case CharacterClass::Barbarian:
        hpGain = 8 + stats.getModifier(stats.constitution);
        break;
      case CharacterClass::Ranger:
        hpGain = 6 + stats.getModifier(stats.constitution);
        break;
      case CharacterClass::Cleric:
        hpGain = 7 + stats.getModifier(stats.constitution);
        break;
      case CharacterClass::Rogue:
        hpGain = 5 + stats.getModifier(stats.constitution);
        break;
      case CharacterClass::Mage:
        hpGain = 4 + stats.getModifier(stats.constitution);
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
  return level * level * 1000;
}

const char* classToString(CharacterClass charClass) {
  switch (charClass) {
    case CharacterClass::Warrior:
      return "Warrior";
    case CharacterClass::Rogue:
      return "Rogue";
    case CharacterClass::Mage:
      return "Mage";
    case CharacterClass::Cleric:
      return "Cleric";
    case CharacterClass::Ranger:
      return "Ranger";
    case CharacterClass::Barbarian:
      return "Barbarian";
    default:
      return "Unknown";
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
