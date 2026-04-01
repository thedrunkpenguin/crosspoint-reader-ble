#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Serialization.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#include "Battery.h"
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
#include "components/icons/pet.h"
#include "components/icons/pet48.h"
#include "components/icons/wifi.h"
#include "components/icons/wifi_wide.h"
#include "fontIds.h"
#include "pet/PetManager.h"
#include "pet/PetSpriteRenderer.h"
#include "util/StringUtils.h"

namespace {
bool isDisplayablePetName(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }

  if (value[0] == '/' || value[0] == '.' || value[0] == '\\') {
    return false;
  }

  for (size_t i = 0; value[i] != '\0'; ++i) {
    const char ch = value[i];
    if (ch == '/' || ch == '\\') {
      return false;
    }
  }

  return true;
}
}

int HomeActivity::getMenuItemCount() const {
  int count = 0;

  int actionCount = 5;  // Browse Files, Network, Games, Pet, Settings
#ifndef DISABLE_OPDS
  if (hasOpdsUrl) {
    actionCount++;  // OPDS
  }
#endif

  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::CARDS) {
    count = 3 + (actionCount - 1);  // Continue + Recents + Pet card + actions excluding Pet
  } else {
    const int coverSlots = std::max(1, UITheme::getInstance().getMetrics().homeRecentBooksCount);
    const int recentCount = std::max(1, std::min(static_cast<int>(recentBooks.size()), coverSlots));
    count = recentCount + actionCount;
  }

  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    if (recentBooks.size() >= maxBooks) {
      break;
    }

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
    const bool isEpub = StringUtils::checkFileExtension(book.path, ".epub");
    const bool isXtc = StringUtils::checkFileExtension(book.path, ".xtch") ||
                       StringUtils::checkFileExtension(book.path, ".xtc");

    if (!isEpub && !isXtc) {
      progress++;
      continue;
    }

    // Keep cached thumb paths current so older recent-book entries regenerate
    // after thumbnail format tweaks.
    if (isXtc) {
      const std::string expectedThumbPath = Xtc(book.path, "/.crosspoint").getThumbBmpPath();
      if (book.coverBmpPath != expectedThumbPath) {
        book.coverBmpPath = expectedThumbPath;
        RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.coverBmpPath);
      }
    }

    const std::string coverPath = book.coverBmpPath.empty() ? "" : UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
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
      if (isEpub) {
        Epub epub(book.path, "/.crosspoint");
        epub.load(false, true);

        if (!showingLoading) {
          showingLoading = true;
          popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
        }
        GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / std::max(1, static_cast<int>(recentBooks.size()))));
        bool success = epub.generateThumbBmp(coverHeight);
        if (success) {
          book.coverBmpPath = epub.getThumbBmpPath();
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.coverBmpPath);
        } else {
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
          book.coverBmpPath = "";
        }
        coverRendered = false;
        requestUpdate();
      } else if (isXtc) {
        try {
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect,
                                  10 + progress * (90 / std::max(1, static_cast<int>(recentBooks.size()))));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (success) {
              book.coverBmpPath = xtc.getThumbBmpPath();
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.coverBmpPath);
            } else {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        } catch (const std::exception& e) {
          LOG_ERR("HOM", "Exception generating XTC cover for %s: %s", book.path.c_str(), e.what());
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
          book.coverBmpPath = "";
        } catch (...) {
          LOG_ERR("HOM", "Unknown exception generating XTC cover for %s", book.path.c_str());
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
          book.coverBmpPath = "";
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

#ifndef DISABLE_OPDS
  hasOpdsUrl = strlen(SETTINGS.opdsServerUrl) > 0;
#else
  hasOpdsUrl = false;
#endif

  selectorIndex = 0;

  auto metrics = UITheme::getInstance().getMetrics();
  const bool isCardsTheme = SETTINGS.uiTheme == CrossPointSettings::UI_THEME::CARDS;
  loadRecentBooks(std::max(isCardsTheme ? 4 : 3, metrics.homeRecentBooksCount));

  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

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

  if (StringUtils::checkFileExtension(book.path, ".epub")) {
    const std::string progressPath = std::string("/.crosspoint/epub_") + std::to_string(hash) + "/progress.bin";
    FsFile f;
    if (Storage.openFileForRead("HOM", progressPath, f)) {
      uint8_t data[7] = {0};
      const int read = f.read(data, 7);
      f.close();
      if (read >= 7) {
        // Byte 6: overall book progress % saved by the reader
        return data[6];
      }
      if (read == 6) {
        // Legacy: fall back to chapter-relative progress
        const int currentPage = data[2] + (data[3] << 8);
        const int totalPages = data[4] + (data[5] << 8);
        if (totalPages > 0) {
          return clampPercent((currentPage + 1) * 100 / totalPages);
        }
      }
    }
    return 0;
  }

  if (StringUtils::checkFileExtension(book.path, ".txt") || StringUtils::checkFileExtension(book.path, ".md")) {
    const std::string cacheBase = std::string("/.crosspoint/txt_") + std::to_string(hash);
    const std::string progressPath = cacheBase + "/progress.bin";
    const std::string indexPath = cacheBase + "/index.bin";

    int currentPage = 0;
    {
      FsFile f;
      if (Storage.openFileForRead("HOM", progressPath, f)) {
        uint8_t data[4] = {0};
        const int read = f.read(data, 4);
        f.close();
        if (read == 4) {
          currentPage = data[0] + (data[1] << 8);
        }
      }
    }

    uint32_t totalPages = 0;
    {
      FsFile f;
      if (Storage.openFileForRead("HOM", indexPath, f)) {
        uint32_t magic = 0;
        uint8_t version = 0;
        uint32_t fileSize = 0;
        int32_t viewportWidth = 0;
        int32_t linesPerPage = 0;
        int32_t fontId = 0;
        int32_t margin = 0;
        uint8_t paragraphAlignment = 0;

        serialization::readPod(f, magic);
        serialization::readPod(f, version);
        serialization::readPod(f, fileSize);
        serialization::readPod(f, viewportWidth);
        serialization::readPod(f, linesPerPage);
        serialization::readPod(f, fontId);
        serialization::readPod(f, margin);
        serialization::readPod(f, paragraphAlignment);
        serialization::readPod(f, totalPages);
        f.close();
      }
    }

    if (totalPages > 0) {
      return clampPercent((currentPage + 1) * 100 / static_cast<int>(totalPages));
    }
    return 0;
  }

  if (StringUtils::checkFileExtension(book.path, ".xtch") || StringUtils::checkFileExtension(book.path, ".xtc")) {
    const std::string progressPath = std::string("/.crosspoint/xtc_") + std::to_string(hash) + "/progress.bin";
    uint32_t currentPage = 0;
    uint32_t totalPages = 0;

    {
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
    }

    if (totalPages == 0) {
      FsFile f;
      if (Storage.openFileForRead("HOM", book.path, f)) {
        xtc::XtcHeader header{};
        if (f.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) == sizeof(header) &&
            (header.magic == xtc::XTC_MAGIC || header.magic == xtc::XTCH_MAGIC)) {
          totalPages = header.pageCount;
        }
        f.close();
      }
    }

    if (totalPages > 0) {
      return clampPercent(static_cast<int>((currentPage + 1) * 100 / totalPages));
    }
    return 0;
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
          onMyLibraryOpen();
        }
        return;
      }

      int idx = 0;
      const int menuSelectedIndex = selectorIndex - recentCount;
      const int myLibraryIdx = idx++;
#ifndef DISABLE_OPDS
      const int opdsLibraryIdx = hasOpdsUrl ? idx++ : -1;
#else
      const int opdsLibraryIdx = -1;
#endif
      const int fileTransferIdx = idx++;
      const int gameIdx = idx++;
      const int petIdx = idx++;
      const int settingsIdx = idx;

      if (menuSelectedIndex == myLibraryIdx) {
        onMyLibraryOpen();
#ifndef DISABLE_OPDS
      } else if (menuSelectedIndex == opdsLibraryIdx) {
        if (onOpdsBrowserOpen) onOpdsBrowserOpen();
#endif
      } else if (menuSelectedIndex == fileTransferIdx) {
        onFileTransferOpen();
      } else if (menuSelectedIndex == gameIdx) {
        if (onGameOpen) onGameOpen();
      } else if (menuSelectedIndex == petIdx) {
        if (onPetOpen) onPetOpen();
      } else if (menuSelectedIndex == settingsIdx) {
        onSettingsOpen();
      }
      return;
    }

    if (selectorIndex == 0) {
      if (!recentBooks.empty()) {
        onSelectBook(recentBooks[0].path);
      } else {
        onMyLibraryOpen();
      }
      return;
    }

    if (selectorIndex == 1) {
      onRecentsOpen();
      return;
    }

    int idx = 0;
    if (selectorIndex == 2) {
      if (onPetOpen) onPetOpen();
      return;
    }

    const int menuSelectedIndex = selectorIndex - 3;
    const int myLibraryIdx = idx++;
#ifndef DISABLE_OPDS
    const int opdsLibraryIdx = hasOpdsUrl ? idx++ : -1;
#else
    const int opdsLibraryIdx = -1;
#endif
    const int fileTransferIdx = idx++;
    const int gameIdx = idx++;
    const int settingsIdx = idx;

    if (menuSelectedIndex == myLibraryIdx) {
      onMyLibraryOpen();
#ifndef DISABLE_OPDS
    } else if (menuSelectedIndex == opdsLibraryIdx) {
      if (onOpdsBrowserOpen) onOpdsBrowserOpen();
#endif
    } else if (menuSelectedIndex == fileTransferIdx) {
      onFileTransferOpen();
    } else if (menuSelectedIndex == gameIdx) {
      if (onGameOpen) onGameOpen();
    } else if (menuSelectedIndex == settingsIdx) {
      onSettingsOpen();
    }
  }
}

void HomeActivity::render(Activity::RenderLock&&) {
  auto metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const bool isCardsTheme = SETTINGS.uiTheme == CrossPointSettings::UI_THEME::CARDS;

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);

  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), "Network", tr(STR_GAMES),
                                        tr(STR_VIRTUAL_PET), tr(STR_SETTINGS_TITLE)};
  std::vector<const uint8_t*> menuIcons = {Folder2Icon, Wifi_wideIcon, GameIcon, PetIcon, CogIcon};

#ifndef DISABLE_OPDS
  if (hasOpdsUrl) {
    menuItems.insert(menuItems.begin() + 1, tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 1, FolderIcon);
  }
#endif

  if (!isCardsTheme) {
    const int coverSlots = std::max(1, metrics.homeRecentBooksCount);
    const int recentCount = std::max(1, std::min(static_cast<int>(recentBooks.size()), coverSlots));
    const int selectedMenuIndex = selectorIndex - recentCount;

    bool bufferRestored = false;
    if (coverBufferStored) {
      bufferRestored = restoreCoverBuffer();
    }

    GUI.drawRecentBookCover(renderer,
                            Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                            recentBooks,
                            selectorIndex,
                            coverRendered,
                            coverBufferStored,
                            bufferRestored,
                            [this] { return storeCoverBuffer(); });

    std::vector<UIIcon> menuThemeIcons = {UIIcon::Folder, UIIcon::Wifi, UIIcon::Book, UIIcon::Book, UIIcon::Settings};
#ifndef DISABLE_OPDS
    if (hasOpdsUrl) {
      menuThemeIcons.insert(menuThemeIcons.begin() + 1, UIIcon::Library);
    }
#endif

    const int menuY = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing;
    const int menuHeight = pageHeight - metrics.buttonHintsHeight - menuY - metrics.verticalSpacing;
    GUI.drawButtonMenu(renderer,
                       Rect{0, menuY, pageWidth, std::max(0, menuHeight)},
                       static_cast<int>(menuItems.size()),
                       selectedMenuIndex,
                       [&menuItems](int index) { return std::string(menuItems[index]); },
                       [&menuThemeIcons](int index) { return menuThemeIcons[index]; });

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

  std::vector<const char*> cardsMenuItems = {tr(STR_BROWSE_FILES), "Network", tr(STR_GAMES),
                                             tr(STR_SETTINGS_TITLE)};
  std::vector<const uint8_t*> cardsMenuIcons = {Folder2Icon, Wifi_wideIcon, GameIcon, CogIcon};
#ifndef DISABLE_OPDS
  if (hasOpdsUrl) {
    cardsMenuItems.insert(cardsMenuItems.begin() + 1, tr(STR_OPDS_BROWSER));
    cardsMenuIcons.insert(cardsMenuIcons.begin() + 1, FolderIcon);
  }
#endif

  const int lowerCount = static_cast<int>(cardsMenuItems.size());
  const int lowerRows = (lowerCount + 1) / 2;

  const int contentX = metrics.contentSidePadding;
  const int contentWidth = pageWidth - metrics.contentSidePadding * 2;
  const int columnGap = metrics.menuSpacing * 2;
  const int cardWidth = (contentWidth - columnGap) / 2;

  constexpr int minTopCardHeight = 220;
  constexpr int minLowerCardHeight = 90;
  const int maxTopCardHeight = pageHeight - metrics.homeTopPadding - metrics.buttonHintsHeight -
                               metrics.verticalSpacing * 2 - (lowerRows - 1) * metrics.menuSpacing -
                               lowerRows * minLowerCardHeight;
  const int desiredTopCardHeight = (pageHeight * 46) / 100;
  int topCardHeight = std::clamp(desiredTopCardHeight, minTopCardHeight, std::max(minTopCardHeight, maxTopCardHeight));

  const int topCardsY = metrics.homeTopPadding;
  const int previewX = contentX;
  const int recentX = contentX + cardWidth + columnGap;

  // Left top card: full-cover preview with in-card overlay text
  const bool previewSelected = selectorIndex == 0;
  bool drewPreviewCover = false;

  if (!recentBooks.empty() && !recentBooks[0].coverBmpPath.empty()) {
    const std::string coverPath = UITheme::getCoverThumbPath(recentBooks[0].coverBmpPath, topCardHeight);

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
        renderer.drawBitmap(bitmap, previewX, topCardsY, cardWidth, topCardHeight, cropX, cropY);
        drewPreviewCover = true;
      }
      file.close();
    }
  }

  if (!drewPreviewCover) {
    renderer.fillRect(previewX, topCardsY, cardWidth, topCardHeight, false);
    renderer.drawIcon(BookIcon, previewX + (cardWidth - 32) / 2, topCardsY + (topCardHeight - 32) / 2, 32, 32);
  }

  const int overlayH = renderer.getLineHeight(UI_10_FONT_ID) + renderer.getLineHeight(UI_12_FONT_ID) + 14;
  const int overlayY = topCardsY + topCardHeight - overlayH - 8;
  renderer.fillRect(previewX + 8, overlayY, cardWidth - 16, overlayH, false);
  renderer.drawRect(previewX + 8, overlayY, cardWidth - 16, overlayH, true);

  const char* continueText = tr(STR_CONTINUE_READING);
  std::string title = !recentBooks.empty() ? recentBooks[0].title : tr(STR_NO_OPEN_BOOK);
  title = renderer.truncatedText(UI_12_FONT_ID, title.c_str(), cardWidth - 24, EpdFontFamily::BOLD);

  const int continueW = renderer.getTextWidth(UI_10_FONT_ID, continueText);
  renderer.drawText(UI_10_FONT_ID, previewX + (cardWidth - continueW) / 2, overlayY + 5, continueText, true);

  const int titleW = renderer.getTextWidth(UI_12_FONT_ID, title.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, previewX + (cardWidth - titleW) / 2,
                    overlayY + 5 + renderer.getLineHeight(UI_10_FONT_ID) + 2, title.c_str(), true,
                    EpdFontFamily::BOLD);

  renderer.drawRoundedRect(previewX, topCardsY, cardWidth, topCardHeight, 1, 8, true);
  if (previewSelected) {
    renderer.drawRoundedRect(previewX + 2, topCardsY + 2, cardWidth - 4, topCardHeight - 4, 1, 7, true);
  }

  // Right column split: recent books card on top (60%), pet card beneath (40%)
  constexpr int rightColumnGap = 10;
  const int recentCardHeight = (topCardHeight - rightColumnGap) * 3 / 5;
  const int petCardHeight = topCardHeight - rightColumnGap - recentCardHeight;

  // Right top card: recent books + progress bars
  const bool recentSelected = selectorIndex == 1;
  renderer.fillRoundedRect(recentX, topCardsY, cardWidth, recentCardHeight, 8,
                           recentSelected ? Color::LightGray : Color::White);
  renderer.drawRoundedRect(recentX, topCardsY, cardWidth, recentCardHeight, 1, 8, true);
  if (recentSelected) {
    renderer.drawRoundedRect(recentX + 2, topCardsY + 2, cardWidth - 4, recentCardHeight - 4, 1, 7, true);
  }

  std::string recentsHeader = renderer.truncatedText(UI_12_FONT_ID, tr(STR_MENU_RECENT_BOOKS), cardWidth - 20,
                                                     EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, recentX + 10, topCardsY + 8, recentsHeader.c_str(), true, EpdFontFamily::BOLD);

  const int listTop = topCardsY + 8 + renderer.getLineHeight(UI_12_FONT_ID) + 6;
  const int titleLineH = renderer.getLineHeight(SMALL_FONT_ID);
  constexpr int progressBarH = 3;
  constexpr int rowItemGap = 3;
  const int rowUnit = titleLineH * 2 + progressBarH + rowItemGap;
  const int listAvail = recentCardHeight - (listTop - topCardsY) - 8;
  const int rowCount = std::min(4, std::max(1, listAvail / rowUnit));

  for (int row = 0; row < rowCount; ++row) {
    const int rowY = listTop + row * rowUnit;
    if (rowY + titleLineH > topCardsY + recentCardHeight - 4) {
      break;
    }

    if (row < static_cast<int>(recentBooks.size())) {
      const RecentBook& book = recentBooks[row];
      const int progress = getRecentBookProgressPercent(book);
      const int titleAvailW = cardWidth - 20;

      // Two-line title: wrap at word boundary if the full title does not fit
      std::string line1, line2;
      if (renderer.getTextWidth(SMALL_FONT_ID, book.title.c_str()) <= titleAvailW) {
        line1 = book.title;
      } else {
        // Find how many characters fit on line 1
        size_t fitsLen = 0;
        for (size_t len = 1; len <= book.title.size(); ++len) {
          if (renderer.getTextWidth(SMALL_FONT_ID, book.title.substr(0, len).c_str()) > titleAvailW) break;
          fitsLen = len;
        }
        // Split at the last space within the fitting portion for a clean word wrap
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
      if (!line2.empty() && rowY + titleLineH + titleLineH <= topCardsY + recentCardHeight - 4) {
        renderer.drawText(SMALL_FONT_ID, recentX + 10, rowY + titleLineH, line2.c_str(), true);
      }

      // Progress bar below the two-line title area
      const int barY = rowY + titleLineH * 2 + 1;
      const int barX = recentX + 10;
      const int barW = cardWidth - 20;
      if (barY + progressBarH <= topCardsY + recentCardHeight - 4) {
        renderer.fillRect(barX, barY, barW, progressBarH, false);
        renderer.drawRect(barX, barY, barW, progressBarH, true);
        if (progress > 0) {
          const int fillW = std::max(1, (barW - 2) * progress / 100);
          renderer.fillRect(barX + 1, barY + 1, fillW, progressBarH - 2, true);
        }
      }
    } else {
      renderer.drawText(SMALL_FONT_ID, recentX + 10, rowY, "-", true);
    }
  }

  const int petCardY = topCardsY + recentCardHeight + rightColumnGap;
  const bool petSelected = selectorIndex == 2;
  renderer.fillRoundedRect(recentX, petCardY, cardWidth, petCardHeight, 8, petSelected ? Color::LightGray : Color::White);
  renderer.drawRoundedRect(recentX, petCardY, cardWidth, petCardHeight, 1, 8, true);
  if (petSelected) {
    renderer.drawRoundedRect(recentX + 2, petCardY + 2, cardWidth - 4, petCardHeight - 4, 1, 7, true);
  }

  PET_MANAGER.tick();
  const auto& petState = PET_MANAGER.state();
  const char* petHeaderLabel = tr(STR_VIRTUAL_PET);
  if (petState.alive && isDisplayablePetName(petState.petName)) {
    petHeaderLabel = petState.petName;
  }
  std::string petHeader = renderer.truncatedText(UI_12_FONT_ID, petHeaderLabel, cardWidth - 20, EpdFontFamily::BOLD);
  const int petHeaderW = renderer.getTextWidth(UI_12_FONT_ID, petHeader.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, recentX + (cardWidth - petHeaderW) / 2, petCardY + 8, petHeader.c_str(), true,
                    EpdFontFamily::BOLD);

  const char* petMood = tr(STR_PET_NO_PET);
  if (petState.alive) {
    switch (petState.mood) {
      case PetMood::Happy:
        petMood = tr(STR_PET_MOOD_HAPPY);
        break;
      case PetMood::Normal:
        petMood = tr(STR_PET_MOOD_NORMAL);
        break;
      case PetMood::Sad:
        petMood = tr(STR_PET_MOOD_SAD);
        break;
      case PetMood::Sick:
        petMood = tr(STR_PET_MOOD_SICK);
        break;
      case PetMood::Sleeping:
        petMood = tr(STR_PET_MOOD_SLEEPING);
        break;
      case PetMood::Dead:
        petMood = tr(STR_PET_MOOD_DEAD);
        break;
      case PetMood::Needy:
        petMood = "Needy";
        break;
      case PetMood::Refusing:
        petMood = "Refusing";
        break;
      default:
        petMood = tr(STR_PET_MOOD_NORMAL);
        break;
    }
  }

  const int petTextTop = petCardY + 8 + renderer.getLineHeight(UI_12_FONT_ID) + 4;
  const int petTextWidth = cardWidth - 20;
  std::string moodText = renderer.truncatedText(SMALL_FONT_ID, petMood, petTextWidth, EpdFontFamily::BOLD);
  const int moodW = renderer.getTextWidth(SMALL_FONT_ID, moodText.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, recentX + (cardWidth - moodW) / 2, petTextTop, moodText.c_str(), true, EpdFontFamily::BOLD);

  const int level = PET_MANAGER.getPetLevel();
  const int levelProgress = PET_MANAGER.getLevelProgressPercent();
  char levelText[20];
  snprintf(levelText, sizeof(levelText), "Lv %d", level);
  const int levelY = petTextTop + renderer.getLineHeight(SMALL_FONT_ID) + 2;
  const int levelW = renderer.getTextWidth(SMALL_FONT_ID, levelText, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, recentX + 10, levelY, levelText, true, EpdFontFamily::BOLD);

  const int barX = recentX + 10 + levelW + 8;
  const int barW = std::max(20, cardWidth - 20 - levelW - 8);
  renderer.drawRect(barX, levelY + 1, barW, 8, true);
  const int fill = ((barW - 2) * std::max(0, std::min(100, levelProgress))) / 100;
  if (fill > 0) {
    renderer.fillRect(barX + 1, levelY + 2, fill, 6, true);
  }

  const int spriteTop = levelY + renderer.getLineHeight(SMALL_FONT_ID) + 4;
  const int spriteBottom = petCardY + petCardHeight - 8;
  const int availableH = spriteBottom - spriteTop;
  const int scale = (availableH >= PetSpriteRenderer::displaySize(2)) ? 2 : 1;
  const int spriteSize = PetSpriteRenderer::displaySize(scale);
  const int spriteY = spriteTop + std::max(0, (availableH - spriteSize) / 2);
  PetSpriteRenderer::drawPet(renderer, recentX + (cardWidth - spriteSize) / 2, spriteY, petState.stage, petState.mood,
                            scale, petState.evolutionVariant, petState.petType, 0);

  constexpr int nativeIconSize = 96;

  // Bottom action cards: fill remaining space between top cards and button hints
  const int lowerY = topCardsY + topCardHeight + metrics.verticalSpacing;
  const int selectedLower = selectorIndex - 3;
  const int availForLower = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - lowerY;
  const int lowerCardHeight = std::max(60, (availForLower - (lowerRows - 1) * metrics.menuSpacing) / lowerRows);

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

    // Icon fills most of card with label overlaid at top
    const int labelH = renderer.getLineHeight(UI_12_FONT_ID);
    std::string label = renderer.truncatedText(UI_12_FONT_ID, cardsMenuItems[i], cardWidth - 16, EpdFontFamily::BOLD);
    const int labelW = renderer.getTextWidth(UI_12_FONT_ID, label.c_str(), EpdFontFamily::BOLD);
    const int labelY = y + 8;
    renderer.fillRectDither(x + 6, labelY - 1, cardWidth - 12, labelH + 2,
                 selected ? Color::LightGray : Color::White);
    renderer.drawText(UI_12_FONT_ID, x + (cardWidth - labelW) / 2, labelY, label.c_str(), true, EpdFontFamily::BOLD);

    const int iconY = y + labelH + 10 + std::max(0, (lowerCardHeight - labelH - 18 - nativeIconSize) / 2);
    renderer.drawIcon(cardsMenuIcons[i], x + (cardWidth - nativeIconSize) / 2, iconY, nativeIconSize, nativeIconSize);
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
