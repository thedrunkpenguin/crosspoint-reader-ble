#pragma once

#include <BluetoothHIDManager.h>
#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;

inline void applyOrientation(GfxRenderer& renderer, const uint8_t orientation) {
  switch (orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

struct PageTurnResult {
  bool prev;
  bool next;
};

inline bool allowLongPressChapterSkip() {
  // BLE page-turn remotes can report delayed or synthetic release frames, which
  // makes release-driven page turns look ghostier than local buttons. Treat
  // recent BLE input as page-turn-only and keep chapter-skip semantics for the
  // local hardware buttons.
  return SETTINGS.longPressChapterSkip && !BluetoothHIDManager::getInstance().hadRecentFree2Input();
}

inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  const bool usePress = !allowLongPressChapterSkip();
  const bool prev = usePress ? (input.wasPressed(MappedInputManager::Button::PageBack) ||
                                input.wasPressed(MappedInputManager::Button::Left))
                             : (input.wasReleased(MappedInputManager::Button::PageBack) ||
                                input.wasReleased(MappedInputManager::Button::Left));
  const bool powerTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                         input.wasReleased(MappedInputManager::Button::Power);
  const bool next = usePress ? (input.wasPressed(MappedInputManager::Button::PageForward) || powerTurn ||
                                input.wasPressed(MappedInputManager::Button::Right))
                             : (input.wasReleased(MappedInputManager::Button::PageForward) || powerTurn ||
                                input.wasReleased(MappedInputManager::Button::Right));
  return {prev, next};
}

inline bool shouldStrengthenBleStatusCounterRefresh(int pagesUntilFullRefresh) {
  return pagesUntilFullRefresh > 1 && (SETTINGS.statusBarChapterPageCount || SETTINGS.statusBarBookProgressPercentage) &&
         BluetoothHIDManager::getInstance().hadRecentFree2Input();
}

inline void refreshStatusBarCounterWindow(const GfxRenderer& renderer, float bookProgress, int currentPage,
                                          int pageCount) {
  if (!(SETTINGS.statusBarChapterPageCount || SETTINGS.statusBarBookProgressPercentage)) {
    return;
  }

  auto metrics = UITheme::getInstance().getMetrics();
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);

  char progressStr[32] = {};
  if (SETTINGS.statusBarBookProgressPercentage && SETTINGS.statusBarChapterPageCount) {
    snprintf(progressStr, sizeof(progressStr), "%d/%d  %.0f%%", currentPage, pageCount, bookProgress);
  } else if (SETTINGS.statusBarBookProgressPercentage) {
    snprintf(progressStr, sizeof(progressStr), "%.0f%%", bookProgress);
  } else {
    snprintf(progressStr, sizeof(progressStr), "%d/%d", currentPage, pageCount);
  }

  const int progressTextWidth = renderer.getTextWidth(SMALL_FONT_ID, progressStr);
  if (progressTextWidth <= 0) {
    return;
  }

  const int statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  const int progressTextX =
      renderer.getScreenWidth() - metrics.statusBarHorizontalMargin - orientedMarginRight - progressTextWidth;
  int refreshX = progressTextX - 12;
  int refreshY = renderer.getScreenHeight() - statusBarHeight - orientedMarginBottom - 8;
  if (refreshY < 0) {
    refreshY = 0;
  }

  refreshX &= ~0x7;  // byte-align for displayWindow
  if (refreshX < 0) {
    refreshX = 0;
  }

  int refreshWidth = renderer.getScreenWidth() - refreshX - orientedMarginRight;
  refreshWidth = (refreshWidth + 7) & ~0x7;
  if (refreshX + refreshWidth > renderer.getScreenWidth()) {
    refreshWidth = renderer.getScreenWidth() - refreshX;
    refreshWidth &= ~0x7;
  }

  const int refreshHeight = renderer.getScreenHeight() - refreshY;
  if (refreshWidth <= 0 || refreshHeight <= 0) {
    return;
  }

  renderer.displayWindow(refreshX, refreshY, refreshWidth, refreshHeight, HalDisplay::FAST_REFRESH);
}

inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }
}

// Grayscale anti-aliasing pass. Renders content twice (LSB + MSB) to build
// the grayscale buffer. Only the content callback is re-rendered — status bars
// and other overlays should be drawn before calling this.
// Kept as a template to avoid std::function overhead; instantiated once per reader type.
template <typename RenderFn>
void renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("READER", "Failed to store BW buffer for anti-aliasing");
    return;
  }

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderFn();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);

  renderer.restoreBwBuffer();
}

}  // namespace ReaderUtils
