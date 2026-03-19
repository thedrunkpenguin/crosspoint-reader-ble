#include "SnakeActivity.h"

#include <I18n.h>

#include <deque>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void SnakeActivity::resetGame() {
  snake.clear();
  snake.push_back({COLS / 2, ROWS / 2});
  snake.push_back({COLS / 2 - 1, ROWS / 2});
  snake.push_back({COLS / 2 - 2, ROWS / 2});
  dirX = 1;
  dirY = 0;
  nextDirX = 1;
  nextDirY = 0;
  score = 0;
  running = true;
  gameOver = false;
  spawnFood();
}

bool SnakeActivity::isOccupied(int x, int y) const {
  for (const auto& p : snake) {
    if (p.x == x && p.y == y) {
      return true;
    }
  }
  return false;
}

void SnakeActivity::spawnFood() {
  for (int tries = 0; tries < 500; ++tries) {
    const int x = random(0, COLS);
    const int y = random(0, ROWS);
    if (!isOccupied(x, y)) {
      food = {x, y};
      return;
    }
  }
  food = {0, 0};
}

void SnakeActivity::step() {
  if (!running || gameOver) {
    return;
  }

  dirX = nextDirX;
  dirY = nextDirY;

  Point head = snake.front();
  head.x += dirX;
  head.y += dirY;

  if (head.x < 0 || head.y < 0 || head.x >= COLS || head.y >= ROWS || isOccupied(head.x, head.y)) {
    gameOver = true;
    running = false;
    requestUpdate();
    return;
  }

  snake.push_front(head);

  if (head.x == food.x && head.y == food.y) {
    score += 10;
    spawnFood();
  } else {
    snake.pop_back();
  }

  requestUpdate();
}

void SnakeActivity::onEnter() {
  Activity::onEnter();
  resetGame();
  lastStep = millis();
  requestUpdate();
}

void SnakeActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (gameOver) {
      resetGame();
    } else {
      running = !running;
    }
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left) && dirX != 1) {
    nextDirX = -1;
    nextDirY = 0;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right) && dirX != -1) {
    nextDirX = 1;
    nextDirY = 0;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Up) && dirY != 1) {
    nextDirX = 0;
    nextDirY = -1;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down) && dirY != -1) {
    nextDirX = 0;
    nextDirY = 1;
  }

  if (millis() - lastStep >= STEP_MS) {
    lastStep = millis();
    step();
  }
}

void SnakeActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SNAKE));

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int areaH = bottom - top;
  const int cell = std::max(8, std::min(pageWidth / COLS, areaH / ROWS));
  const int gridW = cell * COLS;
  const int gridH = cell * ROWS;
  const int x0 = (pageWidth - gridW) / 2;
  const int y0 = top + (areaH - gridH) / 2;

  renderer.drawRect(x0 - 1, y0 - 1, gridW + 2, gridH + 2, true);

  renderer.fillRect(x0 + food.x * cell + 1, y0 + food.y * cell + 1, cell - 2, cell - 2, true);

  bool head = true;
  for (const auto& p : snake) {
    if (head) {
      renderer.fillRect(x0 + p.x * cell + 1, y0 + p.y * cell + 1, cell - 2, cell - 2, true);
      head = false;
    } else {
      renderer.drawRect(x0 + p.x * cell + 1, y0 + p.y * cell + 1, cell - 2, cell - 2, true);
    }
  }

  char scoreText[32];
  snprintf(scoreText, sizeof(scoreText), tr(STR_SNAKE_SCORE), score);
  renderer.drawText(UI_10_FONT_ID, x0, y0 - renderer.getLineHeight(UI_10_FONT_ID) - 4, scoreText);

  if (gameOver) {
    renderer.drawCenteredText(UI_12_FONT_ID, y0 + gridH / 2 - 20, tr(STR_GAME_OVER), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, y0 + gridH / 2 + 10, tr(STR_NEW_GAME));
  } else if (!running) {
    renderer.drawCenteredText(UI_12_FONT_ID, y0 + gridH / 2, tr(STR_SNAKE_PAUSED), true, EpdFontFamily::BOLD);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), gameOver ? tr(STR_NEW_GAME) : tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
