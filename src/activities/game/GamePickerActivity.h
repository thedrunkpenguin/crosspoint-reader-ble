#pragma once

#include <functional>
#include <vector>

#include "../Activity.h"

class GamePickerActivity final : public Activity {
  int selectorIndex = 0;
  uint32_t ignoreBackUntilMs = 0;
  const std::function<void()> onBack;
  const std::function<void()> onStartDeepMines;

  void openSelectedGame();
  int gameCount() const;

 public:
  explicit GamePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack,
                              const std::function<void()>& onStartDeepMines)
      : Activity("GamePicker", renderer, mappedInput), onBack(onBack), onStartDeepMines(onStartDeepMines) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
