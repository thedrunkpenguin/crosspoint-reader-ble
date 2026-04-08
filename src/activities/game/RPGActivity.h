#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>

#include "../Activity.h"
#include "RPGCharacter.h"
#include "RPGEncounter.h"
#include "util/ButtonNavigator.h"

class RPGActivity final : public Activity {
  enum class Screen {
    CharacterSelection,
    MainMenu,
    Encounter,
    Merchant,
    Combat,
    Inventory,
    CombatReward
  };

  Screen currentScreen = Screen::CharacterSelection;
  Screen inventoryReturnScreen = Screen::MainMenu;
  int selectedIndex = 0;
  int previousSelectedIndex = 0;
  int merchantSelectedInventoryIndex = 0;

  // Rendering
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  ButtonNavigator buttonNavigator;
  bool updateRequired = false;

  // Game state
  rpg::Character player{rpg::CharacterClass::Warrior, "Hero"};
  bool gameActive = false;
  const rpg::Encounter* currentEncounter = nullptr;
  const rpg::Enemy* currentEnemy = nullptr;
  uint16_t enemyHp = 0;
  uint16_t dungeonDepth = 1;
  uint16_t encountersWon = 0;
  uint8_t playerPoisonTurns = 0;
  uint8_t playerStunTurns = 0;
  uint8_t enemyPoisonTurns = 0;
  uint8_t enemyStunTurns = 0;
  uint32_t defeatedUniqueMask = 0;
  bool currentFightIsUnique = false;
  uint8_t currentEncounterId = 1;
  bool saveExists = false;
  std::string narrativeText;
  uint32_t lastRewardExperience = 0;
  uint16_t lastRewardGold = 0;
  std::string lastRewardItemsText;

  // Callbacks
  const std::function<void()> onBack;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render();
  void renderCharacterSelection();
  void renderMainMenu();
  void renderEncounter();
  void renderMerchant();
  void renderCombat();
  void renderInventory();
  void renderCombatReward();

  void createCharacter(rpg::CharacterClass charClass, const std::string& name);
  void handleEncounterChoice(int choiceIndex);
  void handleMerchantAction(int index);
  void handleCombatAction(int action);
  bool rollAttack(int attackBonus, int targetAC);
  void enterCombat(const rpg::Enemy* enemy);
  const rpg::Enemy* selectEnemyForDepth() const;
  const rpg::Enemy* selectUniqueBossForDepth() const;
  void applyStartOfRoundEffects();
  void increaseDepthProgression();
  bool maybeTriggerVaultEncounter();
  bool addItemToInventory(uint8_t itemId, uint8_t quantity = 1);
  void recomputeArmorClass();
  int findInventoryIndexByItemId(uint8_t itemId) const;
  bool consumeInventoryItemAt(int inventoryIndex, uint8_t quantity = 1);
  bool useInventoryItemAt(int inventoryIndex);
  bool equipInventoryItemAt(int inventoryIndex);
  void openInventoryFrom(Screen returnScreen);
  void handleInventoryInput();
  bool saveGame();
  bool loadGame();
  void refreshSaveState();
  void setNarrative(const std::string& text);
  void setRewardSummary(uint32_t experience, uint16_t gold, const std::string& itemsText = "");

 public:
  explicit RPGActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack)
      : Activity("RPG", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
};
