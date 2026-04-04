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
#include "components/icons/folder.h"
#include "components/icons/library.h"
#include "components/icons/settings.h"
#include "components/icons/transfer.h"
#include "fontIds.h"

int HomeActivity::getMenuItemCount() const {
  int actionCount = hasOpdsUrl ? 5 : 4;  // Browse, Recents, OPDS?, Transfer, Settings
  if constexpr (CrossPointSettings::deepMinesEnabled) {
    actionCount += 1;  // Games
  }
  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::CARDS) {
    return 2 + actionCount;  // preview card + recents card + actions
  }

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

  int progress = 0;
  for (RecentBook& book : recentBooks) {
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
          const int minDesiredHeight = isCardsTheme ? std::max(180, coverHeight - 8)
                                                    : std::max(120, (coverHeight * 3) / 4);
          const int minDesiredWidth = std::max(80, (minDesiredHeight * 2) / 3);
          if (bitmap.getHeight() < minDesiredHeight || bitmap.getWidth() < minDesiredWidth) {
            needsThumbGeneration = true;
          }
          if (isCardsTheme && isEpub && bitmap.getBpp() == 1) {
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
                            10 + progress * (90 / std::max(1, static_cast<int>(recentBooks.size()))));

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
  loadRecentBooks(metrics.homeRecentBooksCount);

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
      const int fileBrowserIdx = idx++;
      const int recentsIdx = idx++;
      const int opdsLibraryIdx = hasOpdsUrl ? idx++ : -1;
      const int fileTransferIdx = idx++;
      const int gameIdx = CrossPointSettings::deepMinesEnabled ? idx++ : -1;
      const int settingsIdx = idx;

      if (menuSelectedIndex == fileBrowserIdx) {
        onFileBrowserOpen();
      } else if (menuSelectedIndex == recentsIdx) {
        onRecentsOpen();
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
    const int recentsIdx = idx++;
    const int opdsLibraryIdx = hasOpdsUrl ? idx++ : -1;
    const int fileTransferIdx = idx++;
    const int gameIdx = CrossPointSettings::deepMinesEnabled ? idx++ : -1;
    const int settingsIdx = idx;

    if (menuSelectedIndex == fileBrowserIdx) {
      onFileBrowserOpen();
    } else if (menuSelectedIndex == recentsIdx) {
      onRecentsOpen();
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

  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS), tr(STR_FILE_TRANSFER),
                                        tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Recent, Transfer, Settings};

  if (hasOpdsUrl) {
    menuItems.insert(menuItems.begin() + 2, tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 2, Library);
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
  const int topY = metrics.homeTopPadding;
  const int topCardHeight = std::max(190, std::min(260, (pageHeight - topY - metrics.buttonHintsHeight - 60) / 3));

  const int previewX = contentX;
  const int recentX = contentX + cardWidth + columnGap;
  const bool previewSelected = selectorIndex == 0;
  const bool recentSelected = selectorIndex == 1;

  renderer.fillRoundedRect(previewX, topY, cardWidth, topCardHeight, 8,
                           previewSelected ? Color::LightGray : Color::White);
  renderer.drawRoundedRect(previewX, topY, cardWidth, topCardHeight, 1, 8, true);
  if (previewSelected) {
    renderer.drawRoundedRect(previewX + 2, topY + 2, cardWidth - 4, topCardHeight - 4, 1, 7, true);
  }

  bool drewPreviewCover = false;
  if (!recentBooks.empty() && !recentBooks[0].coverBmpPath.empty()) {
    const std::string coverPath = UITheme::getCoverThumbPath(recentBooks[0].coverBmpPath, topCardHeight);
    FsFile file;
    if (Storage.openFileForRead("HOM", coverPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderer.drawBitmap(bitmap, previewX + 8, topY + 8, cardWidth - 16, topCardHeight - 54);
        drewPreviewCover = true;
      }
      file.close();
    }
  }
  if (!drewPreviewCover) {
    renderer.drawIcon(BookIcon, previewX + (cardWidth - 32) / 2, topY + 36, 32, 32);
  }

  const char* previewLabel = !recentBooks.empty() ? tr(STR_CONTINUE_READING) : tr(STR_BROWSE_FILES);
  std::string previewTitle = !recentBooks.empty() ? recentBooks[0].title : tr(STR_NO_OPEN_BOOK);
  previewTitle = renderer.truncatedText(UI_10_FONT_ID, previewTitle.c_str(), cardWidth - 16, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, previewX + 8, topY + topCardHeight - 30, previewLabel, true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, previewX + 8, topY + topCardHeight - 14, previewTitle.c_str(), true);

  renderer.fillRoundedRect(recentX, topY, cardWidth, topCardHeight, 8,
                           recentSelected ? Color::LightGray : Color::White);
  renderer.drawRoundedRect(recentX, topY, cardWidth, topCardHeight, 1, 8, true);
  if (recentSelected) {
    renderer.drawRoundedRect(recentX + 2, topY + 2, cardWidth - 4, topCardHeight - 4, 1, 7, true);
  }
  renderer.drawText(UI_12_FONT_ID, recentX + 8, topY + 8, tr(STR_MENU_RECENT_BOOKS), true, EpdFontFamily::BOLD);

  const int rowTop = topY + 30;
  const int rowHeight = 28;
  for (int i = 0; i < 4; ++i) {
    const int y = rowTop + i * rowHeight;
    if (y + rowHeight > topY + topCardHeight - 6) {
      break;
    }

    if (i < static_cast<int>(recentBooks.size())) {
      const auto& book = recentBooks[i];
      const int progress = getRecentBookProgressPercent(book);
      std::string title = renderer.truncatedText(SMALL_FONT_ID, book.title.c_str(), cardWidth - 16);
      renderer.drawText(SMALL_FONT_ID, recentX + 8, y, title.c_str(), true);
      renderer.drawRect(recentX + 8, y + 12, cardWidth - 16, 6, true);
      if (progress > 0) {
        const int fill = std::max(1, ((cardWidth - 18) * progress) / 100);
        renderer.fillRect(recentX + 9, y + 13, fill, 4, true);
      }
    } else {
      renderer.drawText(SMALL_FONT_ID, recentX + 8, y, "-", true);
    }
  }

  const int lowerY = topY + topCardHeight + metrics.verticalSpacing;
  const int lowerCount = static_cast<int>(menuItems.size());
  const int lowerRows = (lowerCount + 1) / 2;
  const int availForLower = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - lowerY;
  const int lowerCardHeight = std::max(60, (availForLower - (lowerRows - 1) * metrics.menuSpacing) / lowerRows);
  const int selectedLower = selectorIndex - 2;

  for (int i = 0; i < lowerCount; ++i) {
    const int row = i / 2;
    const int col = i % 2;
    const int x = contentX + col * (cardWidth + columnGap);
    const int y = lowerY + row * (lowerCardHeight + metrics.menuSpacing);
    const bool selected = selectedLower == i;

    renderer.fillRoundedRect(x, y, cardWidth, lowerCardHeight, 8, selected ? Color::LightGray : Color::White);
    renderer.drawRoundedRect(x, y, cardWidth, lowerCardHeight, 1, 8, true);
    if (selected) {
      renderer.drawRoundedRect(x + 2, y + 2, cardWidth - 4, lowerCardHeight - 4, 1, 7, true);
    }

    const uint8_t* icon = BookIcon;
    switch (menuIcons[i]) {
      case Folder:
        icon = FolderIcon;
        break;
      case Transfer:
        icon = TransferIcon;
        break;
      case Settings:
        icon = SettingsIcon;
        break;
      case Library:
        icon = LibraryIcon;
        break;
      case Recent:
      case Book:
      default:
        icon = BookIcon;
        break;
    }

    renderer.drawIcon(icon, x + 8, y + (lowerCardHeight - 32) / 2, 32, 32);
    std::string label = renderer.truncatedText(UI_10_FONT_ID, menuItems[i], cardWidth - 52, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, x + 48, y + (lowerCardHeight / 2) - 6, label.c_str(), true, EpdFontFamily::BOLD);
  }

  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(topCardHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onGameOpen() { activityManager.goToGame(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
