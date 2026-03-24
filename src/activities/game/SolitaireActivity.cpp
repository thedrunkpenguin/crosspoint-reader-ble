#include "SolitaireActivity.h"

#include <Arduino.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int CARD_GAP_X = 4;
constexpr int CARD_OVERLAP_Y = 12;
}

bool SolitaireActivity::isAdjacentRank(uint8_t a, uint8_t b) {
  if (a == 0 || b == 0) return false;
  if ((a == 1 && b == 13) || (a == 13 && b == 1)) return true;
  return (a + 1 == b) || (b + 1 == a);
}

const char* SolitaireActivity::rankText(uint8_t rank) {
  static const char* kRanks[] = {"?", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
  return (rank <= 13) ? kRanks[rank] : "?";
}

const char* SolitaireActivity::suitText(uint8_t suit) {
  static const char* kSuits[] = {"H", "D", "C", "S"};
  return (suit < 4) ? kSuits[suit] : "?";
}

void SolitaireActivity::shuffleDeck(std::array<Card, 52>& deck) {
  for (int i = 51; i > 0; --i) {
    const int j = static_cast<int>(esp_random() % static_cast<uint32_t>(i + 1));
    std::swap(deck[i], deck[j]);
  }
}

void SolitaireActivity::startNewGame() {
  std::array<Card, 52> deck{};
  int index = 0;
  for (uint8_t suit = 0; suit < 4; ++suit) {
    for (uint8_t rank = 1; rank <= 13; ++rank) {
      deck[index++] = Card{rank, suit};
    }
  }

  shuffleDeck(deck);

  index = 0;
  for (int column = 0; column < COLUMN_COUNT; ++column) {
    tableauCounts[column] = CARDS_PER_COLUMN;
    for (int row = 0; row < CARDS_PER_COLUMN; ++row) {
      tableau[column][row] = deck[index++];
    }
  }

  waste = deck[index++];
  stockCount = 0;
  while (index < 52 && stockCount < stock.size()) {
    stock[stockCount++] = deck[index++];
  }

  selectedColumn = 0;
  gameWon = false;
  gameLost = false;
  updateEndState();
}

bool SolitaireActivity::canPlayColumn(int column) const {
  if (column < 0 || column >= COLUMN_COUNT) return false;
  if (tableauCounts[column] == 0) return false;
  const Card& card = tableau[column][tableauCounts[column] - 1];
  return isAdjacentRank(card.rank, waste.rank);
}

bool SolitaireActivity::playColumn(int column) {
  if (!canPlayColumn(column)) return false;
  waste = tableau[column][tableauCounts[column] - 1];
  tableauCounts[column]--;
  updateEndState();
  return true;
}

bool SolitaireActivity::drawFromStock() {
  if (stockCount == 0) return false;
  waste = stock[stockCount - 1];
  stockCount--;
  updateEndState();
  return true;
}

bool SolitaireActivity::hasPlayableColumn() const {
  for (int column = 0; column < COLUMN_COUNT; ++column) {
    if (canPlayColumn(column)) return true;
  }
  return false;
}

int SolitaireActivity::remainingCards() const {
  int total = stockCount;
  for (uint8_t count : tableauCounts) total += count;
  return total;
}

void SolitaireActivity::updateEndState() {
  gameWon = true;
  for (uint8_t count : tableauCounts) {
    if (count != 0) {
      gameWon = false;
      break;
    }
  }

  if (gameWon) {
    gameLost = false;
    return;
  }

  gameLost = (stockCount == 0 && !hasPlayableColumn());
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

  char label[8];
  snprintf(label, sizeof(label), "%s%s", rankText(card.rank), suitText(card.suit));
  renderer.drawText(UI_10_FONT_ID, x + 6, y + 6, label, true, EpdFontFamily::BOLD);
  const int centerW = renderer.getTextWidth(UI_12_FONT_ID, label, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, x + (w - centerW) / 2, y + (h - renderer.getLineHeight(UI_12_FONT_ID)) / 2, label,
                    true, EpdFontFamily::BOLD);
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

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    selectedColumn = (selectedColumn - 1 + COLUMN_COUNT) % COLUMN_COUNT;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selectedColumn = (selectedColumn + 1) % COLUMN_COUNT;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (drawFromStock()) {
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (playColumn(selectedColumn)) {
      requestUpdate();
    }
  }
}

void SolitaireActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = 12;
  const int cardW = std::max(32, (pageWidth - sidePadding * 2 - CARD_GAP_X * (COLUMN_COUNT - 1)) / COLUMN_COUNT);
  const int cardH = std::max(42, (cardW * 13) / 10);
  const int columnOverlap = std::max(8, cardH / 5);

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Solitaire");

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  renderer.drawCenteredText(UI_10_FONT_ID, top, "Golf Solitaire: play one rank up or down.");
  renderer.drawCenteredText(UI_10_FONT_ID, top + 14, "Up/Down draws from stock.");

  const int topCardsY = top + 28;
  const int stockX = sidePadding;
  drawCard(stockX, topCardsY, cardW, cardH, Card{}, false, true);
  char stockText[20];
  snprintf(stockText, sizeof(stockText), "Stock %u", stockCount);
  const int stockTextW = renderer.getTextWidth(UI_10_FONT_ID, stockText);
  renderer.drawText(UI_10_FONT_ID, stockX + (cardW - stockTextW) / 2, topCardsY + cardH + 8, stockText);

  const int wasteX = stockX + cardW + 16;
  drawCard(wasteX, topCardsY, cardW, cardH, waste, false);
  const char* wasteLabel = "Waste";
  const int wasteTextW = renderer.getTextWidth(UI_10_FONT_ID, wasteLabel, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, wasteX + (cardW - wasteTextW) / 2, topCardsY + cardH + 8, wasteLabel, true,
                    EpdFontFamily::BOLD);

  char remainText[24];
  snprintf(remainText, sizeof(remainText), "Remaining %d", remainingCards());
  renderer.drawText(UI_10_FONT_ID, wasteX + cardW + 16, topCardsY + 10, remainText, true, EpdFontFamily::BOLD);

  if (!gameWon && !gameLost) {
    renderer.drawText(UI_10_FONT_ID, wasteX + cardW + 16, topCardsY + 34,
                      canPlayColumn(selectedColumn) ? "Selected column is playable" : "Draw or pick another column",
                      true);
  }

  const int columnsWidth = COLUMN_COUNT * cardW + (COLUMN_COUNT - 1) * CARD_GAP_X;
  const int startX = (pageWidth - columnsWidth) / 2;
  const int columnsTop = topCardsY + cardH + 42;

  for (int column = 0; column < COLUMN_COUNT; ++column) {
    const int x = startX + column * (cardW + CARD_GAP_X);

    for (int row = 0; row < tableauCounts[column]; ++row) {
      const int y = columnsTop + row * columnOverlap;
      const bool selected = (column == selectedColumn && row == tableauCounts[column] - 1);
      drawCard(x, y, cardW, cardH, tableau[column][row], selected);
    }

    if (tableauCounts[column] == 0) {
      renderer.drawRoundedRect(x, columnsTop, cardW, cardH, 1, 6, true);
      if (column == selectedColumn) {
        renderer.drawRoundedRect(x + 2, columnsTop + 2, cardW - 4, cardH - 4, 1, 5, true);
      }
      const char* emptyLabel = "Empty";
      const int emptyW = renderer.getTextWidth(UI_10_FONT_ID, emptyLabel);
      renderer.drawText(UI_10_FONT_ID, x + (cardW - emptyW) / 2, columnsTop + cardH / 2 - 6, emptyLabel);
    }
  }

  const int statusY = pageHeight - metrics.buttonHintsHeight - 34;
  if (gameWon) {
    renderer.drawCenteredText(UI_12_FONT_ID, statusY, "You cleared the board.", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, statusY + 22, "Press Confirm for a new game.");
  } else if (gameLost) {
    renderer.drawCenteredText(UI_12_FONT_ID, statusY, "No more moves.", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, statusY + 22, "Press Confirm for a new game.");
  }

  const auto labels = mappedInput.mapLabels("Back", (gameWon || gameLost) ? "New game" : "Play", "Draw", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
