#pragma once

#include <array>
#include <functional>
#include <vector>

#include "../Activity.h"

class ChessActivity final : public Activity {
  struct Move {
    int fromR;
    int fromC;
    int toR;
    int toC;
  };

  std::array<std::array<char, 8>, 8> board{};
  int cursorR = 6;
  int cursorC = 4;
  bool selected = false;
  int selectedR = -1;
  int selectedC = -1;
  bool whiteTurn = true;
  bool gameOver = false;
  char winner = ' ';

  const std::function<void()> onBack;

  void resetBoard();
  bool isWhite(char p) const;
  bool isBlack(char p) const;
  bool inBounds(int r, int c) const;
  bool pathClear(int r1, int c1, int r2, int c2) const;
  bool isValidMove(int fr, int fc, int tr, int tc) const;
  std::vector<Move> collectMoves(bool whiteSide) const;
  void aiMove();
  void updateGameOver();

 public:
  explicit ChessActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack)
      : Activity("Chess", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;
};
