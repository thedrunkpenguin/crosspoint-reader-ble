#pragma once

#include <functional>

#include "activities/ActivityWithSubactivity.h"
#include "util/ButtonNavigator.h"

class SubredditSettingsActivity final : public ActivityWithSubactivity {
 public:
  explicit SubredditSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const std::function<void()>& onBack)
      : ActivityWithSubactivity("SubredditSettings", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  size_t selectedIndex = 0;
  const std::function<void()> onBack;

  void handleSelection();
};
