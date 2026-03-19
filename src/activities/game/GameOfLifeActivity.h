#pragma once

#include <array>
#include <functional>

#include "../Activity.h"

class GameOfLifeActivity final : public Activity {
  static constexpr int W = 40;
  static constexpr int H = 28;
  std::array<std::array<uint8_t, W>, H> grid{};
  std::array<std::array<uint8_t, W>, H> next{};

  int cursorX = W / 2;
  int cursorY = H / 2;
  int generation = 0;
  bool running = false;
  unsigned long lastStep = 0;

  const std::function<void()> onBack;

  void stepLife();

 public:
  explicit GameOfLifeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                              const std::function<void()>& onBack)
      : Activity("GameOfLife", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;
  bool skipLoopDelay() override { return true; }
};
