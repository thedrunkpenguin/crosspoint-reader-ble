#include "SudokuActivity.h"

#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

bool SudokuActivity::isValid(const std::array<std::array<uint8_t, 9>, 9>& b, int r, int c, int v) const {
  for (int i = 0; i < 9; ++i) {
    if (b[r][i] == v || b[i][c] == v) {
      return false;
    }
  }
  const int br = (r / 3) * 3;
  const int bc = (c / 3) * 3;
  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < 3; ++x) {
      if (b[br + y][bc + x] == v) {
        return false;
      }
    }
  }
  return true;
}

bool SudokuActivity::solve(std::array<std::array<uint8_t, 9>, 9>& b, int idx) const {
  if (idx == 81) {
    return true;
  }
  const int r = idx / 9;
  const int c = idx % 9;
  if (b[r][c] != 0) {
    return solve(b, idx + 1);
  }

  int nums[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  for (int i = 0; i < 9; ++i) {
    const int j = random(i, 9);
    std::swap(nums[i], nums[j]);
  }

  for (int i = 0; i < 9; ++i) {
    const int v = nums[i];
    if (isValid(b, r, c, v)) {
      b[r][c] = static_cast<uint8_t>(v);
      if (solve(b, idx + 1)) {
        return true;
      }
      b[r][c] = 0;
    }
  }
  return false;
}

void SudokuActivity::makePuzzle() {
  for (auto& row : puzzle) {
    row.fill(0);
  }
  solve(puzzle, 0);
  solution = puzzle;

  int removeCount = 45;
  while (removeCount > 0) {
    const int r = random(0, 9);
    const int c = random(0, 9);
    if (puzzle[r][c] != 0) {
      puzzle[r][c] = 0;
      removeCount--;
    }
  }

  for (int r = 0; r < 9; ++r) {
    for (int c = 0; c < 9; ++c) {
      fixed[r][c] = puzzle[r][c] != 0;
    }
  }
}

void SudokuActivity::onEnter() {
  Activity::onEnter();
  makePuzzle();
  cursorR = 0;
  cursorC = 0;
  completed = false;
  requestUpdate();
}

void SudokuActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  if (completed) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      onEnter();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    cursorC = std::max(0, cursorC - 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    cursorC = std::min(8, cursorC + 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    cursorR = std::max(0, cursorR - 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    cursorR = std::min(8, cursorR + 1);
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && !fixed[cursorR][cursorC]) {
    uint8_t& cell = puzzle[cursorR][cursorC];
    cell = static_cast<uint8_t>((cell + 1) % 10);

    bool ok = true;
    for (int r = 0; r < 9; ++r) {
      for (int c = 0; c < 9; ++c) {
        if (puzzle[r][c] != solution[r][c]) {
          ok = false;
        }
      }
    }
    completed = ok;
    requestUpdate();
  }
}

void SudokuActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SUDOKU));

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 8;
  const int bottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int area = std::min(pageWidth - 20, bottom - top - 20);
  const int cell = area / 9;
  const int size = cell * 9;
  const int x0 = (pageWidth - size) / 2;
  const int y0 = top;

  for (int i = 0; i <= 9; ++i) {
    const int lw = (i % 3 == 0) ? 2 : 1;
    renderer.drawLine(x0 + i * cell, y0, x0 + i * cell, y0 + size, lw, true);
    renderer.drawLine(x0, y0 + i * cell, x0 + size, y0 + i * cell, lw, true);
  }

  for (int r = 0; r < 9; ++r) {
    for (int c = 0; c < 9; ++c) {
      const uint8_t v = puzzle[r][c];
      if (!v) continue;
      char s[2] = {static_cast<char>('0' + v), 0};
      const int w = renderer.getTextWidth(UI_12_FONT_ID, s, fixed[r][c] ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      renderer.drawText(UI_12_FONT_ID, x0 + c * cell + (cell - w) / 2,
                        y0 + r * cell + (cell - renderer.getLineHeight(UI_12_FONT_ID)) / 2, s, true,
                        fixed[r][c] ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    }
  }

  renderer.drawRect(x0 + cursorC * cell, y0 + cursorR * cell, cell, cell, 2, true);

  if (completed) {
    renderer.drawCenteredText(UI_12_FONT_ID, y0 + size + 8, tr(STR_SUDOKU_COMPLETE), true, EpdFontFamily::BOLD);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
