#pragma once

#include <array>
#include <functional>

#include "../Activity.h"

class SudokuActivity final : public Activity {
  std::array<std::array<uint8_t, 9>, 9> puzzle{};
  std::array<std::array<uint8_t, 9>, 9> solution{};
  std::array<std::array<bool, 9>, 9> fixed{};

  int cursorR = 0;
  int cursorC = 0;
  bool completed = false;

  const std::function<void()> onBack;

  void makePuzzle();
  bool isValid(const std::array<std::array<uint8_t, 9>, 9>& b, int r, int c, int v) const;
  bool solve(std::array<std::array<uint8_t, 9>, 9>& b, int idx) const;

 public:
  explicit SudokuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack)
      : Activity("Sudoku", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;
};
