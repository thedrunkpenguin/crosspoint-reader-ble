#include "EpubReaderActivity.h"

#include <Epub/Page.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include <BluetoothHIDManager.h>
#include "activities/settings/BluetoothSettingsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "pet/PetManager.h"

namespace {
// pagesPerRefresh now comes from SETTINGS.getRefreshFrequency()
constexpr unsigned long skipChapterMs = 700;
constexpr unsigned long goHomeMs = 1000;
constexpr int statusBarMargin = 19;
constexpr int progressBarMarginTop = 1;

int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

// Apply the logical reader orientation to the renderer.
// This centralizes orientation mapping so we don't duplicate switch logic elsewhere.
void applyReaderOrientation(GfxRenderer& renderer, const uint8_t orientation) {
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

}  // namespace

void EpubReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (!epub) {
    return;
  }

  // Configure screen orientation based on settings
  // NOTE: This affects layout math and must be applied before any render calls.
  applyReaderOrientation(renderer, SETTINGS.orientation);

  epub->setupCacheDir();

  const std::string cacheDir = epub->getCachePath();
  const std::string progressPath = cacheDir + "/progress.bin";
  const std::string progressBackupPath = cacheDir + "/progress.bak";

  auto loadProgressFile = [&](const std::string& path, bool& validOut) -> bool {
    validOut = false;
    FsFile f;
    if (!Storage.openFileForRead("ERS", path, f)) {
      return false;
    }

    uint8_t data[7] = {0};
    const int dataSize = f.read(data, 7);
    f.close();

    if (dataSize != 4 && dataSize != 6 && dataSize != 7) {
      return true;
    }

    const int loadedSpine = data[0] + (data[1] << 8);
    const int loadedPage = data[2] + (data[3] << 8);
    int loadedPageCount = 0;
    if (dataSize >= 6) {
      loadedPageCount = data[4] + (data[5] << 8);
    }

    if (loadedPage == UINT16_MAX) {
      return true;
    }

    if (dataSize == 7) {
      const int persistedBookPercent = data[6];
      if (persistedBookPercent < 0 || persistedBookPercent > 100) {
        return true;
      }
    }

    const int spineItemsCount = epub->getSpineItemsCount();
    if (spineItemsCount > 0 && (loadedSpine < 0 || loadedSpine >= spineItemsCount)) {
      return true;
    }

    currentSpineIndex = loadedSpine;
    nextPageNumber = loadedPage;
    cachedSpineIndex = loadedSpine;
    cachedChapterTotalPageCount = loadedPageCount;
    validOut = true;
    LOG_DBG("ERS", "Loaded cache: %d, %d", currentSpineIndex, nextPageNumber);
    return true;
  };

  bool primaryValid = false;
  const bool primaryPresent = loadProgressFile(progressPath, primaryValid);
  bool backupValid = false;
  bool backupPresent = false;

  if (!primaryValid) {
    backupPresent = loadProgressFile(progressBackupPath, backupValid);
    if (backupValid) {
      LOG_DBG("ERS", "Recovered progress from backup cache");
    }
  }

  if (primaryPresent && !primaryValid) {
    // Prevent repeatedly loading the same corrupted cache.
    Storage.remove(progressPath.c_str());
  }

  if (!primaryValid && backupPresent && !backupValid) {
    Storage.remove(progressBackupPath.c_str());
  }

  // We may want a better condition to detect if we are opening for the first time.
  // This will trigger if the book is re-opened at Chapter 0.
  if (currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", textSpineIndex);
    }
  }

  // Save current epub as last opened epub and add to recent books
  APP_STATE.openEpubPath = epub->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath());

  // Trigger first update
  requestUpdate();
}

void EpubReaderActivity::onExit() {
  ActivityWithSubactivity::onExit();

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  chapterSkipConsumedForHold = false;
  lastTrackedSpineIndex = -1;
  lastTrackedPageIndex = -1;
  lastBookProgressPercent = 0;
  bookFinishedReported = false;
  section.reset();
  epub.reset();
}

void EpubReaderActivity::loop() {
  // Pass input responsibility to sub activity if exists
  if (subActivity) {
    subActivity->loop();
    // Deferred exit: process after subActivity->loop() returns to avoid use-after-free
    if (pendingSubactivityExit) {
      pendingSubactivityExit = false;
      exitActivity();
      requestUpdate();
      skipNextButtonCheck = true;  // Skip button processing to ignore stale events
    }
    // Deferred go home: process after subActivity->loop() returns to avoid race condition
    if (pendingGoHome) {
      pendingGoHome = false;
      exitActivity();
      if (onGoHome) {
        onGoHome();
      }
      return;  // Don't access 'this' after callback
    }
    return;
  }

  // Handle pending go home when no subactivity (e.g., from long press back)
  if (pendingGoHome) {
    pendingGoHome = false;
    if (onGoHome) {
      onGoHome();
    }
    return;  // Don't access 'this' after callback
  }

  // Skip button processing after returning from subactivity
  // This prevents stale button release events from triggering actions
  // We wait until: (1) all relevant buttons are released, AND (2) wasReleased events have been cleared
  if (skipNextButtonCheck) {
    const bool confirmCleared = !mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
                                !mappedInput.wasReleased(MappedInputManager::Button::Confirm);
    const bool backCleared = !mappedInput.isPressed(MappedInputManager::Button::Back) &&
                             !mappedInput.wasReleased(MappedInputManager::Button::Back);
    if (confirmCleared && backCleared) {
      skipNextButtonCheck = false;
    }
    return;
  }

  // Enter reader menu activity.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int currentPage = section ? section->currentPage + 1 : 0;
    const int totalPages = section ? section->pageCount : 0;
    const int bookProgressPercent = clampPercent(lastBookProgressPercent);
    exitActivity();
    enterNewActivity(new EpubReaderMenuActivity(
        this->renderer, this->mappedInput, epub->getTitle(), currentPage, totalPages, bookProgressPercent,
        SETTINGS.orientation, [this](const uint8_t orientation) { onReaderMenuBack(orientation); },
        [this](EpubReaderMenuActivity::MenuAction action) { onReaderMenuConfirm(action); }));
  }

  // Long press BACK (1s+) goes to file selection
  const unsigned long backHeldMs = mappedInput.getHeldTime(MappedInputManager::Button::Back);
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && backHeldMs >= goHomeMs) {
    onGoBack();
    return;
  }

  // Short press BACK goes directly to home
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && backHeldMs < goHomeMs) {
    onGoHome();
    return;
  }

  // Free2 stays page-turn-only. Its rolling HID reports are fine for paging but not
  // reliable enough for per-book chapter-skip semantics.
  bool allowLongPressChapterSkip = SETTINGS.longPressChapterSkip;
  try {
    allowLongPressChapterSkip = SETTINGS.longPressChapterSkip &&
                                !BluetoothHIDManager::getInstance().hadRecentFree2Input();
  } catch (...) {
    allowLongPressChapterSkip = SETTINGS.longPressChapterSkip;
  }

  // When long-press chapter skip is disabled, turn pages on press instead of release.
  const bool usePressForPageTurn = !allowLongPressChapterSkip;
  const bool prevTriggered = usePressForPageTurn ? (mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
                                                    mappedInput.wasPressed(MappedInputManager::Button::Left))
                                                 : (mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                                                    mappedInput.wasReleased(MappedInputManager::Button::Left));
  const bool powerPageTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                             mappedInput.wasReleased(MappedInputManager::Button::Power);
  const bool nextTriggered = usePressForPageTurn
                                 ? (mappedInput.wasPressed(MappedInputManager::Button::PageForward) || powerPageTurn ||
                                    mappedInput.wasPressed(MappedInputManager::Button::Right))
                                 : (mappedInput.wasReleased(MappedInputManager::Button::PageForward) || powerPageTurn ||
                                    mappedInput.wasReleased(MappedInputManager::Button::Right));
  const bool prevHeldNow = mappedInput.isPressed(MappedInputManager::Button::PageBack) ||
                           mappedInput.isPressed(MappedInputManager::Button::Left);
  const bool nextHeldNow = mappedInput.isPressed(MappedInputManager::Button::PageForward) ||
                           mappedInput.isPressed(MappedInputManager::Button::Right);

  if (chapterSkipConsumedForHold) {
    if (!prevHeldNow && !nextHeldNow && !prevTriggered && !nextTriggered) {
      chapterSkipConsumedForHold = false;
    }
    return;
  }

  const unsigned long prevHeldMs = std::max(mappedInput.getHeldTime(MappedInputManager::Button::PageBack),
                                            mappedInput.getHeldTime(MappedInputManager::Button::Left));
  const unsigned long nextHeldMs = std::max(mappedInput.getHeldTime(MappedInputManager::Button::PageForward),
                                            mappedInput.getHeldTime(MappedInputManager::Button::Right));

  // any botton press when at end of the book goes back to the last page
  if (currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount()) {
    currentSpineIndex = epub->getSpineItemsCount() - 1;
    nextPageNumber = UINT16_MAX;
    requestUpdate();
    return;
  }

  const bool skipChapterFromHold = allowLongPressChapterSkip &&
                                   ((prevHeldNow && prevHeldMs > skipChapterMs) ||
                                    (nextHeldNow && nextHeldMs > skipChapterMs));

  if (skipChapterFromHold) {
    chapterSkipConsumedForHold = true;
    {
      RenderLock lock(*this);
      nextPageNumber = 0;
      currentSpineIndex = nextHeldNow ? currentSpineIndex + 1 : currentSpineIndex - 1;
      section.reset();
    }
    requestUpdate();
    return;
  }

  if (!prevTriggered && !nextTriggered) {
    return;
  }

  const bool skipChapter = allowLongPressChapterSkip && ((prevTriggered ? prevHeldMs : nextHeldMs) > skipChapterMs);

  if (skipChapter) {
    // We don't want to delete the section mid-render, so grab the semaphore
    {
      RenderLock lock(*this);
      nextPageNumber = 0;
      currentSpineIndex = nextTriggered ? currentSpineIndex + 1 : currentSpineIndex - 1;
      section.reset();
    }
    requestUpdate();
    return;
  }

  // No current section, attempt to rerender the book
  if (!section) {
    requestUpdate();
    return;
  }

  if (prevTriggered) {
    if (section->currentPage > 0) {
      section->currentPage--;
    } else if (currentSpineIndex > 0) {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = UINT16_MAX;
        currentSpineIndex--;
        section.reset();
      }
    }
    requestUpdate();
  } else {
    if (section->currentPage < section->pageCount - 1) {
      section->currentPage++;
    } else {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = 0;
        currentSpineIndex++;
        section.reset();
      }
    }
    requestUpdate();
  }
}

void EpubReaderActivity::onReaderMenuBack(const uint8_t orientation) {
  exitActivity();
  // Apply the user-selected orientation when the menu is dismissed.
  // This ensures the menu can be navigated without immediately rotating the screen.
  applyOrientation(orientation);
  requestUpdate();
}

// Translate an absolute percent into a spine index plus a normalized position
// within that spine so we can jump after the section is loaded.
void EpubReaderActivity::jumpToPercent(int percent) {
  if (!epub) {
    return;
  }

  const size_t bookSize = epub->getBookSize();
  if (bookSize == 0) {
    return;
  }

  // Normalize input to 0-100 to avoid invalid jumps.
  percent = clampPercent(percent);

  // Convert percent into a byte-like absolute position across the spine sizes.
  // Use an overflow-safe computation: (bookSize / 100) * percent + (bookSize % 100) * percent / 100
  size_t targetSize =
      (bookSize / 100) * static_cast<size_t>(percent) + (bookSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) {
    // Ensure the final percent lands inside the last spine item.
    targetSize = bookSize - 1;
  }

  const int spineCount = epub->getSpineItemsCount();
  if (spineCount == 0) {
    return;
  }

  int targetSpineIndex = spineCount - 1;
  size_t prevCumulative = 0;

  for (int i = 0; i < spineCount; i++) {
    const size_t cumulative = epub->getCumulativeSpineItemSize(i);
    if (targetSize <= cumulative) {
      // Found the spine item containing the absolute position.
      targetSpineIndex = i;
      prevCumulative = (i > 0) ? epub->getCumulativeSpineItemSize(i - 1) : 0;
      break;
    }
  }

  const size_t cumulative = epub->getCumulativeSpineItemSize(targetSpineIndex);
  const size_t spineSize = (cumulative > prevCumulative) ? (cumulative - prevCumulative) : 0;
  // Store a normalized position within the spine so it can be applied once loaded.
  pendingSpineProgress =
      (spineSize == 0) ? 0.0f : static_cast<float>(targetSize - prevCumulative) / static_cast<float>(spineSize);
  if (pendingSpineProgress < 0.0f) {
    pendingSpineProgress = 0.0f;
  } else if (pendingSpineProgress > 1.0f) {
    pendingSpineProgress = 1.0f;
  }

  // Reset state so render() reloads and repositions on the target spine.
  {
    RenderLock lock(*this);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    pendingPercentJump = true;
    section.reset();
  }
}

void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
  switch (action) {
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      // Calculate values BEFORE we start destroying things
      const int currentP = section ? section->currentPage : 0;
      const int totalP = section ? section->pageCount : 0;
      const int spineIdx = currentSpineIndex;
      const std::string path = epub->getPath();

      // 1. Close the menu
      exitActivity();

      // 2. Open the Chapter Selector
      enterNewActivity(new EpubReaderChapterSelectionActivity(
          this->renderer, this->mappedInput, epub, path, spineIdx, currentP, totalP,
          [this] {
            exitActivity();
            requestUpdate();
          },
          [this](const int newSpineIndex) {
            if (currentSpineIndex != newSpineIndex) {
              currentSpineIndex = newSpineIndex;
              nextPageNumber = 0;
              section.reset();
            }
            exitActivity();
            requestUpdate();
          },
          [this](const int newSpineIndex, const int newPage) {
            if (currentSpineIndex != newSpineIndex || (section && section->currentPage != newPage)) {
              currentSpineIndex = newSpineIndex;
              nextPageNumber = newPage;
              section.reset();
            }
            exitActivity();
            requestUpdate();
          }));

      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT: {
      // Launch the slider-based percent selector and return here on confirm/cancel.
      float bookProgress = 0.0f;
      if (epub && epub->getBookSize() > 0 && section && section->pageCount > 0) {
        const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
        bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
      }
      const int initialPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
      exitActivity();
      enterNewActivity(new EpubReaderPercentSelectionActivity(
          renderer, mappedInput, initialPercent,
          [this](const int percent) {
            // Apply the new position and exit back to the reader.
            jumpToPercent(percent);
            exitActivity();
            requestUpdate();
          },
          [this]() {
            // Cancel selection and return to the reader.
            exitActivity();
            requestUpdate();
          }));
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_HOME: {
      // Defer go home to avoid race condition with display task
      pendingGoHome = true;
      break;
    }
    case EpubReaderMenuActivity::MenuAction::BLUETOOTH: {
      exitActivity();
      enterNewActivity(new BluetoothSettingsActivity(
          renderer, mappedInput,
          [this]() {
            // Return to the reader after Bluetooth settings
            pendingSubactivityExit = true;
          },
          true));
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE: {
      {
        RenderLock lock(*this);
        if (epub) {
          // 2. BACKUP: Read current progress
          // We use the current variables that track our position
          uint16_t backupSpine = currentSpineIndex;
          uint16_t backupPage = section->currentPage;
          uint16_t backupPageCount = section->pageCount;

          section.reset();
          // 3. WIPE: Clear the cache directory
          epub->clearCache();

          // 4. RESTORE: Re-setup the directory and rewrite the progress file
          epub->setupCacheDir();

          saveProgress(backupSpine, backupPage, backupPageCount);
        }
      }
      // Defer go home to avoid race condition with display task
      pendingGoHome = true;
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SYNC: {
      if (KOREADER_STORE.hasCredentials()) {
        const int currentPage = section ? section->currentPage : 0;
        const int totalPages = section ? section->pageCount : 0;
        exitActivity();
        enterNewActivity(new KOReaderSyncActivity(
            renderer, mappedInput, epub, epub->getPath(), currentSpineIndex, currentPage, totalPages,
            [this]() {
              // On cancel - defer exit to avoid use-after-free
              pendingSubactivityExit = true;
            },
            [this](int newSpineIndex, int newPage) {
              // On sync complete - update position and defer exit
              if (currentSpineIndex != newSpineIndex || (section && section->currentPage != newPage)) {
                currentSpineIndex = newSpineIndex;
                nextPageNumber = newPage;
                section.reset();
              }
              pendingSubactivityExit = true;
            }));
      }
      break;
    }
  }
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  // No-op if the selected orientation matches current settings.
  if (SETTINGS.orientation == orientation) {
    return;
  }

  // Preserve current reading position so we can restore after reflow.
  {
    RenderLock lock(*this);
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }

    // Persist the selection so the reader keeps the new orientation on next launch.
    SETTINGS.orientation = orientation;
    SETTINGS.saveToFile();

    // Update renderer orientation to match the new logical coordinate system.
    applyReaderOrientation(renderer, SETTINGS.orientation);

    // Reset section to force re-layout in the new orientation.
    section.reset();
  }
}

// TODO: Failure handling
void EpubReaderActivity::render(Activity::RenderLock&& lock) {
  if (!epub) {
    return;
  }

  // edge case handling for sub-zero spine index
  if (currentSpineIndex < 0) {
    currentSpineIndex = 0;
  }
  // based bounds of book, show end of book screen
  if (currentSpineIndex > epub->getSpineItemsCount()) {
    currentSpineIndex = epub->getSpineItemsCount();
  }

  // Show end of book screen
  if (currentSpineIndex == epub->getSpineItemsCount()) {
    if (!bookFinishedReported) {
      PET_MANAGER.onBookFinished();
      bookFinishedReported = true;
    }
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_END_OF_BOOK), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Apply screen viewable areas and additional padding
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;
  orientedMarginRight += SETTINGS.screenMargin;
  orientedMarginBottom += SETTINGS.screenMargin;

  auto metrics = UITheme::getInstance().getMetrics();

  // Add status bar margin
  if (SETTINGS.statusBar != CrossPointSettings::STATUS_BAR_MODE::NONE) {
    // Add additional margin for status bar if progress bar is shown
    const bool showProgressBar = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::BOOK_PROGRESS_BAR ||
                                 SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::ONLY_BOOK_PROGRESS_BAR ||
                                 SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::CHAPTER_PROGRESS_BAR;
    orientedMarginBottom += statusBarMargin - SETTINGS.screenMargin +
                            (showProgressBar ? (metrics.bookProgressBarHeight + progressBarMarginTop) : 0);
  }

  if (!section) {
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    LOG_DBG("ERS", "Loading file: %s, index: %d", filepath.c_str(), currentSpineIndex);
    section = std::unique_ptr<Section>(new Section(epub, currentSpineIndex, renderer));

    const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
    const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;

    if (!section->loadSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                  SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth,
                                  viewportHeight, SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle)) {
      LOG_DBG("ERS", "Cache not found, building...");

      const auto popupFn = [this]() { GUI.drawPopup(renderer, tr(STR_INDEXING)); };

      if (!section->createSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                      SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth,
                                      viewportHeight, SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle, popupFn)) {
        LOG_ERR("ERS", "Failed to persist page data to SD");
        section.reset();
        return;
      }
    } else {
      LOG_DBG("ERS", "Cache found, skipping build...");
    }

    if (nextPageNumber == UINT16_MAX) {
      section->currentPage = section->pageCount - 1;
    } else {
      section->currentPage = nextPageNumber;
    }

    if (section->currentPage < 0) {
      section->currentPage = 0;
    } else if (section->currentPage >= section->pageCount) {
      section->currentPage = section->pageCount - 1;
    }

    // handles changes in reader settings and reset to approximate position based on cached progress
    if (cachedChapterTotalPageCount > 0) {
      // only goes to relative position if spine index matches cached value
      if (currentSpineIndex == cachedSpineIndex && section->pageCount != cachedChapterTotalPageCount) {
        float progress = static_cast<float>(section->currentPage) / static_cast<float>(cachedChapterTotalPageCount);
        int newPage = static_cast<int>(progress * section->pageCount);
        section->currentPage = newPage;
      }
      cachedChapterTotalPageCount = 0;  // resets to 0 to prevent reading cached progress again
    }

    if (pendingPercentJump && section->pageCount > 0) {
      // Apply the pending percent jump now that we know the new section's page count.
      int newPage = static_cast<int>(pendingSpineProgress * static_cast<float>(section->pageCount));
      if (newPage >= section->pageCount) {
        newPage = section->pageCount - 1;
      }
      section->currentPage = newPage;
      pendingPercentJump = false;
    }
  }

  renderer.clearScreen();

  if (section->pageCount == 0) {
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::BOLD);
    renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    renderer.displayBuffer();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    LOG_DBG("ERS", "Page out of bounds: %d (max %d)", section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_OUT_OF_BOUNDS), true, EpdFontFamily::BOLD);
    renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    renderer.displayBuffer();
    return;
  }

  {
    auto p = section->loadPageFromSectionFile();
    if (!p) {
      LOG_ERR("ERS", "Failed to load page from SD - clearing section cache");
      section->clearCache();
      section.reset();
      requestUpdate();  // Try again after clearing cache
      // TODO: prevent infinite loop if the page keeps failing to load for some reason
      return;
    }
    const auto start = millis();
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
    renderer.clearFontCache();
  }
  {
    int bookPercent = 0;
    if (epub->getBookSize() > 0 && section->pageCount > 0) {
      const float chapterProgress =
          static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
      bookPercent = clampPercent(
          static_cast<int>(epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f + 0.5f));
    }
    lastBookProgressPercent = bookPercent;
    saveProgress(currentSpineIndex, section->currentPage, section->pageCount, bookPercent);

    if (lastTrackedSpineIndex != currentSpineIndex || lastTrackedPageIndex != section->currentPage) {
      if (lastTrackedSpineIndex != -1 && currentSpineIndex != lastTrackedSpineIndex) {
        PET_MANAGER.onChapterComplete();
      }
      PET_MANAGER.onPageTurn();
      lastTrackedSpineIndex = currentSpineIndex;
      lastTrackedPageIndex = section->currentPage;
    }
  }
}

void EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount, int bookProgressPercent) {
  HalStorage::LockGuard storageLock(Storage);
  FsFile f;
  const std::string cacheDir = epub->getCachePath();
  const std::string progressPath = cacheDir + "/progress.bin";
  const std::string progressBackupPath = cacheDir + "/progress.bak";
  const std::string progressTempPath = cacheDir + "/progress.tmp";
  const std::string progressBackupTempPath = cacheDir + "/progress.bak.tmp";

  // Cache directory should already exist, but recreate defensively to avoid intermittent save failures.
  Storage.mkdir(cacheDir.c_str(), true);

  const bool opened = Storage.openFileForWrite("ERS", progressTempPath, f);

  if (opened) {
    const uint8_t pct = static_cast<uint8_t>(bookProgressPercent < 0 ? 0 : (bookProgressPercent > 100 ? 100 : bookProgressPercent));
    uint8_t data[7];
    data[0] = spineIndex & 0xFF;
    data[1] = (spineIndex >> 8) & 0xFF;
    data[2] = currentPage & 0xFF;
    data[3] = (currentPage >> 8) & 0xFF;
    data[4] = pageCount & 0xFF;
    data[5] = (pageCount >> 8) & 0xFF;
    data[6] = pct;
    const bool writeOk = f.write(data, 7) == 7;
    f.close();

    if (!writeOk) {
      Storage.remove(progressTempPath.c_str());
      LOG_ERR("ERS", "Could not write progress temp file! path=%s", progressTempPath.c_str());
      return;
    }

    if (Storage.exists(progressPath.c_str())) {
      Storage.remove(progressPath.c_str());
    }

    if (!Storage.rename(progressTempPath.c_str(), progressPath.c_str())) {
      Storage.remove(progressTempPath.c_str());
      LOG_ERR("ERS", "Could not finalize progress save! tmp=%s final=%s", progressTempPath.c_str(),
              progressPath.c_str());
      return;
    }

    FsFile backupFile;
    if (Storage.openFileForWrite("ERS", progressBackupTempPath, backupFile)) {
      const bool backupWriteOk = backupFile.write(data, 7) == 7;
      backupFile.close();

      if (backupWriteOk) {
        if (Storage.exists(progressBackupPath.c_str())) {
          Storage.remove(progressBackupPath.c_str());
        }
        if (!Storage.rename(progressBackupTempPath.c_str(), progressBackupPath.c_str())) {
          Storage.remove(progressBackupTempPath.c_str());
          LOG_DBG("ERS", "Backup progress finalize failed: %s", progressBackupPath.c_str());
        }
      } else {
        Storage.remove(progressBackupTempPath.c_str());
      }
    }

    LOG_DBG("ERS", "Progress saved: Chapter %d, Page %d, Book %d%%", spineIndex, currentPage, pct);
  } else {
    LOG_ERR("ERS", "Could not save progress! path=%s", progressPath.c_str());
  }
}
void EpubReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  // Force special handling for pages with images when anti-aliasing is on
  bool imagePageWithAA = page->hasImages() && SETTINGS.textAntiAliasing;

  page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
  renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
  if (imagePageWithAA) {
    // Double FAST_REFRESH with selective image blanking (pablohc's technique):
    // HALF_REFRESH sets particles too firmly for the grayscale LUT to adjust.
    // Instead, blank only the image area and do two fast refreshes.
    // Step 1: Display page with image area blanked (text appears, image area white)
    // Step 2: Re-render with images and display again (images appear clean)
    int16_t imgX, imgY, imgW, imgH;
    if (page->getImageBoundingBox(imgX, imgY, imgW, imgH)) {
      renderer.fillRect(imgX + orientedMarginLeft, imgY + orientedMarginTop, imgW, imgH, false);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);

      // Re-render page content to restore images into the blanked area
      page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
      renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    } else {
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
    // Double FAST_REFRESH handles ghosting for image pages; don't count toward full refresh cadence
  } else if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }

  // Save bw buffer to reset buffer state after grayscale data sync
  renderer.storeBwBuffer();

  // grayscale rendering
  // TODO: Only do this if font supports it
  if (SETTINGS.textAntiAliasing) {
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
    renderer.copyGrayscaleLsbBuffers();

    // Render and copy to MSB buffer
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
    renderer.copyGrayscaleMsbBuffers();

    // display grayscale part
    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }

  // restore the bw data
  renderer.restoreBwBuffer();
}

void EpubReaderActivity::renderStatusBar(const int orientedMarginRight, const int orientedMarginBottom,
                                         const int orientedMarginLeft) const {
  auto metrics = UITheme::getInstance().getMetrics();

  // determine visible status bar elements
  const bool showProgressPercentage = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL;
  const bool showBookProgressBar = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::BOOK_PROGRESS_BAR ||
                                   SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::ONLY_BOOK_PROGRESS_BAR;
  const bool showChapterProgressBar = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::CHAPTER_PROGRESS_BAR;
  const bool showProgressText = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL ||
                                SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::BOOK_PROGRESS_BAR;
  const bool showBookPercentage = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::CHAPTER_PROGRESS_BAR;
  const bool showBattery = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::NO_PROGRESS ||
                           SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL ||
                           SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::BOOK_PROGRESS_BAR ||
                           SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::CHAPTER_PROGRESS_BAR;
  const bool showChapterTitle = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::NO_PROGRESS ||
                                SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL ||
                                SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::BOOK_PROGRESS_BAR ||
                                SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::CHAPTER_PROGRESS_BAR;
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage == CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_NEVER;

  // Position status bar near the bottom of the logical screen, regardless of orientation
  const auto screenHeight = renderer.getScreenHeight();
  const auto textY = screenHeight - orientedMarginBottom - 4;
  int progressTextWidth = 0;

  // Calculate progress in book
  const float sectionChapterProg =
      (section->pageCount > 0) ? (static_cast<float>(section->currentPage) / section->pageCount) : 0.0f;
  const float rawBookProgress = epub->calculateProgress(currentSpineIndex, sectionChapterProg) * 100;
  const float bookProgress = static_cast<float>(clampPercent(static_cast<int>(rawBookProgress + 0.5f)));

  if (showProgressText || showProgressPercentage || showBookPercentage) {
    // Right aligned text for progress counter
    char progressStr[32];

    // Hide percentage when progress bar is shown to reduce clutter
    if (showProgressPercentage) {
      snprintf(progressStr, sizeof(progressStr), "%d/%d  %.0f%%", section->currentPage + 1, section->pageCount,
               bookProgress);
    } else if (showBookPercentage) {
      snprintf(progressStr, sizeof(progressStr), "%.0f%%", bookProgress);
    } else {
      snprintf(progressStr, sizeof(progressStr), "%d/%d", section->currentPage + 1, section->pageCount);
    }

    progressTextWidth = renderer.getTextWidth(SMALL_FONT_ID, progressStr);
    renderer.drawText(SMALL_FONT_ID, renderer.getScreenWidth() - orientedMarginRight - progressTextWidth, textY,
                      progressStr);
  }

  if (showBookProgressBar) {
    // Draw progress bar at the very bottom of the screen, from edge to edge of viewable area
    GUI.drawReadingProgressBar(renderer, static_cast<size_t>(bookProgress));
  }

  if (showChapterProgressBar) {
    // Draw chapter progress bar at the very bottom of the screen, from edge to edge of viewable area
    const float chapterProgress =
        (section->pageCount > 0) ? (static_cast<float>(section->currentPage + 1) / section->pageCount) * 100 : 0;
    GUI.drawReadingProgressBar(renderer, static_cast<size_t>(chapterProgress));
  }

  if (showBattery) {
    GUI.drawBatteryLeft(renderer, Rect{orientedMarginLeft + 1, textY, metrics.batteryWidth, metrics.batteryHeight},
                        showBatteryPercentage);
  }

  if (showChapterTitle) {
    // Centered chatper title text
    // Page width minus existing content with 30px padding on each side
    const int rendererableScreenWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;

    const int batterySize = showBattery ? (showBatteryPercentage ? 50 : 20) : 0;
    const int titleMarginLeft = batterySize + 30;
    const int titleMarginRight = progressTextWidth + 30;

    // Attempt to center title on the screen, but if title is too wide then later we will center it within the
    // available space.
    int titleMarginLeftAdjusted = std::max(titleMarginLeft, titleMarginRight);
    int availableTitleSpace = rendererableScreenWidth - 2 * titleMarginLeftAdjusted;
    const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);

    std::string title;
    int titleWidth;
    if (tocIndex == -1) {
      title = tr(STR_UNNAMED);
      titleWidth = renderer.getTextWidth(SMALL_FONT_ID, title.c_str());
    } else {
      const auto tocItem = epub->getTocItem(tocIndex);
      title = tocItem.title;
      titleWidth = renderer.getTextWidth(SMALL_FONT_ID, title.c_str());
      if (titleWidth > availableTitleSpace) {
        // Not enough space to center on the screen, center it within the remaining space instead
        availableTitleSpace = rendererableScreenWidth - titleMarginLeft - titleMarginRight;
        titleMarginLeftAdjusted = titleMarginLeft;
      }
      if (titleWidth > availableTitleSpace) {
        title = renderer.truncatedText(SMALL_FONT_ID, title.c_str(), availableTitleSpace);
        titleWidth = renderer.getTextWidth(SMALL_FONT_ID, title.c_str());
      }
    }

    renderer.drawText(SMALL_FONT_ID,
                      titleMarginLeftAdjusted + orientedMarginLeft + (availableTitleSpace - titleWidth) / 2, textY,
                      title.c_str());
  }
}
