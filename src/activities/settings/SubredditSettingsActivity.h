#pragma once

#include <functional>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class SubredditSettingsActivity final : public Activity {
 public:
  explicit SubredditSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const std::function<void()>& onBack)
      : Activity("SubredditSettings", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  size_t selectedIndex = 0;
  const std::function<void()> onBack;

  void handleSelection();
};
