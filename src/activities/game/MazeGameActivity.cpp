#include "MazeGameActivity.h"

#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void MazeGameActivity::carve(int x, int y) {
  cells[y][x] = 1;

  int dirs[4][2] = {{0, -2}, {2, 0}, {0, 2}, {-2, 0}};
  for (int i = 0; i < 4; ++i) {
    const int j = random(i, 4);
    std::swap(dirs[i][0], dirs[j][0]);
    std::swap(dirs[i][1], dirs[j][1]);
  }

  for (int i = 0; i < 4; ++i) {
    const int nx = x + dirs[i][0];
    const int ny = y + dirs[i][1];
    if (nx <= 0 || ny <= 0 || nx >= W - 1 || ny >= H - 1) {
      continue;
    }
    if (cells[ny][nx] == 0) {
      cells[y + dirs[i][1] / 2][x + dirs[i][0] / 2] = 1;
      carve(nx, ny);
    }
  }
}

void MazeGameActivity::generateMaze() {
  for (auto& row : cells) {
    row.fill(0);
  }
  carve(1, 1);
  playerX = 1;
  playerY = 1;
  exitX = W - 2;
  exitY = H - 2;
  cells[exitY][exitX] = 1;
}

bool MazeGameActivity::canMoveTo(int x, int y) const {
  if (x < 0 || y < 0 || x >= W || y >= H) {
    return false;
  }
  return cells[y][x] == 1;
}

void MazeGameActivity::onEnter() {
  Activity::onEnter();
  level = 1;
  generateMaze();
  requestUpdate();
}

void MazeGameActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  int nx = playerX;
  int ny = playerY;

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    nx--;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    nx++;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    ny--;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    ny++;
  }

  if ((nx != playerX || ny != playerY) && canMoveTo(nx, ny)) {
    playerX = nx;
    playerY = ny;
    if (playerX == exitX && playerY == exitY) {
      level++;
      generateMaze();
    }
    requestUpdate();
  }
}

void MazeGameActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MAZE));

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 10;
  const int bottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int areaH = bottom - top;
  const int cell = std::max(6, std::min(pageWidth / W, areaH / H));
  const int gridW = cell * W;
  const int gridH = cell * H;
  const int x0 = (pageWidth - gridW) / 2;
  const int y0 = top + (areaH - gridH) / 2;

  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      if (cells[y][x] == 0) {
        renderer.fillRect(x0 + x * cell, y0 + y * cell, cell, cell, true);
      }
    }
  }

  renderer.fillRect(x0 + exitX * cell + 1, y0 + exitY * cell + 1, cell - 2, cell - 2, true);
  renderer.fillRect(x0 + playerX * cell + 2, y0 + playerY * cell + 2, cell - 4, cell - 4, false);
  renderer.drawRect(x0 + playerX * cell + 2, y0 + playerY * cell + 2, cell - 4, cell - 4, true);

  char levelText[24];
  snprintf(levelText, sizeof(levelText), tr(STR_MAZE_LEVEL), level);
  renderer.drawCenteredText(UI_10_FONT_ID, y0 - renderer.getLineHeight(UI_10_FONT_ID) - 4, levelText);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
