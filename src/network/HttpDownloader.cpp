#include "HttpDownloader.h"

#include <HTTPClient.h>
#include <Logging.h>
#include <StreamString.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <base64.h>
#include <esp_task_wdt.h>

#include <cstring>
#include <memory>

#include "CrossPointSettings.h"
#include "util/UrlUtils.h"

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent) {
  // Use WiFiClientSecure for HTTPS, regular WiFiClient for HTTP
  std::unique_ptr<WiFiClient> client;
  if (UrlUtils::isHttpsUrl(url)) {
    auto* secureClient = new WiFiClientSecure();
    secureClient->setInsecure();
    client.reset(secureClient);
  } else {
    client.reset(new WiFiClient());
  }
  HTTPClient http;

  LOG_DBG("HTTP", "Fetching: %s", url.c_str());

  http.begin(*client, url.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  http.addHeader("Accept", "*/*");
  http.addHeader("Referer", "https://www.reddit.com/");

  // Add Basic HTTP auth if credentials are configured
  if (strlen(SETTINGS.opdsUsername) > 0 && strlen(SETTINGS.opdsPassword) > 0) {
    std::string credentials = std::string(SETTINGS.opdsUsername) + ":" + SETTINGS.opdsPassword;
    String encoded = base64::encode(credentials.c_str());
    http.addHeader("Authorization", "Basic " + encoded);
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    LOG_ERR("HTTP", "Fetch failed: %d", httpCode);
    http.end();
    return false;
  }

  http.writeToStream(&outContent);

  http.end();

  LOG_DBG("HTTP", "Fetch success");
  return true;
}

bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent) {
  StreamString stream;
  if (!fetchUrl(url, stream)) {
    return false;
  }
  outContent = stream.c_str();
  return true;
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress) {
  constexpr uint32_t kHttpRequestTimeoutMs = 15000;
  constexpr uint32_t kNoProgressTimeoutMs = 15000;
  constexpr uint32_t kUnknownLengthTotalTimeoutMs = 60000;
  constexpr uint32_t kMinTotalDownloadTimeoutMs = 30000;
  constexpr uint32_t kMaxTotalDownloadTimeoutMs = 180000;
  constexpr size_t kExpectedBytesPerSec = 25000;

  // Use WiFiClientSecure for HTTPS, regular WiFiClient for HTTP
  std::unique_ptr<WiFiClient> client;
  if (UrlUtils::isHttpsUrl(url)) {
    auto* secureClient = new WiFiClientSecure();
    secureClient->setInsecure();
    client.reset(secureClient);
  } else {
    client.reset(new WiFiClient());
  }
  HTTPClient http;

  LOG_DBG("HTTP", "Downloading: %s", url.c_str());
  LOG_DBG("HTTP", "Destination: %s", destPath.c_str());

  http.begin(*client, url.c_str());
  http.setTimeout(kHttpRequestTimeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  http.addHeader("Accept", "*/*");
  http.addHeader("Referer", "https://www.reddit.com/");

  // Add Basic HTTP auth if credentials are configured
  if (strlen(SETTINGS.opdsUsername) > 0 && strlen(SETTINGS.opdsPassword) > 0) {
    std::string credentials = std::string(SETTINGS.opdsUsername) + ":" + SETTINGS.opdsPassword;
    String encoded = base64::encode(credentials.c_str());
    http.addHeader("Authorization", "Basic " + encoded);
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    LOG_ERR("HTTP", "Download failed: %d", httpCode);
    http.end();
    return HTTP_ERROR;
  }

  const size_t contentLength = http.getSize();
  LOG_DBG("HTTP", "Content-Length: %zu", contentLength);

  uint32_t totalDownloadTimeoutMs = kUnknownLengthTotalTimeoutMs;
  if (contentLength > 0) {
    const uint32_t expectedMs = static_cast<uint32_t>((contentLength * 1000ULL) / kExpectedBytesPerSec) + 8000U;
    totalDownloadTimeoutMs = std::max(kMinTotalDownloadTimeoutMs, std::min(kMaxTotalDownloadTimeoutMs, expectedMs));
  }
  LOG_DBG("HTTP", "Timeout budget: total=%lu ms no-progress=%lu ms", static_cast<unsigned long>(totalDownloadTimeoutMs),
          static_cast<unsigned long>(kNoProgressTimeoutMs));

  // Remove existing file if present
  if (Storage.exists(destPath.c_str())) {
    Storage.remove(destPath.c_str());
  }

  // Open file for writing
  FsFile file;
  if (!Storage.openFileForWrite("HTTP", destPath.c_str(), file)) {
    LOG_ERR("HTTP", "Failed to open file for writing");
    http.end();
    return FILE_ERROR;
  }

  // Get the stream for chunked reading
  WiFiClient* stream = http.getStreamPtr();
  if (!stream) {
    LOG_ERR("HTTP", "Failed to get stream");
    file.close();
    Storage.remove(destPath.c_str());
    http.end();
    return HTTP_ERROR;
  }

  // Download in chunks
  uint8_t buffer[DOWNLOAD_CHUNK_SIZE];
  size_t downloaded = 0;
  const size_t total = contentLength > 0 ? contentLength : 0;
  const uint32_t startedAtMs = millis();
  uint32_t lastProgressMs = startedAtMs;

  while (http.connected() && (contentLength == 0 || downloaded < contentLength)) {
    const uint32_t nowMs = millis();
    if (nowMs - startedAtMs > totalDownloadTimeoutMs) {
      LOG_ERR("HTTP", "Download timeout: total=%lu ms, downloaded=%u", static_cast<unsigned long>(nowMs - startedAtMs),
              static_cast<unsigned int>(downloaded));
      file.close();
      Storage.remove(destPath.c_str());
      http.end();
      return ABORTED;
    }

    const size_t available = stream->available();
    if (available == 0) {
      if (nowMs - lastProgressMs > kNoProgressTimeoutMs) {
        LOG_ERR("HTTP", "Download stalled: no progress for %lu ms", static_cast<unsigned long>(nowMs - lastProgressMs));
        file.close();
        Storage.remove(destPath.c_str());
        http.end();
        return ABORTED;
      }
      delay(1);
      esp_task_wdt_reset();
      continue;
    }

    const size_t toRead = available < DOWNLOAD_CHUNK_SIZE ? available : DOWNLOAD_CHUNK_SIZE;
    const size_t bytesRead = stream->readBytes(buffer, toRead);

    if (bytesRead == 0) {
      break;
    }

    const size_t written = file.write(buffer, bytesRead);
    if (written != bytesRead) {
      LOG_ERR("HTTP", "Write failed: wrote %zu of %zu bytes", written, bytesRead);
      file.close();
      Storage.remove(destPath.c_str());
      http.end();
      return FILE_ERROR;
    }

    downloaded += bytesRead;
    lastProgressMs = millis();

    if (progress) {
      progress(downloaded, total);
    }
    esp_task_wdt_reset();
  }

  file.close();
  http.end();

  LOG_DBG("HTTP", "Downloaded %zu bytes", downloaded);

  // Verify download size if known
  if (contentLength > 0 && downloaded != contentLength) {
    LOG_ERR("HTTP", "Size mismatch: got %zu, expected %zu", downloaded, contentLength);
    Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  return OK;
}
