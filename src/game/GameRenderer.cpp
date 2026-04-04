#include "GameRenderer.h"

#include <algorithm>
#include <cstdio>

#include "GameState.h"
#include "fontIds.h"

void GameRenderer::init(GfxRenderer& renderer) {
  screenW = renderer.getScreenWidth();
  screenH = renderer.getScreenHeight();

  // Compute viewport dimensions
  viewportEndY = screenH - MESSAGE_H - HINTS_H;
  viewportH = viewportEndY - VIEWPORT_Y;
  viewportW = screenW;

  viewCols = viewportW / CELL_W;
  viewRows = viewportH / CELL_H;

  // Center the grid horizontally if there's leftover space
  gridOffsetX = (viewportW - viewCols * CELL_W) / 2;

  messageY = viewportEndY;
  hintsY = screenH - HINTS_H;
}

void GameRenderer::draw(GfxRenderer& renderer, const game::Tile* tiles, const uint8_t* fogOfWar,
                        const game::Monster* monsters, uint8_t monsterCount, const game::Item* items, uint8_t itemCount,
                        const bool* visible) {
  renderer.clearScreen();

  drawStatusBar(renderer);
  drawViewport(renderer, tiles, fogOfWar, monsters, monsterCount, items, itemCount, visible);
  drawMessages(renderer);
  drawHints(renderer);

  // Separator lines
  renderer.drawLine(0, STATUS_H, screenW, STATUS_H);
  renderer.drawLine(0, viewportEndY, screenW, viewportEndY);
  renderer.drawLine(0, hintsY, screenW, hintsY);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

// --- Status Bar ---

void GameRenderer::drawStatusBar(GfxRenderer& renderer) const {
  const auto& p = GAME_STATE.player;

  char hpBuf[24];
  snprintf(hpBuf, sizeof(hpBuf), "HP:%u/%u", p.hp, p.maxHp);

  char mpBuf[24];
  snprintf(mpBuf, sizeof(mpBuf), "MP:%u/%u", p.mp, p.maxMp);

  char depthBuf[16];
  snprintf(depthBuf, sizeof(depthBuf), "Dl:%u", p.dungeonDepth);

  char lvlBuf[16];
  snprintf(lvlBuf, sizeof(lvlBuf), "Cl:%u", p.charLevel);

  // Left side: HP and MP
  renderer.drawText(UI_10_FONT_ID, 4, STATUS_Y, hpBuf, true, EpdFontFamily::BOLD);
  int hpWidth = renderer.getTextWidth(UI_10_FONT_ID, hpBuf, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, 4 + hpWidth + 10, STATUS_Y, mpBuf);

  // Right side: Dungeon level and Character level
  int lvlWidth = renderer.getTextWidth(UI_10_FONT_ID, lvlBuf);
  int depthWidth = renderer.getTextWidth(UI_10_FONT_ID, depthBuf);
  renderer.drawText(UI_10_FONT_ID, screenW - lvlWidth - 4, STATUS_Y, lvlBuf);
  renderer.drawText(UI_10_FONT_ID, screenW - lvlWidth - depthWidth - 14, STATUS_Y, depthBuf);
}

// --- Viewport ---

void GameRenderer::drawViewport(GfxRenderer& renderer, const game::Tile* tiles, const uint8_t* fogOfWar,
                                const game::Monster* monsters, uint8_t monsterCount, const game::Item* items,
                                uint8_t itemCount, const bool* visible) const {
  const auto& p = GAME_STATE.player;

  // Calculate viewport origin (center on player, clamped to map bounds)
  int viewX = p.x - viewCols / 2;
  int viewY = p.y - viewRows / 2;
  viewX = std::max(0, std::min(viewX, game::MAP_WIDTH - viewCols));
  viewY = std::max(0, std::min(viewY, game::MAP_HEIGHT - viewRows));

  // Draw each cell in the viewport
  for (int row = 0; row < viewRows; row++) {
    int mapY = viewY + row;
    if (mapY < 0 || mapY >= game::MAP_HEIGHT) continue;

    int screenCellY = VIEWPORT_Y + row * CELL_H;

    for (int col = 0; col < viewCols; col++) {
      int mapX = viewX + col;
      if (mapX < 0 || mapX >= game::MAP_WIDTH) continue;

      int mapIdx = mapY * game::MAP_WIDTH + mapX;
      int screenCellX = gridOffsetX + col * CELL_W;

      bool isExplored = game::fogIsExplored(fogOfWar, mapX, mapY);
      bool isVisible = visible[mapIdx];

      if (!isExplored && !isVisible) {
        // Unseen tile — leave white (cleared screen)
        continue;
      }

      // Determine what glyph to show
      char glyph = game::tileGlyph(tiles[mapIdx]);

      // If currently visible, check for monsters and items on this tile
      if (isVisible) {
        // Player
        if (mapX == p.x && mapY == p.y) {
          glyph = '@';
        } else {
          // Check monsters
          for (uint8_t m = 0; m < monsterCount; m++) {
            if (monsters[m].x == mapX && monsters[m].y == mapY && monsters[m].hp > 0) {
              glyph = game::MONSTER_DEFS[monsters[m].type].glyph;
              break;
            }
          }
          // Check items (only if no monster shown)
          if (glyph == game::tileGlyph(tiles[mapIdx])) {
            for (uint8_t i = 0; i < itemCount; i++) {
              if (items[i].x == mapX && items[i].y == mapY) {
                glyph = game::itemGlyph(items[i].type);
                break;
              }
            }
          }
        }
      }

      drawCell(renderer, screenCellX, screenCellY, glyph, isVisible, isExplored);
    }
  }
}

void GameRenderer::drawCell(GfxRenderer& renderer, int screenX, int screenY, char glyph, bool isVisible,
                            bool isExplored) const {
  if (isVisible) {
    // Visible: black character on white background
    char buf[2] = {glyph, '\0'};
    // Center character horizontally in cell
    int charW = renderer.getTextWidth(UI_10_FONT_ID, buf);
    int offsetX = (CELL_W - charW) / 2;
    renderer.drawText(UI_10_FONT_ID, screenX + offsetX, screenY - 2, buf, true);
  } else if (isExplored) {
    // Remembered: gray dithered character
    renderer.fillRectDither(screenX, screenY, CELL_W, CELL_H, LightGray);
    char buf[2] = {glyph, '\0'};
    int charW = renderer.getTextWidth(UI_10_FONT_ID, buf);
    int offsetX = (CELL_W - charW) / 2;
    renderer.drawText(UI_10_FONT_ID, screenX + offsetX, screenY - 2, buf, true);
  }
}

// --- Message Log ---

void GameRenderer::drawMessages(GfxRenderer& renderer) const {
  // Show 2 most recent messages
  const auto& msg0 = GAME_STATE.getMessage(0);
  const auto& msg1 = GAME_STATE.getMessage(1);

  if (!msg1.empty()) {
    renderer.drawText(SMALL_FONT_ID, 4, messageY + 1, msg1.c_str());
  }
  if (!msg0.empty()) {
    renderer.drawText(SMALL_FONT_ID, 4, messageY + 19, msg0.c_str());
  }
}

// --- Button Hints ---

void GameRenderer::drawHints(GfxRenderer& renderer) const {
  // Simple text hints for the 4 front buttons
  renderer.drawText(SMALL_FONT_ID, 8, hintsY + 6, "Menu");
  renderer.drawText(SMALL_FONT_ID, 120, hintsY + 6, "Action");
  renderer.drawText(SMALL_FONT_ID, 260, hintsY + 6, "Left");
  renderer.drawText(SMALL_FONT_ID, 390, hintsY + 6, "Right");
}
