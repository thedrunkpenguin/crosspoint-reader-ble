#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Serialization.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/cog.h"
#include "components/icons/folder.h"
#include "components/icons/folder2.h"
#include "components/icons/game.h"
#include "components/icons/library.h"
#include "components/icons/settings.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi_wide.h"
#include "fontIds.h"

int HomeActivity::getMenuItemCount() const {
  int actionCount = hasOpdsUrl ? 4 : 3;  // Browse, OPDS?, Network/File Transfer, Settings
  if constexpr (CrossPointSettings::deepMinesEnabled) {
    actionCount += 1;  // Games
  }
  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::CARDS) {
    return 2 + actionCount;  // preview card + recents card + action cards (no virtual pet in this build)
  }

  // Non-cards themes expose Recents as an explicit menu item so the home screen
  // actions stay functionally aligned with the Cards layout.
  actionCount += 1;  // Recent Books

  const int coverSlots = std::max(1, UITheme::getInstance().getMetrics().homeRecentBooksCount);
  const int recentCount = std::max(1, std::min(static_cast<int>(recentBooks.size()), coverSlots));
  return recentCount + actionCount;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  const bool isCardsTheme = SETTINGS.uiTheme == CrossPointSettings::UI_THEME::CARDS;
  bool showingLoading = false;
  Rect popupRect;

  const size_t booksToProcess = isCardsTheme ? std::min<size_t>(1, recentBooks.size()) : recentBooks.size();
  int progress = 0;
  for (size_t bookIndex = 0; bookIndex < booksToProcess; ++bookIndex) {
    RecentBook& book = recentBooks[bookIndex];
    const bool isEpub = FsHelpers::hasEpubExtension(book.path);
    const bool isXtc = FsHelpers::hasXtcExtension(book.path);

    if (!isEpub && !isXtc) {
      progress++;
      continue;
    }

    if (isXtc) {
      const std::string expectedThumbPath = Xtc(book.path, "/.crosspoint").getThumbBmpPath();
      if (book.coverBmpPath != expectedThumbPath) {
        book.coverBmpPath = expectedThumbPath;
        RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.coverBmpPath);
      }
    }

    const std::string coverPath =
        book.coverBmpPath.empty() ? "" : UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
    bool needsThumbGeneration = book.coverBmpPath.empty() || coverPath.empty() || !Storage.exists(coverPath.c_str());

    if (!needsThumbGeneration) {
      FsFile coverFile;
      if (Storage.openFileForRead("HOM", coverPath, coverFile)) {
        Bitmap bitmap(coverFile);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          // Reuse existing cached thumbs in Cards mode too. Forcing a new grayscale
          // render on every home entry makes the cover noticeably slower to appear.
          const int minDesiredHeight = std::max(120, (coverHeight * 3) / 4);
          const int minDesiredWidth = std::max(80, (minDesiredHeight * 2) / 3);
          if (bitmap.getHeight() < minDesiredHeight || bitmap.getWidth() < minDesiredWidth) {
            needsThumbGeneration = true;
          }
        } else {
          needsThumbGeneration = true;
        }
        coverFile.close();
      } else {
        needsThumbGeneration = true;
      }
    }

    if (needsThumbGeneration && !coverPath.empty()) {
      Storage.remove(coverPath.c_str());
    }

    if (needsThumbGeneration) {
      if (!showingLoading) {
        showingLoading = true;
        popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      }
      GUI.fillPopupProgress(renderer, popupRect,
                            10 + progress * (90 / std::max(1, static_cast<int>(booksToProcess))));

      if (isEpub) {
        Epub epub(book.path, "/.crosspoint");
        epub.load(false, true);
        const bool success = epub.generateThumbBmp(coverHeight);
        if (success) {
          book.coverBmpPath = epub.getThumbBmpPath();
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.coverBmpPath);
        } else {
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
          book.coverBmpPath = "";
        }
      } else if (isXtc) {
        Xtc xtc(book.path, "/.crosspoint");
        if (xtc.load() && xtc.generateThumbBmp(coverHeight)) {
          book.coverBmpPath = xtc.getThumbBmpPath();
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.coverBmpPath);
        } else {
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
          book.coverBmpPath = "";
        }
      }

      coverRendered = false;
      requestUpdate();
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  // Check if OPDS browser URL is configured
  hasOpdsUrl = strlen(SETTINGS.opdsServerUrl) > 0;

  selectorIndex = 0;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool isCardsTheme = SETTINGS.uiTheme == CrossPointSettings::UI_THEME::CARDS;
  loadRecentBooks(std::max(isCardsTheme ? 4 : 3, metrics.homeRecentBooksCount));

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = GfxRenderer::getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = GfxRenderer::getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

int HomeActivity::getRecentBookProgressPercent(const RecentBook& book) const {
  auto clampPercent = [](int value) {
    if (value < 0) return 0;
    if (value > 100) return 100;
    return value;
  };

  const size_t hash = std::hash<std::string>{}(book.path);

  if (FsHelpers::hasEpubExtension(book.path)) {
    const std::string progressPath = std::string("/.crosspoint/epub_") + std::to_string(hash) + "/progress.bin";
    FsFile f;
    if (Storage.openFileForRead("HOM", progressPath, f)) {
      uint8_t data[7] = {0};
      const int read = f.read(data, 7);
      f.close();
      if (read >= 7) {
        return data[6];
      }
      if (read == 6) {
        const int currentPage = data[2] + (data[3] << 8);
        const int totalPages = data[4] + (data[5] << 8);
        if (totalPages > 0) {
          return clampPercent((currentPage + 1) * 100 / totalPages);
        }
      }
    }
    return 0;
  }

  if (FsHelpers::hasTxtExtension(book.path) || FsHelpers::hasMarkdownExtension(book.path)) {
    const std::string cacheBase = std::string("/.crosspoint/txt_") + std::to_string(hash);
    const std::string progressPath = cacheBase + "/progress.bin";
    const std::string indexPath = cacheBase + "/index.bin";

    int currentPage = 0;
    FsFile progressFile;
    if (Storage.openFileForRead("HOM", progressPath, progressFile)) {
      uint8_t data[4] = {0};
      const int read = progressFile.read(data, 4);
      progressFile.close();
      if (read == 4) {
        currentPage = data[0] + (data[1] << 8);
      }
    }

    uint32_t totalPages = 0;
    FsFile indexFile;
    if (Storage.openFileForRead("HOM", indexPath, indexFile)) {
      uint32_t magic = 0;
      uint8_t version = 0;
      uint32_t fileSize = 0;
      int32_t viewportWidth = 0;
      int32_t linesPerPage = 0;
      int32_t fontId = 0;
      int32_t margin = 0;
      uint8_t paragraphAlignment = 0;

      serialization::readPod(indexFile, magic);
      serialization::readPod(indexFile, version);
      serialization::readPod(indexFile, fileSize);
      serialization::readPod(indexFile, viewportWidth);
      serialization::readPod(indexFile, linesPerPage);
      serialization::readPod(indexFile, fontId);
      serialization::readPod(indexFile, margin);
      serialization::readPod(indexFile, paragraphAlignment);
      serialization::readPod(indexFile, totalPages);
      indexFile.close();
    }

    if (totalPages > 0) {
      return clampPercent((currentPage + 1) * 100 / static_cast<int>(totalPages));
    }
    return 0;
  }

  if (FsHelpers::hasXtcExtension(book.path)) {
    const std::string progressPath = std::string("/.crosspoint/xtc_") + std::to_string(hash) + "/progress.bin";
    uint32_t currentPage = 0;
    uint32_t totalPages = 0;

    FsFile f;
    if (Storage.openFileForRead("HOM", progressPath, f)) {
      uint8_t data[8] = {0};
      const int read = f.read(data, sizeof(data));
      f.close();
      if (read >= 4) {
        currentPage = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                      (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
      }
      if (read >= 8) {
        totalPages = static_cast<uint32_t>(data[4]) | (static_cast<uint32_t>(data[5]) << 8) |
                     (static_cast<uint32_t>(data[6]) << 16) | (static_cast<uint32_t>(data[7]) << 24);
      }
    }

    if (totalPages == 0) {
      FsFile xtcFile;
      if (Storage.openFileForRead("HOM", book.path, xtcFile)) {
        xtc::XtcHeader header{};
        if (xtcFile.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) == sizeof(header) &&
            (header.magic == xtc::XTC_MAGIC || header.magic == xtc::XTCH_MAGIC)) {
          totalPages = header.pageCount;
        }
        xtcFile.close();
      }
    }

    if (totalPages > 0) {
      return clampPercent(static_cast<int>((currentPage + 1) * 100 / totalPages));
    }
  }

  return 0;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();
  if (selectorIndex >= menuCount) {
    selectorIndex = std::max(0, menuCount - 1);
  }

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const bool isCardsTheme = SETTINGS.uiTheme == CrossPointSettings::UI_THEME::CARDS;

    if (!isCardsTheme) {
      const int coverSlots = std::max(1, UITheme::getInstance().getMetrics().homeRecentBooksCount);
      const int recentCount = std::max(1, std::min(static_cast<int>(recentBooks.size()), coverSlots));
      if (selectorIndex < recentCount) {
        if (!recentBooks.empty() && selectorIndex < static_cast<int>(recentBooks.size())) {
          onSelectBook(recentBooks[selectorIndex].path);
        } else {
          onFileBrowserOpen();
        }
        return;
      }

      int idx = 0;
      const int menuSelectedIndex = selectorIndex - recentCount;
      const int recentsIdx = idx++;
      const int fileBrowserIdx = idx++;
      const int opdsLibraryIdx = hasOpdsUrl ? idx++ : -1;
      const int fileTransferIdx = idx++;
      const int gameIdx = CrossPointSettings::deepMinesEnabled ? idx++ : -1;
      const int settingsIdx = idx;

      if (menuSelectedIndex == recentsIdx) {
        onRecentsOpen();
      } else if (menuSelectedIndex == fileBrowserIdx) {
        onFileBrowserOpen();
      } else if (menuSelectedIndex == opdsLibraryIdx) {
        onOpdsBrowserOpen();
      } else if (menuSelectedIndex == fileTransferIdx) {
        onFileTransferOpen();
      } else if (CrossPointSettings::deepMinesEnabled && menuSelectedIndex == gameIdx) {
        onGameOpen();
      } else if (menuSelectedIndex == settingsIdx) {
        onSettingsOpen();
      }
      return;
    }

    if (selectorIndex == 0) {
      if (!recentBooks.empty()) {
        onSelectBook(recentBooks[0].path);
      } else {
        onFileBrowserOpen();
      }
      return;
    }

    if (selectorIndex == 1) {
      onRecentsOpen();
      return;
    }

    int idx = 0;
    const int menuSelectedIndex = selectorIndex - 2;
    const int fileBrowserIdx = idx++;
    const int opdsLibraryIdx = hasOpdsUrl ? idx++ : -1;
    const int fileTransferIdx = idx++;
    const int gameIdx = CrossPointSettings::deepMinesEnabled ? idx++ : -1;
    const int settingsIdx = idx;

    if (menuSelectedIndex == fileBrowserIdx) {
      onFileBrowserOpen();
    } else if (menuSelectedIndex == opdsLibraryIdx) {
      onOpdsBrowserOpen();
    } else if (menuSelectedIndex == fileTransferIdx) {
      onFileTransferOpen();
    } else if (CrossPointSettings::deepMinesEnabled && menuSelectedIndex == gameIdx) {
      onGameOpen();
    } else if (menuSelectedIndex == settingsIdx) {
      onSettingsOpen();
    }
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const bool isCardsTheme = SETTINGS.uiTheme == CrossPointSettings::UI_THEME::CARDS;

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);

  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_NETWORK), tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Transfer, Settings};

  if (!isCardsTheme) {
    menuItems.insert(menuItems.begin(), tr(STR_MENU_RECENT_BOOKS));
    menuIcons.insert(menuIcons.begin(), Recent);
  }

  if (hasOpdsUrl) {
    menuItems.insert(menuItems.begin() + 1, tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 1, Library);
  }

  if constexpr (CrossPointSettings::deepMinesEnabled) {
    const auto settingsPos = static_cast<int>(menuItems.size()) - 1;
    menuItems.insert(menuItems.begin() + settingsPos, tr(STR_GAMES));
    menuIcons.insert(menuIcons.begin() + settingsPos, Book);
  }

  if (!isCardsTheme) {
    const int coverSlots = std::max(1, metrics.homeRecentBooksCount);
    const int recentCount = std::max(1, std::min(static_cast<int>(recentBooks.size()), coverSlots));

    GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                            recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                            std::bind(&HomeActivity::storeCoverBuffer, this));

    GUI.drawButtonMenu(
        renderer,
        Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing, pageWidth,
             pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing * 2 +
                           metrics.buttonHintsHeight)},
        static_cast<int>(menuItems.size()), selectorIndex - recentCount,
        [&menuItems](int index) { return std::string(menuItems[index]); },
        [&menuIcons](int index) { return menuIcons[index]; });

    const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();

    if (!firstRenderDone) {
      firstRenderDone = true;
      requestUpdate();
    } else if (!recentsLoaded && !recentsLoading) {
      recentsLoading = true;
      loadRecentCovers(metrics.homeCoverHeight);
    }
    return;
  }

  const int contentX = metrics.contentSidePadding;
  const int contentWidth = pageWidth - metrics.contentSidePadding * 2;
  const int columnGap = metrics.menuSpacing * 2;
  const int cardWidth = (contentWidth - columnGap) / 2;

  constexpr int minTopCardHeight = 220;
  constexpr int minLowerCardHeight = 90;
  const int lowerCount = static_cast<int>(menuItems.size());
  const int lowerRows = (lowerCount + 1) / 2;
  const int maxTopCardHeight = pageHeight - metrics.homeTopPadding - metrics.buttonHintsHeight -
                               metrics.verticalSpacing * 2 - (lowerRows - 1) * metrics.menuSpacing -
                               lowerRows * minLowerCardHeight;
  const int desiredTopCardHeight = (pageHeight * 46) / 100;
  const int topCardHeight =
      std::clamp(desiredTopCardHeight, minTopCardHeight, std::max(minTopCardHeight, maxTopCardHeight));
  // Cards crops a wide slice from a portrait book cover, so using a somewhat taller
  // cached thumb keeps the preview sharper without bringing back the old blocking load.
  const int cardsPreviewCoverHeight = std::max(metrics.homeCoverHeight, std::min(400, topCardHeight * 2));

  const int topY = metrics.homeTopPadding;
  const int previewX = contentX;
  const int recentX = contentX + cardWidth + columnGap;
  const bool previewSelected = selectorIndex == 0;
  const bool recentSelected = selectorIndex == 1;

  const int cachedCoverHeight = cardsPreviewCoverHeight;
  bool drewPreviewCover = bufferRestored && coverBufferStored;
  if (!drewPreviewCover && !recentBooks.empty() && !recentBooks[0].coverBmpPath.empty()) {
    const std::string coverPath = UITheme::getCoverThumbPath(recentBooks[0].coverBmpPath, cachedCoverHeight);
    FsFile file;
    if (Storage.openFileForRead("HOM", coverPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        float cropX = 0.0f;
        float cropY = 0.0f;
        const float bitmapRatio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float cardRatio = static_cast<float>(cardWidth) / static_cast<float>(topCardHeight);
        if (bitmapRatio > cardRatio) {
          cropX = 1.0f - (cardRatio / bitmapRatio);
        } else if (bitmapRatio < cardRatio) {
          cropY = 1.0f - (bitmapRatio / cardRatio);
        }
        renderer.drawBitmap(bitmap, previewX, topY, cardWidth, topCardHeight, cropX, cropY);
        drewPreviewCover = true;
        recentsLoaded = true;  // cached cover is already good enough for Cards mode
      }
      file.close();
    }
  }

  if (!drewPreviewCover) {
    renderer.fillRect(previewX, topY, cardWidth, topCardHeight, false);
    renderer.drawIcon(BookIcon, previewX + (cardWidth - 32) / 2, topY + (topCardHeight - 32) / 2, 32, 32);
  }

  if (drewPreviewCover && !coverRendered) {
    if (!coverBufferStored) {
      coverBufferStored = storeCoverBuffer();
    }
    coverRendered = coverBufferStored;
  }

  const int overlayH = renderer.getLineHeight(UI_10_FONT_ID) + renderer.getLineHeight(UI_12_FONT_ID) + 14;
  const int overlayY = topY + topCardHeight - overlayH - 8;
  renderer.fillRect(previewX + 8, overlayY, cardWidth - 16, overlayH, false);
  renderer.drawRect(previewX + 8, overlayY, cardWidth - 16, overlayH, true);

  const char* previewLabel = !recentBooks.empty() ? tr(STR_CONTINUE_READING) : tr(STR_BROWSE_FILES);
  std::string previewTitle = !recentBooks.empty() ? recentBooks[0].title : tr(STR_NO_OPEN_BOOK);
  previewTitle = renderer.truncatedText(UI_12_FONT_ID, previewTitle.c_str(), cardWidth - 24, EpdFontFamily::BOLD);

  const int previewLabelW = renderer.getTextWidth(UI_10_FONT_ID, previewLabel);
  renderer.drawText(UI_10_FONT_ID, previewX + (cardWidth - previewLabelW) / 2, overlayY + 5, previewLabel, true);

  const int previewTitleW = renderer.getTextWidth(UI_12_FONT_ID, previewTitle.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, previewX + (cardWidth - previewTitleW) / 2,
                    overlayY + 5 + renderer.getLineHeight(UI_10_FONT_ID) + 2, previewTitle.c_str(), true,
                    EpdFontFamily::BOLD);

  renderer.drawRoundedRect(previewX, topY, cardWidth, topCardHeight, 1, 8, true);
  if (previewSelected) {
    renderer.drawRoundedRect(previewX + 2, topY + 2, cardWidth - 4, topCardHeight - 4, 1, 7, true);
  }

  renderer.fillRoundedRect(recentX, topY, cardWidth, topCardHeight, 8,
                           recentSelected ? Color::LightGray : Color::White);
  renderer.drawRoundedRect(recentX, topY, cardWidth, topCardHeight, 1, 8, true);
  if (recentSelected) {
    renderer.drawRoundedRect(recentX + 2, topY + 2, cardWidth - 4, topCardHeight - 4, 1, 7, true);
  }

  std::string recentsHeader =
      renderer.truncatedText(UI_12_FONT_ID, tr(STR_MENU_RECENT_BOOKS), cardWidth - 20, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, recentX + 10, topY + 8, recentsHeader.c_str(), true, EpdFontFamily::BOLD);

  const int listTop = topY + 8 + renderer.getLineHeight(UI_12_FONT_ID) + 6;
  const int titleLineH = renderer.getLineHeight(SMALL_FONT_ID);
  constexpr int progressBarH = 3;
  constexpr int rowItemGap = 3;
  const int rowUnit = titleLineH * 2 + progressBarH + rowItemGap;
  const int listAvail = topCardHeight - (listTop - topY) - 8;
  const int rowCount = std::min(4, std::max(1, listAvail / rowUnit));

  for (int row = 0; row < rowCount; ++row) {
    const int rowY = listTop + row * rowUnit;
    if (rowY + titleLineH > topY + topCardHeight - 4) {
      break;
    }

    if (row < static_cast<int>(recentBooks.size())) {
      const RecentBook& book = recentBooks[row];
      const int progress = getRecentBookProgressPercent(book);
      const int titleAvailW = cardWidth - 20;

      std::string line1;
      std::string line2;
      if (renderer.getTextWidth(SMALL_FONT_ID, book.title.c_str()) <= titleAvailW) {
        line1 = book.title;
      } else {
        size_t fitsLen = 0;
        for (size_t len = 1; len <= book.title.size(); ++len) {
          if (renderer.getTextWidth(SMALL_FONT_ID, book.title.substr(0, len).c_str()) > titleAvailW) {
            break;
          }
          fitsLen = len;
        }

        const size_t spacePos = book.title.rfind(' ', fitsLen > 0 ? fitsLen - 1 : 0);
        if (spacePos != std::string::npos && spacePos > 0) {
          line1 = book.title.substr(0, spacePos);
          const std::string rest = book.title.substr(spacePos + 1);
          line2 = renderer.truncatedText(SMALL_FONT_ID, rest.c_str(), titleAvailW);
        } else {
          line1 = renderer.truncatedText(SMALL_FONT_ID, book.title.c_str(), titleAvailW);
        }
      }

      renderer.drawText(SMALL_FONT_ID, recentX + 10, rowY, line1.c_str(), true);
      if (!line2.empty() && rowY + titleLineH + titleLineH <= topY + topCardHeight - 4) {
        renderer.drawText(SMALL_FONT_ID, recentX + 10, rowY + titleLineH, line2.c_str(), true);
      }

      const int barY = rowY + titleLineH * 2 + 1;
      const int barX = recentX + 10;
      const int barW = cardWidth - 20;
      if (barY + progressBarH <= topY + topCardHeight - 4) {
        renderer.fillRect(barX, barY, barW, progressBarH, false);
        renderer.drawRect(barX, barY, barW, progressBarH, true);
        if (progress > 0) {
          const int fillW = std::max(1, ((barW - 2) * progress) / 100);
          renderer.fillRect(barX + 1, barY + 1, fillW, progressBarH - 2, true);
        }
      }
    } else {
      renderer.drawText(SMALL_FONT_ID, recentX + 10, rowY, "-", true);
    }
  }

  constexpr int nativeIconSize = 96;
  const int lowerY = topY + topCardHeight + metrics.verticalSpacing;
  const int availForLower = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - lowerY;
  const int lowerCardHeight = std::max(60, (availForLower - (lowerRows - 1) * metrics.menuSpacing) / lowerRows);
  const int selectedLower = selectorIndex - 2;

  for (int i = 0; i < lowerCount; ++i) {
    const int row = i / 2;
    const int col = i % 2;
    const int x = contentX + col * (cardWidth + columnGap);
    const int y = lowerY + row * (lowerCardHeight + metrics.menuSpacing);
    const bool selected = selectedLower == i;

    if (selected) {
      renderer.fillRoundedRect(x, y, cardWidth, lowerCardHeight, 8, Color::LightGray);
      renderer.drawRoundedRect(x, y, cardWidth, lowerCardHeight, 1, 8, true);
      renderer.drawRoundedRect(x + 2, y + 2, cardWidth - 4, lowerCardHeight - 4, 1, 7, true);
    } else {
      renderer.fillRoundedRect(x, y, cardWidth, lowerCardHeight, 8, Color::White);
      renderer.drawRoundedRect(x, y, cardWidth, lowerCardHeight, 1, 8, true);
    }

    const int labelH = renderer.getLineHeight(UI_12_FONT_ID);
    std::string label = renderer.truncatedText(UI_12_FONT_ID, menuItems[i], cardWidth - 16, EpdFontFamily::BOLD);
    const int labelW = renderer.getTextWidth(UI_12_FONT_ID, label.c_str(), EpdFontFamily::BOLD);
    const int labelY = y + 8;
    renderer.fillRectDither(x + 6, labelY - 1, cardWidth - 12, labelH + 2,
                            selected ? Color::LightGray : Color::White);
    renderer.drawText(UI_12_FONT_ID, x + (cardWidth - labelW) / 2, labelY, label.c_str(), true,
                      EpdFontFamily::BOLD);

    const uint8_t* icon = BookIcon;
    switch (menuIcons[i]) {
      case Folder:
        icon = Folder2Icon;
        break;
      case Transfer:
        icon = Wifi_wideIcon;
        break;
      case Settings:
        icon = CogIcon;
        break;
      case Library:
        icon = LibraryIcon;
        break;
      case Book:
        icon = GameIcon;
        break;
      case Recent:
      default:
        icon = BookIcon;
        break;
    }

    const int iconY = y + labelH + 10 + std::max(0, (lowerCardHeight - labelH - 18 - nativeIconSize) / 2);
    renderer.drawIcon(icon, x + (cardWidth - nativeIconSize) / 2, iconY, nativeIconSize, nativeIconSize);
  }

  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(cardsPreviewCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onGameOpen() { activityManager.goToGame(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
