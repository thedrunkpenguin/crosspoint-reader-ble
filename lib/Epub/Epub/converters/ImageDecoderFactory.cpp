#include "ImageDecoderFactory.h"

#include <HalStorage.h>
#include <Logging.h>

#include <memory>
#include <string>

#include "JpegToFramebufferConverter.h"
#include "PngToFramebufferConverter.h"

std::unique_ptr<JpegToFramebufferConverter> ImageDecoderFactory::jpegDecoder = nullptr;
std::unique_ptr<PngToFramebufferConverter> ImageDecoderFactory::pngDecoder = nullptr;

namespace {
enum class ImageFormat { UNKNOWN, JPEG, PNG, WEBP };

ImageFormat detectImageFormatFromFile(const std::string& imagePath) {
  if (!Storage.exists(imagePath.c_str())) {
    return ImageFormat::UNKNOWN;
  }

  FsFile file;
  if (!Storage.openFileForRead("DEC", imagePath, file)) {
    return ImageFormat::UNKNOWN;
  }

  uint8_t header[12] = {0};
  const int bytesRead = file.read(header, sizeof(header));
  file.close();

  if (bytesRead >= 3 && header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) {
    return ImageFormat::JPEG;
  }

  if (bytesRead >= 8 && header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47 &&
      header[4] == 0x0D && header[5] == 0x0A && header[6] == 0x1A && header[7] == 0x0A) {
    return ImageFormat::PNG;
  }

  if (bytesRead >= 12 && header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F' &&
      header[8] == 'W' && header[9] == 'E' && header[10] == 'B' && header[11] == 'P') {
    return ImageFormat::WEBP;
  }

  return ImageFormat::UNKNOWN;
}
}  // namespace

ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string& imagePath) {
  const ImageFormat detectedFormat = detectImageFormatFromFile(imagePath);
  if (detectedFormat == ImageFormat::JPEG) {
    if (!jpegDecoder) {
      jpegDecoder.reset(new JpegToFramebufferConverter());
    }
    return jpegDecoder.get();
  }

  if (detectedFormat == ImageFormat::PNG) {
    if (!pngDecoder) {
      pngDecoder.reset(new PngToFramebufferConverter());
    }
    return pngDecoder.get();
  }

  if (detectedFormat == ImageFormat::WEBP) {
    LOG_ERR("DEC", "Unsupported WebP image: %s", imagePath.c_str());
    return nullptr;
  }

  std::string ext = imagePath;
  size_t dotPos = ext.rfind('.');
  if (dotPos != std::string::npos) {
    ext = ext.substr(dotPos);
    for (auto& c : ext) {
      c = tolower(c);
    }
  } else {
    ext = "";
  }

  if (JpegToFramebufferConverter::supportsFormat(ext)) {
    if (!jpegDecoder) {
      jpegDecoder.reset(new JpegToFramebufferConverter());
    }
    return jpegDecoder.get();
  } else if (PngToFramebufferConverter::supportsFormat(ext)) {
    if (!pngDecoder) {
      pngDecoder.reset(new PngToFramebufferConverter());
    }
    return pngDecoder.get();
  }

  LOG_ERR("DEC", "No decoder found for image: %s", imagePath.c_str());
  return nullptr;
}

bool ImageDecoderFactory::isFormatSupported(const std::string& imagePath) { return getDecoder(imagePath) != nullptr; }
