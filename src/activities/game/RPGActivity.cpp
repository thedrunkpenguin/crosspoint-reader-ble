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
constexpr uint8_t RPG_SAVE_VERSION = 4;
constexpr const char* RPG_SAVE_DIR = "/.crosspoint/rpg";
constexpr const char* RPG_SAVE_PATH = "/.crosspoint/rpg/save.bin";

// Named unique bosses — aligned with D&D 5e enemies in RPGEncounter.cpp
constexpr uint8_t UNIQUE_DRACONIAN_ID = 3;  // Baaz Draconian captain (Dragonlance)
constexpr uint8_t UNIQUE_WIGHT_ID     = 6;  // Wight lord (Neverwinter undead)
constexpr uint8_t UNIQUE_DROW_ID      = 7;  // Drow commander (Salvatore)
constexpr uint8_t UNIQUE_DRAGON_ID    = 8;  // Young Dragon (Dragonlance)
constexpr int SECTION_BORDER_THICKNESS = 4;
constexpr int SECTION_INNER_PAD_X = 8;
constexpr int SECTION_INNER_PAD_Y = 8;

uint32_t uniqueBit(uint8_t enemyId) {
  if (enemyId >= 31) return 0;
  return (1u << enemyId);
}

int defeatedUniqueCount(uint32_t mask) {
  return __builtin_popcount(mask & (uniqueBit(UNIQUE_DRACONIAN_ID) | uniqueBit(UNIQUE_WIGHT_ID) |
                                    uniqueBit(UNIQUE_DROW_ID)       | uniqueBit(UNIQUE_DRAGON_ID)));
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

void drawBoxTagTopRight(GfxRenderer& renderer, int boxX, int boxY, int boxW, const char* label) {
  if (!label || !label[0]) return;
  const int tagPaddingX = 4;
  const int tagPaddingY = 2;
  const int tagW = renderer.getTextWidth(SMALL_FONT_ID, label) + tagPaddingX * 2;
  const int tagH = renderer.getLineHeight(SMALL_FONT_ID) + tagPaddingY * 2;
  const int tagX = boxX + boxW - tagW - 4;
  const int tagY = boxY + 3;
  renderer.fillRect(tagX, tagY, tagW, tagH, true);
  renderer.drawRect(tagX, tagY, tagW, tagH, true);
  if (tagW > 2 && tagH > 2) {
    renderer.drawRect(tagX + 1, tagY + 1, tagW - 2, tagH - 2, true);
  }
  if (tagW > 4 && tagH > 4) {
    renderer.drawRect(tagX + 2, tagY + 2, tagW - 4, tagH - 4, true);
  }
  renderer.drawText(SMALL_FONT_ID, tagX + tagPaddingX, tagY + tagPaddingY, label, false, EpdFontFamily::BOLD);
}

void drawSectionBox(GfxRenderer& renderer, int x, int y, int w, int h) {
  for (int i = 0; i < SECTION_BORDER_THICKNESS; ++i) {
    const int innerW = w - i * 2;
    const int innerH = h - i * 2;
    if (innerW <= 0 || innerH <= 0) break;
    renderer.drawRect(x + i, y + i, innerW, innerH, true);
  }
}

int taggedSectionContentTop(GfxRenderer& renderer, int boxY) {
  return boxY + SECTION_BORDER_THICKNESS + renderer.getLineHeight(SMALL_FONT_ID) + 18;
}

void drawSelectableRow(GfxRenderer& renderer, int x, int sectionW, int rowY, int rowH, const char* text, bool selected) {
  if (selected) {
    renderer.drawRect(x + 4, rowY, sectionW - 8, rowH - 2, true);
  }

  const int textLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int textY = rowY + (rowH - textLineH) / 2;
  std::string row = renderer.truncatedText(UI_10_FONT_ID, text, sectionW - 20);
  if (selected) {
    renderer.drawText(UI_10_FONT_ID, x + 10, textY, row.c_str(), true, EpdFontFamily::BOLD);
  } else {
    renderer.drawText(UI_10_FONT_ID, x + 10, textY, row.c_str(), true);
  }
}

std::string formatRewardItems(const std::vector<std::string>& items) {
  if (items.empty()) {
    return "";
  }

  std::string result;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i > 0) {
      result += (i + 1 == items.size()) ? ", and " : ", ";
    }
    result += items[i];
  }
  return result;
}

std::string rewardItemLabel(const rpg::ItemDefinition* item, uint8_t quantity) {
  if (!item) {
    return quantity > 1 ? (std::to_string(quantity) + " unknown items") : "an unknown item";
  }

  if (quantity <= 1) {
    return std::string("a ") + item->name;
  }

  return std::to_string(quantity) + "x " + item->name;
}
}  // namespace

void RPGActivity::taskTrampoline(void* param) {
  static_cast<RPGActivity*>(param)->displayTaskLoop();
}

void RPGActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  currentScreen = Screen::MainMenu;
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
  lastRewardExperience = 0;
  lastRewardGold = 0;
  lastRewardItemsText.clear();
  setNarrative("You return to Solace. The Inn of the Last Home glows through the autumn leaves.");
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

void RPGActivity::setRewardSummary(uint32_t experience, uint16_t gold, const std::string& itemsText) {
  lastRewardExperience = experience;
  lastRewardGold = gold;
  lastRewardItemsText = itemsText;
}

void RPGActivity::refreshSaveState() {
  saveExists = Storage.exists(RPG_SAVE_PATH);
}

void RPGActivity::recomputeArmorClass() {
  player.armorClass = player.baseArmorClass;
  if (player.equippedArmorId != rpg::Character::NO_EQUIPPED_ITEM) {
    const auto* armor = rpg::getItem(player.equippedArmorId);
    if (armor && armor->type == rpg::ItemType::Armor) {
      player.armorClass = static_cast<uint16_t>(player.baseArmorClass + armor->armor);
    }
  }
}

int RPGActivity::findInventoryIndexByItemId(uint8_t itemId) const {
  for (uint8_t i = 0; i < player.inventoryCount; ++i) {
    if (player.inventory[i].itemId == itemId) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool RPGActivity::consumeInventoryItemAt(int inventoryIndex, uint8_t quantity) {
  if (inventoryIndex < 0 || inventoryIndex >= player.inventoryCount || quantity == 0) return false;

  auto& slot = player.inventory[static_cast<uint8_t>(inventoryIndex)];
  if (slot.quantity < quantity) return false;

  slot.quantity = static_cast<uint8_t>(slot.quantity - quantity);
  if (slot.quantity > 0) return true;

  if (slot.itemId == player.equippedWeaponId) {
    player.equippedWeaponId = rpg::Character::NO_EQUIPPED_ITEM;
  }
  if (slot.itemId == player.equippedArmorId) {
    player.equippedArmorId = rpg::Character::NO_EQUIPPED_ITEM;
    recomputeArmorClass();
  }

  for (uint8_t i = static_cast<uint8_t>(inventoryIndex); i + 1 < player.inventoryCount; ++i) {
    player.inventory[i] = player.inventory[i + 1];
  }
  player.inventoryCount--;
  if (player.inventoryCount < rpg::Character::MAX_INVENTORY) {
    player.inventory[player.inventoryCount] = {0, 0};
  }

  if (selectedIndex >= player.inventoryCount && player.inventoryCount > 0) {
    selectedIndex = static_cast<int>(player.inventoryCount - 1);
  }

  return true;
}

bool RPGActivity::equipInventoryItemAt(int inventoryIndex) {
  if (inventoryIndex < 0 || inventoryIndex >= player.inventoryCount) return false;
  if (currentScreen == Screen::Combat) {
    setNarrative("You cannot swap gear while blades are drawn.");
    return false;
  }

  const uint8_t itemId = player.inventory[static_cast<uint8_t>(inventoryIndex)].itemId;
  const auto* item = rpg::getItem(itemId);
  if (!item) {
    setNarrative("You inspect a strange trinket with no clear purpose.");
    return false;
  }

  if (item->type == rpg::ItemType::Weapon) {
    if (player.equippedWeaponId == itemId) {
      player.equippedWeaponId = rpg::Character::NO_EQUIPPED_ITEM;
      setNarrative(std::string("You stow ") + item->name + ".");
    } else {
      player.equippedWeaponId = itemId;
      setNarrative(std::string("You equip ") + item->name + ".");
    }
    return true;
  }

  if (item->type == rpg::ItemType::Armor) {
    if (player.equippedArmorId == itemId) {
      player.equippedArmorId = rpg::Character::NO_EQUIPPED_ITEM;
      recomputeArmorClass();
      setNarrative(std::string("You remove ") + item->name + ".");
    } else {
      player.equippedArmorId = itemId;
      recomputeArmorClass();
      setNarrative(std::string("You don ") + item->name + ". AC now " + std::to_string(player.armorClass) + ".");
    }
    return true;
  }

  setNarrative("That item cannot be equipped.");
  return false;
}

bool RPGActivity::useInventoryItemAt(int inventoryIndex) {
  if (inventoryIndex < 0 || inventoryIndex >= player.inventoryCount) return false;

  const uint8_t itemId = player.inventory[static_cast<uint8_t>(inventoryIndex)].itemId;
  const auto* item = rpg::getItem(itemId);
  if (!item) return false;

  bool used = false;

  switch (itemId) {
    case rpg::ITEM_HEALING_POTION: {
      if (player.hp >= player.maxHp) {
        setNarrative("You are already at full health.");
        return false;
      }
      const uint16_t heal = 20;
      player.hp = static_cast<uint16_t>(std::min<int>(player.maxHp, player.hp + heal));
      setNarrative("You drink a healing potion and recover strength.");
      used = true;
      break;
    }
    case rpg::ITEM_SCROLL_HEALING: {
      const uint16_t heal = 35;
      player.hp = static_cast<uint16_t>(std::min<int>(player.maxHp, player.hp + heal));
      playerPoisonTurns = 0;
      playerStunTurns = 0;
      setNarrative("Holy script mends wounds and clears your ailments.");
      used = true;
      break;
    }
    case rpg::ITEM_MANA_POTION: {
      playerStunTurns = 0;
      setNarrative("Your focus sharpens after the draught.");
      used = true;
      break;
    }
    case rpg::ITEM_SCROLL_FIREBALL: {
      if (inventoryReturnScreen != Screen::Combat || currentEnemy == nullptr || enemyHp == 0) {
        setNarrative("The fireball scroll crackles, awaiting a battle target.");
        return false;
      }

      const uint16_t damage = static_cast<uint16_t>(8 + (esp_random() % 7) + std::max(0, abilityMod(player.stats.intelligence)));
      enemyHp = (damage >= enemyHp) ? 0 : static_cast<uint16_t>(enemyHp - damage);
      setNarrative("Flame surges from the scroll for " + std::to_string(damage) + " damage!");
      used = true;
      break;
    }
    default:
      setNarrative("You can't actively use that item right now.");
      return false;
  }

  if (!used) return false;
  consumeInventoryItemAt(inventoryIndex, 1);

  if (inventoryReturnScreen == Screen::Combat && currentEnemy && enemyHp > 0) {
    const uint16_t enemyDamage = currentEnemy->damage;
    if (rollAttack(0, static_cast<int>(player.armorClass))) {
      player.hp = (enemyDamage >= player.hp) ? 0 : static_cast<uint16_t>(player.hp - enemyDamage);
      setNarrative(narrativeText + "  The " + currentEnemy->name + " strikes for " + std::to_string(enemyDamage) + ".");
    } else {
      setNarrative(narrativeText + "  The " + currentEnemy->name + " misses while you recover.");
    }

    if (enemyHp == 0) {
      uint32_t xpGain = currentEnemy->experienceReward;
      uint16_t goldGain = currentEnemy->goldReward;
      if (currentFightIsUnique && currentEnemy) {
        defeatedUniqueMask |= uniqueBit(currentEnemy->id);
        xpGain = static_cast<uint32_t>(xpGain + xpGain / 2);
        goldGain = static_cast<uint16_t>(goldGain + goldGain / 2);
      }
      player.goldCoins = static_cast<uint16_t>(std::min<int>(65535, player.goldCoins + goldGain));
      player.addExperience(xpGain);
      increaseDepthProgression();
      currentEncounterId = 6;
      currentEncounter = rpg::getEncounter(currentEncounterId);
      currentScreen = Screen::CombatReward;
      currentFightIsUnique = false;
      return true;
    }

    if (player.hp == 0) {
      player.hp = 1;
      gameActive = false;
      currentEncounter = nullptr;
      currentEncounterId = 1;
      currentScreen = Screen::MainMenu;
      setNarrative("You wake beneath a healer's torch in the Inn of the Last Home.");
      currentFightIsUnique = false;
      return true;
    }
  }

  return true;
}

void RPGActivity::openInventoryFrom(Screen returnScreen) {
  inventoryReturnScreen = returnScreen;
  previousSelectedIndex = selectedIndex;
  currentScreen = Screen::Inventory;
  selectedIndex = (player.inventoryCount > 0) ? 0 : -1;
}

void RPGActivity::handleInventoryInput() {
  using Button = MappedInputManager::Button;

  if (player.inventoryCount > 0) {
    buttonNavigator.onNextRelease([this] {
      selectedIndex = ButtonNavigator::nextIndex(std::max(0, selectedIndex), player.inventoryCount);
      updateRequired = true;
    });

    buttonNavigator.onPreviousRelease([this] {
      selectedIndex = ButtonNavigator::previousIndex(std::max(0, selectedIndex), player.inventoryCount);
      updateRequired = true;
    });
  }

  if (mappedInput.wasReleased(Button::Confirm) && player.inventoryCount > 0 && selectedIndex >= 0) {
    const int idx = std::min<int>(selectedIndex, player.inventoryCount - 1);
    const auto* item = rpg::getItem(player.inventory[idx].itemId);
    if (item && (item->type == rpg::ItemType::Weapon || item->type == rpg::ItemType::Armor)) {
      equipInventoryItemAt(idx);
    } else {
      useInventoryItemAt(idx);
    }
    updateRequired = true;
  }

  if (mappedInput.wasReleased(Button::Back)) {
    currentScreen = inventoryReturnScreen;
    selectedIndex = std::max(0, previousSelectedIndex);
    updateRequired = true;
  }
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
    setNarrative("The scribe finds no parchment. Save failed.");
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
  serialization::writePod(f, player.baseArmorClass);
  serialization::writePod(f, player.armorClass);
  serialization::writePod(f, player.equippedWeaponId);
  serialization::writePod(f, player.equippedArmorId);

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
  setNarrative("The chronicles of your past deeds are set to parchment. Saved.");
  return true;
}

bool RPGActivity::loadGame() {
  FsFile f;
  if (!Storage.openFileForRead("RPG", RPG_SAVE_PATH, f)) {
    setNarrative("No chronicle of this journey was found.");
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
  if (version >= 4) {
    serialization::readPod(f, player.baseArmorClass);
    serialization::readPod(f, player.armorClass);
    serialization::readPod(f, player.equippedWeaponId);
    serialization::readPod(f, player.equippedArmorId);
  } else {
    serialization::readPod(f, player.armorClass);
    player.baseArmorClass = player.armorClass;
    player.equippedWeaponId = rpg::Character::NO_EQUIPPED_ITEM;
    player.equippedArmorId = rpg::Character::NO_EQUIPPED_ITEM;
  }

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

  if (findInventoryIndexByItemId(player.equippedWeaponId) < 0) {
    player.equippedWeaponId = rpg::Character::NO_EQUIPPED_ITEM;
  }
  if (findInventoryIndexByItemId(player.equippedArmorId) < 0) {
    player.equippedArmorId = rpg::Character::NO_EQUIPPED_ITEM;
  }
  recomputeArmorClass();

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
  setRewardSummary(0, 0, "");

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

  setNarrative("You reopen the weathered journal. Your path through the Darken Wood continues.");
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
  setRewardSummary(0, 0, "");

  switch (charClass) {
    case rpg::CharacterClass::Warrior:
      addItemToInventory(rpg::ITEM_LONGSWORD);
      addItemToInventory(rpg::ITEM_LEATHER_ARMOR);
      player.equippedWeaponId = rpg::ITEM_LONGSWORD;
      player.equippedArmorId = rpg::ITEM_LEATHER_ARMOR;
      break;
    case rpg::CharacterClass::Rogue:
      addItemToInventory(rpg::ITEM_DAGGER);
      addItemToInventory(rpg::ITEM_HEALING_POTION, 2);
      player.equippedWeaponId = rpg::ITEM_DAGGER;
      break;
    case rpg::CharacterClass::Mage:
      addItemToInventory(rpg::ITEM_QUARTERSTAFF);
      addItemToInventory(rpg::ITEM_SCROLL_FIREBALL);
      player.equippedWeaponId = rpg::ITEM_QUARTERSTAFF;
      break;
    case rpg::CharacterClass::Cleric:
      addItemToInventory(rpg::ITEM_SCROLL_HEALING, 2);
      addItemToInventory(rpg::ITEM_HEALING_POTION);
      break;
    case rpg::CharacterClass::Ranger:
      addItemToInventory(rpg::ITEM_DAGGER);
      addItemToInventory(rpg::ITEM_HEALING_POTION);
      player.equippedWeaponId = rpg::ITEM_DAGGER;
      break;
    case rpg::CharacterClass::Barbarian:
      addItemToInventory(rpg::ITEM_LONGSWORD);
      player.equippedWeaponId = rpg::ITEM_LONGSWORD;
      break;
  }

  recomputeArmorClass();
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
  setNarrative("Blades ring out in the forest. The fight is joined.");
}

const rpg::Enemy* RPGActivity::selectUniqueBossForDepth() const {
  if ((dungeonDepth % 5) != 0) return nullptr;

  struct UniqueCandidate {
    uint8_t id;
    uint16_t minDepth;
  };

  static constexpr UniqueCandidate candidates[] = {
      {UNIQUE_DRACONIAN_ID, 5},   // Draconian captain at depth 5+
      {UNIQUE_WIGHT_ID,    10},   // Wight lord at depth 10+
      {UNIQUE_DROW_ID,     20},   // Drow commander at depth 20+
      {UNIQUE_DRAGON_ID,   35},   // Young Dragon at depth 35+
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

  std::vector<std::string> rewardItems;

  if ((esp_random() % 100) < 55) {
    if (addItemToInventory(rpg::ITEM_HEALING_POTION)) {
      rewardItems.push_back(rewardItemLabel(rpg::getItem(rpg::ITEM_HEALING_POTION), 1));
    }
  }
  if ((esp_random() % 100) < 25) {
    if (addItemToInventory(rpg::ITEM_SCROLL_FIREBALL)) {
      rewardItems.push_back(rewardItemLabel(rpg::getItem(rpg::ITEM_SCROLL_FIREBALL), 1));
    }
  }

  const std::string rewardItemsText = formatRewardItems(rewardItems);
  setRewardSummary(0, goldGain, rewardItemsText);

  currentEncounterId = 8;
  currentEncounter = rpg::getEncounter(currentEncounterId);
  currentScreen = Screen::Encounter;
  selectedIndex = 0;
  std::string message = "A hidden chamber opens behind a mossy stone. You claim " + std::to_string(goldGain) + " gold";
  if (!rewardItemsText.empty()) {
    message += " and salvage " + rewardItemsText + ".";
  } else {
    message += ", but the relic shelves are bare.";
  }
  setNarrative(message);
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

  auto grantSatchelLoot = [this](const std::string& intro) {
    constexpr uint16_t satchelGold = 18;
    player.goldCoins = static_cast<uint16_t>(std::min<int>(65535, player.goldCoins + satchelGold));

    const bool gainedPotion = addItemToInventory(rpg::ITEM_HEALING_POTION, 1);
    const bool gainedMana = addItemToInventory(rpg::ITEM_MANA_POTION, 1);

    std::string message = intro + " Inside are " + std::to_string(satchelGold) + " gold";
    if (gainedPotion && gainedMana) {
      message += ", a healing potion, and a mana potion.";
    } else if (gainedPotion) {
      message += " and a healing potion.";
    } else if (gainedMana) {
      message += " and a mana potion.";
    } else {
      message += ", but your pack is too full for the potions.";
    }

    setNarrative(message);
  };

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
      setNarrative("You press to depth " + std::to_string(dungeonDepth) +
                   ". Something stirs the shadows ahead...");
      return;
    }
  }

  if (currentEncounterId == 2 && choiceIndex == 1) {
    const int driftRoll = static_cast<int>(esp_random() % 100);
    if (driftRoll < 35) {
      currentEncounterId = 4;
      currentEncounter = rpg::getEncounter(currentEncounterId);
      currentScreen = Screen::Encounter;
      selectedIndex = 0;
      setNarrative("Mist swallows the trail. You find yourself at a lonely waystone.");
      return;
    }
    if (driftRoll < 70) {
      currentEncounterId = 3;
      currentEncounter = rpg::getEncounter(currentEncounterId);
      currentScreen = Screen::Merchant;
      selectedIndex = 0;
      setNarrative("A trader's wagon waits at a forest crossroads.");
      return;
    }
  }

  if (currentEncounterId == 2 && choiceIndex == 2) {
    if ((esp_random() % 100) < 20) {
      currentEncounterId = 8;
      currentEncounter = rpg::getEncounter(currentEncounterId);
      currentScreen = Screen::Encounter;
      selectedIndex = 0;
      grantSatchelLoot("Heading back, you spot a fallen traveler's pack wedged in the roots.");
      return;
    }
  }

  const rpg::Choice& choice = currentEncounter->choices[choiceIndex];
  player.storyFlags |= choice.storyFlag;

  if (currentEncounterId == 1 && choice.nextEncounterId == 2) {
    const auto* pendingUnique = selectUniqueBossForDepth();
    if (pendingUnique) {
      setNarrative("Word reaches you of a named " + std::string(pendingUnique->name) +
                   " hunting the wood at depth " + std::to_string(dungeonDepth) + ".");
    }
  }

  currentEncounterId = choice.nextEncounterId;
  currentEncounter = rpg::getEncounter(currentEncounterId);
  selectedIndex = 0;

  if (!currentEncounter) {
    gameActive = false;
    currentScreen = Screen::MainMenu;
    setNarrative("The path grows dark. You return to the warmth of Solace.");
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
    if (choice.nextEncounterId == 8) {
      grantSatchelLoot("You empty the fallen pack and sort through the spoils.");
    } else {
      setNarrative(std::string("You chose: ") + choice.text);
    }
  }
}

void RPGActivity::handleMerchantAction(int index) {
  if (player.inventoryCount <= 0) {
    merchantSelectedInventoryIndex = 0;
  } else if (merchantSelectedInventoryIndex >= player.inventoryCount) {
    merchantSelectedInventoryIndex = player.inventoryCount - 1;
  } else if (merchantSelectedInventoryIndex < 0) {
    merchantSelectedInventoryIndex = 0;
  }

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
    case 2: {  // Sell selected item
      if (player.inventoryCount == 0) {
        setNarrative("You have nothing to sell.");
        return;
      }
      const int sellIndex = std::max(0, std::min<int>(merchantSelectedInventoryIndex, player.inventoryCount - 1));
      const auto sold = player.inventory[sellIndex];
      const auto* item = rpg::getItem(sold.itemId);
      const uint16_t value = item ? static_cast<uint16_t>(std::max<int>(1, item->value / 2)) : 5;
      player.goldCoins = static_cast<uint16_t>(player.goldCoins + value);

      consumeInventoryItemAt(sellIndex, 1);
      if (player.inventoryCount > 0 && merchantSelectedInventoryIndex >= player.inventoryCount) {
        merchantSelectedInventoryIndex = player.inventoryCount - 1;
      }

      if (item) {
        setNarrative(std::string("You sell ") + item->name + " for " + std::to_string(value) + " gold.");
      } else {
        setNarrative("You sell an odd trinket for a few coins.");
      }
      return;
    }
    case 3: {  // Equip selected
      if (player.inventoryCount == 0) {
        setNarrative("Your pack is empty.");
        return;
      }

      const int equipIndex = std::max(0, std::min<int>(merchantSelectedInventoryIndex, player.inventoryCount - 1));
      equipInventoryItemAt(equipIndex);
      return;
    }
    case 4:  // Leave
      currentEncounterId = 1;
      currentEncounter = rpg::getEncounter(currentEncounterId);
      currentScreen = Screen::Encounter;
      selectedIndex = 0;
      setNarrative("You leave the trader and return to the road.");
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

        std::vector<std::string> rewardItems;
        bool chestGranted = false;
        if (addItemToInventory(rpg::ITEM_HEALING_POTION, 1)) {
          chestGranted = true;
          rewardItems.push_back(rewardItemLabel(rpg::getItem(rpg::ITEM_HEALING_POTION), 1));
        }
        if ((esp_random() % 2) == 0) {
          if (addItemToInventory(rpg::ITEM_SCROLL_FIREBALL, 1)) {
            chestGranted = true;
            rewardItems.push_back(rewardItemLabel(rpg::getItem(rpg::ITEM_SCROLL_FIREBALL), 1));
          }
        } else {
          if (addItemToInventory(rpg::ITEM_PLATE_ARMOR, 1)) {
            chestGranted = true;
            rewardItems.push_back(rewardItemLabel(rpg::getItem(rpg::ITEM_PLATE_ARMOR), 1));
          }
        }
        if (!chestGranted) {
          goldGain = static_cast<uint16_t>(std::min<int>(65535, goldGain + 80));
        }

        const std::string rewardItemsText = formatRewardItems(rewardItems);
        setRewardSummary(xpGain, goldGain, rewardItemsText);
        player.goldCoins = static_cast<uint16_t>(std::min<int>(65535, player.goldCoins + goldGain));
        player.addExperience(xpGain);
        increaseDepthProgression();
        currentEncounterId = 6;
        currentEncounter = rpg::getEncounter(currentEncounterId);
        currentScreen = Screen::CombatReward;
        if (currentFightIsUnique) {
          std::string message = "The unique guardian falls! A sealed chest bursts open";
          if (!rewardItemsText.empty()) {
            message += ", revealing " + rewardItemsText + ".";
          } else {
            message += ", but only extra coin remains inside.";
          }
          setNarrative(message);
        }
        currentFightIsUnique = false;
        return;
      }
      player.goldCoins = static_cast<uint16_t>(std::min<int>(65535, player.goldCoins + goldGain));
      player.addExperience(xpGain);
      setRewardSummary(xpGain, goldGain, "");
      increaseDepthProgression();
      currentEncounterId = 6;
      currentEncounter = rpg::getEncounter(currentEncounterId);
      currentScreen = Screen::CombatReward;
      setNarrative("The foe collapses from lingering wounds. Victory is yours.");
      currentFightIsUnique = false;
    } else {
      player.hp = 1;
      gameActive = false;
      currentEncounter = nullptr;
      currentEncounterId = 1;
      currentScreen = Screen::MainMenu;
      setNarrative("You collapse and are carried back to Solace under a healer's care.");
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
        setNarrative("Beaten and dazed, you are carried back to the Inn of the Last Home.");
      }
      return;
    }
    playerStunTurns--;
  }

  // D&D 5e: attack = proficiency bonus + ability modifier (DEX for Rogue/Ranger, STR otherwise)
  const int prof = static_cast<int>(player.proficiencyBonus());
  const bool usesDex = (player.charClass == rpg::CharacterClass::Rogue ||
                        player.charClass == rpg::CharacterClass::Ranger);
  const int attackBonus = prof + (usesDex ? abilityMod(player.stats.dexterity) : abilityMod(player.stats.strength));
  const int defendBonus = prof + abilityMod(player.stats.dexterity);  // Dodge uses DEX + prof
  int weaponBonus = 0;
  if (player.equippedWeaponId != rpg::Character::NO_EQUIPPED_ITEM) {
    const auto* weapon = rpg::getItem(player.equippedWeaponId);
    if (weapon && weapon->type == rpg::ItemType::Weapon) {
      weaponBonus = std::max(0, static_cast<int>(weapon->damage) / 2);
    }
  }
  const uint16_t playerDamage = static_cast<uint16_t>(
      std::max(1, 4 + abilityMod(player.stats.strength) + weaponBonus) + static_cast<int>(esp_random() % 5));
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
        std::vector<std::string> rewardItems;
        if (currentFightIsUnique && currentEnemy) {
          defeatedUniqueMask |= uniqueBit(currentEnemy->id);
          xpGain = static_cast<uint32_t>(xpGain + xpGain / 2);
          goldGain = static_cast<uint16_t>(goldGain + goldGain / 2);

          bool chestGranted = false;
          if (addItemToInventory(rpg::ITEM_HEALING_POTION, 1)) {
            chestGranted = true;
            rewardItems.push_back(rewardItemLabel(rpg::getItem(rpg::ITEM_HEALING_POTION), 1));
          }
          if ((esp_random() % 2) == 0) {
            if (addItemToInventory(rpg::ITEM_SCROLL_FIREBALL, 1)) {
              chestGranted = true;
              rewardItems.push_back(rewardItemLabel(rpg::getItem(rpg::ITEM_SCROLL_FIREBALL), 1));
            }
          } else {
            if (addItemToInventory(rpg::ITEM_PLATE_ARMOR, 1)) {
              chestGranted = true;
              rewardItems.push_back(rewardItemLabel(rpg::getItem(rpg::ITEM_PLATE_ARMOR), 1));
            }
          }
          if (!chestGranted) {
            goldGain = static_cast<uint16_t>(std::min<int>(65535, goldGain + 80));
          }
        }
        player.goldCoins = static_cast<uint16_t>(std::min<int>(65535, player.goldCoins + goldGain));
        player.addExperience(xpGain);
        const std::string rewardItemsText = formatRewardItems(rewardItems);
        setRewardSummary(xpGain, goldGain, rewardItemsText);
        increaseDepthProgression();
        currentEncounterId = 6;
        currentEncounter = rpg::getEncounter(currentEncounterId);
        currentScreen = Screen::CombatReward;
        if (currentFightIsUnique) {
          std::string message = "The depth guardian is slain. You crack open its hoard chest";
          if (!rewardItemsText.empty()) {
            message += " and claim " + rewardItemsText + ".";
          } else {
            message += " and gather the extra coin left inside.";
          }
          setNarrative(message);
        } else {
          setNarrative("The foe falls. You seize coin and hard-earned experience.");
        }
        currentFightIsUnique = false;
      } else if (player.hp == 0) {
        player.hp = 1;
        gameActive = false;
        currentEncounter = nullptr;
        currentEncounterId = 1;
        currentScreen = Screen::MainMenu;
        setNarrative("Battered and bleeding, you fall back through the pines toward Solace.");
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
        setNarrative("You survive the onslaught by a thread and withdraw to safety.");
      }
      return;
    }
    case 2: {  // Item
      openInventoryFrom(Screen::Combat);
      updateRequired = true;
      return;
    }
    case 3: {  // Flee
      currentEncounterId = 1;
      currentEncounter = rpg::getEncounter(currentEncounterId);
      currentScreen = Screen::Encounter;
      selectedIndex = 0;
      currentFightIsUnique = false;
      setNarrative("You disengage and flee through the ancient pines toward Solace.");
      return;
    }
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
        setNarrative("The Dragon Army gathers in the east. A new hero answers the call.");
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
        setNarrative("You take up sword and pack, stepping back onto the ancient road.");
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
            openInventoryFrom(Screen::MainMenu);
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
        const int encounterActions = currentEncounter->choiceCount + 1;
        buttonNavigator.onNextRelease([this] {
          selectedIndex = ButtonNavigator::nextIndex(selectedIndex, currentEncounter->choiceCount + 1);
          updateRequired = true;
        });

        buttonNavigator.onPreviousRelease([this] {
          selectedIndex = ButtonNavigator::previousIndex(selectedIndex, currentEncounter->choiceCount + 1);
          updateRequired = true;
        });

        if (mappedInput.wasReleased(Button::Confirm)) {
          if (selectedIndex == encounterActions - 1) {
            openInventoryFrom(Screen::Encounter);
          } else {
            handleEncounterChoice(selectedIndex);
          }
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
      constexpr int merchantItems = 5;

      if (player.inventoryCount <= 0) {
        merchantSelectedInventoryIndex = 0;
      } else if (merchantSelectedInventoryIndex >= player.inventoryCount) {
        merchantSelectedInventoryIndex = player.inventoryCount - 1;
      } else if (merchantSelectedInventoryIndex < 0) {
        merchantSelectedInventoryIndex = 0;
      }

      buttonNavigator.onNextRelease([this] {
        selectedIndex = ButtonNavigator::nextIndex(selectedIndex, merchantItems);
        updateRequired = true;
      });

      buttonNavigator.onPreviousRelease([this] {
        selectedIndex = ButtonNavigator::previousIndex(selectedIndex, merchantItems);
        updateRequired = true;
      });

      if (player.inventoryCount > 0) {
        if (mappedInput.wasReleased(Button::Right)) {
          merchantSelectedInventoryIndex =
              ButtonNavigator::nextIndex(merchantSelectedInventoryIndex, player.inventoryCount);
          updateRequired = true;
        }
        if (mappedInput.wasReleased(Button::Left)) {
          merchantSelectedInventoryIndex =
              ButtonNavigator::previousIndex(merchantSelectedInventoryIndex, player.inventoryCount);
          updateRequired = true;
        }
      }

      if (mappedInput.wasReleased(Button::Confirm)) {
        handleMerchantAction(selectedIndex);
        updateRequired = true;
      }

      if (mappedInput.wasReleased(Button::Back)) {
        currentEncounterId = 1;
        currentEncounter = rpg::getEncounter(currentEncounterId);
        currentScreen = Screen::Encounter;
        selectedIndex = 0;
        setNarrative("You leave the trader and return to Solace.");
        updateRequired = true;
      }
      break;
    }

    case Screen::Combat: {
      constexpr int combatActions = 4;

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
      handleInventoryInput();
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

  const int x = metrics.contentSidePadding;
  const int sectionW = pageWidth - x * 2;
  const int sectionGap = 8;
  const int topY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID);
  // Dynamic top height for description text (2-3 lines)
  const int topH = SECTION_BORDER_THICKNESS + SECTION_INNER_PAD_Y + smallLineH * 2 + 6 + SECTION_INNER_PAD_Y + 4;
  const int middleY = topY + topH + sectionGap;
  const int middleH = pageHeight - middleY - metrics.buttonHintsHeight - 6;

  static const char* classes[] = {"Warrior", "Rogue", "Mage", "Cleric", "Ranger", "Barbarian"};

  drawSectionBox(renderer, x, topY, sectionW, topH);
  drawSectionBox(renderer, x, middleY, sectionW, middleH);
  drawBoxTagTopRight(renderer, x, middleY, sectionW, "CLASSES");

  const int textY = topY + SECTION_BORDER_THICKNESS + SECTION_INNER_PAD_Y;
  const int descW = sectionW - 10;
  const char* desc =
      "Pick your path. Fighter, Rogue, Wizard, Cleric, Ranger, or Barbarian. Each faces the darkness differently.";
  auto lines = wrapText(renderer, SMALL_FONT_ID, desc, descW, 2);
  for (size_t i = 0; i < lines.size(); ++i) {
    renderer.drawText(SMALL_FONT_ID, x + SECTION_INNER_PAD_X,
              textY + static_cast<int>(i) * (smallLineH + 2), lines[i].c_str());
  }

  const int listTop = taggedSectionContentTop(renderer, middleY);
  const int rowH = 28;
  for (int i = 0; i < 6; ++i) {
    const int rowY = listTop + i * rowH;
    if (rowY + rowH > middleY + middleH - 4) break;
    drawSelectableRow(renderer, x, sectionW, rowY, rowH, classes[i], i == selectedIndex);
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

  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), "Companions of the Lance");

  const int x = metrics.contentSidePadding;
  const int sectionW = pageWidth - x * 2;
  const int sectionGap = 8;

  const int topY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int uiLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID);
  // Dynamic top height: title line + 4 stat lines + borders + padding
  const int topH = SECTION_BORDER_THICKNESS + SECTION_INNER_PAD_Y + (uiLineH + 3) + 
                   (smallLineH + 2) * 4 + SECTION_INNER_PAD_Y + 4;

  const int bottomH = 128;  // Doubled from 64
  const int bottomY = pageHeight - metrics.buttonHintsHeight - bottomH - 8;

  const int middleY = topY + topH + sectionGap;
  const int middleH = std::max(31, bottomY - middleY - sectionGap);  // Half from 62

  drawSectionBox(renderer, x, topY, sectionW, topH);
  drawSectionBox(renderer, x, middleY, sectionW, middleH);
  drawSectionBox(renderer, x, bottomY, sectionW, bottomH);

  const int listRowH = uiLineH + 8;
  int y = topY + SECTION_BORDER_THICKNESS + SECTION_INNER_PAD_Y;

  char buf[96];
  snprintf(buf, sizeof(buf), "%s the %s  (Lvl %u)", player.name.c_str(), rpg::classToString(player.charClass),
           player.level);
  renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X, y, buf, true, EpdFontFamily::BOLD);
  y += uiLineH + 3;

  snprintf(buf, sizeof(buf), "HP %u/%u   Gold %u   XP %lu", player.hp, player.maxHp, player.goldCoins,
           static_cast<unsigned long>(player.experience));
  renderer.drawText(SMALL_FONT_ID, x + SECTION_INNER_PAD_X, y, buf);
  y += smallLineH + 2;

  snprintf(buf, sizeof(buf), "Depth: %u   Victories: %u", dungeonDepth, encountersWon);
  renderer.drawText(SMALL_FONT_ID, x + SECTION_INNER_PAD_X, y, buf);
  y += smallLineH + 2;

  snprintf(buf, sizeof(buf), "Uniques defeated: %d/4", defeatedUniqueCount(defeatedUniqueMask));
  renderer.drawText(SMALL_FONT_ID, x + SECTION_INNER_PAD_X, y, buf);
  y += smallLineH + 2;

  renderer.drawText(SMALL_FONT_ID, x + SECTION_INNER_PAD_X, y, saveExists ? "Save file: Found" : "Save file: None");

  const char* startLabel = (gameActive && currentEncounter != nullptr) ? "Continue Adventure" : "Start Adventure";
  const char* items[] = {startLabel, "Save Adventure", "Load Adventure", "Character Sheet", "New Hero", "Exit"};

  drawBoxTagTopRight(renderer, x, middleY, sectionW, "ACTIONS");

  const int listTop = taggedSectionContentTop(renderer, middleY);
  for (int i = 0; i < 6; ++i) {
    const int rowY = listTop + i * listRowH;
    if (rowY + listRowH > middleY + middleH - 4) break;
    drawSelectableRow(renderer, x, sectionW, rowY, listRowH, items[i], i == selectedIndex);
  }

  renderer.drawText(SMALL_FONT_ID, x + SECTION_INNER_PAD_X, bottomY + SECTION_BORDER_THICKNESS + 2, "Log", true,
                    EpdFontFamily::BOLD);
  drawBoxTagTopRight(renderer, x, bottomY, sectionW, "HISTORY");

  if (!narrativeText.empty()) {
    auto noteLines = wrapText(renderer, UI_10_FONT_ID, narrativeText.c_str(), sectionW - 10, 2);
    for (size_t i = 0; i < noteLines.size(); ++i) {
        renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X,
                bottomY + SECTION_BORDER_THICKNESS + renderer.getLineHeight(SMALL_FONT_ID) + 6 +
                  static_cast<int>(i) * (uiLineH + 1),
                noteLines[i].c_str(), true,
                        EpdFontFamily::BOLD);
    }
  } else {
    renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X,
              bottomY + SECTION_BORDER_THICKNESS + renderer.getLineHeight(SMALL_FONT_ID) + 6,
              "No recent events.", true, EpdFontFamily::BOLD);
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
  const int sectionGap = 8;

  const int topY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int topH = 86;

  const int bottomH = 66;
  const int bottomY = pageHeight - metrics.buttonHintsHeight - bottomH - 8;

  const int middleY = topY + topH + sectionGap;
  const int middleH = std::max(52, bottomY - middleY - sectionGap);

  drawSectionBox(renderer, x, topY, sectionW, topH);
  drawSectionBox(renderer, x, middleY, sectionW, middleH);
  drawSectionBox(renderer, x, bottomY, sectionW, bottomH);

  const int uiLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int choiceHeight = uiLineH + 8;
  int y = topY + SECTION_BORDER_THICKNESS + SECTION_INNER_PAD_Y;
  const int textW = sectionW - 10;

  char depthInfo[32];
  snprintf(depthInfo, sizeof(depthInfo), "Dungeon depth: %u", dungeonDepth);
  renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X, y, depthInfo, true, EpdFontFamily::BOLD);
  y += uiLineH + 3;

  auto descLines = wrapText(renderer, UI_10_FONT_ID, currentEncounter->description, textW, 2);
  for (const auto& line : descLines) {
    renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X, y, line.c_str());
    y += uiLineH + 1;
  }

  drawBoxTagTopRight(renderer, x, middleY, sectionW, "ACTIONS");
  y = taggedSectionContentTop(renderer, middleY);

  if (currentEncounter->type == rpg::EncounterType::RestSite) {
    renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X, y, "Select to Rest and Return", true,
              EpdFontFamily::BOLD);
  } else if (currentEncounter->choiceCount > 0) {
    for (int i = 0; i < currentEncounter->choiceCount + 1; ++i) {
      const int rowY = y + i * choiceHeight;
      if (rowY + choiceHeight > middleY + middleH - 4) break;
      if (i < currentEncounter->choiceCount) {
        drawSelectableRow(renderer, x, sectionW, rowY, choiceHeight, currentEncounter->choices[i].text,
                          i == selectedIndex);
      } else {
        drawSelectableRow(renderer, x, sectionW, rowY, choiceHeight, "Open Pack", i == selectedIndex);
      }
    }
  } else {
    renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X, y, "Select to Continue", true, EpdFontFamily::BOLD);
  }

  renderer.drawText(SMALL_FONT_ID, x + SECTION_INNER_PAD_X, bottomY + SECTION_BORDER_THICKNESS + 2, "Log", true,
                    EpdFontFamily::BOLD);
  drawBoxTagTopRight(renderer, x, bottomY, sectionW, "HISTORY");
  if (!narrativeText.empty()) {
    auto noteLines = wrapText(renderer, UI_10_FONT_ID, narrativeText.c_str(), sectionW - 10, 2);
    for (size_t i = 0; i < noteLines.size(); ++i) {
        renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X,
                bottomY + SECTION_BORDER_THICKNESS + renderer.getLineHeight(SMALL_FONT_ID) + 6 +
                  static_cast<int>(i) * (uiLineH + 1),
                noteLines[i].c_str(), true,
                        EpdFontFamily::BOLD);
    }
  } else {
    renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X,
              bottomY + SECTION_BORDER_THICKNESS + renderer.getLineHeight(SMALL_FONT_ID) + 6,
              "No recent events.", true, EpdFontFamily::BOLD);
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
  const int sectionGap = 8;

  const int topY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int topH = 74;

  const int bottomH = 64;
  const int bottomY = pageHeight - metrics.buttonHintsHeight - bottomH - 8;

  const int middleY = topY + topH + sectionGap;
  const int middleH = std::max(52, bottomY - middleY - sectionGap);

  drawSectionBox(renderer, x, topY, sectionW, topH);
  drawSectionBox(renderer, x, middleY, sectionW, middleH);
  drawSectionBox(renderer, x, bottomY, sectionW, bottomH);

  const int uiLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int listRowH = uiLineH + 8;
  int y = topY + SECTION_BORDER_THICKNESS + SECTION_INNER_PAD_Y;
  const int textW = sectionW - 10;

  const char* intro = "The merchant eyes your coin pouch and tips his battered hat.";
  auto introLines = wrapText(renderer, UI_10_FONT_ID, intro, textW, 2);
  for (const auto& line : introLines) {
    renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X, y, line.c_str());
    y += uiLineH + 2;
  }

  char stats[64];
  snprintf(stats, sizeof(stats), "Gold: %u   Pack: %u/%d", player.goldCoins, player.inventoryCount,
           rpg::Character::MAX_INVENTORY);
  renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X, y, stats, true, EpdFontFamily::BOLD);

  y += uiLineH + 2;
  if (player.inventoryCount == 0) {
    renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X, y, "Selected item: (none)");
  } else {
    const int itemIndex = std::max(0, std::min<int>(merchantSelectedInventoryIndex, player.inventoryCount - 1));
    const auto slot = player.inventory[itemIndex];
    const auto* item = rpg::getItem(slot.itemId);
    const char* itemName = item ? item->name : "Unknown item";
    const uint16_t sellValue = item ? static_cast<uint16_t>(std::max<int>(1, item->value / 2)) : 5;
    snprintf(stats, sizeof(stats), "Selected: %s x%u (Sell %ug)", itemName, slot.quantity, sellValue);
    std::string row = renderer.truncatedText(UI_10_FONT_ID, stats, sectionW - 10);
    renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X, y, row.c_str());
  }

  y = taggedSectionContentTop(renderer, middleY);

  std::string sellOption = "Sell Selected Item";
  if (player.inventoryCount > 0) {
    const int itemIndex = std::max(0, std::min<int>(merchantSelectedInventoryIndex, player.inventoryCount - 1));
    const auto* item = rpg::getItem(player.inventory[itemIndex].itemId);
    const uint16_t sellValue = item ? static_cast<uint16_t>(std::max<int>(1, item->value / 2)) : 5;
    sellOption = "Sell Selected (" + std::to_string(sellValue) + "g)";
  }

  const char* options[] = {"Buy Healing Potion (25g)", "Buy Fireball Scroll (60g)", sellOption.c_str(),
                           "Equip Selected Item", "Leave Market"};
  const int optionCount = 5;

  for (int i = 0; i < optionCount; ++i) {
    const int rowY = y + i * listRowH;
    if (rowY + listRowH > middleY + middleH - 4) break;
    drawSelectableRow(renderer, x, sectionW, rowY, listRowH, options[i], i == selectedIndex);
  }

  renderer.drawText(SMALL_FONT_ID, x + SECTION_INNER_PAD_X, bottomY + SECTION_BORDER_THICKNESS + 2, "Log", true,
                    EpdFontFamily::BOLD);
  drawBoxTagTopRight(renderer, x, bottomY, sectionW, "HISTORY");
  if (!narrativeText.empty()) {
    auto noteLines = wrapText(renderer, UI_10_FONT_ID, narrativeText.c_str(), sectionW - 10, 2);
    for (size_t i = 0; i < noteLines.size(); ++i) {
        renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X,
                bottomY + SECTION_BORDER_THICKNESS + renderer.getLineHeight(SMALL_FONT_ID) + 6 +
                  static_cast<int>(i) * (uiLineH + 1),
                noteLines[i].c_str(), true,
                        EpdFontFamily::BOLD);
    }
  } else {
    renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X,
              bottomY + SECTION_BORDER_THICKNESS + renderer.getLineHeight(SMALL_FONT_ID) + 6,
              "No recent events.", true, EpdFontFamily::BOLD);
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
  const int sectionGap = 8;

  const int topY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int topH = 86;

  const int bottomH = 66;
  const int bottomY = pageHeight - metrics.buttonHintsHeight - bottomH - 8;

  const int middleY = topY + topH + sectionGap;
  const int middleH = std::max(52, bottomY - middleY - sectionGap);

  drawSectionBox(renderer, x, topY, sectionW, topH);
  drawSectionBox(renderer, x, middleY, sectionW, middleH);
  drawSectionBox(renderer, x, bottomY, sectionW, bottomH);

  const int uiLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int actionHeight = uiLineH + 8;
  int y = topY + SECTION_BORDER_THICKNESS + SECTION_INNER_PAD_Y;

  char buf[80];
  snprintf(buf, sizeof(buf), "%s   HP: %u/%u", currentEnemy->name.c_str(), enemyHp, currentEnemy->maxHp);
  renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X, y, buf, true, EpdFontFamily::BOLD);
  y += uiLineH + 3;

  snprintf(buf, sizeof(buf), "Your HP: %u/%u   AC: %u   Depth: %u", player.hp, player.maxHp, player.armorClass,
           dungeonDepth);
  renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X, y, buf);
  y += uiLineH + 1;

  char statusLine[64];
  snprintf(statusLine, sizeof(statusLine), "You[P%d S%d]  Enemy[P%d S%d]", playerPoisonTurns, playerStunTurns,
           enemyPoisonTurns, enemyStunTurns);
  renderer.drawText(SMALL_FONT_ID, x + SECTION_INNER_PAD_X, y, statusLine);

  drawBoxTagTopRight(renderer, x, middleY, sectionW, "ACTIONS");
  y = taggedSectionContentTop(renderer, middleY);

  const char* actions[] = {"Attack", "Defend", "Use Item", "Flee"};
  for (int i = 0; i < 4; ++i) {
    const int rowY = y + i * actionHeight;
    if (rowY + actionHeight > middleY + middleH - 4) break;
    drawSelectableRow(renderer, x, sectionW, rowY, actionHeight, actions[i], i == selectedIndex);
  }

  renderer.drawText(SMALL_FONT_ID, x + SECTION_INNER_PAD_X, bottomY + SECTION_BORDER_THICKNESS + 2, "Combat log", true,
                    EpdFontFamily::BOLD);
  drawBoxTagTopRight(renderer, x, bottomY, sectionW, "HISTORY");
  auto lines = wrapText(renderer, UI_10_FONT_ID, narrativeText.c_str(), sectionW - 10, 2);
  for (size_t i = 0; i < lines.size(); ++i) {
    renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X,
              bottomY + SECTION_BORDER_THICKNESS + renderer.getLineHeight(SMALL_FONT_ID) + 6 +
                static_cast<int>(i) * (smallLineH + 2),
              lines[i].c_str(), true,
                      EpdFontFamily::BOLD);
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

  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight),
                 (inventoryReturnScreen == Screen::MainMenu) ? "Character Sheet" : "Inventory");

  const auto& p = player;
  const int x = metrics.contentSidePadding;
  const int sectionW = pageWidth - x * 2;
  const int sectionGap = 8;

  const int topY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int topH = 84;

  const int bottomH = 64;
  const int bottomY = pageHeight - metrics.buttonHintsHeight - bottomH - 8;

  const int middleY = topY + topH + sectionGap;
  const int middleH = std::max(58, bottomY - middleY - sectionGap);

  drawSectionBox(renderer, x, topY, sectionW, topH);
  drawSectionBox(renderer, x, middleY, sectionW, middleH);
  drawSectionBox(renderer, x, bottomY, sectionW, bottomH);

  const int uiLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int inventoryRowH = uiLineH + 8;
  int y = topY + SECTION_BORDER_THICKNESS + SECTION_INNER_PAD_Y;

  char buf[96];
  snprintf(buf, sizeof(buf), "%s the %s  (Lvl %u)", p.name.c_str(), rpg::classToString(p.charClass), p.level);
  renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X, y, buf, true, EpdFontFamily::BOLD);
  y += uiLineH + 3;

  snprintf(buf, sizeof(buf), "HP: %u/%u   AC: %u   Gold: %u", p.hp, p.maxHp, p.armorClass, p.goldCoins);
  renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X, y, buf);
  y += uiLineH + 2;

  const auto* equippedWeapon = rpg::getItem(p.equippedWeaponId);
  const auto* equippedArmor = rpg::getItem(p.equippedArmorId);
  snprintf(buf, sizeof(buf), "W: %s", equippedWeapon ? equippedWeapon->name : "None");
  renderer.drawText(SMALL_FONT_ID, x + SECTION_INNER_PAD_X, y, buf);
  y += smallLineH + 1;
  snprintf(buf, sizeof(buf), "A: %s", equippedArmor ? equippedArmor->name : "None");
  renderer.drawText(SMALL_FONT_ID, x + SECTION_INNER_PAD_X, y, buf);

  y = middleY + SECTION_BORDER_THICKNESS + 4;

  snprintf(buf, sizeof(buf), "STR %u  DEX %u  CON %u", p.stats.strength, p.stats.dexterity, p.stats.constitution);
  renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X, y, buf);
  y += uiLineH + 1;

  snprintf(buf, sizeof(buf), "INT %u  WIS %u  CHA %u", p.stats.intelligence, p.stats.wisdom, p.stats.charisma);
  renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X, y, buf);
  y += uiLineH + 2;

  renderer.drawText(SMALL_FONT_ID, x + SECTION_INNER_PAD_X, y, "Inventory", true, EpdFontFamily::BOLD);
  y += smallLineH + 1;

  if (p.inventoryCount == 0) {
    renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X, y, "(empty)");
  } else {
    const int availableRows = std::max(1, (middleY + middleH - y - 8) / inventoryRowH);
    const int showCount = std::min<int>(p.inventoryCount, availableRows);
    for (int i = 0; i < showCount; ++i) {
      const auto* item = rpg::getItem(p.inventory[i].itemId);
      const char* itemName = item ? item->name : "Unknown item";
      const bool isSelected = (i == selectedIndex);
      const bool isEquipped = (p.inventory[i].itemId == p.equippedWeaponId || p.inventory[i].itemId == p.equippedArmorId);
      std::string label = std::string(itemName) + " x" + std::to_string(p.inventory[i].quantity);
      if (isEquipped) {
        label += " [E]";
      }
      drawSelectableRow(renderer, x, sectionW, y, inventoryRowH, label.c_str(), isSelected);
      y += inventoryRowH;
    }
    if (p.inventoryCount > showCount) {
      snprintf(buf, sizeof(buf), "+%u more item(s)", static_cast<unsigned int>(p.inventoryCount - showCount));
      renderer.drawText(SMALL_FONT_ID, x + SECTION_INNER_PAD_X, y, buf);
    }
  }

  drawBoxTagTopRight(renderer, x, bottomY, sectionW, "DETAILS");
  std::string detailText = "Back: return";
  if (p.inventoryCount > 0 && selectedIndex >= 0 && selectedIndex < p.inventoryCount) {
    const auto* item = rpg::getItem(p.inventory[selectedIndex].itemId);
    if (item) {
      std::string actionText;
      if (item->type == rpg::ItemType::Weapon || item->type == rpg::ItemType::Armor) {
        actionText = "Select: equip/unequip";
      } else {
        actionText = "Select: use item";
      }
      detailText = std::string(item->description) + "  " + actionText;
    }
  }

  auto detailLines = wrapText(renderer, UI_10_FONT_ID, detailText.c_str(), sectionW - 10, 2);
  for (size_t i = 0; i < detailLines.size(); ++i) {
    renderer.drawText(UI_10_FONT_ID, x + SECTION_INNER_PAD_X,
              bottomY + SECTION_BORDER_THICKNESS + 4 + static_cast<int>(i) * (uiLineH + 2),
              detailLines[i].c_str(), true,
                      EpdFontFamily::BOLD);
  }

  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Select", "Up", "Down");
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
    snprintf(buf, sizeof(buf), "Reward: %lu XP", static_cast<unsigned long>(lastRewardExperience));
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
    y += 18;
    snprintf(buf, sizeof(buf), "Loot: +%u gold", lastRewardGold);
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
    y += 18;
    if (!lastRewardItemsText.empty()) {
      auto rewardLines = wrapText(renderer, SMALL_FONT_ID,
                                  (std::string("Items: ") + lastRewardItemsText).c_str(),
                                  pageWidth - 24, 2);
      for (const auto& line : rewardLines) {
        renderer.drawCenteredText(SMALL_FONT_ID, y, line.c_str());
        y += 13;
      }
    }
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
