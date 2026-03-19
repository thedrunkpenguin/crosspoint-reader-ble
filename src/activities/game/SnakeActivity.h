#pragma once

#include <deque>
#include <functional>

#include "../Activity.h"

class SnakeActivity final : public Activity {
  struct Point {
    int x;
    int y;
  };

  static constexpr int COLS = 20;
  static constexpr int ROWS = 24;
  static constexpr unsigned long STEP_MS = 170;

  std::deque<Point> snake;
  Point food{0, 0};
  int dirX = 1;
  int dirY = 0;
  int nextDirX = 1;
  int nextDirY = 0;
  bool running = true;
  bool gameOver = false;
  int score = 0;
  unsigned long lastStep = 0;

  const std::function<void()> onBack;

  void resetGame();
  void spawnFood();
  void step();
  bool isOccupied(int x, int y) const;

 public:
  explicit SnakeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack)
      : Activity("Snake", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;
  bool skipLoopDelay() override { return true; }
};
