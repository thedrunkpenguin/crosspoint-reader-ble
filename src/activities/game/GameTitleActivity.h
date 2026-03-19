#pragma once

#include <functional>

#include "../Activity.h"

class GameTitleActivity final : public Activity {
  const std::function<void()> onStartGame;
  const std::function<void()> onGoBack;
  bool rendered = false;

 public:
  explicit GameTitleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             const std::function<void()>& onStartGame, const std::function<void()>& onGoBack)
      : Activity("GameTitle", renderer, mappedInput), onStartGame(onStartGame), onGoBack(onGoBack) {}
  void onEnter() override;
  void loop() override;
};
