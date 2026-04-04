#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>

#include "../Activity.h"
#include "game/DungeonGenerator.h"
#include "game/GameRenderer.h"
#include "game/GameState.h"
#include "game/GameTypes.h"

class GameActivity final : public Activity {
  // Level data (~5.5KB total)
  game::Tile tiles[game::MAP_SIZE];
  uint8_t fogOfWar[game::FOG_SIZE];
  game::Monster monsters[game::MAX_MONSTERS];
  game::Item levelItems[game::MAX_ITEMS_PER_LEVEL];
  uint8_t monsterCount = 0;
  uint8_t itemCount = 0;

  // Visibility cache (computed per turn)
  bool visible[game::MAP_SIZE];

  // Rendering
  GameRenderer gameRenderer;
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  bool menuOpen = false;

  // Navigation
  const std::function<void()> onGoHome;

  // Internal methods
  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render();

  void loadOrGenerateLevel();
  void saveCurrentLevel();
  void computeVisibility();
  void handleMove(int dx, int dy);
  void handleAction();
  void processMonsterTurns();
  void monsterAttackPlayer(game::Monster& monster);
  void checkLevelUp();
  void handlePlayerDeath();
  void handleVictory();
  void openGameMenu();

 public:
  explicit GameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onGoHome)
      : Activity("Game", renderer, mappedInput), onGoHome(onGoHome) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return true; }
};
