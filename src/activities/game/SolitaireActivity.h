#pragma once

#include <array>
#include <functional>

#include "../Activity.h"

class SolitaireActivity final : public Activity {
  struct Card {
    uint8_t rank = 0;  // 1..13
    uint8_t suit = 0;  // 0..3
  };

  static constexpr int COLUMN_COUNT = 7;
  static constexpr int CARDS_PER_COLUMN = 5;

  std::array<std::array<Card, CARDS_PER_COLUMN>, COLUMN_COUNT> tableau{};
  std::array<uint8_t, COLUMN_COUNT> tableauCounts{};
  std::array<Card, 16> stock{};
  uint8_t stockCount = 0;
  Card waste{};
  int selectedColumn = 0;
  bool gameWon = false;
  bool gameLost = false;

  const std::function<void()> onBack;

  void startNewGame();
  void shuffleDeck(std::array<Card, 52>& deck);
  bool canPlayColumn(int column) const;
  bool playColumn(int column);
  bool drawFromStock();
  bool hasPlayableColumn() const;
  void updateEndState();
  int remainingCards() const;

  static bool isAdjacentRank(uint8_t a, uint8_t b);
  static const char* rankText(uint8_t rank);
  static const char* suitText(uint8_t suit);
  void drawCard(int x, int y, int w, int h, const Card& card, bool selected, bool faceDown = false);

 public:
  explicit SolitaireActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack)
      : Activity("Solitaire", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;
};
