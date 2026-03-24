#include "SolitaireActivity.h"

#include <Arduino.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int CARD_GAP_X = 4;
constexpr int CARD_OVERLAP_Y = 8;
}

bool SolitaireActivity::isRed(uint8_t suit) {
  return suit == 0 || suit == 1;  // Hearts=0, Diamonds=1
}

const char* SolitaireActivity::rankText(uint8_t rank) {
  static const char* kRanks[] = {"?", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
  return (rank <= 13) ? kRanks[rank] : "?";
}

const char* SolitaireActivity::suitSymbol(uint8_t suit) {
  static const char* kSymbols[] = {"♥", "♦", "♣", "♠"};
  return (suit < 4) ? kSymbols[suit] : "?";
}


void SolitaireActivity::shuffleDeck(std::array<Card, 52>& deck) {
  for (int i = 51; i > 0; --i) {
    const int j = static_cast<int>(esp_random() % static_cast<uint32_t>(i + 1));
    std::swap(deck[i], deck[j]);
  }
}

void SolitaireActivity::startNewGame() {
  // Clear all data structures
  for (int i = 0; i < TABLEAU_COUNT; ++i) {
    tableau[i].clear();
  }
  for (int i = 0; i < FOUNDATION_COUNT; ++i) {
    foundation[i] = Card{0, 0};
  }
  stock.clear();
  waste.clear();

  // Create and shuffle deck
  std::array<Card, 52> deck{};
  int index = 0;
  for (uint8_t suit = 0; suit < 4; ++suit) {
    for (uint8_t rank = 1; rank <= 13; ++rank) {
      deck[index++] = Card{rank, suit};
    }
  }
  shuffleDeck(deck);

  // Deal to tableau: column 0 gets 1 card, column 1 gets 2 cards, etc.
  index = 0;
  for (int column = 0; column < TABLEAU_COUNT; ++column) {
    for (int row = 0; row <= column; ++row) {
      tableau[column].push_back(deck[index++]);
    }
  }

  // Remaining cards go to stock
  while (index < 52) {
    stock.push_back(deck[index++]);
  }

  selectedColumn = 0;
  selectionMode = 0;  // Start at tableau
  gameWon = false;
  gameLost = false;
  updateEndState();
}

bool SolitaireActivity::canMoveToTableau(const Card& card, int column) const {
  if (column < 0 || column >= TABLEAU_COUNT) return false;
  if (tableau[column].empty()) {
    // Empty column can accept King
    return card.rank == 13;
  }
  // Target must be one rank higher and opposite color
  const Card& target = tableau[column].back();
  return (card.rank + 1 == target.rank) && (isRed(card.suit) != isRed(target.suit));
}

bool SolitaireActivity::canMoveToFoundation(const Card& card) const {
  if (card.rank == 0) return false;
  const Card& f = foundation[card.suit];
  return (f.rank == 0 && card.rank == 1) || (f.rank + 1 == card.rank);
}

bool SolitaireActivity::tryAutoMove() {
  // Try to move waste to foundation
  if (!waste.empty()) {
    const Card& card = waste.back();
    if (canMoveToFoundation(card)) {
      foundation[card.suit] = card;
      waste.pop_back();
      return true;
    }
  }
  // Try to move from tableau to foundation
  for (int i = 0; i < TABLEAU_COUNT; ++i) {
    if (!tableau[i].empty()) {
      const Card& card = tableau[i].back();
      if (canMoveToFoundation(card)) {
        foundation[card.suit] = card;
        tableau[i].pop_back();
        return true;
      }
    }
  }
  return false;
}

void SolitaireActivity::updateEndState() {
  gameWon = true;
  for (int i = 0; i < FOUNDATION_COUNT; ++i) {
    if (foundation[i].rank != 13) {
      gameWon = false;
      break;
    }
  }

  if (gameWon) {
    gameLost = false;
    return;
  }

  // Check if lost: stock empty, waste empty or top card can't move, no tableau columns have playable cards
  if (stock.empty() && waste.empty()) {
    bool canMove = false;
    for (int i = 0; i < TABLEAU_COUNT; ++i) {
      if (!tableau[i].empty()) {
        const Card& card = tableau[i].back();
        if (canMoveToFoundation(card)) {
          canMove = true;
          break;
        }
        for (int j = 0; j < TABLEAU_COUNT; ++j) {
          if (i != j && canMoveToTableau(card, j)) {
            canMove = true;
            break;
          }
        }
      }
    }
    gameLost = !canMove;
  } else {
    gameLost = false;
  }
}


void SolitaireActivity::drawCard(int x, int y, int w, int h, const Card& card, bool selected, bool faceDown) {
  renderer.fillRoundedRect(x, y, w, h, 6, faceDown ? Color::LightGray : Color::White);
  renderer.drawRoundedRect(x, y, w, h, 1, 6, true);
  if (selected) {
    renderer.drawRoundedRect(x + 2, y + 2, w - 4, h - 4, 1, 5, true);
  }

  if (faceDown) {
    for (int yy = y + 8; yy < y + h - 8; yy += 8) {
      renderer.drawLine(x + 8, yy, x + w - 8, yy);
    }
    return;
  }

  // Draw rank and suit symbol
  char label[8];
  snprintf(label, sizeof(label), "%s%s", rankText(card.rank), suitSymbol(card.suit));
  
  bool isCardRed = isRed(card.suit);
  renderer.drawText(UI_10_FONT_ID, x + 6, y + 6, label, isCardRed);
}


void SolitaireActivity::onEnter() {
  Activity::onEnter();
  startNewGame();
  requestUpdate();
}

void SolitaireActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) onBack();
    return;
  }

  if (gameWon || gameLost) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      startNewGame();
      requestUpdate();
    }
    return;
  }

  // Try auto-move first
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    while (tryAutoMove()) {
      // Keep auto-moving
    }
    requestUpdate();
    return;
  }

  // Navigate between tableau columns
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    selectedColumn = (selectedColumn - 1 + TABLEAU_COUNT) % TABLEAU_COUNT;
    selectionMode = 0;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selectedColumn = (selectedColumn + 1) % TABLEAU_COUNT;
    selectionMode = 0;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    // Draw from stock
    if (!stock.empty()) {
      waste.push_back(stock.back());
      stock.pop_back();
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Move selected tableau card to foundation
    if (!tableau[selectedColumn].empty()) {
      Card card = tableau[selectedColumn].back();
      if (canMoveToFoundation(card)) {
        foundation[card.suit] = card;
        tableau[selectedColumn].pop_back();
        updateEndState();
        requestUpdate();
      } else {
        // Try to move to another tableau column
        for (int i = 0; i < TABLEAU_COUNT; ++i) {
          if (i != selectedColumn && canMoveToTableau(card, i)) {
            tableau[i].push_back(card);
            tableau[selectedColumn].pop_back();
            updateEndState();
            requestUpdate();
            return;
          }
        }
      }
    }
  }
}



void SolitaireActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = 12;
  
  // Draw header
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Solitaire");

  // Top cards area with proper padding
  const int topStartY = metrics.topPadding + metrics.headerHeight + 8;
  const int cardW = 36;
  const int cardH = 48;
  
  // Stock pile
  const int stockX = sidePadding;
  drawCard(stockX, topStartY, cardW, cardH, Card{}, false, !stock.empty());
  const int stockTextW = renderer.getTextWidth(UI_10_FONT_ID, "Stock");
  renderer.drawText(UI_10_FONT_ID, stockX + (cardW - stockTextW) / 2, topStartY + cardH + 4, "Stock");
  
  // Waste pile
  const int wasteX = stockX + cardW + 16;
  if (!waste.empty()) {
    drawCard(wasteX, topStartY, cardW, cardH, waste.back(), false);
  } else {
    renderer.drawRoundedRect(wasteX, topStartY, cardW, cardH, 1, 6, true);
  }
  const int wasteTextW = renderer.getTextWidth(UI_10_FONT_ID, "Waste");
  renderer.drawText(UI_10_FONT_ID, wasteX + (cardW - wasteTextW) / 2, topStartY + cardH + 4, "Waste");
  
  // Foundation piles (4 columns for each suit)
  const int foundationStartX = wasteX + cardW + 32;
  for (int i = 0; i < FOUNDATION_COUNT; ++i) {
    const int fx = foundationStartX + i * (cardW + 8);
    if (foundation[i].rank == 0) {
      renderer.drawRoundedRect(fx, topStartY, cardW, cardH, 1, 6, true);
      const char* suit = suitSymbol(i);
      const int suitW = renderer.getTextWidth(UI_10_FONT_ID, suit);
      renderer.drawText(UI_10_FONT_ID, fx + (cardW - suitW) / 2, topStartY + cardH / 2 - 6, suit);
    } else {
      drawCard(fx, topStartY, cardW, cardH, foundation[i], false);
    }
  }

  // Tableau columns
  const int tableauStartY = topStartY + cardH + 16;
  const int tableauCardW = std::max(32, (pageWidth - sidePadding * 2 - CARD_GAP_X * (TABLEAU_COUNT - 1)) / TABLEAU_COUNT);
  const int tableauCardH = std::max(40, (tableauCardW * 13) / 10);
  const int columnOverlap = std::max(6, tableauCardH / 6);

  const int tableauWidth = TABLEAU_COUNT * tableauCardW + (TABLEAU_COUNT - 1) * CARD_GAP_X;
  const int tableauX = (pageWidth - tableauWidth) / 2;

  for (int column = 0; column < TABLEAU_COUNT; ++column) {
    const int x = tableauX + column * (tableauCardW + CARD_GAP_X);

    if (tableau[column].empty()) {
      renderer.drawRoundedRect(x, tableauStartY, tableauCardW, tableauCardH, 1, 6, true);
      if (column == selectedColumn && selectionMode == 0) {
        renderer.drawRoundedRect(x + 2, tableauStartY + 2, tableauCardW - 4, tableauCardH - 4, 1, 5, true);
      }
      const char* emptyLabel = "K";
      const int emptyW = renderer.getTextWidth(UI_10_FONT_ID, emptyLabel);
      renderer.drawText(UI_10_FONT_ID, x + (tableauCardW - emptyW) / 2, tableauStartY + tableauCardH / 2 - 6, emptyLabel, true);
    } else {
      // Draw all cards in column
      for (size_t row = 0; row < tableau[column].size(); ++row) {
        const int y = tableauStartY + static_cast<int>(row) * columnOverlap;
        const bool isSelected = (column == selectedColumn && row == tableau[column].size() - 1 && selectionMode == 0);
        drawCard(x, y, tableauCardW, tableauCardH, tableau[column][row], isSelected);
      }
    }
  }

  // Status bar
  const int statusY = pageHeight - metrics.buttonHintsHeight - 40;
  if (gameWon) {
    renderer.drawCenteredText(UI_12_FONT_ID, statusY, "You won!", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, statusY + 18, "Press Confirm for new game.");
  } else if (gameLost) {
    renderer.drawCenteredText(UI_12_FONT_ID, statusY, "No more moves.", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, statusY + 18, "Press Confirm for new game.");
  } else {
    char remainText[24];
    snprintf(remainText, sizeof(remainText), "Stock: %zu  Waste: %zu", stock.size(), waste.size());
    renderer.drawCenteredText(UI_10_FONT_ID, statusY, remainText);
  }

  const auto labels = mappedInput.mapLabels("Back", (gameWon || gameLost) ? "New" : "Play", "Draw", "Auto");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
