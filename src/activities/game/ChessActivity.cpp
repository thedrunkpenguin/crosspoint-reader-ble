#include "ChessActivity.h"

#include <I18n.h>

#include <algorithm>
#include <cctype>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ChessActivity::resetBoard() {
  const char* init[8] = {"rnbqkbnr", "pppppppp", "........", "........", "........", "........", "PPPPPPPP", "RNBQKBNR"};
  for (int r = 0; r < 8; ++r) {
    for (int c = 0; c < 8; ++c) {
      board[r][c] = init[r][c];
    }
  }
  whiteTurn = true;
  selected = false;
  gameOver = false;
  winner = ' ';
}

bool ChessActivity::isWhite(char p) const {
  return p >= 'A' && p <= 'Z';
}

bool ChessActivity::isBlack(char p) const {
  return p >= 'a' && p <= 'z';
}

bool ChessActivity::inBounds(int r, int c) const {
  return r >= 0 && c >= 0 && r < 8 && c < 8;
}

bool ChessActivity::pathClear(int r1, int c1, int r2, int c2) const {
  const int dr = (r2 > r1) ? 1 : (r2 < r1 ? -1 : 0);
  const int dc = (c2 > c1) ? 1 : (c2 < c1 ? -1 : 0);
  r1 += dr;
  c1 += dc;
  while (r1 != r2 || c1 != c2) {
    if (board[r1][c1] != '.') {
      return false;
    }
    r1 += dr;
    c1 += dc;
  }
  return true;
}

bool ChessActivity::isValidMove(int fr, int fc, int tr, int tc) const {
  if (!inBounds(fr, fc) || !inBounds(tr, tc) || (fr == tr && fc == tc)) {
    return false;
  }

  char p = board[fr][fc];
  char t = board[tr][tc];
  if (p == '.') {
    return false;
  }

  if (isWhite(p) && isWhite(t)) {
    return false;
  }
  if (isBlack(p) && isBlack(t)) {
    return false;
  }

  const int dr = tr - fr;
  const int dc = tc - fc;
  const int adr = std::abs(dr);
  const int adc = std::abs(dc);
  const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(p)));

  if (u == 'P') {
    const int forward = isWhite(p) ? -1 : 1;
    const int start = isWhite(p) ? 6 : 1;
    if (dc == 0 && t == '.' && dr == forward) {
      return true;
    }
    if (dc == 0 && t == '.' && fr == start && dr == 2 * forward && board[fr + forward][fc] == '.') {
      return true;
    }
    if (adc == 1 && dr == forward && t != '.') {
      return true;
    }
    return false;
  }

  if (u == 'N') {
    return (adr == 1 && adc == 2) || (adr == 2 && adc == 1);
  }

  if (u == 'B') {
    return adr == adc && pathClear(fr, fc, tr, tc);
  }

  if (u == 'R') {
    return (fr == tr || fc == tc) && pathClear(fr, fc, tr, tc);
  }

  if (u == 'Q') {
    return ((adr == adc) || fr == tr || fc == tc) && pathClear(fr, fc, tr, tc);
  }

  if (u == 'K') {
    return adr <= 1 && adc <= 1;
  }

  return false;
}

std::vector<ChessActivity::Move> ChessActivity::collectMoves(bool whiteSide) const {
  std::vector<Move> moves;
  for (int r = 0; r < 8; ++r) {
    for (int c = 0; c < 8; ++c) {
      char p = board[r][c];
      if (p == '.') {
        continue;
      }
      if (whiteSide && !isWhite(p)) {
        continue;
      }
      if (!whiteSide && !isBlack(p)) {
        continue;
      }
      for (int tr = 0; tr < 8; ++tr) {
        for (int tc = 0; tc < 8; ++tc) {
          if (isValidMove(r, c, tr, tc)) {
            moves.push_back({r, c, tr, tc});
          }
        }
      }
    }
  }
  return moves;
}

void ChessActivity::updateGameOver() {
  bool whiteKing = false;
  bool blackKing = false;
  for (const auto& row : board) {
    for (char p : row) {
      if (p == 'K') {
        whiteKing = true;
      }
      if (p == 'k') {
        blackKing = true;
      }
    }
  }
  if (!whiteKing) {
    gameOver = true;
    winner = 'b';
  } else if (!blackKing) {
    gameOver = true;
    winner = 'w';
  }
}

void ChessActivity::aiMove() {
  if (gameOver) {
    return;
  }
  auto moves = collectMoves(false);
  if (moves.empty()) {
    gameOver = true;
    winner = 'w';
    return;
  }
  int bestIdx = random(0, static_cast<int>(moves.size()));
  for (size_t i = 0; i < moves.size(); ++i) {
    const auto& m = moves[i];
    if (board[m.toR][m.toC] != '.') {
      bestIdx = static_cast<int>(i);
      break;
    }
  }

  const auto& move = moves[bestIdx];
  board[move.toR][move.toC] = board[move.fromR][move.fromC];
  board[move.fromR][move.fromC] = '.';
  whiteTurn = true;
  updateGameOver();
}

void ChessActivity::onEnter() {
  Activity::onEnter();
  resetBoard();
  cursorR = 6;
  cursorC = 4;
  requestUpdate();
}

void ChessActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  if (gameOver) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      onEnter();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    cursorC = std::max(0, cursorC - 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    cursorC = std::min(7, cursorC + 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    cursorR = std::max(0, cursorR - 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    cursorR = std::min(7, cursorR + 1);
    requestUpdate();
  }

  if (!whiteTurn) {
    aiMove();
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!selected) {
      if (isWhite(board[cursorR][cursorC])) {
        selected = true;
        selectedR = cursorR;
        selectedC = cursorC;
        requestUpdate();
      }
      return;
    }

    if (isValidMove(selectedR, selectedC, cursorR, cursorC)) {
      board[cursorR][cursorC] = board[selectedR][selectedC];
      board[selectedR][selectedC] = '.';
      selected = false;
      whiteTurn = false;
      updateGameOver();
      requestUpdate();
    } else {
      selected = false;
      requestUpdate();
    }
  }
}

void ChessActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CHESS));

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 6;
  const int bottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int boardSize = std::min(pageWidth - 40, bottom - top - 20);
  const int cell = boardSize / 8;
  const int size = cell * 8;
  const int x0 = (pageWidth - size) / 2;
  const int y0 = top;

  for (int r = 0; r < 8; ++r) {
    for (int c = 0; c < 8; ++c) {
      const int x = x0 + c * cell;
      const int y = y0 + r * cell;
      const bool dark = ((r + c) % 2) == 1;
      renderer.fillRect(x, y, cell, cell, !dark);
      renderer.drawRect(x, y, cell, cell, true);
      char p = board[r][c];
      if (p != '.') {
        char text[2] = {static_cast<char>(std::toupper(static_cast<unsigned char>(p))), 0};
        const bool blackPiece = isBlack(p);
        const int w = renderer.getTextWidth(UI_12_FONT_ID, text, EpdFontFamily::BOLD);
        renderer.drawText(UI_12_FONT_ID, x + (cell - w) / 2, y + (cell - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                          text, !blackPiece, EpdFontFamily::BOLD);
      }
    }
  }

  renderer.drawRect(x0 + cursorC * cell, y0 + cursorR * cell, cell, cell, 2, true);
  if (selected) {
    renderer.drawRect(x0 + selectedC * cell + 2, y0 + selectedR * cell + 2, cell - 4, cell - 4, 2, true);
  }

  if (gameOver) {
    renderer.drawCenteredText(UI_12_FONT_ID, y0 + size + 6,
                              winner == 'w' ? tr(STR_CHESS_WHITE_WINS) : tr(STR_CHESS_BLACK_WINS), true,
                              EpdFontFamily::BOLD);
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, y0 + size + 8,
                              whiteTurn ? tr(STR_CHESS_WHITE_TURN) : tr(STR_CHESS_BLACK_TURN));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CHESS_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
