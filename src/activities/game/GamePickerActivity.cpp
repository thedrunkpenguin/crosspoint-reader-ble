#include "GamePickerActivity.h"

#include <Arduino.h>
#include <I18n.h>

#include <vector>

#include "MappedInputManager.h"
#include "SolitaireActivity.h"
#include "components/UITheme.h"

int GamePickerActivity::gameCount() const {
  return 2;
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
      enterNewActivity(new SolitaireActivity(renderer, mappedInput, onExitToPicker));
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

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const std::function<std::string(int)> rowTitle = [&](int index) {
    switch (index) {
      case 0:
        return std::string(I18N.get(StrId::STR_DEEP_MINES));
      case 1:
        return std::string("Solitaire");
      default:
        return std::string();
    }
  };
  const std::function<std::string(int)> rowSubtitle = [](int index) {
    if (index == 1) {
      return std::string("Golf Solitaire");
    }
    return std::string();
  };
  const std::function<UIIcon(int)> rowIcon = [](int) { return UIIcon::Book; };

  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, gameCount(), selectorIndex, rowTitle,
               rowSubtitle, rowIcon, nullptr, false);

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
