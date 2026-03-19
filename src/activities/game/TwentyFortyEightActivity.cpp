#include "TwentyFortyEightActivity.h"

#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void TwentyFortyEightActivity::addRandomTile() {
  int empty[16][2];
  int count = 0;
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      if (board[r][c] == 0) {
        empty[count][0] = r;
        empty[count][1] = c;
        ++count;
      }
    }
  }
  if (count == 0) {
    return;
  }
  const int pick = random(0, count);
  board[empty[pick][0]][empty[pick][1]] = (random(0, 10) < 9) ? 2 : 4;
}

bool TwentyFortyEightActivity::slideLeft() {
  bool moved = false;
  for (int r = 0; r < 4; ++r) {
    int target = 0;
    int lastValue = 0;
    for (int c = 0; c < 4; ++c) {
      int value = board[r][c];
      if (value == 0) {
        continue;
      }
      board[r][c] = 0;
      if (lastValue == value) {
        board[r][target - 1] = value * 2;
        score += value * 2;
        if (value * 2 >= 2048) {
          won = true;
        }
        lastValue = 0;
        moved = true;
      } else {
        lastValue = value;
        if (board[r][target] != value) {
          moved = true;
        }
        board[r][target++] = value;
      }
    }
  }
  return moved;
}

void TwentyFortyEightActivity::rotateClockwise() {
  std::array<std::array<int, 4>, 4> copy = board;
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      board[c][3 - r] = copy[r][c];
    }
  }
}

bool TwentyFortyEightActivity::applyMove(int turnsToLeft) {
  for (int i = 0; i < turnsToLeft; ++i) {
    rotateClockwise();
  }
  bool moved = slideLeft();
  for (int i = 0; i < (4 - turnsToLeft) % 4; ++i) {
    rotateClockwise();
  }
  return moved;
}

bool TwentyFortyEightActivity::hasMoves() const {
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      if (board[r][c] == 0) {
        return true;
      }
      if (r + 1 < 4 && board[r][c] == board[r + 1][c]) {
        return true;
      }
      if (c + 1 < 4 && board[r][c] == board[r][c + 1]) {
        return true;
      }
    }
  }
  return false;
}

void TwentyFortyEightActivity::onEnter() {
  Activity::onEnter();
  for (auto& row : board) {
    row.fill(0);
  }
  score = 0;
  won = false;
  gameOver = false;
  addRandomTile();
  addRandomTile();
  requestUpdate();
}

void TwentyFortyEightActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  if (gameOver || won) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      onEnter();
    }
    return;
  }

  bool moved = false;
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    moved = applyMove(0);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    moved = applyMove(3);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    moved = applyMove(2);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    moved = applyMove(1);
  }

  if (moved) {
    addRandomTile();
    gameOver = !hasMoves();
    requestUpdate();
  }
}

void TwentyFortyEightActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_2048));

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int area = std::min(pageWidth - 40, bottom - top - 20);
  const int cell = area / 4;
  const int grid = cell * 4;
  const int x0 = (pageWidth - grid) / 2;
  const int y0 = top + 20;

  renderer.drawRect(x0 - 2, y0 - 2, grid + 4, grid + 4, true);

  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      const int x = x0 + c * cell;
      const int y = y0 + r * cell;
      renderer.drawRect(x, y, cell, cell, true);
      if (board[r][c] > 0) {
        char s[8];
        snprintf(s, sizeof(s), "%d", board[r][c]);
        const int textW = renderer.getTextWidth(UI_12_FONT_ID, s, EpdFontFamily::BOLD);
        renderer.drawText(UI_12_FONT_ID, x + (cell - textW) / 2, y + (cell - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                          s, true, EpdFontFamily::BOLD);
      }
    }
  }

  char scoreText[32];
  snprintf(scoreText, sizeof(scoreText), tr(STR_2048_SCORE), score);
  renderer.drawCenteredText(UI_10_FONT_ID, y0 + grid + 10, scoreText);

  if (won) {
    renderer.drawCenteredText(UI_12_FONT_ID, y0 + grid + 40, tr(STR_2048_WIN), true, EpdFontFamily::BOLD);
  } else if (gameOver) {
    renderer.drawCenteredText(UI_12_FONT_ID, y0 + grid + 40, tr(STR_GAME_OVER), true, EpdFontFamily::BOLD);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), (won || gameOver) ? tr(STR_NEW_GAME) : "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
