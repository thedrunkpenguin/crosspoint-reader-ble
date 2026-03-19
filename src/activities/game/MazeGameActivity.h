#pragma once

#include <array>
#include <functional>

#include "../Activity.h"

class MazeGameActivity final : public Activity {
  static constexpr int W = 17;
  static constexpr int H = 17;
  std::array<std::array<uint8_t, W>, H> cells{};

  int playerX = 1;
  int playerY = 1;
  int exitX = W - 2;
  int exitY = H - 2;
  int level = 1;

  const std::function<void()> onBack;

  void generateMaze();
  void carve(int x, int y);
  bool canMoveTo(int x, int y) const;

 public:
  explicit MazeGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack)
      : Activity("Maze", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;
};
