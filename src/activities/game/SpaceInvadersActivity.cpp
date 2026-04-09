#include "SpaceInvadersActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <esp_random.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int PLAYER_HALF_WIDTH = 12;
constexpr int PLAYER_HEIGHT = 8;
constexpr int PLAYER_MOVE_STEP = 12;
constexpr unsigned long PLAYER_MOVE_REPEAT_MS = 80;
constexpr unsigned long BULLET_STEP_MS = 55;
constexpr unsigned long SHOT_COOLDOWN_MS = 180;
constexpr int ALIEN_STEP_X = 10;
constexpr int ALIEN_DROP_Y = 12;
constexpr int LASER_WIDTH = 5;
constexpr int PLAYER_LASER_HEIGHT = 12;
constexpr int ENEMY_LASER_HEIGHT = 10;
constexpr int ALIEN_SPACING_X = 26;
constexpr int ALIEN_SPACING_Y = 22;
constexpr int ALIEN_WIDTH = 18;
constexpr int ALIEN_HEIGHT = 12;
constexpr int BUNKER_WIDTH = 34;
constexpr int BUNKER_HEIGHT = 12;
}  // namespace

void SpaceInvadersActivity::computePlayfieldRect(int* x, int* y, int* width, int* height) const {
  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  *x = 10;
  *y = contentTop + 22;
  *width = pageWidth - 20;
  *height = pageHeight - *y - metrics.buttonHintsHeight - 52;

  if (*height < 120) {
    *height = 120;
  }
}

int SpaceInvadersActivity::playerY() const {
  int fieldX = 0;
  int fieldY = 0;
  int fieldW = 0;
  int fieldH = 0;
  computePlayfieldRect(&fieldX, &fieldY, &fieldW, &fieldH);
  return fieldY + fieldH - PLAYER_HEIGHT - 8;
}

int SpaceInvadersActivity::aliveAlienCount() const {
  int alive = 0;
  for (const uint8_t alien : aliens) {
    alive += alien ? 1 : 0;
  }
  return alive;
}

int SpaceInvadersActivity::alienStepDelayMs() const {
  const int defeated = ALIEN_ROWS * ALIEN_COLS - aliveAlienCount();
  const int levelPenalty = std::min(220, (level - 1) * 55);
  return std::max(150, 620 - levelPenalty - defeated * 8);
}

int SpaceInvadersActivity::enemyFireCooldownMs() const {
  return std::max(420, 1200 - (level - 1) * 160);
}

int SpaceInvadersActivity::enemyBulletStep() const {
  return std::min(10, 5 + (level - 1));
}

int SpaceInvadersActivity::maxActiveEnemyBullets() const {
  return std::min(MAX_ENEMY_BULLETS, 1 + (level - 1) / 2);
}

void SpaceInvadersActivity::setupLevel() {
  aliens.fill(1);
  for (auto& bullet : playerBullets) {
    bullet = Bullet{};
  }
  for (auto& bullet : enemyBullets) {
    bullet = Bullet{};
  }

  int fieldX = 0;
  int fieldY = 0;
  int fieldW = 0;
  int fieldH = 0;
  computePlayfieldRect(&fieldX, &fieldY, &fieldW, &fieldH);

  for (int i = 0; i < BUNKER_COUNT; ++i) {
    bunkers[i].x = fieldX + ((i + 1) * fieldW) / (BUNKER_COUNT + 1) - BUNKER_WIDTH / 2;
    bunkers[i].y = fieldY + fieldH - 48;
    bunkers[i].hp = 4;
  }

  playerX = fieldX + fieldW / 2;
  alienOffsetX = fieldX + 16;
  alienOffsetY = fieldY + 18;
  alienDirection = 1;

  const unsigned long now = millis();
  lastAlienStepMs = now;
  lastBulletStepMs = now;
  lastMoveMs = now;
  lastShotMs = 0;
  lastEnemyShotMs = now;
}

void SpaceInvadersActivity::resetGame() {
  score = 0;
  lives = 3;
  level = 1;
  gameOver = false;
  playerWon = false;
  setupLevel();
}

void SpaceInvadersActivity::movePlayer(const int dx) {
  int fieldX = 0;
  int fieldY = 0;
  int fieldW = 0;
  int fieldH = 0;
  computePlayfieldRect(&fieldX, &fieldY, &fieldW, &fieldH);

  playerX += dx;
  playerX = std::max(fieldX + PLAYER_HALF_WIDTH + 4, std::min(fieldX + fieldW - PLAYER_HALF_WIDTH - 4, playerX));
}

bool SpaceInvadersActivity::firePlayerBullet() {
  for (auto& bullet : playerBullets) {
    if (!bullet.active) {
      bullet.active = true;
      bullet.x = playerX;
      bullet.y = playerY() - 2;
      bullet.dy = -10;
      lastShotMs = millis();
      return true;
    }
  }
  return false;
}

bool SpaceInvadersActivity::spawnEnemyBullet() {
  int activeCount = 0;
  int freeIndex = -1;
  for (size_t i = 0; i < enemyBullets.size(); ++i) {
    if (enemyBullets[i].active) {
      ++activeCount;
    } else if (freeIndex < 0) {
      freeIndex = static_cast<int>(i);
    }
  }
  if (freeIndex < 0 || activeCount >= maxActiveEnemyBullets()) {
    return false;
  }

  int liveColumns[ALIEN_COLS] = {0};
  int liveColumnCount = 0;
  for (int col = 0; col < ALIEN_COLS; ++col) {
    for (int row = ALIEN_ROWS - 1; row >= 0; --row) {
      if (aliens[row * ALIEN_COLS + col]) {
        liveColumns[liveColumnCount++] = col;
        break;
      }
    }
  }

  if (liveColumnCount == 0) {
    return false;
  }

  const int pickedColumn = liveColumns[esp_random() % static_cast<uint32_t>(liveColumnCount)];
  int pickedRow = -1;
  for (int row = ALIEN_ROWS - 1; row >= 0; --row) {
    if (aliens[row * ALIEN_COLS + pickedColumn]) {
      pickedRow = row;
      break;
    }
  }
  if (pickedRow < 0) {
    return false;
  }

  enemyBullets[freeIndex].active = true;
  enemyBullets[freeIndex].x = alienOffsetX + pickedColumn * ALIEN_SPACING_X + ALIEN_WIDTH / 2;
  enemyBullets[freeIndex].y = alienOffsetY + pickedRow * ALIEN_SPACING_Y + ALIEN_HEIGHT + 2;
  enemyBullets[freeIndex].dy = enemyBulletStep();
  lastEnemyShotMs = millis();
  return true;
}

bool SpaceInvadersActivity::handleBunkerHit(const int x, const int y) {
  for (auto& bunker : bunkers) {
    if (bunker.hp == 0) {
      continue;
    }
    if (x >= bunker.x && x <= bunker.x + BUNKER_WIDTH && y >= bunker.y && y <= bunker.y + BUNKER_HEIGHT) {
      bunker.hp--;
      return true;
    }
  }
  return false;
}

bool SpaceInvadersActivity::handleAlienHit(const int x, const int y) {
  for (int row = 0; row < ALIEN_ROWS; ++row) {
    for (int col = 0; col < ALIEN_COLS; ++col) {
      const int index = row * ALIEN_COLS + col;
      if (!aliens[index]) {
        continue;
      }

      const int alienX = alienOffsetX + col * ALIEN_SPACING_X;
      const int alienY = alienOffsetY + row * ALIEN_SPACING_Y;
      if (x >= alienX && x <= alienX + ALIEN_WIDTH && y >= alienY && y <= alienY + ALIEN_HEIGHT) {
        aliens[index] = 0;
        score += 10;
        if (aliveAlienCount() == 0) {
          if (level >= FINAL_LEVEL) {
            playerWon = true;
          } else {
            ++level;
            setupLevel();
          }
        }
        return true;
      }
    }
  }
  return false;
}

int SpaceInvadersActivity::bottomAliveAlienY() const {
  int maxY = alienOffsetY;
  for (int row = 0; row < ALIEN_ROWS; ++row) {
    for (int col = 0; col < ALIEN_COLS; ++col) {
      if (aliens[row * ALIEN_COLS + col]) {
        maxY = std::max(maxY, alienOffsetY + row * ALIEN_SPACING_Y + ALIEN_HEIGHT);
      }
    }
  }
  return maxY;
}

bool SpaceInvadersActivity::stepBullets() {
  bool changed = false;
  const int shipY = playerY();
  int fieldX = 0;
  int fieldY = 0;
  int fieldW = 0;
  int fieldH = 0;
  computePlayfieldRect(&fieldX, &fieldY, &fieldW, &fieldH);

  for (auto& bullet : playerBullets) {
    if (!bullet.active) {
      continue;
    }
    bullet.y += bullet.dy;
    changed = true;

    if (bullet.y < fieldY + 2) {
      bullet.active = false;
      continue;
    }
    if (handleAlienHit(bullet.x, bullet.y) || handleBunkerHit(bullet.x, bullet.y)) {
      bullet.active = false;
    }
  }

  for (auto& bullet : enemyBullets) {
    if (!bullet.active) {
      continue;
    }
    bullet.y += bullet.dy;
    changed = true;

    if (bullet.y > fieldY + fieldH - 2) {
      bullet.active = false;
      continue;
    }
    if (handleBunkerHit(bullet.x, bullet.y)) {
      bullet.active = false;
      continue;
    }
    if (bullet.x >= playerX - PLAYER_HALF_WIDTH && bullet.x <= playerX + PLAYER_HALF_WIDTH &&
        bullet.y >= shipY - 6 && bullet.y <= shipY + PLAYER_HEIGHT + 2) {
      bullet.active = false;
      if (lives > 0) {
        --lives;
      }
      if (lives <= 0) {
        gameOver = true;
      }
    }
  }

  return changed;
}

bool SpaceInvadersActivity::stepAliens() {
  int fieldX = 0;
  int fieldY = 0;
  int fieldW = 0;
  int fieldH = 0;
  computePlayfieldRect(&fieldX, &fieldY, &fieldW, &fieldH);

  int leftmost = ALIEN_COLS * ALIEN_SPACING_X;
  int rightmost = 0;
  bool hasAlive = false;

  for (int row = 0; row < ALIEN_ROWS; ++row) {
    for (int col = 0; col < ALIEN_COLS; ++col) {
      if (!aliens[row * ALIEN_COLS + col]) {
        continue;
      }
      hasAlive = true;
      leftmost = std::min(leftmost, col * ALIEN_SPACING_X);
      rightmost = std::max(rightmost, col * ALIEN_SPACING_X + ALIEN_WIDTH);
    }
  }

  if (!hasAlive) {
    playerWon = true;
    return true;
  }

  const bool wouldHitRightEdge = alienDirection > 0 && (alienOffsetX + rightmost + ALIEN_STEP_X >= fieldX + fieldW - 8);
  const bool wouldHitLeftEdge = alienDirection < 0 && (alienOffsetX + leftmost - ALIEN_STEP_X <= fieldX + 8);

  if (wouldHitLeftEdge || wouldHitRightEdge) {
    alienDirection = -alienDirection;
    alienOffsetY += ALIEN_DROP_Y;
  } else {
    alienOffsetX += alienDirection * ALIEN_STEP_X;
  }

  if (bottomAliveAlienY() >= playerY() - 10) {
    gameOver = true;
  }

  return true;
}

void SpaceInvadersActivity::onEnter() {
  Activity::onEnter();
  resetGame();
  requestUpdate();
}

void SpaceInvadersActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  const unsigned long now = millis();

  if (isFinished()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      resetGame();
      requestUpdate();
    }
    return;
  }

  bool changed = false;

  if (mappedInput.isPressed(MappedInputManager::Button::Left) && now - lastMoveMs >= PLAYER_MOVE_REPEAT_MS) {
    movePlayer(-PLAYER_MOVE_STEP);
    lastMoveMs = now;
    changed = true;
  } else if (mappedInput.isPressed(MappedInputManager::Button::Right) && now - lastMoveMs >= PLAYER_MOVE_REPEAT_MS) {
    movePlayer(PLAYER_MOVE_STEP);
    lastMoveMs = now;
    changed = true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && now - lastShotMs >= SHOT_COOLDOWN_MS) {
    changed = firePlayerBullet() || changed;
  }

  if (now - lastBulletStepMs >= BULLET_STEP_MS) {
    lastBulletStepMs = now;
    changed = stepBullets() || changed;
  }

  if (!isFinished() && now - lastAlienStepMs >= static_cast<unsigned long>(alienStepDelayMs())) {
    lastAlienStepMs = now;
    changed = stepAliens() || changed;
  }

  if (!isFinished() && now - lastEnemyShotMs >= static_cast<unsigned long>(enemyFireCooldownMs())) {
    changed = spawnEnemyBullet() || changed;
  }

  if (changed) {
    requestUpdate();
  }
}

void SpaceInvadersActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SPACE_INVADERS));

  int fieldX = 0;
  int fieldY = 0;
  int fieldW = 0;
  int fieldH = 0;
  computePlayfieldRect(&fieldX, &fieldY, &fieldW, &fieldH);

  char statusText[72];
  snprintf(statusText, sizeof(statusText), "%s %d   %s %d   %s %d", tr(STR_SCORE), score, tr(STR_LIVES), lives,
           tr(STR_LEVEL), level);
  renderer.drawCenteredText(UI_10_FONT_ID, contentTop - 8, statusText);

  renderer.drawRoundedRect(fieldX, fieldY, fieldW, fieldH, 1, 6, true);

  for (int row = 0; row < ALIEN_ROWS; ++row) {
    for (int col = 0; col < ALIEN_COLS; ++col) {
      if (!aliens[row * ALIEN_COLS + col]) {
        continue;
      }
      const int x = alienOffsetX + col * ALIEN_SPACING_X;
      const int y = alienOffsetY + row * ALIEN_SPACING_Y;
      const Color bodyColor = (row % 2 == 0) ? Color::DarkGray : Color::Black;
      renderer.fillRoundedRect(x, y, ALIEN_WIDTH, ALIEN_HEIGHT, 3, bodyColor);
      renderer.fillRect(x + 4, y + 3, 3, 2, false);
      renderer.fillRect(x + ALIEN_WIDTH - 7, y + 3, 3, 2, false);
      renderer.fillRect(x + 3, y + ALIEN_HEIGHT - 2, 3, 2, true);
      renderer.fillRect(x + ALIEN_WIDTH - 6, y + ALIEN_HEIGHT - 2, 3, 2, true);
    }
  }

  for (const auto& bunker : bunkers) {
    if (bunker.hp == 0) {
      continue;
    }
    const Color bunkerColor = bunker.hp >= 3 ? Color::DarkGray : (bunker.hp == 2 ? Color::LightGray : Color::White);
    renderer.fillRoundedRect(bunker.x, bunker.y, BUNKER_WIDTH, BUNKER_HEIGHT, 3, bunkerColor);
    renderer.drawRoundedRect(bunker.x, bunker.y, BUNKER_WIDTH, BUNKER_HEIGHT, 1, 3, true);
    renderer.fillRect(bunker.x + BUNKER_WIDTH / 2 - 3, bunker.y + BUNKER_HEIGHT / 2, 6, BUNKER_HEIGHT / 2 + 1, false);
  }

  const int shipY = playerY();
  const int shipXs[] = {playerX - PLAYER_HALF_WIDTH + 2, playerX, playerX + PLAYER_HALF_WIDTH - 2};
  const int shipYs[] = {shipY, shipY - PLAYER_HEIGHT, shipY};
  renderer.fillPolygon(shipXs, shipYs, 3, true);
  renderer.fillRect(playerX - PLAYER_HALF_WIDTH, shipY, PLAYER_HALF_WIDTH * 2, 4, true);

  for (const auto& bullet : playerBullets) {
    if (bullet.active) {
      renderer.fillRect(bullet.x - LASER_WIDTH / 2, bullet.y - PLAYER_LASER_HEIGHT, LASER_WIDTH, PLAYER_LASER_HEIGHT, true);
    }
  }
  for (const auto& bullet : enemyBullets) {
    if (bullet.active) {
      renderer.fillRect(bullet.x - LASER_WIDTH / 2, bullet.y, LASER_WIDTH, ENEMY_LASER_HEIGHT, true);
    }
  }

  const int messageY = pageHeight - metrics.buttonHintsHeight - 32;
  if (playerWon) {
    renderer.drawCenteredText(UI_12_FONT_ID, messageY - 8, tr(STR_SPACE_INVADERS_WIN), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, messageY + 12, tr(STR_PRESS_CONFIRM_RETRY));
  } else if (gameOver) {
    renderer.drawCenteredText(UI_12_FONT_ID, messageY - 8, tr(STR_SPACE_INVADERS_LOSE), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, messageY + 12, tr(STR_PRESS_CONFIRM_RETRY));
  } else {
    const int remaining = aliveAlienCount();
    char remainingText[32];
    snprintf(remainingText, sizeof(remainingText), tr(STR_INVADERS_LEFT), remaining);
    renderer.drawCenteredText(UI_10_FONT_ID, messageY + 2, remainingText);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), isFinished() ? tr(STR_RETRY) : tr(STR_FIRE),
                                            tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
