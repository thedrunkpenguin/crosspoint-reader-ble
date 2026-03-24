#include "RPGActivity.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Serialization.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "MappedInputManager.h"
#include "RPGItem.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr uint8_t RPG_SAVE_VERSION = 3;
constexpr const char* RPG_SAVE_DIR = "/.crosspoint/rpg";
constexpr const char* RPG_SAVE_PATH = "/.crosspoint/rpg/save.bin";

constexpr uint8_t UNIQUE_ORC_ID = 2;
constexpr uint8_t UNIQUE_TROLL_ID = 3;
constexpr uint8_t UNIQUE_VAMPIRE_ID = 5;
constexpr uint8_t UNIQUE_DRAGON_ID = 6;

uint32_t uniqueBit(uint8_t enemyId) {
  if (enemyId >= 31) return 0;
  return (1u << enemyId);
}

int defeatedUniqueCount(uint32_t mask) {
  return __builtin_popcount(mask & (uniqueBit(UNIQUE_ORC_ID) | uniqueBit(UNIQUE_TROLL_ID) | uniqueBit(UNIQUE_VAMPIRE_ID) |
                                    uniqueBit(UNIQUE_DRAGON_ID)));
}

int abilityMod(uint8_t score) {
  return (static_cast<int>(score) - 10) / 2;
}

std::vector<std::string> wrapText(GfxRenderer& renderer, int fontId, const char* text, int maxWidth, int maxLines) {
  std::vector<std::string> lines;
  if (!text || !text[0]) return lines;

  std::string input(text);
  size_t pos = 0;
  while (pos < input.size() && static_cast<int>(lines.size()) < maxLines) {
    while (pos < input.size() && input[pos] == ' ') pos++;
    if (pos >= input.size()) break;

    if (input[pos] == '\n') {
      lines.emplace_back("");
      pos++;
      continue;
    }

    size_t end = pos;
    size_t best = pos;
    while (end <= input.size()) {
      if (end == input.size() || input[end] == ' ' || input[end] == '\n') {
        std::string candidate = input.substr(pos, end - pos);
        if (renderer.getTextWidth(fontId, candidate.c_str()) <= maxWidth) {
          best = end;
          if (end < input.size() && input[end] == '\n') {
            lines.emplace_back(candidate);
            pos = end + 1;
            goto next_line;
          }
        } else {
          break;
        }
      }
      end++;
    }

    if (best == pos) {
      // Force truncate one long token
      std::string forced = renderer.truncatedText(fontId, input.substr(pos).c_str(), maxWidth);
      lines.push_back(forced);
      break;
    }

    lines.push_back(input.substr(pos, best - pos));
    pos = best;
    while (pos < input.size() && input[pos] == ' ') pos++;

  next_line:
    continue;
  }

  return lines;
}
}  // namespace

void RPGActivity::taskTrampoline(void* param) {
  static_cast<RPGActivity*>(param)->displayTaskLoop();
}

void RPGActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  currentScreen = Screen::CharacterSelection;
  selectedIndex = 0;
  gameActive = false;
  currentEncounter = nullptr;
  currentEncounterId = 1;
  dungeonDepth = 1;
  encountersWon = 0;
  playerPoisonTurns = 0;
  playerStunTurns = 0;
  enemyPoisonTurns = 0;
  enemyStunTurns = 0;
  defeatedUniqueMask = 0;
  currentFightIsUnique = false;
  setNarrative("Choose a class and begin your journey in Ashwick.");
  refreshSaveState();
  updateRequired = true;

  xTaskCreate(&RPGActivity::taskTrampoline, "RPGTask", 4096, this, 1, &displayTaskHandle);
}

void RPGActivity::onExit() {
  Activity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void RPGActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void RPGActivity::setNarrative(const std::string& text) {
  narrativeText = text;
}

void RPGActivity::refreshSaveState() {
  saveExists = Storage.exists(RPG_SAVE_PATH);
}

bool RPGActivity::addItemToInventory(uint8_t itemId, uint8_t quantity) {
  if (quantity == 0) return true;

  for (uint8_t i = 0; i < player.inventoryCount; ++i) {
    if (player.inventory[i].itemId == itemId) {
      player.inventory[i].quantity = static_cast<uint8_t>(std::min<int>(255, player.inventory[i].quantity + quantity));
      return true;
    }
  }

  if (player.inventoryCount >= rpg::Character::MAX_INVENTORY) {
    return false;
  }

  player.inventory[player.inventoryCount++] = {itemId, quantity};
  return true;
}

bool RPGActivity::saveGame() {
  Storage.mkdir(RPG_SAVE_DIR);

  FsFile f;
  if (!Storage.openFileForWrite("RPG", RPG_SAVE_PATH, f)) {
    setNarrative("The scribe cannot find parchment. Save failed.");
    return false;
  }

  serialization::writePod(f, RPG_SAVE_VERSION);
  serialization::writePod(f, static_cast<uint8_t>(player.charClass));
  serialization::writeString(f, player.name);
  serialization::writePod(f, player.level);
  serialization::writePod(f, player.experience);
  serialization::writePod(f, player.hp);
  serialization::writePod(f, player.maxHp);
  serialization::writePod(f, player.goldCoins);

  serialization::writePod(f, player.stats.strength);
  serialization::writePod(f, player.stats.dexterity);
  serialization::writePod(f, player.stats.constitution);
  serialization::writePod(f, player.stats.intelligence);
  serialization::writePod(f, player.stats.wisdom);
  serialization::writePod(f, player.stats.charisma);
  serialization::writePod(f, player.armorClass);

  serialization::writePod(f, player.inventoryCount);
  for (uint8_t i = 0; i < player.inventoryCount; ++i) {
    serialization::writePod(f, player.inventory[i].itemId);
    serialization::writePod(f, player.inventory[i].quantity);
  }

  serialization::writePod(f, player.storyFlags);
  serialization::writePod(f, gameActive);
  serialization::writePod(f, currentEncounterId);
  serialization::writePod(f, dungeonDepth);
  serialization::writePod(f, encountersWon);
  serialization::writePod(f, playerPoisonTurns);
  serialization::writePod(f, playerStunTurns);
  serialization::writePod(f, enemyPoisonTurns);
  serialization::writePod(f, enemyStunTurns);
  serialization::writePod(f, defeatedUniqueMask);

  f.close();
  refreshSaveState();
  setNarrative("You journal your journey by firelight. Progress saved.");
  return true;
}

bool RPGActivity::loadGame() {
  FsFile f;
  if (!Storage.openFileForRead("RPG", RPG_SAVE_PATH, f)) {
    setNarrative("No saved chronicle was found.");
    refreshSaveState();
    return false;
  }

  uint8_t version = 0;
  serialization::readPod(f, version);
  if (version > RPG_SAVE_VERSION) {
    f.close();
    setNarrative("This save was written by a newer build.");
    return false;
  }

  uint8_t classRaw = 0;
  std::string name;
  serialization::readPod(f, classRaw);
  serialization::readString(f, name);

  if (name.empty()) name = "Hero";
  createCharacter(static_cast<rpg::CharacterClass>(classRaw % 6), name);

  serialization::readPod(f, player.level);
  serialization::readPod(f, player.experience);
  serialization::readPod(f, player.hp);
  serialization::readPod(f, player.maxHp);
  serialization::readPod(f, player.goldCoins);

  serialization::readPod(f, player.stats.strength);
  serialization::readPod(f, player.stats.dexterity);
  serialization::readPod(f, player.stats.constitution);
  serialization::readPod(f, player.stats.intelligence);
  serialization::readPod(f, player.stats.wisdom);
  serialization::readPod(f, player.stats.charisma);
  serialization::readPod(f, player.armorClass);

  uint8_t invCount = 0;
  serialization::readPod(f, invCount);
  player.inventoryCount = std::min<uint8_t>(invCount, rpg::Character::MAX_INVENTORY);
  for (uint8_t i = 0; i < player.inventoryCount; ++i) {
    serialization::readPod(f, player.inventory[i].itemId);
    serialization::readPod(f, player.inventory[i].quantity);
  }
  for (uint8_t i = player.inventoryCount; i < rpg::Character::MAX_INVENTORY; ++i) {
    player.inventory[i] = {0, 0};
  }

  serialization::readPod(f, player.storyFlags);
  serialization::readPod(f, gameActive);
  serialization::readPod(f, currentEncounterId);
  if (version >= 2) {
    serialization::readPod(f, dungeonDepth);
    serialization::readPod(f, encountersWon);
    serialization::readPod(f, playerPoisonTurns);
    serialization::readPod(f, playerStunTurns);
    serialization::readPod(f, enemyPoisonTurns);
    serialization::readPod(f, enemyStunTurns);
    if (version >= 3) {
      serialization::readPod(f, defeatedUniqueMask);
    } else {
      defeatedUniqueMask = 0;
    }
  } else {
    dungeonDepth = 1;
    encountersWon = 0;
    playerPoisonTurns = 0;
    playerStunTurns = 0;
    enemyPoisonTurns = 0;
    enemyStunTurns = 0;
    defeatedUniqueMask = 0;
  }
  f.close();

  currentEncounter = rpg::getEncounter(currentEncounterId);
  currentEnemy = nullptr;
  enemyHp = 0;
  currentFightIsUnique = false;

  if (gameActive && currentEncounter) {
    currentScreen = (currentEncounter->type == rpg::EncounterType::Merchant) ? Screen::Merchant : Screen::Encounter;
    selectedIndex = 0;
  } else {
    gameActive = false;
    currentEncounter = nullptr;
    currentEncounterId = 1;
    currentScreen = Screen::MainMenu;
    selectedIndex = 0;
  }

  setNarrative("You reopen your weathered journal and continue the adventure.");
  refreshSaveState();
  return true;
}

void RPGActivity::createCharacter(rpg::CharacterClass charClass, const std::string& name) {
  player = rpg::Character(charClass, name);
  dungeonDepth = 1;
  encountersWon = 0;
  playerPoisonTurns = 0;
  playerStunTurns = 0;
  enemyPoisonTurns = 0;
  enemyStunTurns = 0;
  defeatedUniqueMask = 0;
  currentFightIsUnique = false;
  player.inventoryCount = 0;

  switch (charClass) {
    case rpg::CharacterClass::Warrior:
      addItemToInventory(rpg::ITEM_LONGSWORD);
      addItemToInventory(rpg::ITEM_LEATHER_ARMOR);
      break;
    case rpg::CharacterClass::Rogue:
      addItemToInventory(rpg::ITEM_DAGGER);
      addItemToInventory(rpg::ITEM_HEALING_POTION, 2);
      break;
    case rpg::CharacterClass::Mage:
      addItemToInventory(rpg::ITEM_QUARTERSTAFF);
      addItemToInventory(rpg::ITEM_SCROLL_FIREBALL);
      break;
    case rpg::CharacterClass::Cleric:
      addItemToInventory(rpg::ITEM_SCROLL_HEALING, 2);
      addItemToInventory(rpg::ITEM_HEALING_POTION);
      break;
    case rpg::CharacterClass::Ranger:
      addItemToInventory(rpg::ITEM_DAGGER);
      addItemToInventory(rpg::ITEM_HEALING_POTION);
      break;
    case rpg::CharacterClass::Barbarian:
      addItemToInventory(rpg::ITEM_LONGSWORD);
      break;
  }
}

bool RPGActivity::rollAttack(int attackBonus, int targetAC) {
  const int roll = static_cast<int>((esp_random() % 20) + 1);  // d20
  return (roll + attackBonus) >= targetAC;
}

void RPGActivity::enterCombat(const rpg::Enemy* enemy) {
  if (enemy == nullptr) return;
  currentEnemy = enemy;
  enemyHp = currentEnemy->maxHp;
  enemyPoisonTurns = 0;
  enemyStunTurns = 0;
  selectedIndex = 0;
  setNarrative("Steel rings out as battle is joined.");
}

const rpg::Enemy* RPGActivity::selectUniqueBossForDepth() const {
  if ((dungeonDepth % 5) != 0) return nullptr;

  struct UniqueCandidate {
    uint8_t id;
    uint16_t minDepth;
  };

  static constexpr UniqueCandidate candidates[] = {
      {UNIQUE_ORC_ID, 5},
      {UNIQUE_TROLL_ID, 10},
      {UNIQUE_VAMPIRE_ID, 20},
      {UNIQUE_DRAGON_ID, 35},
  };

  for (const auto& candidate : candidates) {
    if (dungeonDepth < candidate.minDepth) continue;
    if (defeatedUniqueMask & uniqueBit(candidate.id)) continue;

    for (int i = 0; i < rpg::ENEMY_COUNT; ++i) {
      if (rpg::ENEMIES[i].id == candidate.id) {
        return &rpg::ENEMIES[i];
      }
    }
  }

  return nullptr;
}

bool RPGActivity::maybeTriggerVaultEncounter() {
  const int roll = static_cast<int>(esp_random() % 100);
  if (roll >= 8) {
    return false;
  }

  const uint16_t goldGain = static_cast<uint16_t>(25 + dungeonDepth * 4 + (esp_random() % 20));
  player.goldCoins = static_cast<uint16_t>(std::min<int>(65535, player.goldCoins + goldGain));

  if ((esp_random() % 100) < 55) {
    (void)addItemToInventory(rpg::ITEM_HEALING_POTION);
  }
  if ((esp_random() % 100) < 25) {
    (void)addItemToInventory(rpg::ITEM_SCROLL_FIREBALL);
  }

  currentEncounterId = 8;
  currentEncounter = rpg::getEncounter(currentEncounterId);
  currentScreen = Screen::Encounter;
  selectedIndex = 0;
  setNarrative("A hidden vault chamber cracks open. You claim " + std::to_string(goldGain) +
               " gold and salvage a few relics.");
  return true;
}

const rpg::Enemy* RPGActivity::selectEnemyForDepth() const {
  if (rpg::ENEMY_COUNT <= 0) return nullptr;

  const int roll = static_cast<int>(esp_random() % 100);
  int effectiveDepth = dungeonDepth;
  if (roll < 12) {
    effectiveDepth = std::min<int>(dungeonDepth + 3, 100);  // Out-of-depth chance, Angband style
  }

  std::vector<const rpg::Enemy*> weighted;
  weighted.reserve(64);

  for (int i = 0; i < rpg::ENEMY_COUNT; ++i) {
    const auto& enemy = rpg::ENEMIES[i];
    const int distance = std::abs(static_cast<int>(enemy.level) - effectiveDepth);
    int weight = 0;
    if (distance == 0)
      weight = 12;
    else if (distance == 1)
      weight = 8;
    else if (distance == 2)
      weight = 4;
    else if (distance <= 4)
      weight = 2;
    else
      weight = 1;

    if (enemy.level > effectiveDepth + 3) {
      weight = 0;
    }

    for (int j = 0; j < weight; ++j) {
      weighted.push_back(&enemy);
    }
  }

  if (weighted.empty()) {
    return &rpg::ENEMIES[0];
  }

  const int idx = static_cast<int>(esp_random() % weighted.size());
  return weighted[idx];
}

void RPGActivity::applyStartOfRoundEffects() {
  if (playerPoisonTurns > 0 && player.hp > 0) {
    const uint16_t poisonDmg = static_cast<uint16_t>(1 + std::max<int>(0, dungeonDepth / 7));
    player.hp = (poisonDmg >= player.hp) ? 0 : static_cast<uint16_t>(player.hp - poisonDmg);
    playerPoisonTurns--;
    setNarrative("Poison burns through your veins for " + std::to_string(poisonDmg) + " damage.");
  }

  if (enemyPoisonTurns > 0 && enemyHp > 0) {
    const uint16_t poisonDmg = static_cast<uint16_t>(1 + std::max<int>(0, static_cast<int>(player.level) / 3));
    enemyHp = (poisonDmg >= enemyHp) ? 0 : static_cast<uint16_t>(enemyHp - poisonDmg);
    enemyPoisonTurns--;
    setNarrative(narrativeText + "  The enemy suffers " + std::to_string(poisonDmg) + " poison damage.");
  }
}

void RPGActivity::increaseDepthProgression() {
  encountersWon = static_cast<uint16_t>(std::min<int>(65535, encountersWon + 1));
  if ((encountersWon % 2) == 0 && dungeonDepth < 100) {
    dungeonDepth = static_cast<uint16_t>(std::min<int>(100, dungeonDepth + 1));
  }
}

void RPGActivity::handleEncounterChoice(int choiceIndex) {
  if (currentEncounter == nullptr || currentEncounter->choiceCount == 0 || choiceIndex >= currentEncounter->choiceCount) {
    return;
  }

  if (currentEncounterId == 2 && choiceIndex == 0) {
    if (const auto* uniqueBoss = selectUniqueBossForDepth()) {
      currentEncounterId = 5;
      currentEncounter = rpg::getEncounter(currentEncounterId);
      currentFightIsUnique = true;
      enterCombat(uniqueBoss);
      currentScreen = Screen::Combat;
      setNarrative("A depth guardian emerges from the dark! Defeat this unique foe to delve deeper.");
      return;
    }

    if (maybeTriggerVaultEncounter()) {
      return;
    }

    const auto* enemy = selectEnemyForDepth();
    if (enemy) {
      currentEncounterId = 5;
      currentEncounter = rpg::getEncounter(currentEncounterId);
      currentFightIsUnique = false;
      enterCombat(enemy);
      currentScreen = Screen::Combat;
      setNarrative("You descend to depth " + std::to_string(dungeonDepth) +
                   " and hear claws scraping stone...");
      return;
    }
  }

  const rpg::Choice& choice = currentEncounter->choices[choiceIndex];
  player.storyFlags |= choice.storyFlag;

  if (currentEncounterId == 1 && choice.nextEncounterId == 2) {
    const auto* pendingUnique = selectUniqueBossForDepth();
    if (pendingUnique) {
      setNarrative(std::string("A warning spreads through the tavern: a unique ") + pendingUnique->name +
                   " prowls depth " + std::to_string(dungeonDepth) + ".");
    }
  }

  currentEncounterId = choice.nextEncounterId;
  currentEncounter = rpg::getEncounter(currentEncounterId);
  selectedIndex = 0;

  if (!currentEncounter) {
    gameActive = false;
    currentScreen = Screen::MainMenu;
    setNarrative("The trail goes cold. You return to town.");
    return;
  }

  if (currentEncounter->type == rpg::EncounterType::Merchant) {
    currentScreen = Screen::Merchant;
  } else if (currentEncounter->type == rpg::EncounterType::RestSite) {
    player.hp = player.maxHp;
    setNarrative("You rest deeply and recover your strength.");
    currentScreen = Screen::Encounter;
  } else {
    currentScreen = Screen::Encounter;
    setNarrative(std::string("You chose: ") + choice.text);
  }
}

void RPGActivity::handleMerchantAction(int index) {
  switch (index) {
    case 0: {  // Healing potion
      constexpr uint16_t price = 25;
      if (player.goldCoins < price) {
        setNarrative("You count your coins. Not enough for a healing potion.");
        return;
      }
      if (!addItemToInventory(rpg::ITEM_HEALING_POTION)) {
        setNarrative("Your pack is full. The merchant shrugs.");
        return;
      }
      player.goldCoins = static_cast<uint16_t>(player.goldCoins - price);
      setNarrative("You buy a healing potion and tuck it into your satchel.");
      return;
    }
    case 1: {  // Fireball scroll
      constexpr uint16_t price = 60;
      if (player.goldCoins < price) {
        setNarrative("Arcane parchment is costly. You need more gold.");
        return;
      }
      if (!addItemToInventory(rpg::ITEM_SCROLL_FIREBALL)) {
        setNarrative("No room in your inventory for the scroll.");
        return;
      }
      player.goldCoins = static_cast<uint16_t>(player.goldCoins - price);
      setNarrative("You purchase a crackling fireball scroll.");
      return;
    }
    case 2: {  // Sell first item
      if (player.inventoryCount == 0) {
        setNarrative("You have nothing to sell.");
        return;
      }
      const auto sold = player.inventory[0];
      const auto* item = rpg::getItem(sold.itemId);
      const uint16_t value = item ? static_cast<uint16_t>(std::max<int>(1, item->value / 2)) : 5;
      player.goldCoins = static_cast<uint16_t>(player.goldCoins + value);

      for (uint8_t i = 0; i + 1 < player.inventoryCount; ++i) {
        player.inventory[i] = player.inventory[i + 1];
      }
      player.inventoryCount--;
      if (player.inventoryCount < rpg::Character::MAX_INVENTORY) {
        player.inventory[player.inventoryCount] = {0, 0};
      }

      if (item) {
        setNarrative(std::string("You sell ") + item->name + " for " + std::to_string(value) + " gold.");
      } else {
        setNarrative("You sell an odd trinket for a few coins.");
      }
      return;
    }
    case 3:  // Leave
      currentEncounterId = 1;
      currentEncounter = rpg::getEncounter(currentEncounterId);
      currentScreen = Screen::Encounter;
      selectedIndex = 0;
      setNarrative("You leave the merchant and return to Ashwick tavern.");
      return;
    default:
      return;
  }
}

void RPGActivity::handleCombatAction(int action) {
  if (currentEnemy == nullptr || enemyHp == 0) return;

  applyStartOfRoundEffects();
  if (enemyHp == 0 || player.hp == 0) {
    // Resolve immediate KO from status ticks
    if (enemyHp == 0) {
      uint32_t xpGain = currentEnemy->experienceReward;
      uint16_t goldGain = currentEnemy->goldReward;
      if (currentFightIsUnique && currentEnemy) {
        defeatedUniqueMask |= uniqueBit(currentEnemy->id);
        xpGain = static_cast<uint32_t>(xpGain + xpGain / 2);
        goldGain = static_cast<uint16_t>(goldGain + goldGain / 2);

        bool chestGranted = false;
        chestGranted |= addItemToInventory(rpg::ITEM_HEALING_POTION, 1);
        if ((esp_random() % 2) == 0) {
          chestGranted |= addItemToInventory(rpg::ITEM_SCROLL_FIREBALL, 1);
        } else {
          chestGranted |= addItemToInventory(rpg::ITEM_PLATE_ARMOR, 1);
        }
        if (!chestGranted) {
          goldGain = static_cast<uint16_t>(std::min<int>(65535, goldGain + 80));
        }
      }
      player.goldCoins = static_cast<uint16_t>(std::min<int>(65535, player.goldCoins + goldGain));
      player.addExperience(xpGain);
      increaseDepthProgression();
      currentEncounterId = 6;
      currentEncounter = rpg::getEncounter(currentEncounterId);
      currentScreen = Screen::CombatReward;
      setNarrative(currentFightIsUnique
                       ? "The unique guardian falls! A sealed chest bursts open with rare spoils."
                       : "The foe collapses from lingering wounds. Victory is yours.");
      currentFightIsUnique = false;
    } else {
      player.hp = 1;
      gameActive = false;
      currentEncounter = nullptr;
      currentEncounterId = 1;
      currentScreen = Screen::MainMenu;
      setNarrative("You collapse, then awaken in Ashwick under a healer's care.");
      currentFightIsUnique = false;
    }
    return;
  }

  if (playerStunTurns > 0) {
    if ((esp_random() % 100) < 45) {
      playerStunTurns--;
      setNarrative("Your vision spins. You lose your action this turn.");
      if (rollAttack(0, static_cast<int>(player.armorClass))) {
        const uint16_t enemyDamage = currentEnemy->damage;
        player.hp = (enemyDamage >= player.hp) ? 0 : static_cast<uint16_t>(player.hp - enemyDamage);
        setNarrative(narrativeText + "  The " + currentEnemy->name + " strikes for " + std::to_string(enemyDamage) + ".");
      }
      if (player.hp == 0) {
        player.hp = 1;
        gameActive = false;
        currentEncounter = nullptr;
        currentEncounterId = 1;
        currentScreen = Screen::MainMenu;
        setNarrative("Beaten and dazed, you barely crawl back to town.");
      }
      return;
    }
    playerStunTurns--;
  }

  const int attackBonus = 2 + abilityMod(player.stats.strength);
  const int defendBonus = 2 + abilityMod(player.stats.dexterity);
  const uint16_t playerDamage = static_cast<uint16_t>(std::max(1, 4 + abilityMod(player.stats.strength)) +
                                                       static_cast<int>(esp_random() % 5));
  const uint16_t enemyDamage = currentEnemy->damage;

  switch (action) {
    case 0: {  // Attack
      if (rollAttack(attackBonus, currentEnemy->armorClass)) {
        uint16_t dealt = playerDamage;
        const int critChance = std::max(5, std::min(35, 8 + abilityMod(player.stats.dexterity) * 3));
        const bool crit = static_cast<int>(esp_random() % 100) < critChance;
        if (crit) {
          dealt = static_cast<uint16_t>(dealt * 2);
        }
        enemyHp = (dealt >= enemyHp) ? 0 : static_cast<uint16_t>(enemyHp - dealt);

        setNarrative(crit ? ("Critical hit! You deal " + std::to_string(dealt) + " damage.")
                          : ("Your strike lands for " + std::to_string(dealt) + " damage."));

        if ((esp_random() % 100) < 18) {
          enemyPoisonTurns = static_cast<uint8_t>(std::max<uint8_t>(enemyPoisonTurns, 2));
          setNarrative(narrativeText + "  The wound festers with poison.");
        }
        if ((esp_random() % 100) < 12) {
          enemyStunTurns = static_cast<uint8_t>(std::max<uint8_t>(enemyStunTurns, 1));
          setNarrative(narrativeText + "  The enemy is staggered.");
        }
      } else {
        setNarrative("Your blade slices only mist. You miss.");
      }

      bool enemyActs = true;
      if (enemyStunTurns > 0) {
        if ((esp_random() % 100) < 50) {
          enemyActs = false;
          setNarrative(narrativeText + "  The " + currentEnemy->name + " is stunned and misses a turn.");
        }
        enemyStunTurns--;
      }

      if (enemyHp > 0 && enemyActs && rollAttack(0, static_cast<int>(player.armorClass))) {
        player.hp = (enemyDamage >= player.hp) ? 0 : static_cast<uint16_t>(player.hp - enemyDamage);
        setNarrative(narrativeText + "  The " + currentEnemy->name + " hits you for " + std::to_string(enemyDamage) + ".");

        if ((esp_random() % 100) < 14) {
          playerPoisonTurns = static_cast<uint8_t>(std::max<uint8_t>(playerPoisonTurns, 2));
          setNarrative(narrativeText + "  You are poisoned.");
        }
        if ((esp_random() % 100) < 10) {
          playerStunTurns = static_cast<uint8_t>(std::max<uint8_t>(playerStunTurns, 1));
          setNarrative(narrativeText + "  You are stunned.");
        }
      }

      if (enemyHp == 0) {
        uint32_t xpGain = currentEnemy->experienceReward;
        uint16_t goldGain = currentEnemy->goldReward;
        if (currentFightIsUnique && currentEnemy) {
          defeatedUniqueMask |= uniqueBit(currentEnemy->id);
          xpGain = static_cast<uint32_t>(xpGain + xpGain / 2);
          goldGain = static_cast<uint16_t>(goldGain + goldGain / 2);

          bool chestGranted = false;
          chestGranted |= addItemToInventory(rpg::ITEM_HEALING_POTION, 1);
          if ((esp_random() % 2) == 0) {
            chestGranted |= addItemToInventory(rpg::ITEM_SCROLL_FIREBALL, 1);
          } else {
            chestGranted |= addItemToInventory(rpg::ITEM_PLATE_ARMOR, 1);
          }
          if (!chestGranted) {
            goldGain = static_cast<uint16_t>(std::min<int>(65535, goldGain + 80));
          }
        }
        player.goldCoins = static_cast<uint16_t>(std::min<int>(65535, player.goldCoins + goldGain));
        player.addExperience(xpGain);
        increaseDepthProgression();
        currentEncounterId = 6;
        currentEncounter = rpg::getEncounter(currentEncounterId);
        currentScreen = Screen::CombatReward;
        setNarrative(currentFightIsUnique
                         ? "The depth guardian is slain. You crack open its hoard chest and claim the relics."
                         : "The foe falls. You seize coin and hard-earned experience.");
        currentFightIsUnique = false;
      } else if (player.hp == 0) {
        player.hp = 1;
        gameActive = false;
        currentEncounter = nullptr;
        currentEncounterId = 1;
        currentScreen = Screen::MainMenu;
        setNarrative("Wounded and staggering, you retreat to town.");
        currentFightIsUnique = false;
      }
      return;
    }
    case 1: {  // Defend
      if (rollAttack(defendBonus, currentEnemy->armorClass + 2)) {
        player.hp = (enemyDamage / 2 >= player.hp) ? 0 : static_cast<uint16_t>(player.hp - enemyDamage / 2);
        setNarrative("You brace behind your guard and soften the blow.");
      } else {
        setNarrative("You hold your stance; the enemy cannot break your defense.");
      }

      if ((esp_random() % 100) < 8) {
        enemyStunTurns = static_cast<uint8_t>(std::max<uint8_t>(enemyStunTurns, 1));
        setNarrative(narrativeText + "  Your defensive riposte staggers the enemy.");
      }

      if (player.hp == 0) {
        player.hp = 1;
        gameActive = false;
        currentEncounter = nullptr;
        currentEncounterId = 1;
        currentScreen = Screen::MainMenu;
        setNarrative("You survive by inches and withdraw to safety.");
      }
      return;
    }
    case 2:  // Flee
      currentEncounterId = 1;
      currentEncounter = rpg::getEncounter(currentEncounterId);
      currentScreen = Screen::Encounter;
      selectedIndex = 0;
      currentFightIsUnique = false;
      setNarrative("You disengage and flee back toward Ashwick.");
      return;
    default:
      return;
  }
}

void RPGActivity::loop() {
  using Button = MappedInputManager::Button;

  switch (currentScreen) {
    case Screen::CharacterSelection: {
      constexpr int classCount = 6;

      buttonNavigator.onNextRelease([this] {
        selectedIndex = ButtonNavigator::nextIndex(selectedIndex, classCount);
        updateRequired = true;
      });

      buttonNavigator.onPreviousRelease([this] {
        selectedIndex = ButtonNavigator::previousIndex(selectedIndex, classCount);
        updateRequired = true;
      });

      if (mappedInput.wasReleased(Button::Confirm)) {
        createCharacter(static_cast<rpg::CharacterClass>(selectedIndex),
                        rpg::classToString(static_cast<rpg::CharacterClass>(selectedIndex)));
        currentScreen = Screen::MainMenu;
        selectedIndex = 0;
        gameActive = false;
        currentEncounter = nullptr;
        currentEncounterId = 1;
        setNarrative("A new hero answers the call.");
        updateRequired = true;
      }

      if (mappedInput.wasReleased(Button::Back)) {
        if (onBack) onBack();
        return;
      }
      break;
    }

    case Screen::MainMenu: {
      constexpr int menuItems = 6;

      buttonNavigator.onNextRelease([this] {
        selectedIndex = ButtonNavigator::nextIndex(selectedIndex, menuItems);
        updateRequired = true;
      });

      buttonNavigator.onPreviousRelease([this] {
        selectedIndex = ButtonNavigator::previousIndex(selectedIndex, menuItems);
        updateRequired = true;
      });

      if (mappedInput.wasReleased(Button::Confirm)) {
        switch (selectedIndex) {
          case 0:  // Continue / Start
            if (!gameActive || !currentEncounter) {
              gameActive = true;
              currentEncounterId = 1;
              currentEncounter = rpg::getEncounter(currentEncounterId);
            }
            currentScreen = (currentEncounter && currentEncounter->type == rpg::EncounterType::Merchant) ? Screen::Merchant
                                                                                                           : Screen::Encounter;
            selectedIndex = 0;
            setNarrative("You step back onto the road.");
            updateRequired = true;
            break;
          case 1:  // Save
            saveGame();
            updateRequired = true;
            break;
          case 2:  // Load
            loadGame();
            updateRequired = true;
            break;
          case 3:  // Character
            currentScreen = Screen::Inventory;
            selectedIndex = 0;
            updateRequired = true;
            break;
          case 4:  // New hero
            currentScreen = Screen::CharacterSelection;
            selectedIndex = 0;
            updateRequired = true;
            break;
          case 5:  // Exit
            if (onBack) onBack();
            return;
        }
      }

      if (mappedInput.wasReleased(Button::Back)) {
        if (onBack) onBack();
        return;
      }
      break;
    }

    case Screen::Encounter: {
      if (currentEncounter == nullptr) {
        currentScreen = Screen::MainMenu;
        updateRequired = true;
        break;
      }

      if (currentEncounter->type == rpg::EncounterType::Combat) {
        enterCombat(currentEncounter->enemy);
        currentScreen = Screen::Combat;
        updateRequired = true;
        break;
      }

      if (currentEncounter->type == rpg::EncounterType::Merchant) {
        currentScreen = Screen::Merchant;
        selectedIndex = 0;
        updateRequired = true;
        break;
      }

      if (currentEncounter->type == rpg::EncounterType::RestSite && mappedInput.wasReleased(Button::Confirm)) {
        player.hp = player.maxHp;
        currentEncounterId = 1;
        currentEncounter = rpg::getEncounter(currentEncounterId);
        currentScreen = Screen::Encounter;
        setNarrative("You wake rested and ready for danger.");
        selectedIndex = 0;
        updateRequired = true;
        break;
      }

      if (currentEncounter->choiceCount > 0) {
        buttonNavigator.onNextRelease([this] {
          selectedIndex = ButtonNavigator::nextIndex(selectedIndex, currentEncounter->choiceCount);
          updateRequired = true;
        });

        buttonNavigator.onPreviousRelease([this] {
          selectedIndex = ButtonNavigator::previousIndex(selectedIndex, currentEncounter->choiceCount);
          updateRequired = true;
        });

        if (mappedInput.wasReleased(Button::Confirm)) {
          handleEncounterChoice(selectedIndex);
          updateRequired = true;
        }
      } else {
        if (mappedInput.wasReleased(Button::Confirm)) {
          currentEncounterId = 1;
          currentEncounter = rpg::getEncounter(currentEncounterId);
          selectedIndex = 0;
          setNarrative("You head back toward the safety of the tavern.");
          updateRequired = true;
        }
      }

      if (mappedInput.wasReleased(Button::Back)) {
        currentScreen = Screen::MainMenu;
        selectedIndex = 0;
        updateRequired = true;
      }
      break;
    }

    case Screen::Merchant: {
      constexpr int merchantItems = 4;

      buttonNavigator.onNextRelease([this] {
        selectedIndex = ButtonNavigator::nextIndex(selectedIndex, merchantItems);
        updateRequired = true;
      });

      buttonNavigator.onPreviousRelease([this] {
        selectedIndex = ButtonNavigator::previousIndex(selectedIndex, merchantItems);
        updateRequired = true;
      });

      if (mappedInput.wasReleased(Button::Confirm)) {
        handleMerchantAction(selectedIndex);
        updateRequired = true;
      }

      if (mappedInput.wasReleased(Button::Back)) {
        currentEncounterId = 1;
        currentEncounter = rpg::getEncounter(currentEncounterId);
        currentScreen = Screen::Encounter;
        selectedIndex = 0;
        setNarrative("You leave the market and return to Ashwick.");
        updateRequired = true;
      }
      break;
    }

    case Screen::Combat: {
      constexpr int combatActions = 3;

      buttonNavigator.onNextRelease([this] {
        selectedIndex = ButtonNavigator::nextIndex(selectedIndex, combatActions);
        updateRequired = true;
      });

      buttonNavigator.onPreviousRelease([this] {
        selectedIndex = ButtonNavigator::previousIndex(selectedIndex, combatActions);
        updateRequired = true;
      });

      if (mappedInput.wasReleased(Button::Confirm)) {
        handleCombatAction(selectedIndex);
        updateRequired = true;
      }
      break;
    }

    case Screen::Inventory: {
      if (mappedInput.wasReleased(Button::Back) || mappedInput.wasReleased(Button::Confirm)) {
        currentScreen = Screen::MainMenu;
        selectedIndex = 0;
        updateRequired = true;
      }
      break;
    }

    case Screen::CombatReward: {
      if (mappedInput.wasReleased(Button::Confirm)) {
        currentScreen = Screen::Encounter;
        selectedIndex = 0;
        updateRequired = true;
      }
      break;
    }
  }
}

void RPGActivity::render() {
  switch (currentScreen) {
    case Screen::CharacterSelection:
      renderCharacterSelection();
      break;
    case Screen::MainMenu:
      renderMainMenu();
      break;
    case Screen::Encounter:
      renderEncounter();
      break;
    case Screen::Merchant:
      renderMerchant();
      break;
    case Screen::Combat:
      renderCombat();
      break;
    case Screen::Inventory:
      renderInventory();
      break;
    case Screen::CombatReward:
      renderCombatReward();
      break;
  }
}

void RPGActivity::renderCharacterSelection() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), "Choose Your Class");

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  static const char* classes[] = {"Warrior", "Rogue", "Mage", "Cleric", "Ranger", "Barbarian"};

  GUI.drawButtonMenu(renderer, Rect(0, contentTop + 14, pageWidth, contentHeight - 14), 6, selectedIndex,
                     [](int index) { return std::string(classes[index]); }, nullptr);

  const int textY = contentTop;
  const int descW = pageWidth - metrics.contentSidePadding * 2;
  const char* desc =
      "Pick a hero archetype. You can create a new hero later from the main menu.";
  auto lines = wrapText(renderer, SMALL_FONT_ID, desc, descW, 2);
  for (size_t i = 0; i < lines.size(); ++i) {
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, textY + static_cast<int>(i) * 14, lines[i].c_str());
  }

  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Select", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void RPGActivity::renderMainMenu() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), "Chronicles of Ashwick");

  const int x = metrics.contentSidePadding;
  const int sectionW = pageWidth - x * 2;
  const int sectionGap = 6;

  const int topY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int topH = 76;

  const int bottomH = 44;
  const int bottomY = pageHeight - metrics.buttonHintsHeight - bottomH - 2;

  const int middleY = topY + topH + sectionGap;
  const int middleH = std::max(62, bottomY - middleY - sectionGap);

  renderer.drawRect(x, topY, sectionW, topH, true);
  renderer.drawRect(x, middleY, sectionW, middleH, true);
  renderer.drawRect(x, bottomY, sectionW, bottomH, true);

  int y = topY + 6;

  char buf[96];
  snprintf(buf, sizeof(buf), "%s the %s  (Lvl %u)", player.name.c_str(), rpg::classToString(player.charClass),
           player.level);
  renderer.drawText(UI_10_FONT_ID, x + 5, y, buf, true, EpdFontFamily::BOLD);
  y += 16;

  snprintf(buf, sizeof(buf), "HP %u/%u   Gold %u   XP %lu", player.hp, player.maxHp, player.goldCoins,
           static_cast<unsigned long>(player.experience));
  renderer.drawText(UI_10_FONT_ID, x + 5, y, buf);
  y += 15;

  snprintf(buf, sizeof(buf), "Depth: %u   Victories: %u", dungeonDepth, encountersWon);
  renderer.drawText(SMALL_FONT_ID, x + 5, y, buf);
  y += 13;

  snprintf(buf, sizeof(buf), "Uniques defeated: %d/4", defeatedUniqueCount(defeatedUniqueMask));
  renderer.drawText(SMALL_FONT_ID, x + 5, y, buf);
  y += 13;

  renderer.drawText(SMALL_FONT_ID, x + 5, y, saveExists ? "Save file: Found" : "Save file: None");

  const char* startLabel = (gameActive && currentEncounter != nullptr) ? "Continue Adventure" : "Start Adventure";
  const char* items[] = {startLabel, "Save Adventure", "Load Adventure", "Character Sheet", "New Hero", "Exit"};

  renderer.drawText(SMALL_FONT_ID, x + 5, middleY + 4, "Actions", true, EpdFontFamily::BOLD);

  GUI.drawButtonMenu(renderer, Rect(x + 2, middleY + 16, sectionW - 4, middleH - 20), 6, selectedIndex,
                     [&](int index) { return std::string(items[index]); }, nullptr);

  renderer.drawText(SMALL_FONT_ID, x + 5, bottomY + 4, "Log", true, EpdFontFamily::BOLD);

  if (!narrativeText.empty()) {
    auto noteLines = wrapText(renderer, UI_10_FONT_ID, narrativeText.c_str(), sectionW - 10, 2);
    for (size_t i = 0; i < noteLines.size(); ++i) {
      renderer.drawText(UI_10_FONT_ID, x + 5, bottomY + 16 + static_cast<int>(i) * 13, noteLines[i].c_str());
    }
  } else {
    renderer.drawText(UI_10_FONT_ID, x + 5, bottomY + 16, "No recent events.");
  }

  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Select", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void RPGActivity::renderEncounter() {
  renderer.clearScreen();

  if (!currentEncounter) {
    renderer.displayBuffer();
    return;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), currentEncounter->title);

  const int x = metrics.contentSidePadding;
  const int sectionW = pageWidth - x * 2;
  const int sectionGap = 6;

  const int topY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int topH = 62;

  const int bottomH = 44;
  const int bottomY = pageHeight - metrics.buttonHintsHeight - bottomH - 2;

  const int middleY = topY + topH + sectionGap;
  const int middleH = std::max(52, bottomY - middleY - sectionGap);

  renderer.drawRect(x, topY, sectionW, topH, true);
  renderer.drawRect(x, middleY, sectionW, middleH, true);
  renderer.drawRect(x, bottomY, sectionW, bottomH, true);

  int y = topY + 6;
  const int textW = sectionW - 10;

  char depthInfo[32];
  snprintf(depthInfo, sizeof(depthInfo), "Dungeon depth: %u", dungeonDepth);
  renderer.drawText(UI_10_FONT_ID, x + 5, y, depthInfo, true, EpdFontFamily::BOLD);
  y += 18;

  auto descLines = wrapText(renderer, UI_10_FONT_ID, currentEncounter->description, textW, 2);
  for (const auto& line : descLines) {
    renderer.drawText(UI_10_FONT_ID, x + 5, y, line.c_str());
    y += 15;
  }

  y = middleY + 6;
  renderer.drawText(SMALL_FONT_ID, x + 5, y, "Actions", true, EpdFontFamily::BOLD);
  y += 14;

  if (currentEncounter->type == rpg::EncounterType::RestSite) {
    renderer.drawText(UI_10_FONT_ID, x + 5, y, "Select to Rest and Return", true, EpdFontFamily::BOLD);
  } else if (currentEncounter->choiceCount > 0) {
    const int choiceHeight = 24;
    for (int i = 0; i < currentEncounter->choiceCount; ++i) {
      const bool selected = (i == selectedIndex);
      const int rowY = y + i * choiceHeight;
      if (rowY + choiceHeight > middleY + middleH - 4) break;
      if (selected) {
        renderer.fillRect(x + 4, rowY, sectionW - 8, choiceHeight - 2, true);
      }
      std::string row = renderer.truncatedText(UI_10_FONT_ID, currentEncounter->choices[i].text, sectionW - 18);
      renderer.drawText(UI_10_FONT_ID, x + 8, rowY + 4, row.c_str(), !selected);
    }
  } else {
    renderer.drawText(UI_10_FONT_ID, x + 5, y, "Select to Continue", true, EpdFontFamily::BOLD);
  }

  renderer.drawText(SMALL_FONT_ID, x + 5, bottomY + 4, "Log", true, EpdFontFamily::BOLD);
  if (!narrativeText.empty()) {
    auto noteLines = wrapText(renderer, UI_10_FONT_ID, narrativeText.c_str(), sectionW - 10, 2);
    for (size_t i = 0; i < noteLines.size(); ++i) {
      renderer.drawText(UI_10_FONT_ID, x + 5, bottomY + 16 + static_cast<int>(i) * 13, noteLines[i].c_str());
    }
  } else {
    renderer.drawText(UI_10_FONT_ID, x + 5, bottomY + 16, "No recent events.");
  }

  const auto labels = mappedInput.mapLabels("\xC2\xAB Menu", "Choose", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void RPGActivity::renderMerchant() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), "Wandering Merchant");

  const int x = metrics.contentSidePadding;
  const int sectionW = pageWidth - x * 2;
  const int sectionGap = 6;

  const int topY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int topH = 58;

  const int bottomH = 44;
  const int bottomY = pageHeight - metrics.buttonHintsHeight - bottomH - 2;

  const int middleY = topY + topH + sectionGap;
  const int middleH = std::max(52, bottomY - middleY - sectionGap);

  renderer.drawRect(x, topY, sectionW, topH, true);
  renderer.drawRect(x, middleY, sectionW, middleH, true);
  renderer.drawRect(x, bottomY, sectionW, bottomH, true);

  int y = topY + 6;
  const int textW = sectionW - 10;

  const char* intro = "The merchant adjusts his hood and taps a brass lockbox.";
  auto introLines = wrapText(renderer, UI_10_FONT_ID, intro, textW, 2);
  for (const auto& line : introLines) {
    renderer.drawText(UI_10_FONT_ID, x + 5, y, line.c_str());
    y += 15;
  }

  char stats[64];
  snprintf(stats, sizeof(stats), "Gold: %u   Pack: %u/%d", player.goldCoins, player.inventoryCount,
           rpg::Character::MAX_INVENTORY);
  renderer.drawText(UI_10_FONT_ID, x + 5, y, stats, true, EpdFontFamily::BOLD);

  y = middleY + 6;
  renderer.drawText(SMALL_FONT_ID, x + 5, y, "Trade", true, EpdFontFamily::BOLD);
  y += 14;

  const char* options[] = {"Buy Healing Potion (25g)", "Buy Fireball Scroll (60g)", "Sell First Item", "Leave Market"};
  const int optionCount = 4;
  const int rowH = 24;

  for (int i = 0; i < optionCount; ++i) {
    const bool selected = (i == selectedIndex);
    const int rowY = y + i * rowH;
    if (rowY + rowH > middleY + middleH - 4) break;
    if (selected) {
      renderer.fillRect(x + 4, rowY, sectionW - 8, rowH - 2, true);
    }
    renderer.drawText(UI_10_FONT_ID, x + 8, rowY + 4, options[i], !selected);
  }

  renderer.drawText(SMALL_FONT_ID, x + 5, bottomY + 4, "Log", true, EpdFontFamily::BOLD);
  if (!narrativeText.empty()) {
    auto noteLines = wrapText(renderer, UI_10_FONT_ID, narrativeText.c_str(), sectionW - 10, 2);
    for (size_t i = 0; i < noteLines.size(); ++i) {
      renderer.drawText(UI_10_FONT_ID, x + 5, bottomY + 16 + static_cast<int>(i) * 13, noteLines[i].c_str());
    }
  } else {
    renderer.drawText(UI_10_FONT_ID, x + 5, bottomY + 16, "No recent events.");
  }

  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Trade", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void RPGActivity::renderCombat() {
  renderer.clearScreen();

  if (!currentEnemy) {
    renderer.displayBuffer();
    return;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight),
                 currentFightIsUnique ? "Combat - Unique" : "Combat");

  const int x = metrics.contentSidePadding;
  const int sectionW = pageWidth - x * 2;
  const int sectionGap = 6;

  const int topY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int topH = 64;

  const int bottomH = 46;
  const int bottomY = pageHeight - metrics.buttonHintsHeight - bottomH - 2;

  const int middleY = topY + topH + sectionGap;
  const int middleH = std::max(52, bottomY - middleY - sectionGap);

  renderer.drawRect(x, topY, sectionW, topH, true);
  renderer.drawRect(x, middleY, sectionW, middleH, true);
  renderer.drawRect(x, bottomY, sectionW, bottomH, true);

  int y = topY + 6;

  char buf[80];
  snprintf(buf, sizeof(buf), "%s   HP: %u/%u", currentEnemy->name.c_str(), enemyHp, currentEnemy->maxHp);
  renderer.drawText(UI_10_FONT_ID, x + 5, y, buf, true, EpdFontFamily::BOLD);
  y += 18;

  snprintf(buf, sizeof(buf), "Your HP: %u/%u   AC: %u   Depth: %u", player.hp, player.maxHp, player.armorClass,
           dungeonDepth);
  renderer.drawText(UI_10_FONT_ID, x + 5, y, buf);
  y += 16;

  char statusLine[64];
  snprintf(statusLine, sizeof(statusLine), "You[P%d S%d]  Enemy[P%d S%d]", playerPoisonTurns, playerStunTurns,
           enemyPoisonTurns, enemyStunTurns);
  renderer.drawText(SMALL_FONT_ID, x + 5, y, statusLine);

  y = middleY + 6;
  renderer.drawText(SMALL_FONT_ID, x + 5, y, "Actions", true, EpdFontFamily::BOLD);
  y += 14;

  const char* actions[] = {"Attack", "Defend", "Flee"};
  const int actionHeight = 22;
  for (int i = 0; i < 3; ++i) {
    const bool selected = (i == selectedIndex);
    const int rowY = y + i * actionHeight;
    if (rowY + actionHeight > middleY + middleH - 4) break;
    if (selected) {
      renderer.fillRect(x + 4, rowY, sectionW - 8, actionHeight - 2, true);
    }
    renderer.drawText(UI_10_FONT_ID, x + 8, rowY + 3, actions[i], !selected);
  }

  renderer.drawText(SMALL_FONT_ID, x + 5, bottomY + 4, "Combat log", true, EpdFontFamily::BOLD);
  auto lines = wrapText(renderer, UI_10_FONT_ID, narrativeText.c_str(), sectionW - 10, 2);
  for (size_t i = 0; i < lines.size(); ++i) {
    renderer.drawText(UI_10_FONT_ID, x + 5, bottomY + 16 + static_cast<int>(i) * 13, lines[i].c_str());
  }

  const auto labels = mappedInput.mapLabels("", "Choose", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void RPGActivity::renderInventory() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), "Character Sheet");

  const auto& p = player;
  const int x = metrics.contentSidePadding;
  const int sectionW = pageWidth - x * 2;
  const int sectionGap = 6;

  const int topY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int topH = 62;

  const int bottomH = 36;
  const int bottomY = pageHeight - metrics.buttonHintsHeight - bottomH - 2;

  const int middleY = topY + topH + sectionGap;
  const int middleH = std::max(58, bottomY - middleY - sectionGap);

  renderer.drawRect(x, topY, sectionW, topH, true);
  renderer.drawRect(x, middleY, sectionW, middleH, true);
  renderer.drawRect(x, bottomY, sectionW, bottomH, true);

  int y = topY + 6;

  char buf[96];
  snprintf(buf, sizeof(buf), "%s the %s  (Lvl %u)", p.name.c_str(), rpg::classToString(p.charClass), p.level);
  renderer.drawText(UI_10_FONT_ID, x + 5, y, buf, true, EpdFontFamily::BOLD);
  y += 16;

  snprintf(buf, sizeof(buf), "HP: %u/%u   AC: %u   Gold: %u", p.hp, p.maxHp, p.armorClass, p.goldCoins);
  renderer.drawText(UI_10_FONT_ID, x + 5, y, buf);
  y += 15;

  snprintf(buf, sizeof(buf), "XP: %lu   Next: %lu", static_cast<unsigned long>(p.experience),
           static_cast<unsigned long>(std::max<uint32_t>(0, p.experienceForNextLevel() - p.experience)));
  renderer.drawText(SMALL_FONT_ID, x + 5, y, buf);

  y = middleY + 6;

  snprintf(buf, sizeof(buf), "STR %u  DEX %u  CON %u", p.stats.strength, p.stats.dexterity, p.stats.constitution);
  renderer.drawText(UI_10_FONT_ID, x + 5, y, buf);
  y += 14;

  snprintf(buf, sizeof(buf), "INT %u  WIS %u  CHA %u", p.stats.intelligence, p.stats.wisdom, p.stats.charisma);
  renderer.drawText(UI_10_FONT_ID, x + 5, y, buf);
  y += 16;

  renderer.drawText(SMALL_FONT_ID, x + 5, y, "Inventory", true, EpdFontFamily::BOLD);
  y += 14;

  if (p.inventoryCount == 0) {
    renderer.drawText(UI_10_FONT_ID, x + 5, y, "(empty)");
  } else {
    const int availableRows = std::max(1, (middleY + middleH - y - 6) / 14);
    const int showCount = std::min<int>(p.inventoryCount, availableRows);
    for (int i = 0; i < showCount; ++i) {
      const auto* item = rpg::getItem(p.inventory[i].itemId);
      const char* itemName = item ? item->name : "Unknown item";
      snprintf(buf, sizeof(buf), "- %s x%u", itemName, p.inventory[i].quantity);
      std::string row = renderer.truncatedText(UI_10_FONT_ID, buf, sectionW - 10);
      renderer.drawText(UI_10_FONT_ID, x + 5, y, row.c_str());
      y += 14;
    }
    if (p.inventoryCount > showCount) {
      snprintf(buf, sizeof(buf), "+%u more item(s)", static_cast<unsigned int>(p.inventoryCount - showCount));
      renderer.drawText(SMALL_FONT_ID, x + 5, y, buf);
    }
  }

  renderer.drawText(SMALL_FONT_ID, x + 5, bottomY + 10, "Back to return", true, EpdFontFamily::BOLD);

  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void RPGActivity::renderCombatReward() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), "Victory");

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 22;

  renderer.drawCenteredText(UI_12_FONT_ID, y, "The battle is won.", true, EpdFontFamily::BOLD);
  y += 28;

  if (currentEnemy) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Reward: %u XP", currentEnemy->experienceReward);
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
    y += 18;
    snprintf(buf, sizeof(buf), "Loot: +%u gold", currentEnemy->goldReward);
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
    y += 18;
  }

  char depthMsg[40];
  snprintf(depthMsg, sizeof(depthMsg), "Current depth: %u", dungeonDepth);
  renderer.drawCenteredText(SMALL_FONT_ID, y, depthMsg);
  y += 16;

  auto lines = wrapText(renderer, SMALL_FONT_ID, narrativeText.c_str(), pageWidth - 24, 3);
  for (const auto& line : lines) {
    renderer.drawCenteredText(SMALL_FONT_ID, y, line.c_str());
    y += 13;
  }

  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - metrics.buttonHintsHeight - 18, "Select to continue");

  const auto labels = mappedInput.mapLabels("", "Continue", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
