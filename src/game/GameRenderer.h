#pragma once

#include <GfxRenderer.h>

#include "GameTypes.h"

class GameState;

// Renders the dungeon viewport, status bar, message log, and button hints.
// Stateless — all data passed in or accessed via GameState singleton.
class GameRenderer {
 public:
  // Grid cell dimensions (pixels)
  static constexpr int CELL_W = 14;
  static constexpr int CELL_H = 20;

  // Screen layout (portrait 480x800)
  static constexpr int STATUS_Y = 2;
  static constexpr int STATUS_H = 26;
  static constexpr int VIEWPORT_Y = STATUS_H + 2;
  static constexpr int MESSAGE_H = 38;
  static constexpr int HINTS_H = 34;

  // Computed at init
  int viewportW = 0;   // Pixels
  int viewportH = 0;   // Pixels
  int viewCols = 0;    // Grid columns
  int viewRows = 0;    // Grid rows
  int viewportEndY = 0;
  int messageY = 0;
  int hintsY = 0;
  int screenW = 0;
  int screenH = 0;
  int gridOffsetX = 0; // Left padding to center grid

  void init(GfxRenderer& renderer);

  // Draw the full game screen
  void draw(GfxRenderer& renderer, const game::Tile* tiles, const uint8_t* fogOfWar, const game::Monster* monsters,
            uint8_t monsterCount, const game::Item* items, uint8_t itemCount, const bool* visible);

 private:
  void drawStatusBar(GfxRenderer& renderer) const;
  void drawViewport(GfxRenderer& renderer, const game::Tile* tiles, const uint8_t* fogOfWar,
                    const game::Monster* monsters, uint8_t monsterCount, const game::Item* items, uint8_t itemCount,
                    const bool* visible) const;
  void drawCell(GfxRenderer& renderer, int screenX, int screenY, char glyph, bool isVisible, bool isExplored) const;
  void drawMessages(GfxRenderer& renderer) const;
  void drawHints(GfxRenderer& renderer) const;
};
