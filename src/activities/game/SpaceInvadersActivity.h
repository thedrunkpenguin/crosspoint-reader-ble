#pragma once

#include <array>
#include <functional>

#include "../Activity.h"

class SpaceInvadersActivity final : public Activity {
  static constexpr int ALIEN_ROWS = 4;
  static constexpr int ALIEN_COLS = 7;
  static constexpr int MAX_PLAYER_BULLETS = 2;
  static constexpr int MAX_ENEMY_BULLETS = 3;
  static constexpr int BUNKER_COUNT = 3;
  static constexpr int FINAL_LEVEL = 5;

  struct Bullet {
    int16_t x = 0;
    int16_t y = 0;
    int8_t dy = 0;
    bool active = false;
  };

  struct Bunker {
    int16_t x = 0;
    int16_t y = 0;
    uint8_t hp = 0;
  };

  std::array<uint8_t, ALIEN_ROWS * ALIEN_COLS> aliens{};
  std::array<Bullet, MAX_PLAYER_BULLETS> playerBullets{};
  std::array<Bullet, MAX_ENEMY_BULLETS> enemyBullets{};
  std::array<Bunker, BUNKER_COUNT> bunkers{};

  int playerX = 0;
  int alienOffsetX = 0;
  int alienOffsetY = 0;
  int alienDirection = 1;
  int score = 0;
  int lives = 3;
  int level = 1;
  bool gameOver = false;
  bool playerWon = false;

  unsigned long lastAlienStepMs = 0;
  unsigned long lastBulletStepMs = 0;
  unsigned long lastMoveMs = 0;
  unsigned long lastShotMs = 0;
  unsigned long lastEnemyShotMs = 0;

  const std::function<void()> onBack;

  void resetGame();
  void setupLevel();
  void computePlayfieldRect(int* x, int* y, int* width, int* height) const;
  int alienStepDelayMs() const;
  int enemyFireCooldownMs() const;
  int enemyBulletStep() const;
  int maxActiveEnemyBullets() const;
  int aliveAlienCount() const;
  int playerY() const;
  void movePlayer(int dx);
  bool firePlayerBullet();
  bool spawnEnemyBullet();
  bool stepBullets();
  bool stepAliens();
  bool handleBunkerHit(int x, int y);
  bool handleAlienHit(int x, int y);
  int bottomAliveAlienY() const;
  bool isFinished() const { return gameOver || playerWon; }

 public:
  explicit SpaceInvadersActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack)
      : Activity("SpaceInvaders", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool skipLoopDelay() override { return true; }
  bool preventAutoSleep() override { return true; }
};
