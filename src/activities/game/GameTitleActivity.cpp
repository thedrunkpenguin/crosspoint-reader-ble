#include "GameTitleActivity.h"

#include <GfxRenderer.h>

#include "MappedInputManager.h"
#include "fontIds.h"

// --- Pixel-art block letters (5 wide x 7 tall bitmaps) ---

namespace {

// Each letter is 7 rows of 5-bit patterns (MSB = leftmost pixel)
// clang-format off
constexpr uint8_t GLYPH_D[] = {0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110};
constexpr uint8_t GLYPH_E[] = {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111};
constexpr uint8_t GLYPH_P[] = {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000};
constexpr uint8_t GLYPH_M[] = {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001};
constexpr uint8_t GLYPH_I[] = {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b11111};
constexpr uint8_t GLYPH_N[] = {0b10001, 0b11001, 0b10101, 0b10101, 0b10011, 0b10001, 0b10001};
constexpr uint8_t GLYPH_S[] = {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110};
// clang-format on

constexpr int GLYPH_W = 5;
constexpr int GLYPH_H = 7;

void drawBlockLetter(GfxRenderer& renderer, const uint8_t* glyph, int originX, int originY, int scale) {
  for (int row = 0; row < GLYPH_H; row++) {
    for (int col = 0; col < GLYPH_W; col++) {
      if (glyph[row] & (1 << (GLYPH_W - 1 - col))) {
        renderer.fillRect(originX + col * scale, originY + row * scale, scale - 1, scale - 1);
      }
    }
  }
}

struct LetterEntry {
  const uint8_t* glyph;
};

void drawWord(GfxRenderer& renderer, const LetterEntry* letters, int count, int centerX, int originY, int scale) {
  int letterW = GLYPH_W * scale + scale;  // Letter width + gap
  int totalW = count * letterW - scale;   // No trailing gap
  int startX = centerX - totalW / 2;

  for (int i = 0; i < count; i++) {
    drawBlockLetter(renderer, letters[i].glyph, startX + i * letterW, originY, scale);
  }
}

}  // namespace

void GameTitleActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const int centerX = pageWidth / 2;

  // White background (default)
  renderer.clearScreen();

  // Draw a decorative double border
  renderer.drawRect(10, 10, pageWidth - 20, 480);
  renderer.drawRect(13, 13, pageWidth - 26, 474);

  // Flavor text
  renderer.drawCenteredText(UI_10_FONT_ID, 40, "A roguelike for the CrossPoint Reader");

  // Separator line
  renderer.drawLine(40, 70, pageWidth - 40, 70);

  // Block letter title: "DEEP"
  constexpr int SCALE = 8;
  const LetterEntry deep[] = {{GLYPH_D}, {GLYPH_E}, {GLYPH_E}, {GLYPH_P}};
  drawWord(renderer, deep, 4, centerX, 90, SCALE);

  // Block letter title: "MINES"
  const LetterEntry mines[] = {{GLYPH_M}, {GLYPH_I}, {GLYPH_N}, {GLYPH_E}, {GLYPH_S}};
  drawWord(renderer, mines, 5, centerX, 160, SCALE);

  // Separator line
  renderer.drawLine(40, 230, pageWidth - 40, 230);

  // Subtitle
  renderer.drawCenteredText(UI_10_FONT_ID, 250, "D E E P    M I N E S", true, EpdFontFamily::BOLD);

  // Decorative pickaxe symbol
  renderer.drawCenteredText(UI_10_FONT_ID, 290, "--- * ---");

  // Version
  renderer.drawCenteredText(UI_10_FONT_ID, 340, "v0.1.0");

  // Credits
  renderer.drawCenteredText(SMALL_FONT_ID, 400, "Inspired by Moria & Angband");

  // Separator
  renderer.drawLine(40, 460, pageWidth - 40, 460);

  // Goal hint
  renderer.drawCenteredText(SMALL_FONT_ID, 500, "Descend 26 levels. Defeat the Necromancer.");
  renderer.drawCenteredText(SMALL_FONT_ID, 525, "Claim the Ring of Power.");

  // Press any key prompt
  renderer.drawCenteredText(UI_10_FONT_ID, 740, "[ press any key to continue ]");

  renderer.displayBuffer();
  rendered = true;
}

void GameTitleActivity::loop() {
  if (!rendered) return;

  if (mappedInput.wasAnyReleased()) {
    onStartGame();
  }
}
