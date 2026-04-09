#pragma once

#include <array>
#include <deque>
#include <functional>

#include "../Activity.h"

class SolitaireActivity final : public Activity {
  struct Card {
    uint8_t rank = 0;      // 1..13 (1=Ace, 13=King)
    uint8_t suit = 0;      // 0=Hearts, 1=Diamonds, 2=Clubs, 3=Spades
    bool faceUp = false;   // Classic solitaire starts with only the top card visible
  };

  static constexpr int TABLEAU_COUNT = 7;
  static constexpr int FOUNDATION_COUNT = 4;

  std::array<std::deque<Card>, TABLEAU_COUNT> tableau{};
  std::array<Card, FOUNDATION_COUNT> foundation{};  // Top card in each foundation
  std::deque<Card> stock{};
  std::deque<Card> waste{};
  
  int selectedColumn = 0;  // 0-6 target tableau column
  int selectionMode = 0;   // 0=tableau focus, 2=waste focus
  bool gameWon = false;
  bool gameLost = false;

  const std::function<void()> onBack;

  void startNewGame();
  void shuffleDeck(std::array<Card, 52>& deck);
  bool canMoveToTableau(const Card& card, int column) const;
  bool canMoveToFoundation(const Card& card) const;
  bool tryAutoMove();
  bool tryMoveWasteToFoundation();
  bool tryMoveWasteToTableau();
  bool tryMoveSelectedTableauCard();
  void revealTopCard(int column);
  void updateEndState();

  static bool isRed(uint8_t suit);
  static const char* rankText(uint8_t rank);
  void drawSuitSprite(int x, int y, int size, uint8_t suit) const;
  void drawCard(int x, int y, int w, int h, const Card& card, bool selected, bool faceDown = false);

 public:
  explicit SolitaireActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack)
      : Activity("Solitaire", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
