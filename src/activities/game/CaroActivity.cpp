#include "CaroActivity.h"

#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

bool CaroActivity::hasFive(int r, int c, uint8_t v) const {
  const int dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
  for (auto& d : dirs) {
    int count = 1;
    for (int s = 1; s < 5; ++s) {
      int nr = r + d[1] * s;
      int nc = c + d[0] * s;
      if (nr < 0 || nc < 0 || nr >= SIZE || nc >= SIZE || grid[nr][nc] != v) {
        break;
      }
      count++;
    }
    for (int s = 1; s < 5; ++s) {
      int nr = r - d[1] * s;
      int nc = c - d[0] * s;
      if (nr < 0 || nc < 0 || nr >= SIZE || nc >= SIZE || grid[nr][nc] != v) {
        break;
      }
      count++;
    }
    if (count >= 5) {
      return true;
    }
  }
  return false;
}

int CaroActivity::scorePos(int r, int c, uint8_t me, uint8_t opp) const {
  if (grid[r][c] != 0) {
    return -1;
  }

  int score = 1;
  const int dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
  for (auto& d : dirs) {
    int mine = 0;
    int theirs = 0;
    for (int s = -4; s <= 4; ++s) {
      if (s == 0) continue;
      int nr = r + d[1] * s;
      int nc = c + d[0] * s;
      if (nr < 0 || nc < 0 || nr >= SIZE || nc >= SIZE) {
        continue;
      }
      if (grid[nr][nc] == me) mine++;
      if (grid[nr][nc] == opp) theirs++;
    }
    score += mine * mine * 10 + theirs * theirs * 8;
  }
  return score;
}

void CaroActivity::aiMove() {
  int bestR = -1;
  int bestC = -1;
  int best = -1;

  for (int r = 0; r < SIZE; ++r) {
    for (int c = 0; c < SIZE; ++c) {
      const int s = scorePos(r, c, 2, 1);
      if (s > best) {
        best = s;
        bestR = r;
        bestC = c;
      }
    }
  }

  if (bestR >= 0) {
    grid[bestR][bestC] = 2;
    if (hasFive(bestR, bestC, 2)) {
      winner = 2;
      return;
    }
  }

  bool anyEmpty = false;
  for (int r = 0; r < SIZE; ++r) {
    for (int c = 0; c < SIZE; ++c) {
      if (grid[r][c] == 0) {
        anyEmpty = true;
      }
    }
  }
  if (!anyEmpty) {
    draw = true;
  }
}

void CaroActivity::onEnter() {
  Activity::onEnter();
  for (auto& row : grid) {
    row.fill(0);
  }
  cursorR = SIZE / 2;
  cursorC = SIZE / 2;
  winner = 0;
  draw = false;
  requestUpdate();
}

void CaroActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  if (winner || draw) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      onEnter();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    cursorC = std::max(0, cursorC - 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    cursorC = std::min(SIZE - 1, cursorC + 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    cursorR = std::max(0, cursorR - 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    cursorR = std::min(SIZE - 1, cursorR + 1);
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && grid[cursorR][cursorC] == 0) {
    grid[cursorR][cursorC] = 1;
    if (hasFive(cursorR, cursorC, 1)) {
      winner = 1;
      requestUpdate();
      return;
    }
    aiMove();
    requestUpdate();
  }
}

void CaroActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CARO));

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int area = std::min(pageWidth - 20, bottom - top - 20);
  const int cell = std::max(8, area / SIZE);
  const int boardPx = cell * SIZE;
  const int x0 = (pageWidth - boardPx) / 2;
  const int y0 = top + 8;

  for (int i = 0; i <= SIZE; ++i) {
    const int x = x0 + i * cell;
    const int y = y0 + i * cell;
    renderer.drawLine(x, y0, x, y0 + boardPx, true);
    renderer.drawLine(x0, y, x0 + boardPx, y, true);
  }

  for (int r = 0; r < SIZE; ++r) {
    for (int c = 0; c < SIZE; ++c) {
      const int cx = x0 + c * cell + cell / 2;
      const int cy = y0 + r * cell + cell / 2;
      if (grid[r][c] == 1) {
        renderer.drawLine(cx - cell / 3, cy - cell / 3, cx + cell / 3, cy + cell / 3, true);
        renderer.drawLine(cx + cell / 3, cy - cell / 3, cx - cell / 3, cy + cell / 3, true);
      } else if (grid[r][c] == 2) {
        renderer.drawRect(cx - cell / 3, cy - cell / 3, (cell / 3) * 2, (cell / 3) * 2, true);
      }
    }
  }

  renderer.drawRect(x0 + cursorC * cell, y0 + cursorR * cell, cell, cell, 2, true);

  const char* status = tr(STR_CARO_YOUR_TURN);
  if (winner == 1) {
    status = tr(STR_CARO_WIN_X);
  } else if (winner == 2) {
    status = tr(STR_CARO_WIN_O);
  } else if (draw) {
    status = tr(STR_CARO_DRAW);
  }
  renderer.drawCenteredText(UI_10_FONT_ID, y0 + boardPx + 8, status);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CARO_PLACE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
