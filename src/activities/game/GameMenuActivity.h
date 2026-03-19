#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class GameMenuActivity final : public Activity {
  enum class Screen { Menu, Inventory, Character };

  Screen currentScreen = Screen::Menu;
  int selectedIndex = 0;

  // Rendering
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  ButtonNavigator buttonNavigator;
  bool updateRequired = false;

  // Callbacks
  const std::function<void()> onResume;
  const std::function<void()> onSaveQuit;
  const std::function<void()> onAbandon;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render();
  void renderMenu();
  void renderInventory();
  void renderCharacter();
  void useInventoryItem(int index);

 public:
  explicit GameMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                            const std::function<void()>& onResume, const std::function<void()>& onSaveQuit,
                            const std::function<void()>& onAbandon)
      : Activity("GameMenu", renderer, mappedInput),
        onResume(onResume),
        onSaveQuit(onSaveQuit),
        onAbandon(onAbandon) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
};
