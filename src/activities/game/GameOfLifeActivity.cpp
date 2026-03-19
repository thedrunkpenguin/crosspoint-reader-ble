#include "GameOfLifeActivity.h"

#include <I18n.h>

#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void GameOfLifeActivity::stepLife() {
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      int neighbors = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0) {
            continue;
          }
          const int nx = x + dx;
          const int ny = y + dy;
          if (nx >= 0 && ny >= 0 && nx < W && ny < H && grid[ny][nx]) {
            neighbors++;
          }
        }
      }

      if (grid[y][x]) {
        next[y][x] = (neighbors == 2 || neighbors == 3) ? 1 : 0;
      } else {
        next[y][x] = (neighbors == 3) ? 1 : 0;
      }
    }
  }

  grid = next;
  generation++;
}

void GameOfLifeActivity::onEnter() {
  Activity::onEnter();
  for (auto& row : grid) {
    row.fill(0);
  }
  generation = 0;
  running = false;
  lastStep = millis();
  requestUpdate();
}

void GameOfLifeActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mappedInput.isPressed(MappedInputManager::Button::Power)) {
      running = !running;
    } else {
      grid[cursorY][cursorX] = !grid[cursorY][cursorX];
    }
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    cursorX = std::max(0, cursorX - 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    cursorX = std::min(W - 1, cursorX + 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    cursorY = std::max(0, cursorY - 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    cursorY = std::min(H - 1, cursorY + 1);
    requestUpdate();
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
    stepLife();
    requestUpdate();
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
    for (auto& row : grid) {
      row.fill(0);
    }
    generation = 0;
    requestUpdate();
  }

  if (running && millis() - lastStep > 180) {
    lastStep = millis();
    stepLife();
    requestUpdate();
  }
}

void GameOfLifeActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_GAME_OF_LIFE));

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int areaH = bottom - top;
  const int cell = std::max(4, std::min(pageWidth / W, areaH / H));
  const int gridW = cell * W;
  const int gridH = cell * H;
  const int x0 = (pageWidth - gridW) / 2;
  const int y0 = top + (areaH - gridH) / 2;

  renderer.drawRect(x0 - 1, y0 - 1, gridW + 2, gridH + 2, true);

  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      if (grid[y][x]) {
        renderer.fillRect(x0 + x * cell, y0 + y * cell, cell, cell, true);
      }
    }
  }

  renderer.drawRect(x0 + cursorX * cell, y0 + cursorY * cell, cell, cell, 1, true);

  char genText[24];
  snprintf(genText, sizeof(genText), tr(STR_LIFE_GEN), generation);
  renderer.drawText(UI_10_FONT_ID, x0, y0 - renderer.getLineHeight(UI_10_FONT_ID) - 4, genText);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_LIFE_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
