#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"

struct RedditPost {
  std::string id;
  std::string title;
  std::string body;
  std::string permalink;
  std::vector<std::string> imageUrls;
  bool hasImage = false;
};

class SubredditActivity final : public Activity {
 public:
  explicit SubredditActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Subreddit", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  enum class State {
    CHECK_WIFI,
    WIFI_SELECTION,
    LOADING,
    LIST,
    POST,
    IMAGE,
    ERROR,
  };

  State state = State::CHECK_WIFI;
  std::vector<RedditPost> posts;
  std::vector<std::string> wrappedLines;
  std::vector<std::string> commentLines;
  std::string sortMode = "new";
  std::string nextPageAfter;
  bool hasMorePosts = false;
  std::string subredditName;
  std::string imageCachePath;
  std::string errorMessage;
  std::string statusMessage;
  std::string debugReason;
  std::string debugEndpoint;
  bool errorFromImage = false;
  int selectedPost = 0;
  int currentImageIndex = 0;
  int topVisibleLine = 0;
  uint32_t lastRefreshMs = 0;
  uint32_t refreshIntervalMs = 15UL * 60UL * 1000UL;
  uint32_t downloadStartMs = 0;
  uint32_t renderStartMs = 0;
  bool imageReady = false;
  bool imageRenderPhaseShown = false;
  int imageContrastLevel = 2;

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  bool loadCachedFeed();
  bool saveCachedFeed(const std::string& json);
  bool parseFeedJson(const std::string& json);
  void refreshFeed(bool automatic);
  void loadMorePosts();
  void openSelectedPost();
  void loadCommentsForSelectedPost();
  void prepareWrappedPost();
  bool ensureImageDownloaded();
  bool renderCurrentImage();
  void loadSubredditConfig();
  void openSubredditPicker();
  void cycleContrast();
};
