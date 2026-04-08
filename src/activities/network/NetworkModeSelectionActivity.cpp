#include "NetworkModeSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int MENU_ITEM_COUNT = 4;
}  // namespace

void NetworkModeSelectionActivity::onEnter() {
  Activity::onEnter();

  // Reset selection
  selectedIndex = 0;

  // Trigger first update
  requestUpdate();
}

void NetworkModeSelectionActivity::onExit() { Activity::onExit(); }

void NetworkModeSelectionActivity::loop() {
  // Handle back button - cancel
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onCancel();
    return;
  }

  // Handle confirm button - select current option
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    NetworkMode mode = NetworkMode::JOIN_NETWORK;
    if (selectedIndex == 1) {
      mode = NetworkMode::CONNECT_CALIBRE;
    } else if (selectedIndex == 2) {
      mode = NetworkMode::CREATE_HOTSPOT;
    } else if (selectedIndex == 3) {
      mode = NetworkMode::SUBREDDIT_READER;
    }
    onModeSelected(mode);
    return;
  }

  // Handle navigation
  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, MENU_ITEM_COUNT);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, MENU_ITEM_COUNT);
    requestUpdate();
  });
}

void NetworkModeSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_NETWORK));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(MENU_ITEM_COUNT), selectedIndex,
      [](int index) {
        switch (index) {
          case 0:
            return std::string(I18N.get(StrId::STR_JOIN_NETWORK));
          case 1:
            return std::string(I18N.get(StrId::STR_CALIBRE_WIRELESS));
          case 2:
            return std::string(I18N.get(StrId::STR_CREATE_HOTSPOT));
          case 3:
            return std::string(I18N.get(StrId::STR_SUBREDDIT_READER));
          default:
            return std::string();
        }
      },
      [](int index) {
        switch (index) {
          case 0:
            return std::string(I18N.get(StrId::STR_JOIN_DESC));
          case 1:
            return std::string(I18N.get(StrId::STR_CALIBRE_DESC));
          case 2:
            return std::string(I18N.get(StrId::STR_HOTSPOT_DESC));
          case 3:
            return std::string(I18N.get(StrId::STR_SUBREDDIT_DESC));
          default:
            return std::string();
        }
      },
      [](int index) {
        switch (index) {
          case 0:
            return UIIcon::Wifi;
          case 1:
            return UIIcon::Library;
          case 2:
            return UIIcon::Hotspot;
          case 3:
          default:
            return UIIcon::Book;
        }
      });

  // Draw help text at bottom
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void NetworkModeSelectionActivity::onModeSelected(NetworkMode mode) {
  setResult(NetworkModeResult{mode});
  finish();
}

void NetworkModeSelectionActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
