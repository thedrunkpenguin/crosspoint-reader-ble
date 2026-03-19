#include "GameMenuActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "game/GameState.h"
#include "game/GameTypes.h"

// --- Lifecycle ---

void GameMenuActivity::taskTrampoline(void* param) {
  static_cast<GameMenuActivity*>(param)->displayTaskLoop();
}

void GameMenuActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  currentScreen = Screen::Menu;
  selectedIndex = 0;
  updateRequired = true;

  xTaskCreate(&GameMenuActivity::taskTrampoline, "GameMenuTask", 4096, this, 1, &displayTaskHandle);
}

void GameMenuActivity::onExit() {
  Activity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void GameMenuActivity::displayTaskLoop() {
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

// --- Input ---

void GameMenuActivity::loop() {
  using Button = MappedInputManager::Button;

  switch (currentScreen) {
    case Screen::Menu: {
      constexpr int menuSize = 5;  // Resume, Inventory, Character, Save & Quit, Abandon

      buttonNavigator.onNextRelease([this] {
        selectedIndex = ButtonNavigator::nextIndex(selectedIndex, menuSize);
        updateRequired = true;
      });

      buttonNavigator.onPreviousRelease([this] {
        selectedIndex = ButtonNavigator::previousIndex(selectedIndex, menuSize);
        updateRequired = true;
      });

      if (mappedInput.wasReleased(Button::Confirm)) {
        switch (selectedIndex) {
          case 0:  // Resume
            onResume();
            return;
          case 1:  // Inventory
            currentScreen = Screen::Inventory;
            selectedIndex = 0;
            updateRequired = true;
            break;
          case 2:  // Character
            currentScreen = Screen::Character;
            selectedIndex = 0;
            updateRequired = true;
            break;
          case 3:  // Save & Quit
            onSaveQuit();
            return;
          case 4:  // Abandon Run
            onAbandon();
            return;
        }
      }

      if (mappedInput.wasReleased(Button::Back)) {
        onResume();
        return;
      }
      break;
    }

    case Screen::Inventory: {
      int invCount = static_cast<int>(GAME_STATE.inventoryCount);
      if (invCount == 0) {
        // Empty inventory — just go back
        if (mappedInput.wasReleased(Button::Back) || mappedInput.wasReleased(Button::Confirm)) {
          currentScreen = Screen::Menu;
          selectedIndex = 1;
          updateRequired = true;
        }
        break;
      }

      buttonNavigator.onNextRelease([this, invCount] {
        selectedIndex = ButtonNavigator::nextIndex(selectedIndex, invCount);
        updateRequired = true;
      });

      buttonNavigator.onPreviousRelease([this, invCount] {
        selectedIndex = ButtonNavigator::previousIndex(selectedIndex, invCount);
        updateRequired = true;
      });

      if (mappedInput.wasReleased(Button::Confirm)) {
        useInventoryItem(selectedIndex);
        updateRequired = true;
      }

      if (mappedInput.wasReleased(Button::Back)) {
        currentScreen = Screen::Menu;
        selectedIndex = 1;
        updateRequired = true;
      }
      break;
    }

    case Screen::Character: {
      if (mappedInput.wasReleased(Button::Back) || mappedInput.wasReleased(Button::Confirm)) {
        currentScreen = Screen::Menu;
        selectedIndex = 2;
        updateRequired = true;
      }
      break;
    }
  }
}

// --- Use Inventory Item ---

void GameMenuActivity::useInventoryItem(int index) {
  if (index < 0 || index >= GAME_STATE.inventoryCount) return;

  auto& item = GAME_STATE.inventory[index];
  auto& p = GAME_STATE.player;
  auto type = static_cast<game::ItemType>(item.type);

  bool consumed = false;
  char msgBuf[64];

  switch (type) {
    case game::ItemType::Potion:
      if (item.subtype == 0) {  // Healing
        uint16_t heal = p.maxHp / 3;
        if (heal < 5) heal = 5;
        p.hp = std::min(static_cast<uint16_t>(p.hp + heal), p.maxHp);
        snprintf(msgBuf, sizeof(msgBuf), "You feel better! (HP +%u)", heal);
        consumed = true;
      } else if (item.subtype == 1) {  // Mana
        uint16_t mana = p.maxMp / 3;
        if (mana < 3) mana = 3;
        p.mp = std::min(static_cast<uint16_t>(p.mp + mana), p.maxMp);
        snprintf(msgBuf, sizeof(msgBuf), "Magical energy flows! (MP +%u)", mana);
        consumed = true;
      } else if (item.subtype == 2) {  // Strength
        p.strength += 2;
        snprintf(msgBuf, sizeof(msgBuf), "You feel stronger! (STR +2)");
        consumed = true;
      }
      break;

    case game::ItemType::Food:
      if (item.subtype == 0) {  // Rations
        uint16_t heal = 5;
        p.hp = std::min(static_cast<uint16_t>(p.hp + heal), p.maxHp);
        snprintf(msgBuf, sizeof(msgBuf), "That hit the spot. (HP +%u)", heal);
        consumed = true;
      } else if (item.subtype == 1) {  // Lembas
        uint16_t heal = p.maxHp / 2;
        p.hp = std::min(static_cast<uint16_t>(p.hp + heal), p.maxHp);
        uint16_t mana = p.maxMp / 2;
        p.mp = std::min(static_cast<uint16_t>(p.mp + mana), p.maxMp);
        snprintf(msgBuf, sizeof(msgBuf), "The elven bread restores you!");
        consumed = true;
      }
      break;

    case game::ItemType::Scroll:
      if (item.subtype == 0) {  // Identify
        // Mark all inventory as identified
        for (uint8_t i = 0; i < GAME_STATE.inventoryCount; i++) {
          GAME_STATE.inventory[i].flags |= static_cast<uint8_t>(game::ItemFlag::Identified);
        }
        snprintf(msgBuf, sizeof(msgBuf), "Your items glow briefly.");
        consumed = true;
      } else if (item.subtype == 1) {  // Teleport
        // Handled back in game activity — for now just message
        snprintf(msgBuf, sizeof(msgBuf), "You blink! (use in dungeon)");
        consumed = true;
      } else if (item.subtype == 2) {  // Mapping
        snprintf(msgBuf, sizeof(msgBuf), "The level is revealed! (use in dungeon)");
        consumed = true;
      }
      break;

    default:
      // Weapons, armor, etc. — toggle equip
      if (item.flags & static_cast<uint8_t>(game::ItemFlag::Equipped)) {
        item.flags &= ~static_cast<uint8_t>(game::ItemFlag::Equipped);
        snprintf(msgBuf, sizeof(msgBuf), "Unequipped.");
      } else {
        // Unequip any existing item of same type
        for (uint8_t i = 0; i < GAME_STATE.inventoryCount; i++) {
          if (static_cast<int>(i) != index && GAME_STATE.inventory[i].type == item.type &&
              (GAME_STATE.inventory[i].flags & static_cast<uint8_t>(game::ItemFlag::Equipped))) {
            GAME_STATE.inventory[i].flags &= ~static_cast<uint8_t>(game::ItemFlag::Equipped);
          }
        }
        item.flags |= static_cast<uint8_t>(game::ItemFlag::Equipped);
        snprintf(msgBuf, sizeof(msgBuf), "Equipped!");
      }
      break;
  }

  GAME_STATE.addMessage(msgBuf);

  if (consumed) {
    // Remove item by shifting
    for (int i = index; i < GAME_STATE.inventoryCount - 1; i++) {
      GAME_STATE.inventory[i] = GAME_STATE.inventory[i + 1];
    }
    GAME_STATE.inventoryCount--;
    if (selectedIndex >= GAME_STATE.inventoryCount && selectedIndex > 0) {
      selectedIndex--;
    }
  }
}

// --- Rendering ---

void GameMenuActivity::render() {
  switch (currentScreen) {
    case Screen::Menu:
      renderMenu();
      break;
    case Screen::Inventory:
      renderInventory();
      break;
    case Screen::Character:
      renderCharacter();
      break;
  }
}

void GameMenuActivity::renderMenu() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), "Deep Mines");

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  static const char* items[] = {"Resume Game", "Inventory", "Character", "Save & Quit", "Abandon Run"};

  GUI.drawButtonMenu(
      renderer, Rect(0, contentTop, pageWidth, contentHeight), 5, selectedIndex,
      [](int index) { return std::string(items[index]); }, nullptr);

  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Select", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void GameMenuActivity::renderInventory() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), "Inventory");

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  int invCount = static_cast<int>(GAME_STATE.inventoryCount);

  if (invCount == 0) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, "Your pack is empty.");
  } else {
    GUI.drawList(
        renderer, Rect(0, contentTop, pageWidth, contentHeight), invCount, selectedIndex,
        [](int index) {
          const auto& item = GAME_STATE.inventory[index];
          // Find the item name from definitions
          for (int d = 0; d < game::ITEM_DEF_COUNT; d++) {
            if (game::ITEM_DEFS[d].type == item.type && game::ITEM_DEFS[d].subtype == item.subtype) {
              std::string name = game::ITEM_DEFS[d].name;
              if (item.enchantment > 0) {
                name += " +" + std::to_string(item.enchantment);
              }
              if (item.flags & static_cast<uint8_t>(game::ItemFlag::Equipped)) {
                name += " [E]";
              }
              return name;
            }
          }
          return std::string("Unknown Item");
        },
        [](int index) {
          const auto& item = GAME_STATE.inventory[index];
          auto type = static_cast<game::ItemType>(item.type);
          switch (type) {
            case game::ItemType::Weapon:
              return std::string("Weapon");
            case game::ItemType::Armor:
              return std::string("Armor");
            case game::ItemType::Shield:
              return std::string("Shield");
            case game::ItemType::Potion:
              return std::string("Potion");
            case game::ItemType::Scroll:
              return std::string("Scroll");
            case game::ItemType::Food:
              return std::string("Food");
            case game::ItemType::Ring:
              return std::string("Ring");
            case game::ItemType::Amulet:
              return std::string("Amulet");
            default:
              return std::string("");
          }
        },
        nullptr, nullptr);
  }

  const char* confirmLabel = invCount > 0 ? "Use/Equip" : "";
  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", confirmLabel, "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void GameMenuActivity::renderCharacter() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect(0, metrics.topPadding, pageWidth, metrics.headerHeight), "Character");

  const auto& p = GAME_STATE.player;
  const int x = metrics.contentSidePadding;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 10;
  constexpr int lineH = 28;

  char buf[64];

  snprintf(buf, sizeof(buf), "Level: %u", p.charLevel);
  renderer.drawText(UI_10_FONT_ID, x, y, buf, true, EpdFontFamily::BOLD);
  y += lineH;

  snprintf(buf, sizeof(buf), "HP: %u / %u", p.hp, p.maxHp);
  renderer.drawText(UI_10_FONT_ID, x, y, buf);
  y += lineH;

  snprintf(buf, sizeof(buf), "MP: %u / %u", p.mp, p.maxMp);
  renderer.drawText(UI_10_FONT_ID, x, y, buf);
  y += lineH;

  y += 10;  // spacer

  snprintf(buf, sizeof(buf), "Strength:     %u", p.strength);
  renderer.drawText(UI_10_FONT_ID, x, y, buf);
  y += lineH;

  snprintf(buf, sizeof(buf), "Dexterity:    %u", p.dexterity);
  renderer.drawText(UI_10_FONT_ID, x, y, buf);
  y += lineH;

  snprintf(buf, sizeof(buf), "Constitution: %u", p.constitution);
  renderer.drawText(UI_10_FONT_ID, x, y, buf);
  y += lineH;

  snprintf(buf, sizeof(buf), "Intelligence: %u", p.intelligence);
  renderer.drawText(UI_10_FONT_ID, x, y, buf);
  y += lineH;

  y += 10;  // spacer

  snprintf(buf, sizeof(buf), "Experience: %lu", static_cast<unsigned long>(p.experience));
  renderer.drawText(UI_10_FONT_ID, x, y, buf);
  y += lineH;

  uint32_t nextLvlXp = game::xpForLevel(p.charLevel + 1);
  uint32_t xpRemaining = (nextLvlXp > p.experience) ? (nextLvlXp - p.experience) : 0;
  snprintf(buf, sizeof(buf), "Next level: %lu XP", static_cast<unsigned long>(xpRemaining));
  renderer.drawText(UI_10_FONT_ID, x, y, buf);
  y += lineH;

  snprintf(buf, sizeof(buf), "Gold: %u", p.gold);
  renderer.drawText(UI_10_FONT_ID, x, y, buf);
  y += lineH;

  snprintf(buf, sizeof(buf), "Dungeon depth: %u", p.dungeonDepth);
  renderer.drawText(UI_10_FONT_ID, x, y, buf);
  y += lineH;

  snprintf(buf, sizeof(buf), "Turns: %u", p.turnCount);
  renderer.drawText(UI_10_FONT_ID, x, y, buf);

  // Show equipped gear at bottom
  y += lineH + 10;
  renderer.drawText(UI_10_FONT_ID, x, y, "Equipment:", true, EpdFontFamily::BOLD);
  y += lineH;

  bool hasEquipped = false;
  for (uint8_t i = 0; i < GAME_STATE.inventoryCount; i++) {
    const auto& item = GAME_STATE.inventory[i];
    if (item.flags & static_cast<uint8_t>(game::ItemFlag::Equipped)) {
      for (int d = 0; d < game::ITEM_DEF_COUNT; d++) {
        if (game::ITEM_DEFS[d].type == item.type && game::ITEM_DEFS[d].subtype == item.subtype) {
          std::string name = game::ITEM_DEFS[d].name;
          if (item.enchantment > 0) {
            name += " +" + std::to_string(item.enchantment);
          }
          renderer.drawText(UI_10_FONT_ID, x + 10, y, name.c_str());
          y += lineH;
          hasEquipped = true;
          break;
        }
      }
    }
  }
  if (!hasEquipped) {
    renderer.drawText(UI_10_FONT_ID, x + 10, y, "(none)");
  }

  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
