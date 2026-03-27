#include "SubredditActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <HTTPClient.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace {
constexpr const char* kCacheDir = "/.crosspoint/subreddit";
constexpr const char* kFeedCachePath = "/.crosspoint/subreddit/feed.json";
constexpr const char* kTempFeedPath = "/.crosspoint/subreddit/temp.json";
constexpr const char* kTempCommentsPath = "/.crosspoint/subreddit/comments_temp.json";
constexpr const char* kConfigPath = "/.crosspoint/subreddit.txt";
constexpr const char* kSortConfigPath = "/.crosspoint/subreddit_sort.txt";
constexpr const char* kContrastConfigPath = "/.crosspoint/subreddit_contrast.txt";
constexpr const char* kRefreshConfigPath = "/.crosspoint/subreddit_refresh_minutes.txt";

// 5 contrast levels: index 0 = lightest, 4 = darkest/most contrasty
static const int kContrastBrightnessOffsets[5] = {64, 40, 24, 8, -16};
static const char* const kContrastLabels[5] = {"1-Light", "2-Soft", "3-Normal", "4-Bold", "5-Dark"};
constexpr int kListPageItems = 12;
constexpr size_t kMaxImageBytes = 2 * 1024 * 1024;
constexpr int kMaxDecoderPixels = 3145728;
constexpr size_t kMaxConfigFileBytes = 128;
constexpr size_t kMaxCachedFeedBytes = 256 * 1024;

uint32_t fnv1a32(const std::string& value) {
  uint32_t hash = 2166136261u;
  for (const unsigned char c : value) {
    hash ^= c;
    hash *= 16777619u;
  }
  return hash;
}

void prepareRedditFilter(JsonDocument& filter) {
  // Only standard Reddit Listing shape: {data:{children:[{data:{...}}]}}
  // Keep error fields so we can report them in the debug reason
  filter["kind"] = true;
  filter["error"] = true;
  filter["message"] = true;
  filter["reason"] = true;

  JsonVariant cd = filter["data"]["children"][0]["data"];
  cd["id"] = true;
  cd["title"] = true;
  cd["selftext"] = true;
  cd["url"] = true;
  cd["url_overridden_by_dest"] = true;
  cd["post_hint"] = true;
  cd["is_gallery"] = true;
  cd["thumbnail"] = true;
  cd["gallery_data"]["items"][0]["media_id"] = true;
  cd["preview"]["images"][0]["source"]["url"] = true;
  cd["preview"]["images"][0]["source"]["width"] = true;
  cd["preview"]["images"][0]["source"]["height"] = true;
  for (size_t i = 0; i < 8; ++i) {
    cd["preview"]["images"][0]["resolutions"][i]["url"] = true;
    cd["preview"]["images"][0]["resolutions"][i]["width"] = true;
    cd["preview"]["images"][0]["resolutions"][i]["height"] = true;
  }
}

std::string trim(const std::string& in) {
  if (in.empty()) {
    return in;
  }
  size_t left = 0;
  size_t right = in.size();
  while (left < in.size() && std::isspace(static_cast<unsigned char>(in[left])) != 0) {
    left++;
  }
  while (right > left && std::isspace(static_cast<unsigned char>(in[right - 1])) != 0) {
    right--;
  }
  return in.substr(left, right - left);
}

std::string normalizeSubredditName(std::string value) {
  value = trim(value);
  if (value.empty()) {
    return "esp32";
  }

  // Accept full URLs like https://www.reddit.com/r/esp32/
  const std::string marker = "/r/";
  const size_t markerPos = value.find(marker);
  if (markerPos != std::string::npos) {
    value = value.substr(markerPos + marker.size());
  }

  if (value.rfind("r/", 0) == 0) {
    value = value.substr(2);
  }

  // Strip leading/trailing slashes
  while (!value.empty() && value.front() == '/') {
    value.erase(value.begin());
  }
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }

  // Keep only first path segment if user passed extras
  const size_t slashPos = value.find('/');
  if (slashPos != std::string::npos) {
    value = value.substr(0, slashPos);
  }

  if (value.empty()) {
    return "esp32";
  }
  return value;
}

std::string nextSortMode(const std::string& current) {
  if (current == "new") {
    return "best";
  }
  if (current == "best") {
    return "top";
  }
  return "new";
}

std::string sortModeLabel(const std::string& mode) {
  if (mode == "top") {
    return "Top";
  }
  if (mode == "best") {
    return "Best";
  }
  return "New";
}

std::string normalizeSortMode(const std::string& mode) {
  const std::string v = trim(mode);
  if (v == "new" || v == "best" || v == "top") {
    return v;
  }
  return "new";
}

bool writeWholeFile(const char* moduleName, const char* path, const std::string& value) {
  Storage.mkdir("/.crosspoint");
  FsFile f;
  if (!Storage.openFileForWrite(moduleName, path, f)) {
    return false;
  }
  const size_t written = f.write(reinterpret_cast<const uint8_t*>(value.data()), value.size());
  f.close();
  return written == value.size();
}

bool readWholeFileCapped(const char* moduleName, const char* path, std::string& out, const size_t maxBytes) {
  FsFile f;
  if (!Storage.openFileForRead(moduleName, path, f)) {
    return false;
  }

  const uint32_t fileSize = f.size();
  if (fileSize == 0 || fileSize > maxBytes) {
    f.close();
    return false;
  }

  out.resize(static_cast<size_t>(fileSize));
  const int bytes = f.read(out.data(), out.size());
  f.close();
  if (bytes <= 0) {
    out.clear();
    return false;
  }

  out.resize(static_cast<size_t>(bytes));
  return true;
}

std::string normalizeRedditText(std::string text) {
  auto replaceAll = [](std::string& value, const char* from, const char* to) {
    size_t pos = 0;
    const std::string needle(from);
    const std::string replacement(to);
    while ((pos = value.find(needle, pos)) != std::string::npos) {
      value.replace(pos, needle.size(), replacement);
      pos += replacement.size();
    }
  };

  replaceAll(text, "&amp;", "&");
  replaceAll(text, "&lt;", "<");
  replaceAll(text, "&gt;", ">");
  replaceAll(text, "&#39;", "'");
  replaceAll(text, "&quot;", "\"");

  std::string out;
  out.reserve(text.size());
  bool previousWasNewline = false;
  for (char c : text) {
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (previousWasNewline) {
        continue;
      }
      previousWasNewline = true;
      out.push_back(c);
      continue;
    }
    previousWasNewline = false;
    out.push_back(c);
  }
  return trim(out);
}

bool hasImageExtension(std::string url);
bool looksLikeVideo(std::string url);

std::string forcePreviewJpegUrl(std::string url) {
  // Keep Reddit preview URLs exactly as returned by the API.
  // These URLs include signatures and can return 403 if query params are modified.
  return url;
}

int queryParamInt(const std::string& url, const std::string& key) {
  const std::string needle = key + "=";
  const size_t qPos = url.find('?');
  if (qPos == std::string::npos) {
    return 0;
  }
  size_t pos = url.find(needle, qPos + 1);
  if (pos == std::string::npos) {
    return 0;
  }
  pos += needle.size();
  size_t end = url.find('&', pos);
  if (end == std::string::npos) {
    end = url.size();
  }
  const std::string value = url.substr(pos, end - pos);
  if (value.empty()) {
    return 0;
  }
  return std::atoi(value.c_str());
}

std::vector<std::string> buildTinyPreviewFallbackCandidates(const std::string& url) {
  std::vector<std::string> out;
  if (url.empty()) {
    return out;
  }

  std::string lower = url;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower.find("preview.redd.it/") == std::string::npos) {
    out.push_back(url);
    return out;
  }

  const int width = queryParamInt(url, "width");
  const size_t qPos = url.find('?');
  const size_t sigPos = (qPos == std::string::npos) ? std::string::npos : url.find("s=", qPos + 1);
  if (width <= 0 || width > 260 || qPos == std::string::npos || sigPos == std::string::npos) {
    out.push_back(url);
    return out;
  }

  size_t sigEnd = url.find('&', sigPos);
  if (sigEnd == std::string::npos) {
    sigEnd = url.size();
  }
  const std::string signature = url.substr(sigPos + 2, sigEnd - (sigPos + 2));
  if (signature.empty()) {
    out.push_back(url);
    return out;
  }

  const std::string base = url.substr(0, qPos);
  const int candidateWidths[] = {1080, 960, 720, 640, 480, 320};
  for (const int candidateWidth : candidateWidths) {
    out.push_back(base + "?width=" + std::to_string(candidateWidth) + "&crop=smart&auto=webp&s=" + signature);
  }

  out.push_back(url);
  return out;
}

std::vector<std::string> buildDownloadUrlCandidates(const std::string& url) {
  std::vector<std::string> out;
  if (!url.empty()) {
    out.push_back(url);
  }
  return out;
}

struct ImageCandidate {
  std::string url;
  int width = 0;
  int height = 0;
};

std::string chooseBestImageCandidate(const std::vector<ImageCandidate>& candidates) {
  if (candidates.empty()) {
    return "";
  }

  const ImageCandidate* bestWithinLimit = nullptr;
  for (const ImageCandidate& candidate : candidates) {
    if (candidate.url.empty() || !hasImageExtension(candidate.url)) {
      continue;
    }
    if (candidate.width <= 0 || candidate.height <= 0) {
      continue;
    }
    const int64_t pixels = static_cast<int64_t>(candidate.width) * static_cast<int64_t>(candidate.height);
    if (pixels <= kMaxDecoderPixels) {
      if (!bestWithinLimit ||
          pixels > static_cast<int64_t>(bestWithinLimit->width) * static_cast<int64_t>(bestWithinLimit->height)) {
        bestWithinLimit = &candidate;
      }
    }
  }

  if (bestWithinLimit) {
    return bestWithinLimit->url;
  }

  const ImageCandidate* smallestKnown = nullptr;
  for (const ImageCandidate& candidate : candidates) {
    if (candidate.url.empty() || !hasImageExtension(candidate.url)) {
      continue;
    }
    if (candidate.width <= 0 || candidate.height <= 0) {
      continue;
    }
    if (!smallestKnown ||
        static_cast<int64_t>(candidate.width) * static_cast<int64_t>(candidate.height) <
            static_cast<int64_t>(smallestKnown->width) * static_cast<int64_t>(smallestKnown->height)) {
      smallestKnown = &candidate;
    }
  }

  if (smallestKnown) {
    return smallestKnown->url;
  }

  for (const ImageCandidate& candidate : candidates) {
    if (!candidate.url.empty() && hasImageExtension(candidate.url)) {
      return candidate.url;
    }
  }

  return "";
}

std::string pickRedditBestImageUrl(JsonVariant data) {
  std::vector<ImageCandidate> candidates;
  candidates.reserve(10);

  JsonArray resolutions = data["preview"]["images"][0]["resolutions"].as<JsonArray>();
  if (!resolutions.isNull()) {
    for (JsonVariant resolution : resolutions) {
      const char* candidate = resolution["url"] | "";
      std::string url = forcePreviewJpegUrl(normalizeRedditText(candidate != nullptr ? candidate : ""));
      if (url.empty() || !hasImageExtension(url) || looksLikeVideo(url)) {
        continue;
      }
      ImageCandidate c;
      c.url = url;
      c.width = resolution["width"] | 0;
      c.height = resolution["height"] | 0;
      candidates.push_back(std::move(c));
    }
  }

  const char* source = data["preview"]["images"][0]["source"]["url"] | "";
  std::string sourceUrl = forcePreviewJpegUrl(normalizeRedditText(source != nullptr ? source : ""));
  int sourceWidth = data["preview"]["images"][0]["source"]["width"] | 0;
  int sourceHeight = data["preview"]["images"][0]["source"]["height"] | 0;
  if (!sourceUrl.empty() && hasImageExtension(sourceUrl) && !looksLikeVideo(sourceUrl)) {
    ImageCandidate c;
    c.url = sourceUrl;
    c.width = sourceWidth;
    c.height = sourceHeight;
    candidates.push_back(std::move(c));
  }

  return chooseBestImageCandidate(candidates);
}

std::vector<std::string> extractGalleryImageUrls(JsonVariant data) {
  std::vector<std::string> urls;
  JsonArray items = data["gallery_data"]["items"].as<JsonArray>();
  JsonObject metadata = data["media_metadata"].as<JsonObject>();
  if (items.isNull() || metadata.isNull()) {
    return urls;
  }

  auto appendUnique = [&urls](const std::string& candidate) {
    if (candidate.empty() || !hasImageExtension(candidate) || looksLikeVideo(candidate)) {
      return;
    }
    if (std::find(urls.begin(), urls.end(), candidate) == urls.end()) {
      urls.push_back(candidate);
    }
  };

  for (JsonVariant item : items) {
    const char* mediaId = item["media_id"] | "";
    if (mediaId == nullptr || mediaId[0] == '\0') {
      continue;
    }

    JsonVariant media = metadata[mediaId];
    if (media.isNull()) {
      continue;
    }

    std::vector<ImageCandidate> candidates;

    JsonArray previews = media["p"].as<JsonArray>();
    if (!previews.isNull()) {
      for (JsonVariant p : previews) {
        const char* raw = p["u"] | "";
        std::string url = forcePreviewJpegUrl(normalizeRedditText(raw != nullptr ? raw : ""));
        if (url.empty() || !hasImageExtension(url) || looksLikeVideo(url)) {
          continue;
        }
        ImageCandidate c;
        c.url = url;
        c.width = p["x"] | 0;
        c.height = p["y"] | 0;
        candidates.push_back(std::move(c));
      }
    }

    const char* sourceRaw = media["s"]["u"] | "";
    std::string sourceUrl = forcePreviewJpegUrl(normalizeRedditText(sourceRaw != nullptr ? sourceRaw : ""));
    if (!sourceUrl.empty() && hasImageExtension(sourceUrl) && !looksLikeVideo(sourceUrl)) {
      ImageCandidate source;
      source.url = sourceUrl;
      source.width = media["s"]["x"] | 0;
      source.height = media["s"]["y"] | 0;
      candidates.push_back(std::move(source));
    }

    appendUnique(chooseBestImageCandidate(candidates));
  }

  return urls;
}

bool hasImageExtension(std::string url) {
  std::transform(url.begin(), url.end(), url.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return url.find(".jpg") != std::string::npos || url.find(".jpeg") != std::string::npos ||
         url.find(".png") != std::string::npos;
}

std::vector<std::string> extractAllRedditImageUrls(JsonVariant data) {
  std::vector<std::string> urls;
  JsonArray images = data["preview"]["images"].as<JsonArray>();
  if (images.isNull()) {
    return urls;
  }

  auto appendUnique = [&urls](const std::string& candidate) {
    if (candidate.empty() || !hasImageExtension(candidate)) {
      return;
    }
    if (std::find(urls.begin(), urls.end(), candidate) == urls.end()) {
      urls.push_back(candidate);
    }
  };

  for (JsonVariant img : images) {
    JsonArray resolutions = img["resolutions"].as<JsonArray>();
    if (!resolutions.isNull() && resolutions.size() > 0) {
      // Keep exactly one URL per OP image: largest preview resolution.
      const size_t idx = resolutions.size() - 1;
      const char* candidate = resolutions[idx]["url"] | "";
      std::string bestPreviewUrl = forcePreviewJpegUrl(normalizeRedditText(candidate != nullptr ? candidate : ""));
      appendUnique(bestPreviewUrl);
      continue;
    }

    // Fallback when no resolution list exists.
    const char* source = img["source"]["url"] | "";
    std::string sourceUrl = forcePreviewJpegUrl(normalizeRedditText(source != nullptr ? source : ""));
    appendUnique(sourceUrl);
  }

  return urls;
}

bool looksLikeVideo(std::string url) {
  std::transform(url.begin(), url.end(), url.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return url.find("v.redd.it") != std::string::npos || url.find(".mp4") != std::string::npos ||
         url.find(".gif") != std::string::npos || url.find(".webm") != std::string::npos;
}

bool isExternalPreviewUrl(std::string url) {
  std::transform(url.begin(), url.end(), url.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return url.find("external-preview.redd.it") != std::string::npos;
}

std::string extensionForUrl(std::string url) {
  std::transform(url.begin(), url.end(), url.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (url.find(".png") != std::string::npos) {
    return ".png";
  }
  return ".jpg";
}

bool validateRenderableImageFile(const std::string& imagePath, const int64_t maxPixels, std::string* reason = nullptr) {
  ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(imagePath);
  if (!decoder) {
    if (reason) {
      *reason = "decoder-unavailable";
    }
    return false;
  }

  ImageDimensions dims;
  if (!decoder->getDimensions(imagePath, dims) || dims.width <= 0 || dims.height <= 0) {
    std::string lowerPath = imagePath;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lowerPath.find(".png") != std::string::npos) {
      return true;
    }
    if (reason) {
      *reason = "bad-dimensions";
    }
    return false;
  }

  const int64_t pixels = static_cast<int64_t>(dims.width) * static_cast<int64_t>(dims.height);
  if (pixels > maxPixels) {
    if (reason) {
      *reason = "too-large " + std::to_string(dims.width) + "x" + std::to_string(dims.height);
    }
    return false;
  }

  return true;
}

uint32_t loadRefreshIntervalMs() {
  FsFile f;
  if (!Storage.openFileForRead("SUB", kRefreshConfigPath, f)) {
    writeWholeFile("SUB", kRefreshConfigPath, "15");
    return 15UL * 60UL * 1000UL;
  }

  std::string raw;
  raw.resize(static_cast<size_t>(f.size()));
  const int bytes = f.read(raw.data(), raw.size());
  f.close();
  if (bytes <= 0) {
    return 15UL * 60UL * 1000UL;
  }
  raw.resize(static_cast<size_t>(bytes));

  const int minutes = std::atoi(trim(raw).c_str());
  if (minutes <= 0) {
    return 15UL * 60UL * 1000UL;
  }

  switch (minutes) {
    case 5:
    case 15:
    case 30:
    case 60:
      return static_cast<uint32_t>(minutes) * 60UL * 1000UL;
    default:
      return 15UL * 60UL * 1000UL;
  }
}

std::vector<std::string> wrapTextToWidth(GfxRenderer& renderer, const int fontId, const std::string& text, const int maxWidth) {
  std::vector<std::string> lines;
  if (text.empty()) {
    lines.push_back("(no text)");
    return lines;
  }

  std::string currentLine;
  std::string currentWord;

  auto flushWord = [&]() {
    if (currentWord.empty()) {
      return;
    }

    std::string candidate = currentLine;
    if (!candidate.empty()) {
      candidate += " ";
    }
    candidate += currentWord;

    if (renderer.getTextWidth(fontId, candidate.c_str()) <= maxWidth) {
      currentLine = candidate;
      currentWord.clear();
      return;
    }

    if (!currentLine.empty()) {
      lines.push_back(currentLine);
      currentLine.clear();
      candidate = currentWord;
      if (renderer.getTextWidth(fontId, candidate.c_str()) <= maxWidth) {
        currentLine = candidate;
        currentWord.clear();
        return;
      }
    }

    std::string chunk;
    for (char ch : currentWord) {
      std::string test = chunk;
      test.push_back(ch);
      if (!chunk.empty() && renderer.getTextWidth(fontId, test.c_str()) > maxWidth) {
        lines.push_back(chunk);
        chunk.clear();
      }
      chunk.push_back(ch);
    }
    currentLine = chunk;
    currentWord.clear();
  };

  for (char c : text) {
    if (c == '\n') {
      flushWord();
      if (!currentLine.empty()) {
        lines.push_back(currentLine);
        currentLine.clear();
      }
      lines.push_back("");
      continue;
    }

    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      flushWord();
      continue;
    }

    currentWord.push_back(c);
  }

  flushWord();
  if (!currentLine.empty()) {
    lines.push_back(currentLine);
  }

  if (lines.empty()) {
    lines.push_back("(no text)");
  }
  return lines;
}

bool extractPostsFromDocument(JsonDocument& doc, std::vector<RedditPost>& outPosts, std::string* outReason = nullptr) {
  JsonArray children = doc["data"]["children"].as<JsonArray>();
  if (children.isNull()) {
    children = doc["json"]["data"]["children"].as<JsonArray>();
  }
  if (children.isNull()) {
    children = doc[0]["data"]["children"].as<JsonArray>();
  }

  if (children.isNull()) {
    if (outReason) {
      std::string reason = "children=null";
      const char* kind = doc["kind"] | "";
      const char* message = doc["message"] | "";
      const char* reasonField = doc["reason"] | "";
      int apiError = doc["error"] | 0;
      const char* jsonErrCode = doc["json"]["errors"][0][0] | "";
      const char* jsonErrMsg = doc["json"]["errors"][0][1] | "";

      if (kind != nullptr && kind[0] != '\0') {
        reason += " kind=" + std::string(kind);
      }
      if (apiError != 0) {
        reason += " error=" + std::to_string(apiError);
      }
      if (reasonField != nullptr && reasonField[0] != '\0') {
        reason += " reason=" + std::string(reasonField);
      }
      if (message != nullptr && message[0] != '\0') {
        std::string msg = message;
        if (msg.size() > 48) {
          msg = msg.substr(0, 48);
        }
        reason += " msg=" + msg;
      }
      if (jsonErrCode != nullptr && jsonErrCode[0] != '\0') {
        reason += " jerr=" + std::string(jsonErrCode);
      }
      if (jsonErrMsg != nullptr && jsonErrMsg[0] != '\0') {
        std::string msg = jsonErrMsg;
        if (msg.size() > 32) {
          msg = msg.substr(0, 32);
        }
        reason += " jmsg=" + msg;
      }

      *outReason = reason;
    }
    return false;
  }

  std::vector<RedditPost> parsed;
  parsed.reserve(children.size());
  size_t titleCandidateCount = 0;

  for (JsonVariant child : children) {
    JsonVariant data = child["data"];
    const char* titleRaw = data["title"] | "";
    const char* idRaw = data["id"] | "";
    const char* selfTextRaw = data["selftext"] | "";
    const char* permalinkRaw = data["permalink"] | "";
    const char* urlRaw = data["url_overridden_by_dest"] | data["url"] | "";
    const char* hintRaw = data["post_hint"] | "";
    const char* thumbnailRaw = data["thumbnail"] | "";

    if (titleRaw == nullptr || titleRaw[0] == '\0') {
      continue;
    }
    titleCandidateCount++;

    RedditPost post;
    post.id = idRaw != nullptr ? idRaw : "";
    post.title = normalizeRedditText(titleRaw);
    post.body = normalizeRedditText(selfTextRaw != nullptr ? selfTextRaw : "");
    post.permalink = normalizeRedditText(permalinkRaw != nullptr ? permalinkRaw : "");

    std::string url = normalizeRedditText(urlRaw != nullptr ? urlRaw : "");
    std::string thumbnailUrl = normalizeRedditText(thumbnailRaw != nullptr ? thumbnailRaw : "");
    const std::string hint = hintRaw != nullptr ? hintRaw : "";
    const bool isGallery = data["is_gallery"] | false;

    bool imageCandidate = (hint == "image") || hasImageExtension(url) || isGallery;

    std::vector<std::string> allImageUrls;
    allImageUrls.reserve(8);
    auto appendUniqueImageUrl = [&allImageUrls](const std::string& candidateUrl) {
      if (candidateUrl.empty() || !hasImageExtension(candidateUrl) || looksLikeVideo(candidateUrl)) {
        return;
      }
      if (std::find(allImageUrls.begin(), allImageUrls.end(), candidateUrl) == allImageUrls.end()) {
        allImageUrls.push_back(candidateUrl);
      }
    };

    // Keep one canonical OP image URL.
    if (isGallery) {
      const std::vector<std::string> galleryUrls = extractGalleryImageUrls(data);
      for (const std::string& galleryUrl : galleryUrls) {
        appendUniqueImageUrl(galleryUrl);
      }
    } else {
      const std::string bestApiImageUrl = pickRedditBestImageUrl(data);
      if (!bestApiImageUrl.empty()) {
        appendUniqueImageUrl(bestApiImageUrl);
      } else {
        appendUniqueImageUrl(url);
      }
    }

    if (!allImageUrls.empty()) {
      imageCandidate = true;
    }

    post.hasImage = imageCandidate && (allImageUrls.empty() || !looksLikeVideo(allImageUrls[0]));
    post.imageUrls = std::move(allImageUrls);

    if (post.title.size() > 220) {
      post.title = post.title.substr(0, 217) + "...";
    }
    if (post.body.size() > 4000) {
      post.body = post.body.substr(0, 4000) + "\n\n[truncated]";
    }

    parsed.push_back(std::move(post));
    if (parsed.size() >= 30) {
      break;
    }
  }

  if (parsed.empty()) {
    if (outReason) {
      *outReason = "children=" + std::to_string(children.size()) + " title_candidates=" +
                   std::to_string(titleCandidateCount);
    }
    return false;
  }

  outPosts = std::move(parsed);
  return true;
}

std::string buildCommentsUrl(const std::string& subreddit, const RedditPost& post, const int limit = 20,
                             const int depth = 3, const std::string& host = "www.reddit.com") {
  std::string permalink = post.permalink;
  if (permalink.empty()) {
    if (post.id.empty()) {
      return "";
    }
    permalink = "/r/" + subreddit + "/comments/" + post.id;
  }

  if (!permalink.empty() && permalink.front() != '/') {
    permalink = "/" + permalink;
  }

  if (permalink.size() < 5 || permalink.substr(permalink.size() - 5) != ".json") {
    permalink += ".json";
  }

  const int safeLimit = std::max(1, std::min(limit, 50));
  const int safeDepth = std::max(1, std::min(depth, 6));
  return "https://" + host + permalink + "?raw_json=1&limit=" + std::to_string(safeLimit) +
         "&depth=" + std::to_string(safeDepth) + "&sort=new";
}

std::string buildListingUrl(const std::string& host, const std::string& subreddit, const std::string& sortMode,
                            const std::string& afterToken, const int limit = 5) {
  const int safeLimit = std::max(1, std::min(limit, 10));
  std::string url = "https://" + host + "/r/" + subreddit + "/" + sortMode + ".json?raw_json=1&limit=" +
                    std::to_string(safeLimit);
  if (!afterToken.empty()) {
    url += "&after=" + afterToken;
  }
  if (sortMode == "top") {
    url += "&t=week";
  }
  url += "&cb=" + std::to_string(static_cast<unsigned long>(millis()));
  return url;
}

void buildFeedJsonFilter(JsonDocument& filter) {
  auto setupChildrenFilter = [](JsonObject root) {
    root["after"] = true;
    JsonArray children = root["children"].to<JsonArray>();
    JsonObject child = children.add<JsonObject>();
    JsonObject data = child["data"].to<JsonObject>();
    data["title"] = true;
    data["id"] = true;
    data["selftext"] = true;
    data["permalink"] = true;
    data["url_overridden_by_dest"] = true;
    data["url"] = true;
    data["post_hint"] = true;
    data["is_gallery"] = true;
    data["thumbnail"] = true;
    data["gallery_data"]["items"][0]["media_id"] = true;

    JsonArray images = data["preview"]["images"].to<JsonArray>();
    JsonObject image = images.add<JsonObject>();
    image["source"]["url"] = true;
    image["source"]["width"] = true;
    image["source"]["height"] = true;
    JsonArray resolutions = image["resolutions"].to<JsonArray>();
    for (size_t i = 0; i < 8; ++i) {
      JsonObject res = resolutions.add<JsonObject>();
      res["url"] = true;
      res["width"] = true;
      res["height"] = true;
    }
  };

  setupChildrenFilter(filter["data"].to<JsonObject>());
  setupChildrenFilter(filter["json"]["data"].to<JsonObject>());
}

void collectCommentLines(JsonArray children, std::vector<std::string>& outComments, int depth, const size_t maxCount) {
  if (children.isNull() || outComments.size() >= maxCount) {
    return;
  }

  for (JsonVariant comment : children) {
    if (outComments.size() >= maxCount) {
      break;
    }

    const char* kind = comment["kind"] | "";
    if (kind == nullptr || std::string(kind) != "t1") {
      continue;
    }

    JsonVariant data = comment["data"];
    const char* authorRaw = data["author"] | "";
    const char* bodyRaw = data["body"] | "";
    std::string body = normalizeRedditText(bodyRaw != nullptr ? bodyRaw : "");
    if (body.empty()) {
      continue;
    }

    std::string author = normalizeRedditText(authorRaw != nullptr ? authorRaw : "");
    if (author.empty()) {
      author = "unknown";
    }

    std::string indent;
    for (int i = 0; i < depth; i++) {
      indent += "  ";
    }
    outComments.push_back(indent + author + ": " + body);

    JsonArray replies = data["replies"]["data"]["children"].as<JsonArray>();
    if (!replies.isNull()) {
      collectCommentLines(replies, outComments, depth + 1, maxCount);
    }
  }
}

bool fetchCommentsFromEndpoint(const std::string& url, std::vector<std::string>& outComments,
                               std::string& outReason, std::vector<std::string>* outGalleryUrls = nullptr) {
  std::unique_ptr<WiFiClient> client;
  auto* secureClient = new WiFiClientSecure();
  secureClient->setInsecure();
  client.reset(secureClient);

  HTTPClient http;
  http.begin(*client, url.c_str());
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64) CrossPoint-RedditReader/1.0");
  http.addHeader("Accept", "application/json");
  http.addHeader("Cache-Control", "no-cache, no-store, max-age=0");
  http.addHeader("Pragma", "no-cache");

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    outReason = "comments-http=" + std::to_string(httpCode);
    http.end();
    return false;
  }

  Storage.mkdir(kCacheDir);
  Storage.remove(kTempCommentsPath);

  {
    FsFile tempFile;
    if (!Storage.openFileForWrite("SUB", kTempCommentsPath, tempFile)) {
      outReason = "comments-sd-write";
      http.end();
      return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t chunkBuf[512];
    int remaining = http.getSize();
    while (http.connected() && (remaining > 0 || remaining == -1)) {
      const int avail = stream->available();
      if (avail > 0) {
        const int toRead = std::min(avail, (int)sizeof(chunkBuf));
        const int got = stream->readBytes(chunkBuf, toRead);
        if (got > 0) {
          tempFile.write(chunkBuf, static_cast<size_t>(got));
          if (remaining > 0) {
            remaining -= got;
          }
        }
      } else {
        delay(1);
      }
    }
    tempFile.close();
  }
  http.end();

  FsFile parseFile;
  if (!Storage.openFileForRead("SUB", kTempCommentsPath, parseFile)) {
    outReason = "comments-sd-read";
    Storage.remove(kTempCommentsPath);
    return false;
  }

  JsonDocument doc;
  const DeserializationError parseError =
      deserializeJson(doc, parseFile, DeserializationOption::NestingLimit(40));
  parseFile.close();
  Storage.remove(kTempCommentsPath);

  if (parseError) {
    outReason = std::string("comments-json=") + parseError.c_str();
    return false;
  }

  JsonArray commentsArray = doc[1]["data"]["children"].as<JsonArray>();
  if (commentsArray.isNull()) {
    outReason = "comments-null";
    return false;
  }

  if (outGalleryUrls != nullptr) {
    outGalleryUrls->clear();
    JsonVariant postData = doc[0]["data"]["children"][0]["data"];
    if (!postData.isNull()) {
      *outGalleryUrls = extractGalleryImageUrls(postData);
    }
  }

  outComments.clear();
  outComments.reserve(48);
  collectCommentLines(commentsArray, outComments, 0, 60);

  if (outComments.empty()) {
    outReason = "comments-empty";
    return false;
  }

  return true;
}

bool fetchPostsFromEndpoint(const std::string& url, std::vector<RedditPost>& outPosts, std::string& outAfter,
                            std::string& outReason, const bool cacheToFeedFile = true) {
  std::unique_ptr<WiFiClient> client;
  if (url.rfind("https://", 0) == 0) {
    auto* secureClient = new WiFiClientSecure();
    secureClient->setInsecure();
    client.reset(secureClient);
  } else {
    client.reset(new WiFiClient());
  }

  HTTPClient http;
  http.begin(*client, url.c_str());
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64) CrossPoint-RedditReader/1.0");
  http.addHeader("Accept", "application/json");
  http.addHeader("Cache-Control", "no-cache, no-store, max-age=0");
  http.addHeader("Pragma", "no-cache");

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    outReason = "http=" + std::to_string(httpCode);
    http.end();
    return false;
  }

  // Stream HTTP body to SD temp file using a small stack buffer.
  // This avoids any large heap allocation for the raw response.
  Storage.mkdir(kCacheDir);
  Storage.remove(kTempFeedPath);

  {
    FsFile tempFile;
    if (!Storage.openFileForWrite("SUB", kTempFeedPath, tempFile)) {
      outReason = "sd-write-fail";
      http.end();
      return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t chunkBuf[512];
    int remaining = http.getSize();  // -1 for chunked transfer
    while (http.connected() && (remaining > 0 || remaining == -1)) {
      const int avail = stream->available();
      if (avail > 0) {
        const int toRead = std::min(avail, (int)sizeof(chunkBuf));
        const int got = stream->readBytes(chunkBuf, toRead);
        if (got > 0) {
          tempFile.write(chunkBuf, (size_t)got);
          if (remaining > 0) {
            remaining -= got;
          }
        }
      } else {
        delay(1);
      }
    }
    tempFile.close();
  }
  http.end();

  // Parse from SD file after closing HTTP (WiFi/TLS buffers released).
  FsFile parseFile;
  if (!Storage.openFileForRead("SUB", kTempFeedPath, parseFile)) {
    outReason = "sd-read-fail";
    Storage.remove(kTempFeedPath);
    return false;
  }

    JsonDocument doc;
    JsonDocument filter;
    buildFeedJsonFilter(filter);
    const DeserializationError parseError =
      deserializeJson(doc, parseFile, DeserializationOption::NestingLimit(30), DeserializationOption::Filter(filter));
  parseFile.close();

  if (parseError) {
    Storage.remove(kTempFeedPath);
    outReason = std::string("json=") + parseError.c_str();
    return false;
  }

  std::vector<RedditPost> parsed;
  std::string extractReason;
  if (!extractPostsFromDocument(doc, parsed, &extractReason)) {
    outReason = "no-posts";
    if (!extractReason.empty()) {
      outReason += "(" + extractReason + ")";
    }
    Storage.remove(kTempFeedPath);
    return false;
  }

  const char* afterRaw = doc["data"]["after"] | "";
  outAfter = (afterRaw != nullptr) ? afterRaw : "";

  if (cacheToFeedFile) {
    Storage.mkdir("/.crosspoint");
    Storage.mkdir(kCacheDir);
    Storage.remove(kFeedCachePath);
    {
      FsFile src;
      FsFile dst;
      if (Storage.openFileForRead("SUB", kTempFeedPath, src) &&
          Storage.openFileForWrite("SUB", kFeedCachePath, dst)) {
        uint8_t copyBuf[512];
        while (true) {
          const int got = src.read(copyBuf, sizeof(copyBuf));
          if (got <= 0) {
            break;
          }
          dst.write(copyBuf, static_cast<size_t>(got));
        }
        dst.close();
        src.close();
      } else {
        if (src) {
          src.close();
        }
        if (dst) {
          dst.close();
        }
      }
    }
  }
  Storage.remove(kTempFeedPath);

  outPosts = std::move(parsed);
  return true;
}
}  // namespace

void SubredditActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  state = State::CHECK_WIFI;
  posts.clear();
  wrappedLines.clear();
  errorMessage.clear();
  errorFromImage = false;
  debugReason.clear();
  debugEndpoint.clear();
  statusMessage = "Preparing subreddit reader...";
  selectedPost = 0;
  topVisibleLine = 0;
  imageReady = false;
  imageRenderPhaseShown = false;
  imageCachePath.clear();
  refreshIntervalMs = loadRefreshIntervalMs();

  loadSubredditConfig();
  loadCachedFeed();

  requestUpdate();
  checkAndConnectWifi();
}

void SubredditActivity::onExit() {
  ActivityWithSubactivity::onExit();
  Storage.remove(kTempFeedPath);
  Storage.remove(kTempCommentsPath);
  Storage.removeDir(kCacheDir);
  WiFi.mode(WIFI_OFF);
}

void SubredditActivity::loadSubredditConfig() {
  subredditName = "esp32";
  sortMode = "new";

  {
    std::string raw;
    if (readWholeFileCapped("SUB", kConfigPath, raw, kMaxConfigFileBytes)) {
      subredditName = normalizeSubredditName(raw);
    }
  }

  {
    std::string raw;
    if (readWholeFileCapped("SUB", kSortConfigPath, raw, kMaxConfigFileBytes)) {
      sortMode = normalizeSortMode(raw);
    }
  }

  {
    std::string raw;
    if (readWholeFileCapped("SUB", kContrastConfigPath, raw, kMaxConfigFileBytes)) {
      const int level = atoi(raw.c_str());
      if (level >= 0 && level <= 4) {
        imageContrastLevel = level;
      }
    }
  }
}

void SubredditActivity::openSubredditPicker() {
  const std::string currentSubreddit = subredditName;
  enterNewActivity(new KeyboardEntryActivity(
      renderer, mappedInput, "Subreddit (no r/)", currentSubreddit, 48, false,
      [this](const std::string& text) {
        subredditName = normalizeSubredditName(text);
        writeWholeFile("SUB", kConfigPath, subredditName);
        exitActivity();

        selectedPost = 0;
        topVisibleLine = 0;
        currentImageIndex = 0;
        nextPageAfter.clear();
        hasMorePosts = false;
        refreshFeed(false);
      },
      [this]() {
        exitActivity();
        requestUpdate();
      }));
}

bool SubredditActivity::loadCachedFeed() {
  FsFile f;
  if (!Storage.openFileForRead("SUB", kFeedCachePath, f)) {
    return false;
  }

  const uint32_t fileSize = f.size();
  if (fileSize == 0 || fileSize > kMaxCachedFeedBytes) {
    f.close();
    LOG_ERR("SUB", "Cached feed size invalid: %u bytes", static_cast<unsigned int>(fileSize));
    Storage.remove(kFeedCachePath);
    return false;
  }

  JsonDocument doc;
  JsonDocument filter;
  buildFeedJsonFilter(filter);
  const DeserializationError error =
      deserializeJson(doc, f, DeserializationOption::NestingLimit(30), DeserializationOption::Filter(filter));
  f.close();
  if (error) {
    LOG_ERR("SUB", "Cached feed parse error: %s", error.c_str());
    return false;
  }

  std::vector<RedditPost> parsed;
  std::string extractReason;
  if (!extractPostsFromDocument(doc, parsed, &extractReason)) {
    LOG_ERR("SUB", "Cached feed extract failed: %s", extractReason.c_str());
    return false;
  }

  posts = std::move(parsed);
  if (selectedPost >= static_cast<int>(posts.size())) {
    selectedPost = 0;
  }
  return true;
}

bool SubredditActivity::saveCachedFeed(const std::string& json) {
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(kCacheDir);

  FsFile f;
  if (!Storage.openFileForWrite("SUB", kFeedCachePath, f)) {
    return false;
  }

  const size_t written = f.write(reinterpret_cast<const uint8_t*>(json.data()), json.size());
  f.close();
  return written == json.size();
}

bool SubredditActivity::parseFeedJson(const std::string& json) {
  JsonDocument doc;
  JsonDocument filter;
  buildFeedJsonFilter(filter);
  const DeserializationError error =
      deserializeJson(doc, json, DeserializationOption::NestingLimit(30), DeserializationOption::Filter(filter));
  if (error) {
    LOG_ERR("SUB", "JSON parse error: %s", error.c_str());
    return false;
  }

  std::vector<RedditPost> parsed;
  std::string extractReason;
  if (!extractPostsFromDocument(doc, parsed, &extractReason)) {
    LOG_ERR("SUB", "extractPosts failed: %s", extractReason.c_str());
    return false;
  }

  posts = std::move(parsed);
  if (selectedPost >= static_cast<int>(posts.size())) {
    selectedPost = 0;
  }
  return true;
}

void SubredditActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    refreshFeed(false);
    return;
  }
  launchWifiSelection();
}

void SubredditActivity::launchWifiSelection() {
  state = State::WIFI_SELECTION;
  requestUpdate();
  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                             [this](const bool connected) { onWifiSelectionComplete(connected); }));
}

void SubredditActivity::onWifiSelectionComplete(const bool connected) {
  exitActivity();

  if (!connected) {
    if (!posts.empty()) {
      state = State::LIST;
      statusMessage = "Offline: showing cached posts";
      requestUpdate();
      return;
    }
    state = State::ERROR;
    errorMessage = "WiFi connection required to fetch subreddit";
    requestUpdate();
    return;
  }

  refreshFeed(false);
}

void SubredditActivity::refreshFeed(const bool automatic) {
  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    if (!automatic) {
      launchWifiSelection();
    }
    return;
  }

  state = State::LOADING;
  errorFromImage = false;
  debugReason.clear();
  debugEndpoint.clear();
  statusMessage = automatic ? "Auto-refreshing..." : ("Refreshing " + sortMode + "...");
  requestUpdate();

  std::vector<RedditPost> parsed;
  std::string afterToken;
  std::string lastReason;
  bool fetched = false;
  const std::vector<int> limits = {5, 3, 2, 1};
  const std::vector<std::string> hosts = {"www.reddit.com", "old.reddit.com"};
  for (const int limit : limits) {
    for (const std::string& host : hosts) {
      const std::string url = buildListingUrl(host, subredditName, sortMode, "", limit);
      if (fetchPostsFromEndpoint(url, parsed, afterToken, lastReason, true)) {
        fetched = true;
        break;
      }
      debugEndpoint = url;
      debugReason = lastReason;
      LOG_ERR("SUB", "Endpoint failed: %s (%s)", url.c_str(), lastReason.c_str());
      if (lastReason.find("json=NoMemory") == std::string::npos) {
        break;
      }
    }
    if (fetched || lastReason.find("json=NoMemory") == std::string::npos) {
      break;
    }
  }

  if (!fetched) {
    if (loadCachedFeed()) {
      state = State::LIST;
      errorMessage = "Feed unavailable. Showing cached posts.";
      requestUpdate();
      return;
    }
    if (!posts.empty()) {
      state = State::LIST;
      errorMessage = "Feed unavailable. Showing last loaded posts.";
      requestUpdate();
      return;
    }
    state = State::ERROR;
    errorMessage = "Failed to load subreddit feed";
    if (debugReason.empty()) {
      debugReason = "unknown";
    }
    if (debugEndpoint.empty()) {
      debugEndpoint = buildListingUrl("www.reddit.com", subredditName, sortMode, "", 5);
    }
    requestUpdate();
    return;
  }

  posts = std::move(parsed);
  nextPageAfter = afterToken;
  hasMorePosts = !nextPageAfter.empty();
  if (selectedPost >= static_cast<int>(posts.size())) {
    selectedPost = 0;
  }
  lastRefreshMs = millis();
  state = State::LIST;
  errorMessage.clear();
  requestUpdate();
}

void SubredditActivity::loadMorePosts() {
  if (!hasMorePosts || nextPageAfter.empty()) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    return;
  }

  state = State::LOADING;
  statusMessage = "Loading more posts...";
  debugReason.clear();
  debugEndpoint.clear();
  requestUpdate();

  const std::vector<std::string> urls = {
      buildListingUrl("www.reddit.com", subredditName, sortMode, nextPageAfter),
      buildListingUrl("old.reddit.com", subredditName, sortMode, nextPageAfter),
  };

  std::vector<RedditPost> parsed;
  std::string afterToken;
  std::string lastReason;
  bool fetched = false;
  for (const std::string& url : urls) {
    if (fetchPostsFromEndpoint(url, parsed, afterToken, lastReason, false)) {
      fetched = true;
      break;
    }
    debugEndpoint = url;
    debugReason = lastReason;
  }

  if (!fetched) {
    state = State::LIST;
    errorMessage = "Load more failed";
    if (!lastReason.empty()) {
      errorMessage += ": " + lastReason;
    }
    requestUpdate();
    return;
  }

  for (RedditPost& post : parsed) {
    bool exists = false;
    for (const RedditPost& existing : posts) {
      if (!post.id.empty() && post.id == existing.id) {
        exists = true;
        break;
      }
    }
    if (!exists) {
      posts.push_back(std::move(post));
    }
  }

  nextPageAfter = afterToken;
  hasMorePosts = !nextPageAfter.empty();
  state = State::LIST;
  errorMessage.clear();
  requestUpdate();
}

void SubredditActivity::openSelectedPost() {
  if (posts.empty() || selectedPost < 0 || selectedPost >= static_cast<int>(posts.size())) {
    return;
  }
  state = State::LOADING;
  statusMessage = "Loading post and comments...";
  requestUpdate();

  loadCommentsForSelectedPost();

  topVisibleLine = 0;
  imageReady = false;
  imageRenderPhaseShown = false;
  imageCachePath.clear();
  prepareWrappedPost();
  state = State::POST;
  requestUpdate();
}

void SubredditActivity::loadCommentsForSelectedPost() {
  commentLines.clear();
  if (posts.empty()) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    commentLines.push_back("(offline: comments unavailable)");
    return;
  }

  const RedditPost& post = posts[selectedPost];
  if (buildCommentsUrl(subredditName, post).empty()) {
    commentLines.push_back("(comments unavailable: missing permalink)");
    return;
  }

  std::string reason;
  std::vector<std::string> galleryUrls;
  bool commentsOk = false;
  const std::vector<std::pair<int, int>> attempts = {{20, 3}, {10, 2}, {5, 1}};
  const std::vector<std::string> hosts = {"www.reddit.com", "old.reddit.com"};
  for (const auto& attempt : attempts) {
    for (const std::string& host : hosts) {
      const std::string commentsUrl = buildCommentsUrl(subredditName, post, attempt.first, attempt.second, host);
      if (fetchCommentsFromEndpoint(commentsUrl, commentLines, reason, &galleryUrls)) {
        commentsOk = true;
        break;
      }
      LOG_ERR("SUB", "Comments fetch failed host=%s limit=%d depth=%d (%s)", host.c_str(), attempt.first,
              attempt.second, reason.c_str());
    }
    if (commentsOk) {
      break;
    }
    if (reason.find("comments-json=NoMemory") != std::string::npos ||
        reason.find("comments-json=TooDeep") != std::string::npos) {
      LOG_ERR("SUB", "Comments parse pressure; retrying with smaller payload");
    }
  }

  // Apply gallery URLs regardless of whether comment fetch succeeded.
  // fetchCommentsFromEndpoint populates outGalleryUrls before testing for empty
  // comments, so a gallery post with zero comments still provides image URLs.
  if (!galleryUrls.empty()) {
    RedditPost& editablePost = posts[selectedPost];
    std::vector<std::string> preferredUrls;
    preferredUrls.reserve(galleryUrls.size());
    for (const std::string& galleryUrl : galleryUrls) {
      if (galleryUrl.empty() || !hasImageExtension(galleryUrl) || looksLikeVideo(galleryUrl)) {
        continue;
      }
      if (std::find(preferredUrls.begin(), preferredUrls.end(), galleryUrl) == preferredUrls.end()) {
        preferredUrls.push_back(galleryUrl);
      }
    }
    if (!preferredUrls.empty()) {
      editablePost.imageUrls = std::move(preferredUrls);
      currentImageIndex = 0;
      imageCachePath.clear();
    }
    if (!editablePost.imageUrls.empty()) {
      editablePost.hasImage = true;
    }
  }

  if (!commentsOk) {
    if (reason.empty()) {
      reason = "unknown";
    }
    commentLines.clear();
    commentLines.push_back("(comments unavailable: " + reason + ")");
    return;
  }
}

void SubredditActivity::cycleContrast() {
  imageContrastLevel = (imageContrastLevel + 1) % 5;
  writeWholeFile("SUB", kContrastConfigPath, std::to_string(imageContrastLevel));
  requestUpdate();
}

void SubredditActivity::prepareWrappedPost() {
  wrappedLines.clear();
  if (posts.empty()) {
    return;
  }

  const RedditPost& post = posts[selectedPost];
  const int textWidth = renderer.getScreenWidth() - 24;

  wrappedLines.push_back("Title:");
  auto titleLines = wrapTextToWidth(renderer, UI_10_FONT_ID, post.title, textWidth);
  wrappedLines.insert(wrappedLines.end(), titleLines.begin(), titleLines.end());
  wrappedLines.push_back("");

  if (post.hasImage) {
    wrappedLines.push_back("[This post has an image - press Select to view]");
    wrappedLines.push_back("");
  }

  wrappedLines.push_back("Body:");
  auto bodyLines = wrapTextToWidth(renderer, SMALL_FONT_ID, post.body.empty() ? "(no self text)" : post.body, textWidth);
  wrappedLines.insert(wrappedLines.end(), bodyLines.begin(), bodyLines.end());

  wrappedLines.push_back("");
  wrappedLines.push_back("Comments:");
  if (commentLines.empty()) {
    wrappedLines.push_back("(no comments)");
    return;
  }

  for (const std::string& comment : commentLines) {
    auto lines = wrapTextToWidth(renderer, SMALL_FONT_ID, "- " + comment, textWidth);
    wrappedLines.insert(wrappedLines.end(), lines.begin(), lines.end());
    wrappedLines.push_back("");
  }
}

bool SubredditActivity::ensureImageDownloaded() {
  if (posts.empty() || !posts[selectedPost].hasImage || posts[selectedPost].imageUrls.empty()) {
    return false;
  }

  const RedditPost& post = posts[selectedPost];
  if (currentImageIndex >= static_cast<int>(post.imageUrls.size())) {
    currentImageIndex = 0;
  }
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(kCacheDir);

  state = State::LOADING;
  downloadStartMs = millis();
  statusMessage = "Downloading image...";

  const size_t idx = static_cast<size_t>(currentImageIndex);
  const std::vector<std::string> candidateUrls = buildDownloadUrlCandidates(post.imageUrls[idx]);

  HttpDownloader::DownloadError lastError = HttpDownloader::HTTP_ERROR;
  std::string lastUrl;

  for (size_t candidateIndex = 0; candidateIndex < candidateUrls.size(); ++candidateIndex) {
    const std::string& imageUrl = candidateUrls[candidateIndex];
    lastUrl = imageUrl;

    std::string safeId = post.id.empty() ? std::to_string(selectedPost) : post.id;
    const uint32_t urlHash = fnv1a32(imageUrl);
    safeId += "_" + std::to_string(idx) + "_" + std::to_string(static_cast<unsigned long>(urlHash));
    imageCachePath = std::string(kCacheDir) + "/img_" + safeId + extensionForUrl(imageUrl);

    LOG_INF("SUB", "IMG request url[%u]=%s", static_cast<unsigned int>(candidateIndex), imageUrl.c_str());
    LOG_INF("SUB", "IMG cache path=%s", imageCachePath.c_str());

    if (Storage.exists(imageCachePath.c_str())) {
      std::string cacheReason;
      if (validateRenderableImageFile(imageCachePath, kMaxDecoderPixels, &cacheReason)) {
        LOG_INF("SUB", "IMG cache hit index=%u", static_cast<unsigned int>(idx));
        return true;
      }

      LOG_INF("SUB", "IMG cache invalid; removing (%s)", cacheReason.c_str());
      Storage.remove(imageCachePath.c_str());
    }

    statusMessage = "Downloading image...";

    HttpDownloader::DownloadError result = HttpDownloader::HTTP_ERROR;
    for (int attempt = 0; attempt < 2; ++attempt) {
      result = HttpDownloader::downloadToFile(imageUrl, imageCachePath, {});
      if (result == HttpDownloader::OK) {
        break;
      }
      if (attempt == 0) {
        delay(200);
      }
    }

    if (result != HttpDownloader::OK) {
      lastError = result;
      Storage.remove(imageCachePath.c_str());
      LOG_ERR("SUB", "IMG download failed code=%d url=%s", static_cast<int>(result), imageUrl.c_str());
      continue;
    }

    FsFile downloaded;
    if (Storage.openFileForRead("SUB", imageCachePath.c_str(), downloaded)) {
      const size_t bytes = static_cast<size_t>(downloaded.size());
      downloaded.close();
      LOG_INF("SUB", "IMG downloaded bytes=%u", static_cast<unsigned int>(bytes));
      if (bytes > kMaxImageBytes) {
        Storage.remove(imageCachePath.c_str());
        LOG_ERR("SUB", "IMG rejected too large bytes=%u", static_cast<unsigned int>(bytes));
        continue;
      }
    }

    std::string downloadReason;
    if (!validateRenderableImageFile(imageCachePath, kMaxDecoderPixels, &downloadReason)) {
      Storage.remove(imageCachePath.c_str());
      LOG_ERR("SUB", "IMG rejected after download (%s)", downloadReason.c_str());
      continue;
    }

    LOG_INF("SUB", "IMG download completed in %u ms index=%u",
            static_cast<unsigned int>(millis() - downloadStartMs), static_cast<unsigned int>(idx));
    return true;
  }

  state = State::ERROR;
  errorFromImage = true;
  if (lastError == HttpDownloader::ABORTED) {
    errorMessage = "Image download stalled/timeout";
  } else {
    errorMessage = "Image download failed";
  }
  LOG_ERR("SUB", "IMG all URLs failed; last code=%d last url=%s", static_cast<int>(lastError), lastUrl.c_str());
  requestUpdate();
  return false;
}

bool SubredditActivity::renderCurrentImage() {
  if (imageCachePath.empty()) {
    LOG_ERR("SUB", "IMG render failed: empty path");
    return false;
  }

  renderStartMs = millis();
  LOG_INF("SUB", "IMG render phase started path=%s", imageCachePath.c_str());

  ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(imageCachePath);
  if (!decoder) {
    errorMessage = "Unsupported image format";
    LOG_ERR("SUB", "IMG render failed: decoder null for %s", imageCachePath.c_str());
    return false;
  }

  ImageDimensions dims;
  if (!decoder->getDimensions(imageCachePath, dims) || dims.width <= 0 || dims.height <= 0) {
    errorMessage = "Failed to read image size";
    LOG_ERR("SUB", "IMG render failed: bad dims for %s", imageCachePath.c_str());
    return false;
  }

  RenderConfig cfg;
  const int availableX = 0;
  const int availableY = 22;
  const int availableWidth = renderer.getScreenWidth();
  const int availableHeight = renderer.getScreenHeight() - 58;

  const float scaleX = static_cast<float>(availableWidth) / static_cast<float>(dims.width);
  const float scaleY = static_cast<float>(availableHeight) / static_cast<float>(dims.height);
  // Decoder path is optimized for downscaling. Upscaling small previews can look faint/sparse,
  // but a modest upscale helps very small previews remain readable.
  const float scale = std::min(1.35f, std::min(scaleX, scaleY));

  const int destWidth = std::max(1, static_cast<int>(dims.width * scale));
  const int destHeight = std::max(1, static_cast<int>(dims.height * scale));

  cfg.x = availableX + std::max(0, (availableWidth - destWidth) / 2);
  cfg.y = availableY + std::max(0, (availableHeight - destHeight) / 2);
  cfg.maxWidth = destWidth;
  cfg.maxHeight = destHeight;
  cfg.useDithering = true;
  cfg.useGrayscale = false;
  cfg.brightnessOffset = kContrastBrightnessOffsets[imageContrastLevel];
  cfg.performanceMode = false;
  cfg.useExactDimensions = false;

  LOG_INF("SUB", "IMG dims=%dx%d scale=%.2f dst=%dx%d pos=%d,%d", dims.width, dims.height, scale, cfg.maxWidth,
          cfg.maxHeight, cfg.x, cfg.y);
  LOG_INF("SUB", "IMG decode start path=%s max=%dx%d", imageCachePath.c_str(), cfg.maxWidth, cfg.maxHeight);
  const bool ok = decoder->decodeToFramebuffer(imageCachePath, renderer, cfg);
  LOG_INF("SUB", "IMG decode result=%d elapsed=%u ms", ok ? 1 : 0,
          static_cast<unsigned int>(millis() - renderStartMs));
  return ok;
}

void SubredditActivity::loop() {
  if (subActivity) {
    ActivityWithSubactivity::loop();
    return;
  }

  if ((state == State::LIST || state == State::POST) && millis() - lastRefreshMs >= refreshIntervalMs) {
    refreshFeed(true);
    return;
  }

  if (state == State::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (errorFromImage) {
        if (!imageCachePath.empty()) {
          Storage.remove(imageCachePath.c_str());
        }
        if (!posts.empty() && posts[selectedPost].hasImage && ensureImageDownloaded()) {
          state = State::IMAGE;
          imageReady = false;
          imageRenderPhaseShown = false;
          errorMessage.clear();
          requestUpdate();
        }
      } else {
        refreshFeed(false);
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (!posts.empty()) {
        state = State::LIST;
        errorFromImage = false;
        requestUpdate();
      } else {
        onGoHome();
      }
    }
    return;
  }

  if (state == State::CHECK_WIFI || state == State::LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    }
    return;
  }

  if (state == State::LIST) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (selectedPost == -2) {
        openSubredditPicker();
        return;
      }
      if (selectedPost == -1) {
        cycleContrast();
        return;
      }
      openSelectedPost();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      sortMode = nextSortMode(sortMode);
      writeWholeFile("SUB", kSortConfigPath, sortMode);
      nextPageAfter.clear();
      hasMorePosts = false;
      refreshFeed(false);
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      loadMorePosts();
      return;
    }

    // Up/Down for post navigation
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      const int totalItems = static_cast<int>(posts.size()) + 2;
      int selectedIndex = selectedPost + 2;
      selectedIndex = (selectedIndex - 1 + totalItems) % totalItems;
      selectedPost = selectedIndex - 2;
      currentImageIndex = 0;
      requestUpdate();
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      const int totalItems = static_cast<int>(posts.size()) + 2;
      int selectedIndex = selectedPost + 2;
      selectedIndex = (selectedIndex + 1) % totalItems;
      selectedPost = selectedIndex - 2;
      currentImageIndex = 0;
      requestUpdate();
    }
    return;
  }

  if (state == State::POST) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = State::LIST;
      currentImageIndex = 0;
      requestUpdate();
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (posts[selectedPost].hasImage) {
        if (ensureImageDownloaded()) {
          state = State::IMAGE;
          imageReady = false;
          imageRenderPhaseShown = false;
          requestUpdate();
        }
      } else {
        state = State::LIST;
        requestUpdate();
      }
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      topVisibleLine = std::max(0, topVisibleLine - 1);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      topVisibleLine = std::min(static_cast<int>(wrappedLines.size()) - 1, topVisibleLine + 1);
      requestUpdate();
    }
    return;
  }

  if (state == State::IMAGE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = State::POST;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      imageReady = false;
      imageRenderPhaseShown = false;
      requestUpdate();
      return;
    }
    
    // Left/Right to navigate between images in multi-image posts
    if (!posts.empty() && posts[selectedPost].imageUrls.size() > 1) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
        currentImageIndex = (currentImageIndex - 1 + static_cast<int>(posts[selectedPost].imageUrls.size())) % 
                           static_cast<int>(posts[selectedPost].imageUrls.size());
        imageReady = false;
        imageRenderPhaseShown = false;
        if (ensureImageDownloaded()) {
          requestUpdate();
        }
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
        currentImageIndex = (currentImageIndex + 1) % static_cast<int>(posts[selectedPost].imageUrls.size());
        imageReady = false;
        imageRenderPhaseShown = false;
        if (ensureImageDownloaded()) {
          requestUpdate();
        }
        return;
      }
    }
  }
}

void SubredditActivity::render(Activity::RenderLock&&) {
  if (!(state == State::IMAGE && imageReady)) {
    renderer.clearScreen();
  }

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const std::string header = "r/" + subredditName + " [" + sortModeLabel(sortMode) + "]";
  renderer.drawCenteredText(UI_12_FONT_ID, 15, header.c_str(), true, EpdFontFamily::BOLD);

  if (state == State::CHECK_WIFI || state == State::LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == State::ERROR) {
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 20, "Subreddit error", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 8, errorMessage.c_str());
    if (!debugReason.empty()) {
      std::string reasonLine = "Reason: " + debugReason;
      if (reasonLine.size() > 78) {
        reasonLine = reasonLine.substr(0, 78);
      }
      renderer.drawText(SMALL_FONT_ID, 10, pageHeight / 2 + 28, reasonLine.c_str());
    }
    if (!debugEndpoint.empty()) {
      std::string endpointLine = "Endpoint: " + debugEndpoint;
      if (endpointLine.size() > 78) {
        endpointLine = endpointLine.substr(0, 78);
      }
      renderer.drawText(SMALL_FONT_ID, 10, pageHeight / 2 + 46, endpointLine.c_str());
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == State::LIST) {
    const int totalItems = static_cast<int>(posts.size()) + 2;
    const int selectedIndex = selectedPost + 2;
    const int startIndex = (selectedIndex / kListPageItems) * kListPageItems;
    const int rowHeight = 30;
    const int top = 44;
    const int visibleRows = std::min(kListPageItems, totalItems - startIndex);
    const int selectedRow = selectedIndex - startIndex;

    renderer.fillRect(0, top + selectedRow * rowHeight - 2, pageWidth - 1, rowHeight);

    for (int i = 0; i < visibleRows; i++) {
      const int itemIndex = startIndex + i;
      std::string text;
      if (itemIndex == 0) {
        text = "[Change subreddit...]";
      } else if (itemIndex == 1) {
        text = std::string("[Contrast: ") + kContrastLabels[imageContrastLevel] + "]";
      } else {
        const int postIndex = itemIndex - 2;
        const RedditPost& post = posts[postIndex];
        text = post.hasImage ? "[IMG] " + post.title : post.title;
      }
      text = renderer.truncatedText(UI_10_FONT_ID, text.c_str(), pageWidth - 24);
      renderer.drawText(UI_10_FONT_ID, 12, top + i * rowHeight, text.c_str(), itemIndex != selectedIndex);
    }

    const auto sortLabel = std::string("Sort: ") + sortModeLabel(sortMode);
    const char* moreLabel = hasMorePosts ? "More" : "No more";
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), sortLabel.c_str(), moreLabel);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == State::POST) {
    const int bodyTop = 44;
    const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID) + 2;
    const int maxLines = std::max(1, (pageHeight - bodyTop - 34) / lineHeight);

    for (int i = 0; i < maxLines; i++) {
      const int lineIndex = topVisibleLine + i;
      if (lineIndex >= static_cast<int>(wrappedLines.size())) {
        break;
      }
      const std::string& line = wrappedLines[lineIndex];
      renderer.drawText(SMALL_FONT_ID, 12, bodyTop + i * lineHeight, line.c_str());
    }

    const bool hasImage = !posts.empty() && posts[selectedPost].hasImage;
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), hasImage ? "Image" : "List", "", tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == State::IMAGE) {
    // Show image counter if multiple images
    std::string imageInfo = "Image view";
    if (!posts.empty() && posts[selectedPost].imageUrls.size() > 1) {
      imageInfo += " [" + std::to_string(currentImageIndex + 1) + "/" + 
                  std::to_string(posts[selectedPost].imageUrls.size()) + "]";
    }
    renderer.drawText(SMALL_FONT_ID, 8, pageHeight - 28, imageInfo.c_str());
    
    // Button hints: Back/Retry + Left/Right if multiple images
    const char* btn3Label = "";
    const char* btn4Label = "";
    if (!posts.empty() && posts[selectedPost].imageUrls.size() > 1) {
      btn3Label = "Prev";
      btn4Label = "Next";
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), btn3Label, btn4Label);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

    if (!imageReady) {
      if (!imageRenderPhaseShown) {
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Rendering image...");
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        imageRenderPhaseShown = true;
        requestUpdate();
        return;
      }

      renderer.setRenderMode(GfxRenderer::BW);
      const bool ok = renderCurrentImage();
      if (!ok) {
        if (!posts.empty() && posts[selectedPost].imageUrls.size() > 1) {
          const int imageCount = static_cast<int>(posts[selectedPost].imageUrls.size());
          const int failedIndex = currentImageIndex;
          if (!imageCachePath.empty()) {
            Storage.remove(imageCachePath.c_str());
          }

          for (int attempt = 1; attempt < imageCount; ++attempt) {
            currentImageIndex = (failedIndex + attempt) % imageCount;
            LOG_INF("SUB", "IMG render failed; trying alternate index=%d", currentImageIndex);
            if (ensureImageDownloaded()) {
              state = State::IMAGE;
              imageReady = false;
              imageRenderPhaseShown = false;
              errorMessage.clear();
              requestUpdate();
              return;
            }
          }

          currentImageIndex = failedIndex;
        }

        state = State::ERROR;
        errorFromImage = true;
        if (errorMessage.empty()) {
          errorMessage = "Failed to render image";
        }
        requestUpdate();
        return;
      }

      renderer.displayBuffer();

      imageReady = true;
      imageRenderPhaseShown = false;
      return;
    }

    renderer.displayBuffer();
    return;
  }

  renderer.displayBuffer();
}
