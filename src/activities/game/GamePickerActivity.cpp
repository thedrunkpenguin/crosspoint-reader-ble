#include "GamePickerActivity.h"

#include <Arduino.h>
#include <I18n.h>

#include <vector>

#include "CaroActivity.h"
#include "ChessActivity.h"
#include "GameOfLifeActivity.h"
#include "MappedInputManager.h"
#include "MazeGameActivity.h"
#include "MinesweeperActivity.h"
#include "SnakeActivity.h"
#include "SudokuActivity.h"
#include "TwentyFortyEightActivity.h"
#include "components/UITheme.h"

int GamePickerActivity::gameCount() const {
  return 9;
}

void GamePickerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  selectorIndex = 0;
  ignoreBackUntilMs = millis() + 250;
  requestUpdate();
}

void GamePickerActivity::openSelectedGame() {
  auto onExitToPicker = [this]() {
    exitActivity();
    ignoreBackUntilMs = millis() + 250;
    requestUpdate();
  };

  switch (selectorIndex) {
    case 0:
      if (onStartDeepMines) {
        onStartDeepMines();
      }
      break;
    case 1:
      enterNewActivity(new SnakeActivity(renderer, mappedInput, onExitToPicker));
      break;
    case 2:
      enterNewActivity(new TwentyFortyEightActivity(renderer, mappedInput, onExitToPicker));
      break;
    case 3:
      enterNewActivity(new MazeGameActivity(renderer, mappedInput, onExitToPicker));
      break;
    case 4:
      enterNewActivity(new GameOfLifeActivity(renderer, mappedInput, onExitToPicker));
      break;
    case 5:
      enterNewActivity(new ChessActivity(renderer, mappedInput, onExitToPicker));
      break;
    case 6:
      enterNewActivity(new CaroActivity(renderer, mappedInput, onExitToPicker));
      break;
    case 7:
      enterNewActivity(new SudokuActivity(renderer, mappedInput, onExitToPicker));
      break;
    case 8:
      enterNewActivity(new MinesweeperActivity(renderer, mappedInput, onExitToPicker));
      break;
    default:
      break;
  }
}

void GamePickerActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  const int count = gameCount();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (millis() < ignoreBackUntilMs) {
      return;
    }
    if (onBack) {
      onBack();
    }
    return;
  }

    if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    selectorIndex = (selectorIndex + 1) % count;
    requestUpdate();
  }

    if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    selectorIndex = (selectorIndex - 1 + count) % count;
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelectedGame();
  }
}

void GamePickerActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_GAMES));

  static const StrId gameIds[] = {
      StrId::STR_DEEP_MINES, StrId::STR_SNAKE, StrId::STR_2048, StrId::STR_MAZE, StrId::STR_GAME_OF_LIFE,
      StrId::STR_CHESS, StrId::STR_CARO, StrId::STR_SUDOKU, StrId::STR_MINESWEEPER};

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const std::function<std::string(int)> rowTitle = [&](int index) {
    return std::string(I18N.get(gameIds[index]));
  };
  const std::function<std::string(int)> rowSubtitle = [](int) { return std::string(); };
  const std::function<UIIcon(int)> rowIcon = [](int) { return UIIcon::Book; };

  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, gameCount(), selectorIndex, rowTitle,
               rowSubtitle, rowIcon, nullptr, false);

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
