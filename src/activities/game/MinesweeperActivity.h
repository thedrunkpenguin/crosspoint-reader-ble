#pragma once

#include <array>
#include <functional>

#include "../Activity.h"

class MinesweeperActivity final : public Activity {
  static constexpr int ROWS = 14;
  static constexpr int COLS = 10;
  static constexpr int MINES = 18;

  std::array<std::array<uint8_t, COLS>, ROWS> grid{};
  std::array<std::array<uint8_t, COLS>, ROWS> state{};  // 0 hidden, 1 revealed, 2 flagged
  int cursorR = 0;
  int cursorC = 0;
  bool lost = false;
  bool won = false;
  bool placedMines = false;

  const std::function<void()> onBack;

  void resetBoard();
  void placeMines(int safeR, int safeC);
  int countAdj(int r, int c) const;
  void floodReveal(int r, int c);
  void checkWin();

 public:
  explicit MinesweeperActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                               const std::function<void()>& onBack)
      : Activity("Minesweeper", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;
};
