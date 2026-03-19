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
#include "components/icons/wifi.h"
#include "components/icons/wifi_wide.h"
#include "fontIds.h"
#include "pet/PetManager.h"
#include "util/StringUtils.h"

int HomeActivity::getMenuItemCount() const {
  int count = 0;

  int actionCount = 5;  // Browse Files, File Transfer, Games, Pet, Settings
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
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      bool needsThumbGeneration = !Storage.exists(coverPath.c_str());

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
            if (isCardsTheme && StringUtils::checkFileExtension(book.path, ".epub") && bitmap.getBpp() == 1) {
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

      if (needsThumbGeneration) {
        Storage.remove(coverPath.c_str());
        if (StringUtils::checkFileExtension(book.path, ".epub")) {
          Epub epub(book.path, "/.crosspoint");
          epub.load(false, true);

          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / std::max(1, static_cast<int>(recentBooks.size()))));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (StringUtils::checkFileExtension(book.path, ".xtch") ||
                   StringUtils::checkFileExtension(book.path, ".xtc")) {
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / std::max(1, static_cast<int>(recentBooks.size()))));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
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

    {
      FsFile f;
      if (Storage.openFileForRead("HOM", progressPath, f)) {
        uint8_t data[4] = {0};
        const int read = f.read(data, 4);
        f.close();
        if (read == 4) {
          currentPage = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                        (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
        }
      }
    }

    Xtc xtc(book.path, "/.crosspoint");
    if (xtc.load()) {
      const uint32_t totalPages = xtc.getPageCount();
      if (totalPages > 0) {
        return clampPercent(static_cast<int>((currentPage + 1) * 100 / totalPages));
      }
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

  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_FILE_TRANSFER), tr(STR_GAMES),
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

  std::vector<const char*> cardsMenuItems = {tr(STR_BROWSE_FILES), tr(STR_FILE_TRANSFER), tr(STR_GAMES),
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

  // Right column split: recent books card on top, pet card beneath it
  constexpr int rightColumnGap = 10;
  const int recentCardHeight = (topCardHeight - rightColumnGap) / 2;
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
  const int rowCount = 4;
  const int rowHeight = std::max(renderer.getLineHeight(SMALL_FONT_ID),
                                 (recentCardHeight - (listTop - topCardsY) - 8) / rowCount);

  for (int row = 0; row < rowCount; ++row) {
    const int rowY = listTop + row * rowHeight;
    if (rowY + renderer.getLineHeight(SMALL_FONT_ID) > topCardsY + recentCardHeight - 4) {
      break;
    }

    if (row < static_cast<int>(recentBooks.size())) {
      const RecentBook& book = recentBooks[row];
      const int progress = getRecentBookProgressPercent(book);
      std::string bookTitle = renderer.truncatedText(SMALL_FONT_ID, book.title.c_str(), cardWidth - 58);

      renderer.drawText(SMALL_FONT_ID, recentX + 10, rowY, bookTitle.c_str(), true);

      char progressText[8];
      snprintf(progressText, sizeof(progressText), "%d%%", progress);
      const int progressW = renderer.getTextWidth(SMALL_FONT_ID, progressText);
      renderer.drawText(SMALL_FONT_ID, recentX + cardWidth - 10 - progressW, rowY, progressText, true);
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

  std::string petHeader = renderer.truncatedText(UI_12_FONT_ID, tr(STR_VIRTUAL_PET), cardWidth - 20, EpdFontFamily::BOLD);
  const int petHeaderW = renderer.getTextWidth(UI_12_FONT_ID, petHeader.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, recentX + (cardWidth - petHeaderW) / 2, petCardY + 8, petHeader.c_str(), true,
                    EpdFontFamily::BOLD);

  PET_MANAGER.tick();
  const auto& petState = PET_MANAGER.state();
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

  const int iconTopMin = petTextTop + renderer.getLineHeight(SMALL_FONT_ID) + 6;
  const int availableIconHeight = petCardY + petCardHeight - 8 - iconTopMin;
  const int petIconDrawSize = std::clamp(availableIconHeight, 32, 96);
  const int petIconY = petCardY + petCardHeight - 8 - petIconDrawSize;
  renderer.drawIcon(PetIcon, recentX + (cardWidth - petIconDrawSize) / 2, petIconY, petIconDrawSize, petIconDrawSize);

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
