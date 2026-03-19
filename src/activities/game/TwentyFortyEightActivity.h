#pragma once

#include <array>
#include <functional>

#include "../Activity.h"

class TwentyFortyEightActivity final : public Activity {
  std::array<std::array<int, 4>, 4> board{};
  int score = 0;
  bool won = false;
  bool gameOver = false;

  const std::function<void()> onBack;

  bool slideLeft();
  void rotateClockwise();
  bool applyMove(int turnsToLeft);
  void addRandomTile();
  bool hasMoves() const;

 public:
  explicit TwentyFortyEightActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                    const std::function<void()>& onBack)
      : Activity("2048", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;
};
