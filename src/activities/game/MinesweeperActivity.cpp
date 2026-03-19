#include "MinesweeperActivity.h"

#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void MinesweeperActivity::resetBoard() {
  for (auto& row : grid) {
    row.fill(0);
  }
  for (auto& row : state) {
    row.fill(0);
  }
  cursorR = 0;
  cursorC = 0;
  lost = false;
  won = false;
  placedMines = false;
}

int MinesweeperActivity::countAdj(int r, int c) const {
  int count = 0;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if (!dx && !dy) continue;
      const int nr = r + dy;
      const int nc = c + dx;
      if (nr >= 0 && nc >= 0 && nr < ROWS && nc < COLS && grid[nr][nc] == 9) {
        count++;
      }
    }
  }
  return count;
}

void MinesweeperActivity::placeMines(int safeR, int safeC) {
  int placed = 0;
  while (placed < MINES) {
    const int r = random(0, ROWS);
    const int c = random(0, COLS);
    if (grid[r][c] == 9) continue;
    if (std::abs(r - safeR) <= 1 && std::abs(c - safeC) <= 1) continue;
    grid[r][c] = 9;
    placed++;
  }

  for (int r = 0; r < ROWS; ++r) {
    for (int c = 0; c < COLS; ++c) {
      if (grid[r][c] != 9) {
        grid[r][c] = static_cast<uint8_t>(countAdj(r, c));
      }
    }
  }

  placedMines = true;
}

void MinesweeperActivity::floodReveal(int r, int c) {
  if (r < 0 || c < 0 || r >= ROWS || c >= COLS || state[r][c] == 1 || state[r][c] == 2) {
    return;
  }
  state[r][c] = 1;
  if (grid[r][c] != 0) {
    return;
  }
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if (dx || dy) {
        floodReveal(r + dy, c + dx);
      }
    }
  }
}

void MinesweeperActivity::checkWin() {
  int hiddenSafe = 0;
  for (int r = 0; r < ROWS; ++r) {
    for (int c = 0; c < COLS; ++c) {
      if (grid[r][c] != 9 && state[r][c] != 1) {
        hiddenSafe++;
      }
    }
  }
  if (hiddenSafe == 0) {
    won = true;
  }
}

void MinesweeperActivity::onEnter() {
  Activity::onEnter();
  resetBoard();
  requestUpdate();
}

void MinesweeperActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  if (lost || won) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      onEnter();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    cursorC = std::max(0, cursorC - 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    cursorC = std::min(COLS - 1, cursorC + 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    cursorR = std::max(0, cursorR - 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    cursorR = std::min(ROWS - 1, cursorR + 1);
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    if (state[cursorR][cursorC] == 0) {
      state[cursorR][cursorC] = 2;
    } else if (state[cursorR][cursorC] == 2) {
      state[cursorR][cursorC] = 0;
    }
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    if (!placedMines) {
      placeMines(cursorR, cursorC);
    }
    if (state[cursorR][cursorC] == 2) {
      return;
    }

    if (grid[cursorR][cursorC] == 9) {
      lost = true;
      for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) {
          if (grid[r][c] == 9) {
            state[r][c] = 1;
          }
        }
      }
    } else {
      floodReveal(cursorR, cursorC);
      checkWin();
    }
    requestUpdate();
  }
}

void MinesweeperActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MINESWEEPER));

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 8;
  const int bottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int areaH = bottom - top;
  const int cell = std::max(10, std::min(pageWidth / COLS, areaH / ROWS));
  const int boardW = cell * COLS;
  const int boardH = cell * ROWS;
  const int x0 = (pageWidth - boardW) / 2;
  const int y0 = top + (areaH - boardH) / 2;

  for (int r = 0; r < ROWS; ++r) {
    for (int c = 0; c < COLS; ++c) {
      const int x = x0 + c * cell;
      const int y = y0 + r * cell;
      if (state[r][c] == 1) {
        renderer.fillRect(x, y, cell, cell, false);
        renderer.drawRect(x, y, cell, cell, true);
        if (grid[r][c] == 9) {
          renderer.fillRect(x + 3, y + 3, cell - 6, cell - 6, true);
        } else if (grid[r][c] > 0) {
          char s[2] = {static_cast<char>('0' + grid[r][c]), 0};
          renderer.drawText(UI_10_FONT_ID, x + cell / 3, y + cell / 5, s, true, EpdFontFamily::BOLD);
        }
      } else if (state[r][c] == 2) {
        renderer.fillRect(x, y, cell, cell, true);
        renderer.drawRect(x, y, cell, cell, true);
        renderer.drawText(UI_10_FONT_ID, x + cell / 3, y + cell / 5, "F", false, EpdFontFamily::BOLD);
      } else {
        renderer.fillRect(x, y, cell, cell, false);
        renderer.drawRect(x, y, cell, cell, true);
      }
    }
  }

  renderer.drawRect(x0 + cursorC * cell, y0 + cursorR * cell, cell, cell, 2, true);

  if (lost) {
    renderer.drawCenteredText(UI_12_FONT_ID, y0 + boardH + 8, tr(STR_GAME_OVER), true, EpdFontFamily::BOLD);
  } else if (won) {
    renderer.drawCenteredText(UI_12_FONT_ID, y0 + boardH + 8, tr(STR_YOU_WIN), true, EpdFontFamily::BOLD);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_MINES_REVEAL), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
