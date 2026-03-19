#pragma once

#include <array>
#include <functional>

#include "../Activity.h"

class CaroActivity final : public Activity {
  static constexpr int SIZE = 13;
  std::array<std::array<uint8_t, SIZE>, SIZE> grid{};
  int cursorR = SIZE / 2;
  int cursorC = SIZE / 2;
  uint8_t winner = 0;
  bool draw = false;

  const std::function<void()> onBack;

  bool hasFive(int r, int c, uint8_t v) const;
  int scorePos(int r, int c, uint8_t me, uint8_t opp) const;
  void aiMove();

 public:
  explicit CaroActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack)
      : Activity("Caro", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;
};
