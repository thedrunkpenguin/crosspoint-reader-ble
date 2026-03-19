#pragma once

#include <functional>
#include <vector>

#include "../ActivityWithSubactivity.h"

class GamePickerActivity final : public ActivityWithSubactivity {
  int selectorIndex = 0;
  uint32_t ignoreBackUntilMs = 0;
  const std::function<void()> onBack;
  const std::function<void()> onStartDeepMines;

  void openSelectedGame();
  int gameCount() const;

 public:
  explicit GamePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack,
                              const std::function<void()>& onStartDeepMines)
      : ActivityWithSubactivity("GamePicker", renderer, mappedInput), onBack(onBack), onStartDeepMines(onStartDeepMines) {}

  void onEnter() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;
};
