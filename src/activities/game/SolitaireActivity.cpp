#include "SolitaireActivity.h"

#include <Arduino.h>
#include <esp_random.h>

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

void SolitaireActivity::drawSuitSprite(int x, int y, int size, uint8_t suit) const {
  const int half = size / 2;
  const int third = std::max(2, size / 3);

  switch (suit) {
    case 0: {  // Heart
      renderer.fillRoundedRect(x, y, half, half, std::max(2, half / 2), Color::Black);
      renderer.fillRoundedRect(x + half, y, half, half, std::max(2, half / 2), Color::Black);
      const int xs[] = {x - 1, x + size + 1, x + half};
      const int ys[] = {y + third, y + third, y + size + 2};
      renderer.fillPolygon(xs, ys, 3, true);
      break;
    }
    case 1: {  // Diamond
      const int xs[] = {x + half, x + size, x + half, x};
      const int ys[] = {y, y + half, y + size, y + half};
      renderer.fillPolygon(xs, ys, 4, true);
      break;
    }
    case 2: {  // Club
      renderer.fillRoundedRect(x + half - third, y, third + 2, third + 2, 2, Color::Black);
      renderer.fillRoundedRect(x, y + third - 1, third + 2, third + 2, 2, Color::Black);
      renderer.fillRoundedRect(x + size - third - 2, y + third - 1, third + 2, third + 2, 2, Color::Black);
      renderer.fillRect(x + half - 1, y + half, 3, std::max(4, size / 3), true);
      renderer.fillRect(x + half - third, y + size - 2, third * 2, 2, true);
      break;
    }
    case 3: {  // Spade
      const int xs[] = {x + half, x + size, x + half, x};
      const int ys[] = {y, y + half + 1, y + size - third, y + half + 1};
      renderer.fillPolygon(xs, ys, 4, true);
      renderer.fillRoundedRect(x, y + third, half, half, std::max(2, half / 2), Color::Black);
      renderer.fillRoundedRect(x + half, y + third, half, half, std::max(2, half / 2), Color::Black);
      renderer.fillRect(x + half - 1, y + size - third, 3, std::max(4, size / 3), true);
      renderer.fillRect(x + half - third, y + size - 2, third * 2, 2, true);
      break;
    }
    default:
      break;
  }
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
      deck[index++] = Card{rank, suit, false};
    }
  }
  shuffleDeck(deck);

  // Deal classic solitaire tableau: only the last card in each column is face up.
  index = 0;
  for (int column = 0; column < TABLEAU_COUNT; ++column) {
    for (int row = 0; row <= column; ++row) {
      Card dealt = deck[index++];
      dealt.faceUp = (row == column);
      tableau[column].push_back(dealt);
    }
  }

  // Remaining cards go to stock face down.
  while (index < 52) {
    Card dealt = deck[index++];
    dealt.faceUp = false;
    stock.push_back(dealt);
  }

  selectedColumn = 0;
  selectionMode = 0;  // Start at tableau
  gameWon = false;
  gameLost = false;
  updateEndState();
}

bool SolitaireActivity::canMoveToTableau(const Card& card, int column) const {
  if (column < 0 || column >= TABLEAU_COUNT || card.rank == 0 || !card.faceUp) return false;
  if (tableau[column].empty()) {
    return card.rank == 13;  // Empty column can accept King
  }

  const Card& target = tableau[column].back();
  if (!target.faceUp) {
    return false;
  }

  return (card.rank + 1 == target.rank) && (isRed(card.suit) != isRed(target.suit));
}

bool SolitaireActivity::canMoveToFoundation(const Card& card) const {
  if (card.rank == 0 || !card.faceUp) return false;
  const Card& f = foundation[card.suit];
  return (f.rank == 0 && card.rank == 1) || (f.rank + 1 == card.rank);
}

void SolitaireActivity::revealTopCard(int column) {
  if (column < 0 || column >= TABLEAU_COUNT || tableau[column].empty()) {
    return;
  }
  tableau[column].back().faceUp = true;
}

bool SolitaireActivity::tryMoveWasteToFoundation() {
  if (waste.empty()) {
    return false;
  }

  const Card card = waste.back();
  if (!canMoveToFoundation(card)) {
    return false;
  }

  foundation[card.suit] = card;
  waste.pop_back();
  return true;
}

bool SolitaireActivity::tryMoveWasteToTableau() {
  if (waste.empty()) {
    return false;
  }

  const Card card = waste.back();
  if (!canMoveToTableau(card, selectedColumn)) {
    return false;
  }

  tableau[selectedColumn].push_back(card);
  waste.pop_back();
  return true;
}

bool SolitaireActivity::tryMoveSelectedTableauCard() {
  if (selectedColumn < 0 || selectedColumn >= TABLEAU_COUNT || tableau[selectedColumn].empty()) {
    return false;
  }

  Card card = tableau[selectedColumn].back();
  if (!card.faceUp) {
    tableau[selectedColumn].back().faceUp = true;
    return true;
  }

  if (canMoveToFoundation(card)) {
    foundation[card.suit] = card;
    tableau[selectedColumn].pop_back();
    revealTopCard(selectedColumn);
    return true;
  }

  for (int offset = 1; offset < TABLEAU_COUNT; ++offset) {
    const int targetColumn = (selectedColumn + offset) % TABLEAU_COUNT;
    if (canMoveToTableau(card, targetColumn)) {
      tableau[targetColumn].push_back(card);
      tableau[selectedColumn].pop_back();
      revealTopCard(selectedColumn);
      selectedColumn = targetColumn;
      return true;
    }
  }

  return false;
}

bool SolitaireActivity::tryAutoMove() {
  if (tryMoveWasteToFoundation()) {
    return true;
  }

  for (int i = 0; i < TABLEAU_COUNT; ++i) {
    if (tableau[i].empty()) {
      continue;
    }

    const Card card = tableau[i].back();
    if (canMoveToFoundation(card)) {
      foundation[card.suit] = card;
      tableau[i].pop_back();
      revealTopCard(i);
      return true;
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

  gameLost = false;

  if (!stock.empty()) {
    return;
  }

  if (!waste.empty()) {
    const Card& wasteCard = waste.back();
    if (canMoveToFoundation(wasteCard)) {
      return;
    }
    for (int column = 0; column < TABLEAU_COUNT; ++column) {
      if (canMoveToTableau(wasteCard, column)) {
        return;
      }
    }
  }

  for (int i = 0; i < TABLEAU_COUNT; ++i) {
    if (tableau[i].empty()) {
      continue;
    }

    const Card& card = tableau[i].back();
    if (!card.faceUp || canMoveToFoundation(card)) {
      return;
    }

    for (int j = 0; j < TABLEAU_COUNT; ++j) {
      if (i != j && canMoveToTableau(card, j)) {
        return;
      }
    }
  }

  gameLost = true;
}


void SolitaireActivity::drawCard(int x, int y, int w, int h, const Card& card, bool selected, bool faceDown) {
  renderer.fillRoundedRect(x, y, w, h, 6, faceDown ? Color::LightGray : Color::White);
  renderer.drawRoundedRect(x, y, w, h, selected ? 2 : 1, 6, true);

  if (faceDown) {
    for (int yy = y + 8; yy < y + h - 8; yy += 8) {
      renderer.drawLine(x + 6, yy, x + w - 6, yy, true);
    }
    for (int xx = x + 8; xx < x + w - 8; xx += 8) {
      renderer.drawLine(xx, y + 6, xx, y + h - 6, true);
    }
    return;
  }

  renderer.drawText(UI_10_FONT_ID, x + 5, y + 4, rankText(card.rank), true, EpdFontFamily::BOLD);

  const int spriteSize = std::max(10, std::min(w - 12, h - 18) / 2);
  const int spriteX = x + (w - spriteSize) / 2;
  const int spriteY = y + std::max(22, (h - spriteSize) / 2 + 6);
  drawSuitSprite(spriteX, spriteY, spriteSize, card.suit);
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

  // Up toggles focus to the waste pile so a drawn card can be visibly selected and moved.
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (!waste.empty()) {
      selectionMode = (selectionMode == 2) ? 0 : 2;
      requestUpdate();
    } else if (tryAutoMove()) {
      updateEndState();
      requestUpdate();
    }
    return;
  }

  // Navigate between tableau columns.
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    selectedColumn = (selectedColumn - 1 + TABLEAU_COUNT) % TABLEAU_COUNT;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selectedColumn = (selectedColumn + 1) % TABLEAU_COUNT;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (!stock.empty()) {
      Card card = stock.back();
      stock.pop_back();
      card.faceUp = true;
      waste.push_back(card);
      selectionMode = 2;
      updateEndState();
      requestUpdate();
    } else if (!waste.empty()) {
      while (!waste.empty()) {
        Card card = waste.back();
        waste.pop_back();
        card.faceUp = false;
        stock.push_back(card);
      }
      selectionMode = 0;
      updateEndState();
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    bool moved = false;

    if (selectionMode == 2 && !waste.empty()) {
      moved = tryMoveWasteToFoundation();
      if (!moved) {
        moved = tryMoveWasteToTableau();
      }
      if (moved && waste.empty()) {
        selectionMode = 0;
      }
    } else {
      moved = tryMoveSelectedTableauCard();
      if (!moved && !waste.empty()) {
        moved = tryMoveWasteToFoundation();
      }
    }

    if (moved) {
      updateEndState();
      requestUpdate();
    }
  }
}



void SolitaireActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = 12;
  
  // Draw header
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Solitaire");

  // Top card row: larger cards and more breathing room for readability.
  const int topStartY = metrics.topPadding + metrics.headerHeight + 12;
  const int cardW = 44;
  const int cardH = 60;
  const int topLabelY = topStartY + cardH + 6;

  // Stock pile
  const int stockX = sidePadding;
  drawCard(stockX, topStartY, cardW, cardH, Card{}, selectionMode == 1, !stock.empty());
  const int stockTextW = renderer.getTextWidth(UI_10_FONT_ID, "Draw");
  renderer.drawText(UI_10_FONT_ID, stockX + (cardW - stockTextW) / 2, topLabelY, "Draw");

  // Waste pile
  const int wasteX = stockX + cardW + 20;
  if (!waste.empty()) {
    drawCard(wasteX, topStartY, cardW, cardH, waste.back(), selectionMode == 2);
  } else {
    renderer.drawRoundedRect(wasteX, topStartY, cardW, cardH, selectionMode == 2 ? 2 : 1, 6, true);
  }
  const int wasteTextW = renderer.getTextWidth(UI_10_FONT_ID, "Waste");
  renderer.drawText(UI_10_FONT_ID, wasteX + (cardW - wasteTextW) / 2, topLabelY, "Waste");

  // Foundation piles (4 columns for each suit)
  const int foundationStartX = wasteX + cardW + 38;
  for (int i = 0; i < FOUNDATION_COUNT; ++i) {
    const int fx = foundationStartX + i * (cardW + 8);
    if (foundation[i].rank == 0) {
      renderer.drawRoundedRect(fx, topStartY, cardW, cardH, 1, 6, true);
      drawSuitSprite(fx + (cardW - 12) / 2, topStartY + (cardH - 12) / 2, 12, i);
    } else {
      drawCard(fx, topStartY, cardW, cardH, foundation[i], false);
    }
  }

  // Tableau columns
  const int tableauStartY = topStartY + cardH + 28;
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
        const bool isFaceDown = !tableau[column][row].faceUp;
        drawCard(x, y, tableauCardW, tableauCardH, tableau[column][row], isSelected, isFaceDown);
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

  const auto labels = mappedInput.mapLabels("Back", (gameWon || gameLost) ? "New" : "Move", "Waste", "Draw");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
