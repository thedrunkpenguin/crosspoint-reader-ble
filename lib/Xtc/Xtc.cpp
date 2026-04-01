/**
 * Xtc.cpp
 *
 * Main XTC ebook class implementation
 * XTC ebook support for CrossPoint Reader
 */

#include "Xtc.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <new>

namespace {

inline void markMonoBitmapBlack(std::vector<uint8_t>& bitmap, const size_t rowSize, const uint16_t x,
                                const uint16_t y) {
  const size_t byteIndex = static_cast<size_t>(y) * rowSize + (x / 8);
  bitmap[byteIndex] &= static_cast<uint8_t>(~(1u << (7 - (x % 8))));
}

bool buildMonochromePageBitmap(const Xtc& xtc, const xtc::PageInfo& pageInfo, const uint32_t pageIndex,
                               const uint16_t outWidth, const uint16_t outHeight,
                               std::vector<uint8_t>& outBitmap) {
  if (pageInfo.width == 0 || pageInfo.height == 0 || outWidth == 0 || outHeight == 0) {
    return false;
  }

  const size_t dstRowSize = (outWidth + 7) / 8;
  outBitmap.assign(dstRowSize * outHeight, 0xFF);

  const bool isDownscaled = outWidth < pageInfo.width || outHeight < pageInfo.height;

  // Keep only a tiny per-output-pixel darkness score when downscaling. For
  // full-size covers we write pixels directly to avoid any large extra buffer.
  std::vector<uint8_t> darknessScores;
  if (isDownscaled) {
    darknessScores.assign(static_cast<size_t>(outWidth) * outHeight, 0);
  }

  auto accumulateScaledDarkness = [&](const uint16_t srcX, const uint16_t srcY, const uint8_t darkness) {
    const uint16_t dstX = std::min<uint16_t>(outWidth - 1,
                                             static_cast<uint16_t>((static_cast<uint32_t>(srcX) * outWidth) / pageInfo.width));
    const uint16_t dstY = std::min<uint16_t>(outHeight - 1,
                                             static_cast<uint16_t>((static_cast<uint32_t>(srcY) * outHeight) / pageInfo.height));

    if (!isDownscaled) {
      if (darkness >= 160) {
        markMonoBitmapBlack(outBitmap, dstRowSize, dstX, dstY);
      }
      return;
    }

    const size_t idx = static_cast<size_t>(dstY) * outWidth + dstX;
    uint8_t increment = 0;
    if (darkness >= 224) {
      increment = 4;
    } else if (darkness >= 160) {
      increment = 2;
    } else if (darkness >= 96) {
      increment = 1;
    }

    const uint16_t sum = static_cast<uint16_t>(darknessScores[idx]) + increment;
    darknessScores[idx] = static_cast<uint8_t>(std::min<uint16_t>(255, sum));
  };

  if (pageInfo.bitDepth == 2) {
    const size_t planeSize = (static_cast<size_t>(pageInfo.width) * pageInfo.height + 7) / 8;
    const size_t colBytes = (pageInfo.height + 7) / 8;
    const uint32_t pageDataOffset = pageInfo.offset + sizeof(xtc::XtgPageHeader);

    HalStorage::LockGuard storageLock(Storage);
    FsFile plane1File;
    FsFile plane2File;
    if (!Storage.openFileForRead("XTC", xtc.getPath(), plane1File) ||
        !Storage.openFileForRead("XTC", xtc.getPath(), plane2File)) {
      LOG_ERR("XTC", "Failed to open XTCH page for BMP generation: %s", xtc.getPath().c_str());
      if (plane1File) plane1File.close();
      if (plane2File) plane2File.close();
      return false;
    }

    if (!plane1File.seek(pageDataOffset) || !plane2File.seek(pageDataOffset + planeSize)) {
      LOG_ERR("XTC", "Failed to seek XTCH page for BMP generation");
      plane1File.close();
      plane2File.close();
      return false;
    }

    uint8_t plane1Chunk[1024];
    uint8_t plane2Chunk[1024];
    size_t remaining = planeSize;
    size_t planeOffset = 0;

    while (remaining > 0) {
      const size_t toRead = std::min(remaining, sizeof(plane1Chunk));
      const size_t bytesRead1 = plane1File.read(plane1Chunk, toRead);
      const size_t bytesRead2 = plane2File.read(plane2Chunk, toRead);
      if (bytesRead1 != toRead || bytesRead2 != toRead) {
        LOG_ERR("XTC", "Short read while streaming XTCH page for BMP: %u/%u expected %u", bytesRead1, bytesRead2,
                toRead);
        plane1File.close();
        plane2File.close();
        return false;
      }

      for (size_t i = 0; i < toRead; i++) {
        const size_t packedOffset = planeOffset + i;
        const size_t colIndex = packedOffset / colBytes;
        if (colIndex >= pageInfo.width) {
          continue;
        }

        const uint16_t srcX = pageInfo.width - 1 - static_cast<uint16_t>(colIndex);
        const uint16_t yBase = static_cast<uint16_t>((packedOffset % colBytes) * 8);
        for (uint8_t bit = 0; bit < 8; bit++) {
          const uint16_t srcY = yBase + bit;
          if (srcY >= pageInfo.height) {
            break;
          }

          const bool bit1 = ((plane1Chunk[i] >> (7 - bit)) & 0x01) != 0;
          const bool bit2 = ((plane2Chunk[i] >> (7 - bit)) & 0x01) != 0;

          // XTCH uses 2-bit grayscale. Reconstruct approximate darkness instead
          // of collapsing every non-white dither dot to black.
          uint8_t darkness = 0;
          if (bit1 && bit2) {
            darkness = 255;  // black
          } else if (!bit1 && bit2) {
            darkness = 170;  // dark gray
          } else if (bit1 && !bit2) {
            darkness = 96;  // light gray
          }
          accumulateScaledDarkness(srcX, srcY, darkness);
        }
      }

      planeOffset += toRead;
      remaining -= toRead;
    }

    plane1File.close();
    plane2File.close();
  } else {
    const size_t srcRowSize = (pageInfo.width + 7) / 8;
    const xtc::XtcError streamErr = xtc.loadPageStreaming(
        pageIndex,
        [&](const uint8_t* data, const size_t size, const size_t offset) {
          for (size_t i = 0; i < size; i++) {
            const size_t globalByte = offset + i;
            const uint16_t srcY = globalByte / srcRowSize;
            if (srcY >= pageInfo.height) {
              break;
            }

            const uint16_t srcByteInRow = globalByte % srcRowSize;
            const uint16_t srcXBase = srcByteInRow * 8;
            const uint8_t packed = data[i];
            for (uint8_t bit = 0; bit < 8; bit++) {
              const uint16_t srcX = srcXBase + bit;
              if (srcX >= pageInfo.width) {
                break;
              }
              const bool isBlack = ((packed >> (7 - bit)) & 0x01) == 0;
              accumulateScaledDarkness(srcX, srcY, isBlack ? 255 : 0);
            }
          }
        },
        1024);

    if (streamErr != xtc::XtcError::OK) {
      LOG_ERR("XTC", "Failed to stream page %lu for BMP generation (%s)", pageIndex, xtc::errorToString(streamErr));
      return false;
    }
  }

  if (isDownscaled) {
    const uint32_t approxSamplesPerPixel =
        std::max<uint32_t>(1, ((static_cast<uint32_t>(pageInfo.width) + outWidth - 1) / outWidth) *
                                  ((static_cast<uint32_t>(pageInfo.height) + outHeight - 1) / outHeight));
    const uint8_t darknessThreshold = static_cast<uint8_t>(std::min<uint32_t>(24, approxSamplesPerPixel + 2));

    for (uint16_t y = 0; y < outHeight; y++) {
      for (uint16_t x = 0; x < outWidth; x++) {
        const size_t idx = static_cast<size_t>(y) * outWidth + x;
        if (darknessScores[idx] >= darknessThreshold) {
          markMonoBitmapBlack(outBitmap, dstRowSize, x, y);
        }
      }
    }
  }

  return true;
}

}  // namespace

bool Xtc::load() {
  try {
    LOG_DBG("XTC", "Loading XTC: %s", filepath.c_str());

    // Initialize parser
    parser.reset(new xtc::XtcParser());

    // Open XTC file
    xtc::XtcError err = parser->open(filepath.c_str());
    if (err != xtc::XtcError::OK) {
      LOG_ERR("XTC", "Failed to load: %s", xtc::errorToString(err));
      parser.reset();
      return false;
    }

    loaded = true;
    LOG_DBG("XTC", "Loaded XTC: %s (%lu pages)", filepath.c_str(), parser->getPageCount());
    return true;
  } catch (const std::bad_alloc&) {
    LOG_ERR("XTC", "Out of memory while loading XTC: %s", filepath.c_str());
  } catch (const std::exception& e) {
    LOG_ERR("XTC", "Exception while loading XTC: %s", e.what());
  } catch (...) {
    LOG_ERR("XTC", "Unknown exception while loading XTC");
  }

  parser.reset();
  loaded = false;
  return false;
}

bool Xtc::clearCache() const {
  if (!Storage.exists(cachePath.c_str())) {
    LOG_DBG("XTC", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.removeDir(cachePath.c_str())) {
    LOG_ERR("XTC", "Failed to clear cache");
    return false;
  }

  LOG_DBG("XTC", "Cache cleared successfully");
  return true;
}

void Xtc::setupCacheDir() const {
  if (Storage.exists(cachePath.c_str())) {
    return;
  }

  // `Storage.mkdir(..., true)` already creates parent directories recursively.
  // Avoid repeated `substr()` allocations here because Home may call this while
  // heap is tight right after exiting a large XTC book.
  Storage.mkdir(cachePath.c_str());
}

std::string Xtc::getTitle() const {
  if (!loaded || !parser) {
    return "";
  }

  // Try to get title from XTC metadata first
  std::string title = parser->getTitle();
  if (!title.empty()) {
    return title;
  }

  // Fallback: extract filename from path as title
  size_t lastSlash = filepath.find_last_of('/');
  size_t lastDot = filepath.find_last_of('.');

  if (lastSlash == std::string::npos) {
    lastSlash = 0;
  } else {
    lastSlash++;
  }

  if (lastDot == std::string::npos || lastDot <= lastSlash) {
    return filepath.substr(lastSlash);
  }

  return filepath.substr(lastSlash, lastDot - lastSlash);
}

std::string Xtc::getAuthor() const {
  if (!loaded || !parser) {
    return "";
  }

  // Try to get author from XTC metadata
  return parser->getAuthor();
}

bool Xtc::hasChapters() const {
  if (!loaded || !parser) {
    return false;
  }
  return parser->hasChapters();
}

const std::vector<xtc::ChapterInfo>& Xtc::getChapters() const {
  static const std::vector<xtc::ChapterInfo> kEmpty;
  if (!loaded || !parser) {
    return kEmpty;
  }
  return parser->getChapters();
}

std::string Xtc::getCoverBmpPath() const { return cachePath + "/cover_v2.bmp"; }

bool Xtc::generateCoverBmp() const {
  try {
    if (Storage.exists(getCoverBmpPath().c_str())) {
      return true;
    }

    if (!loaded || !parser) {
      LOG_ERR("XTC", "Cannot generate cover BMP, file not loaded");
      return false;
    }

    if (parser->getPageCount() == 0) {
      LOG_ERR("XTC", "No pages in XTC file");
      return false;
    }

    setupCacheDir();

    xtc::PageInfo pageInfo;
    if (!parser->getPageInfo(0, pageInfo)) {
      LOG_DBG("XTC", "Failed to get first page info");
      return false;
    }

    std::vector<uint8_t> coverBitmap;
    if (!buildMonochromePageBitmap(*this, pageInfo, 0, pageInfo.width, pageInfo.height, coverBitmap)) {
      return false;
    }

    FsFile coverBmp;
    if (!Storage.openFileForWrite("XTC", getCoverBmpPath(), coverBmp)) {
      LOG_DBG("XTC", "Failed to create cover BMP file");
      return false;
    }

  const uint32_t rowSize = ((pageInfo.width + 31) / 32) * 4;
  const uint32_t imageSize = rowSize * pageInfo.height;
  const uint32_t fileSize = 14 + 40 + 8 + imageSize;

  coverBmp.write('B');
  coverBmp.write('M');
  coverBmp.write(reinterpret_cast<const uint8_t*>(&fileSize), 4);
  uint32_t reserved = 0;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&reserved), 4);
  uint32_t dataOffset = 14 + 40 + 8;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&dataOffset), 4);

  uint32_t dibHeaderSize = 40;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&dibHeaderSize), 4);
  int32_t width = pageInfo.width;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&width), 4);
  int32_t height = -static_cast<int32_t>(pageInfo.height);
  coverBmp.write(reinterpret_cast<const uint8_t*>(&height), 4);
  uint16_t planes = 1;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&planes), 2);
  uint16_t bitsPerPixel = 1;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&bitsPerPixel), 2);
  uint32_t compression = 0;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&compression), 4);
  coverBmp.write(reinterpret_cast<const uint8_t*>(&imageSize), 4);
  int32_t ppmX = 2835;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&ppmX), 4);
  int32_t ppmY = 2835;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&ppmY), 4);
  uint32_t colorsUsed = 2;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&colorsUsed), 4);
  uint32_t colorsImportant = 2;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&colorsImportant), 4);

  uint8_t black[4] = {0x00, 0x00, 0x00, 0x00};
  coverBmp.write(black, 4);
  uint8_t white[4] = {0xFF, 0xFF, 0xFF, 0x00};
  coverBmp.write(white, 4);

  const size_t dstRowSize = (pageInfo.width + 7) / 8;
  for (uint16_t y = 0; y < pageInfo.height; y++) {
    coverBmp.write(coverBitmap.data() + static_cast<size_t>(y) * dstRowSize, dstRowSize);

    uint8_t padding[4] = {0, 0, 0, 0};
    const size_t paddingSize = rowSize - dstRowSize;
    if (paddingSize > 0) {
      coverBmp.write(padding, paddingSize);
    }
  }

    coverBmp.close();
    LOG_DBG("XTC", "Generated cover BMP: %s", getCoverBmpPath().c_str());
    return true;
  } catch (const std::bad_alloc&) {
    LOG_ERR("XTC", "Out of memory while generating cover BMP");
  } catch (const std::exception& e) {
    LOG_ERR("XTC", "Exception while generating cover BMP: %s", e.what());
  } catch (...) {
    LOG_ERR("XTC", "Unknown exception while generating cover BMP");
  }

  return false;
}

std::string Xtc::getThumbBmpPath() const { return cachePath + "/thumb_v3_[HEIGHT].bmp"; }
std::string Xtc::getThumbBmpPath(int height) const { return cachePath + "/thumb_v3_" + std::to_string(height) + ".bmp"; }

bool Xtc::generateThumbBmp(int height) const {
  try {
    // Already generated
    if (Storage.exists(getThumbBmpPath(height).c_str())) {
      return true;
    }

    if (!loaded || !parser) {
      LOG_ERR("XTC", "Cannot generate thumb BMP, file not loaded");
      return false;
    }

    if (parser->getPageCount() == 0) {
      LOG_ERR("XTC", "No pages in XTC file");
      return false;
    }

    // Setup cache directory
    setupCacheDir();

    // Get first page info for cover
    xtc::PageInfo pageInfo;
    if (!parser->getPageInfo(0, pageInfo)) {
      LOG_DBG("XTC", "Failed to get first page info");
      return false;
    }

  // Use requested height (capped) for better quality while keeping memory bounded.
  const int thumbTargetHeight = std::max(180, std::min(height, 320));
  const int thumbTargetWidth = std::max(120, (thumbTargetHeight * 2) / 3);

  // Calculate scale factor
  float scaleX = static_cast<float>(thumbTargetWidth) / pageInfo.width;
  float scaleY = static_cast<float>(thumbTargetHeight) / pageInfo.height;
  float scale = (scaleX < scaleY) ? scaleX : scaleY;  // fit inside target

  // Only scale down, never up
  if (scale >= 1.0f) {
    // Page is already small enough, try to just write a minimal cover
    LOG_DBG("XTC", "Page too small for thumbnail, skipping");
    // Write empty thumb to avoid retrying
    FsFile thumbBmp;
    if (Storage.openFileForWrite("XTC", getThumbBmpPath(height), thumbBmp)) {
      thumbBmp.close();
    }
    return false;
  }

  uint16_t thumbWidth = static_cast<uint16_t>(pageInfo.width * scale);
  uint16_t thumbHeight = static_cast<uint16_t>(pageInfo.height * scale);

  // Ensure minimum size
  if (thumbWidth < 1) thumbWidth = 1;
  if (thumbHeight < 1) thumbHeight = 1;

  LOG_DBG("XTC", "Generating thumb BMP: %dx%d -> %dx%d (scale: %.3f)", pageInfo.width, pageInfo.height,
          thumbWidth, thumbHeight, scale);

  std::vector<uint8_t> thumbBitmap;
  if (!buildMonochromePageBitmap(*this, pageInfo, 0, thumbWidth, thumbHeight, thumbBitmap)) {
    LOG_ERR("XTC", "Failed to generate thumbnail bitmap from page stream");
    return false;
  }

  // Create thumbnail BMP file - use 1-bit format for fast rendering
  FsFile thumbBmp;
  if (!Storage.openFileForWrite("XTC", getThumbBmpPath(height), thumbBmp)) {
    LOG_DBG("XTC", "Failed to create thumb BMP file");
    return false;
  }

  // Write 1-bit BMP header for fast home screen rendering
  const uint32_t rowSize = (thumbWidth + 31) / 32 * 4;  // 1 bit per pixel, aligned to 4 bytes
  const uint32_t imageSize = rowSize * thumbHeight;
  const uint32_t fileSize = 14 + 40 + 8 + imageSize;  // 8 bytes for 2-color palette

  // File header
  thumbBmp.write('B');
  thumbBmp.write('M');
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&fileSize), 4);
  uint32_t reserved = 0;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&reserved), 4);
  uint32_t dataOffset = 14 + 40 + 8;  // 1-bit palette has 2 colors (8 bytes)
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&dataOffset), 4);

  // DIB header
  uint32_t dibHeaderSize = 40;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&dibHeaderSize), 4);
  int32_t widthVal = thumbWidth;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&widthVal), 4);
  int32_t heightVal = -static_cast<int32_t>(thumbHeight);  // Negative for top-down
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&heightVal), 4);
  uint16_t planes = 1;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&planes), 2);
  uint16_t bitsPerPixel = 1;  // 1-bit for black and white
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&bitsPerPixel), 2);
  uint32_t compression = 0;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&compression), 4);
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&imageSize), 4);
  int32_t ppmX = 2835;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&ppmX), 4);
  int32_t ppmY = 2835;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&ppmY), 4);
  uint32_t colorsUsed = 2;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&colorsUsed), 4);
  uint32_t colorsImportant = 2;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&colorsImportant), 4);

  // Color palette (2 colors for 1-bit: black and white)
  uint8_t palette[8] = {
      0x00, 0x00, 0x00, 0x00,  // Color 0: Black
      0xFF, 0xFF, 0xFF, 0x00   // Color 1: White
  };
  thumbBmp.write(palette, 8);

  // Write bitmap rows (top-down).
  const size_t dstRowSize = (thumbWidth + 7) / 8;
  for (uint16_t y = 0; y < thumbHeight; y++) {
    thumbBmp.write(thumbBitmap.data() + static_cast<size_t>(y) * dstRowSize, dstRowSize);

    uint8_t padding[4] = {0, 0, 0, 0};
    size_t paddingSize = rowSize - dstRowSize;
    if (paddingSize > 0) {
      thumbBmp.write(padding, paddingSize);
    }
  }

    LOG_INF("XTC", "Generated thumb BMP (1-bit %dx%d)", thumbWidth, thumbHeight);
    thumbBmp.close();
    return true;
  } catch (const std::bad_alloc&) {
    LOG_ERR("XTC", "Out of memory while generating thumb BMP");
  } catch (const std::exception& e) {
    LOG_ERR("XTC", "Exception while generating thumb BMP: %s", e.what());
  } catch (...) {
    LOG_ERR("XTC", "Unknown exception while generating thumb BMP");
  }

  return false;
}

uint32_t Xtc::getPageCount() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getPageCount();
}

uint16_t Xtc::getPageWidth() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getWidth();
}

uint16_t Xtc::getPageHeight() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getHeight();
}

uint8_t Xtc::getBitDepth() const {
  if (!loaded || !parser) {
    return 1;  // Default to 1-bit
  }
  return parser->getBitDepth();
}

bool Xtc::getPageInfo(uint32_t pageIndex, xtc::PageInfo& info) const {
  if (!loaded || !parser) {
    return false;
  }
  return parser->getPageInfo(pageIndex, info);
}

size_t Xtc::loadPage(uint32_t pageIndex, uint8_t* buffer, size_t bufferSize) const {
  if (!loaded || !parser) {
    return 0;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPage(pageIndex, buffer, bufferSize);
}

xtc::XtcError Xtc::loadPageStreaming(uint32_t pageIndex,
                                     std::function<void(const uint8_t* data, size_t size, size_t offset)> callback,
                                     size_t chunkSize) const {
  if (!loaded || !parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPageStreaming(pageIndex, callback, chunkSize);
}

uint8_t Xtc::calculateProgress(uint32_t currentPage) const {
  if (!loaded || !parser || parser->getPageCount() == 0) {
    return 0;
  }
  return static_cast<uint8_t>((currentPage + 1) * 100 / parser->getPageCount());
}

xtc::XtcError Xtc::getLastError() const {
  if (!parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return parser->getLastError();
}
