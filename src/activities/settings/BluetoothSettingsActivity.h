#pragma once

#include <BluetoothHIDManager.h>
#include <GfxRenderer.h>
#include <string>

#include "activities/Activity.h"
#include "MappedInputManager.h"

class BluetoothSettingsActivity : public Activity {
 private:
  enum class ViewMode {
    MAIN_MENU,
    DEVICE_LIST,
    LEARN_KEYS
  };

  enum class LearnStep {
    WAIT_PREV,
    WAIT_NEXT,
    DONE
  };

  ViewMode viewMode = ViewMode::MAIN_MENU;
  int selectedIndex = 0;
  BluetoothHIDManager* btMgr = nullptr;
  std::string lastError = "";
  unsigned long lastScanTime = 0;
  LearnStep learnStep = LearnStep::WAIT_PREV;
  uint8_t pendingLearnKey = 0;
  uint8_t pendingLearnIndex = 0xFF;
  uint8_t learnedPrevKey = 0;
  uint8_t learnedNextKey = 0;
  uint8_t learnedReportIndex = 2;
  bool exitOnSuccessfulConnect = false;

 public:
  explicit BluetoothSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const std::function<void()>& onComplete,
                                     const bool exitOnSuccessfulConnect = false)
      : Activity("BluetoothSettings", renderer, mappedInput),
        exitOnSuccessfulConnect(exitOnSuccessfulConnect),
        onComplete(onComplete) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;

 private:
  void handleMainMenuInput();
  void handleDeviceListInput();
  void handleLearnInput();
  void renderMainMenu();
  void renderDeviceList();
  void renderLearnKeys();
  std::string getSignalStrengthIndicator(const int32_t rssi) const;
  
  const std::function<void()> onComplete;
};
