#include "SubredditSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int MENU_ITEMS = 2;
constexpr const char* kSubredditPath = "/.crosspoint/subreddit.txt";
constexpr const char* kRefreshPath = "/.crosspoint/subreddit_refresh_minutes.txt";
constexpr std::array<uint16_t, 4> kRefreshOptions = {5, 15, 30, 60};

std::string trim(const std::string& in) {
  size_t left = 0;
  size_t right = in.size();
  while (left < right && std::isspace(static_cast<unsigned char>(in[left])) != 0) {
    left++;
  }
  while (right > left && std::isspace(static_cast<unsigned char>(in[right - 1])) != 0) {
    right--;
  }
  return in.substr(left, right - left);
}

std::string normalizeSubreddit(const std::string& input) {
  std::string value = trim(input);
  if (value.rfind("r/", 0) == 0) {
    value = value.substr(2);
  }
  while (!value.empty() && value[0] == '/') {
    value.erase(value.begin());
  }
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }
  if (value.empty()) {
    value = "esp32";
  }
  return value;
}

bool readWholeFile(const char* path, std::string& out) {
  FsFile f;
  if (!Storage.openFileForRead("SUBCFG", path, f)) {
    return false;
  }
  out.resize(static_cast<size_t>(f.size()));
  const int bytes = f.read(out.data(), out.size());
  f.close();
  if (bytes <= 0) {
    return false;
  }
  out.resize(static_cast<size_t>(bytes));
  return true;
}

bool writeWholeFile(const char* path, const std::string& value) {
  Storage.mkdir("/.crosspoint");
  FsFile f;
  if (!Storage.openFileForWrite("SUBCFG", path, f)) {
    return false;
  }
  const size_t written = f.write(reinterpret_cast<const uint8_t*>(value.data()), value.size());
  f.close();
  return written == value.size();
}

std::string readSubreddit() {
  std::string value;
  if (!readWholeFile(kSubredditPath, value)) {
    return "esp32";
  }
  return normalizeSubreddit(value);
}

uint16_t readRefreshMinutes() {
  std::string value;
  if (!readWholeFile(kRefreshPath, value)) {
    return 15;
  }

  const int parsed = std::atoi(trim(value).c_str());
  for (uint16_t option : kRefreshOptions) {
    if (parsed == static_cast<int>(option)) {
      return option;
    }
  }
  return 15;
}

void writeRefreshMinutes(uint16_t minutes) {
  writeWholeFile(kRefreshPath, std::to_string(minutes));
}
}  // namespace

void SubredditSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void SubredditSettingsActivity::onExit() { Activity::onExit(); }

void SubredditSettingsActivity::loop() {

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = (selectedIndex + 1) % MENU_ITEMS;
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = (selectedIndex + MENU_ITEMS - 1) % MENU_ITEMS;
    requestUpdate();
  });
}

void SubredditSettingsActivity::handleSelection() {
  if (selectedIndex == 0) {
    const std::string current = readSubreddit();
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Subreddit (no r/)", current, 48, false),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            writeWholeFile(kSubredditPath, normalizeSubreddit(std::get<KeyboardResult>(result.data).text));
          }
          requestUpdate();
        });
    return;
  }

  uint16_t current = readRefreshMinutes();
  auto it = std::find(kRefreshOptions.begin(), kRefreshOptions.end(), current);
  size_t idx = (it == kRefreshOptions.end()) ? 1 : static_cast<size_t>(it - kRefreshOptions.begin());
  idx = (idx + 1) % kRefreshOptions.size();
  writeRefreshMinutes(kRefreshOptions[idx]);
  requestUpdate();
}

void SubredditSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Subreddit Reader");
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    "Configure feed + refresh");

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + metrics.tabBarHeight;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, MENU_ITEMS, static_cast<int>(selectedIndex),
      [](int index) {
        if (index == 0) {
          return std::string("Subreddit");
        }
        return std::string("Refresh interval");
      },
      nullptr, nullptr,
      [](int index) {
        if (index == 0) {
          return readSubreddit();
        }
        return std::to_string(readRefreshMinutes()) + " min";
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
