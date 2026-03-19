#include "BluetoothSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <cstring>

#include "CrossPointSettings.h"
#include "DeviceProfiles.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void BluetoothSettingsActivity::onEnter() {
  Activity::onEnter();
  
  selectedIndex = 0;
  viewMode = ViewMode::MAIN_MENU;
  lastError = "";
  lastScanTime = 0;
  pendingLearnKey = 0;
  pendingLearnIndex = 0xFF;
  learnedPrevKey = 0;
  learnedNextKey = 0;
  learnedReportIndex = 2;
  learnStep = LearnStep::WAIT_PREV;
  
  // Get BLE manager instance
  try {
    btMgr = &BluetoothHIDManager::getInstance();
    LOG_INF("BT", "BluetoothHIDManager ready");
    
    // Restore Bluetooth persistent state on entry
    if (SETTINGS.bluetoothEnabled && !btMgr->isEnabled()) {
      LOG_INF("BT", "Restoring Bluetooth from settings (enabled)");
      if (btMgr->enable()) {
        lastError = "Bluetooth restored";
      } else {
        lastError = "Failed to restore BT";
        SETTINGS.bluetoothEnabled = 0;
      }
    } else if (!SETTINGS.bluetoothEnabled && btMgr->isEnabled()) {
      LOG_INF("BT", "Disabling Bluetooth per settings (disabled)");
      btMgr->disable();
      lastError = "Bluetooth disabled per settings";
    }

  } catch (const std::exception& e) {
    LOG_ERR("BT", "Failed to get BLE manager: %s", e.what());
    lastError = "BLE manager error";
    btMgr = nullptr;
  } catch (...) {
    LOG_ERR("BT", "Unknown error getting BLE manager");
    lastError = "Unknown error";
    btMgr = nullptr;
  }
  
  requestUpdate();
}

void BluetoothSettingsActivity::onExit() {
  if (btMgr) {
    btMgr->setLearnInputCallback(nullptr);
  }
  Activity::onExit();
}

void BluetoothSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (viewMode == ViewMode::DEVICE_LIST) {
      // Return to main menu
      viewMode = ViewMode::MAIN_MENU;
      selectedIndex = 0;
      if (btMgr && btMgr->isScanning()) {
        btMgr->stopScan();
      }
      requestUpdate();
      return;
    } else if (viewMode == ViewMode::LEARN_KEYS) {
      if (btMgr) {
        btMgr->setLearnInputCallback(nullptr);
      }
      viewMode = ViewMode::MAIN_MENU;
      selectedIndex = 0;
      lastError = "Learn mode canceled";
      requestUpdate();
      return;
    } else {
      if (onComplete) onComplete();
      return;
    }
  }

  // Check if scan completed
  if (btMgr && viewMode == ViewMode::DEVICE_LIST && !btMgr->isScanning() && lastScanTime > 0) {
    if (millis() - lastScanTime > 500) { // Small delay to see final results
      lastScanTime = 0;
      requestUpdate();
    }
  }

  if (viewMode == ViewMode::MAIN_MENU) {
    handleMainMenuInput();
  } else if (viewMode == ViewMode::DEVICE_LIST) {
    handleDeviceListInput();
  } else {
    handleLearnInput();
  }
}

void BluetoothSettingsActivity::handleMainMenuInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedIndex = (selectedIndex > 0) ? selectedIndex - 1 : 5;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedIndex = (selectedIndex < 5) ? selectedIndex + 1 : 0;
    requestUpdate();
  }
  
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (!btMgr) {
      lastError = "BLE not available";
      LOG_ERR("BT", "BLE manager not available");
      requestUpdate();
      return;
    }

    if (selectedIndex == 0) {
      // Toggle Bluetooth
      try {
        if (btMgr->isEnabled()) {
          LOG_INF("BT", "Disabling Bluetooth...");
          if (btMgr->disable()) {
            lastError = "Bluetooth disabled";
            SETTINGS.bluetoothEnabled = 0;
            SETTINGS.saveToFile();
          } else {
            lastError = "Failed to disable";
          }
        } else {
          LOG_INF("BT", "Enabling Bluetooth...");
          if (btMgr->enable()) {
            lastError = "Bluetooth enabled";
            SETTINGS.bluetoothEnabled = 1;
            SETTINGS.saveToFile();
          } else {
            lastError = btMgr->lastError.empty() ? "Failed to enable" : btMgr->lastError;
          }
        }
      } catch (const std::exception& e) {
        lastError = std::string("Error: ") + e.what();
        LOG_ERR("BT", "Toggle error: %s", e.what());
      } catch (...) {
        lastError = "Unknown toggle error";
        LOG_ERR("BT", "Unknown error toggling Bluetooth");
      }
      requestUpdate();
    } else if (selectedIndex == 1) {
      if (!btMgr->isEnabled()) {
        lastError = "Enable BT first";
      } else if (SETTINGS.bleBondedDeviceAddr[0] == '\0') {
        lastError = "No bonded remote saved";
      } else if (btMgr->isConnected(SETTINGS.bleBondedDeviceAddr)) {
        lastError = "Bonded remote already connected";
      } else {
        LOG_INF("BT", "Reconnecting to bonded remote %s (%s)", SETTINGS.bleBondedDeviceName,
                SETTINGS.bleBondedDeviceAddr);
        lastError = "Reconnecting...";
        requestUpdate();

        if (btMgr->connectToDevice(SETTINGS.bleBondedDeviceAddr)) {
          lastError = std::string("Reconnected to ") +
                      (SETTINGS.bleBondedDeviceName[0] ? SETTINGS.bleBondedDeviceName : "bonded remote");
        } else {
          lastError = btMgr->lastError.empty() ? "Reconnect failed" : btMgr->lastError;
        }
      }
      requestUpdate();
    } else if (selectedIndex == 2) {
      // Start scan and switch to device list
      if (btMgr->isEnabled()) {
        btMgr->startScan(10000);
        lastScanTime = millis();
        viewMode = ViewMode::DEVICE_LIST;
        selectedIndex = 0;
        lastError = "";
      } else {
        lastError = "Enable BT first";
      }
      requestUpdate();
    } else if (selectedIndex == 3) {
      if (!btMgr->isEnabled()) {
        lastError = "Enable BT first";
      } else if (btMgr->getConnectedDevices().empty()) {
        lastError = "Connect a remote first";
      } else {
        viewMode = ViewMode::LEARN_KEYS;
        learnStep = LearnStep::WAIT_PREV;
        pendingLearnKey = 0;
        pendingLearnIndex = 0xFF;
        learnedPrevKey = 0;
        learnedNextKey = 0;
        learnedReportIndex = 2;
        btMgr->setLearnInputCallback([this](uint8_t keycode, uint8_t reportIndex) {
          if (viewMode == ViewMode::LEARN_KEYS && keycode > 0 && reportIndex != 0xFF) {
            pendingLearnKey = keycode;
            pendingLearnIndex = reportIndex;
          }
        });
        lastError = "Press PREVIOUS PAGE button";
      }
      requestUpdate();
    } else if (selectedIndex == 4) {
      DeviceProfiles::clearCustomProfile();
      lastError = "Learned mapping cleared";
      requestUpdate();
    } else if (selectedIndex == 5) {
      SETTINGS.bleBondedDeviceAddr[0] = '\0';
      SETTINGS.bleBondedDeviceName[0] = '\0';
      SETTINGS.bleBondedDeviceAddrType = 0;
      SETTINGS.saveToFile();
      btMgr->setBondedDevice("", "");
      lastError = "Bonded remote cleared";
      requestUpdate();
    }
  }
}

void BluetoothSettingsActivity::handleLearnInput() {
  if (pendingLearnKey != 0) {
    const uint8_t capturedKey = pendingLearnKey;
    const uint8_t capturedIndex = pendingLearnIndex;
    pendingLearnKey = 0;
    pendingLearnIndex = 0xFF;

    if (learnStep == LearnStep::WAIT_PREV) {
      learnedPrevKey = capturedKey;
      learnedReportIndex = (capturedIndex == 0xFF) ? 2 : capturedIndex;
      learnStep = LearnStep::WAIT_NEXT;
      char buf[96];
      snprintf(buf, sizeof(buf), "Prev=0x%02X @byte[%u], press NEXT", learnedPrevKey,
               static_cast<unsigned>(learnedReportIndex));
      lastError = buf;
      requestUpdate();
      return;
    }

    if (learnStep == LearnStep::WAIT_NEXT) {
      if (capturedKey == learnedPrevKey) {
        lastError = "Next key must be different";
        requestUpdate();
        return;
      }

      if (capturedIndex != 0xFF && capturedIndex != learnedReportIndex) {
        char buf[96];
        snprintf(buf, sizeof(buf), "NEXT key must use byte[%u]", static_cast<unsigned>(learnedReportIndex));
        lastError = buf;
        requestUpdate();
        return;
      }

      learnedNextKey = capturedKey;
      DeviceProfiles::setCustomProfile(learnedPrevKey, learnedNextKey, learnedReportIndex);
      if (btMgr) {
        btMgr->setLearnInputCallback(nullptr);
      }
      learnStep = LearnStep::DONE;
      char buf[96];
      snprintf(buf, sizeof(buf), "Saved Prev=0x%02X Next=0x%02X @byte[%u]", learnedPrevKey, learnedNextKey,
               static_cast<unsigned>(learnedReportIndex));
      lastError = buf;
      requestUpdate();
      return;
    }
  }

  if (learnStep == LearnStep::DONE && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (btMgr) {
      btMgr->setLearnInputCallback(nullptr);
    }
    viewMode = ViewMode::MAIN_MENU;
    selectedIndex = 0;
    requestUpdate();
  }
}

void BluetoothSettingsActivity::handleDeviceListInput() {
  if (!btMgr) return;

  const auto& devices = btMgr->getDiscoveredDevices();
  const auto& connectedDevices = btMgr->getConnectedDevices();
  
  // Calculate menu items: devices + "Refresh" + "Disconnect" (if connected)
  int menuItems = devices.size() + 1; // +1 for Refresh
  if (!connectedDevices.empty()) {
    menuItems++; // +1 for Disconnect
  }
  int maxIndex = menuItems - 1;

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedIndex = (selectedIndex > 0) ? selectedIndex - 1 : maxIndex;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedIndex = (selectedIndex < maxIndex) ? selectedIndex + 1 : 0;
    requestUpdate();
  }
  
  // Left/Right for back/refresh
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    // Go back to main menu
    viewMode = ViewMode::MAIN_MENU;
    selectedIndex = 0;
    if (btMgr && btMgr->isScanning()) {
      btMgr->stopScan();
    }
    requestUpdate();
    return;
  }
  
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    // Quick rescan
    LOG_INF("BT", "Quick rescan...");
    lastError = "Scanning...";
    btMgr->startScan(10000);
    lastScanTime = millis();
    selectedIndex = 0;
    requestUpdate();
    return;
  }
  
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    // Check if "Refresh" is selected
    if (selectedIndex == static_cast<int>(devices.size())) {
      LOG_INF("BT", "Refreshing scan...");
      lastError = "Scanning...";
      btMgr->startScan(10000);
      lastScanTime = millis();
      selectedIndex = 0;
      requestUpdate();
      return;
    }
    
    // Check if "Disconnect" is selected
    if (!connectedDevices.empty() && selectedIndex == static_cast<int>(devices.size()) + 1) {
      LOG_INF("BT", "Disconnecting from all devices...");
      // Make a copy of addresses to avoid iterator invalidation
      std::vector<std::string> deviceAddresses = connectedDevices;
      for (const auto& addr : deviceAddresses) {
        LOG_DBG("BT", "Disconnecting from %s", addr.c_str());
        btMgr->disconnectFromDevice(addr);
      }
      lastError = "Disconnected";
      selectedIndex = 0;
      requestUpdate();
      return;
    }
    
    // Otherwise, connect to selected device
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(devices.size())) {
      const auto& device = devices[selectedIndex];
      
      LOG_INF("BT", "Connecting to %s (%s)", device.name.c_str(), device.address.c_str());
      lastError = "Connecting...";
      requestUpdate();
      
      if (btMgr->connectToDevice(device.address)) {
        strncpy(SETTINGS.bleBondedDeviceAddr, device.address.c_str(), sizeof(SETTINGS.bleBondedDeviceAddr) - 1);
        SETTINGS.bleBondedDeviceAddr[sizeof(SETTINGS.bleBondedDeviceAddr) - 1] = '\0';
        strncpy(SETTINGS.bleBondedDeviceName, device.name.c_str(), sizeof(SETTINGS.bleBondedDeviceName) - 1);
        SETTINGS.bleBondedDeviceName[sizeof(SETTINGS.bleBondedDeviceName) - 1] = '\0';
        SETTINGS.bleBondedDeviceAddrType = 0;
        SETTINGS.saveToFile();
        btMgr->setBondedDevice(device.address, device.name);

        lastError = "Bluetooth enabled";
        LOG_INF("BT", "Successfully connected to %s", device.name.c_str());
        if (exitOnSuccessfulConnect) {
          if (onComplete) onComplete();
          return;
        }
      } else {
        lastError = btMgr->lastError.empty() ? "Connection failed" : btMgr->lastError;
        LOG_ERR("BT", "Failed to connect: %s", lastError.c_str());
      }
      requestUpdate();
    }
  }
}

void BluetoothSettingsActivity::render(Activity::RenderLock&&) {
  if (viewMode == ViewMode::MAIN_MENU) {
    renderMainMenu();
  } else if (viewMode == ViewMode::DEVICE_LIST) {
    renderDeviceList();
  } else {
    renderLearnKeys();
  }
}

void BluetoothSettingsActivity::renderMainMenu() {
  auto metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // Header with Bluetooth title
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BLUETOOTH));

  // Status subheader
  std::string statusLine;
  if (btMgr) {
    if (btMgr->isEnabled()) {
      auto connDevices = btMgr->getConnectedDevices();
      if (!connDevices.empty()) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Enabled, %zu device(s) connected", connDevices.size());
        statusLine = buf;
      } else {
        statusLine = "Enabled, no devices connected";
      }
    } else {
      statusLine = "Disabled";
    }
  } else {
    statusLine = "Error initializing Bluetooth";
  }
  
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    statusLine.c_str());

  int listOffsetY = 0;
  if (btMgr && btMgr->isEnabled() && SETTINGS.bleBondedDeviceName[0] != '\0') {
    const int nameY = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + 4;
    std::string deviceLine = std::string("Remote: ") + SETTINGS.bleBondedDeviceName;
    deviceLine = renderer.truncatedText(UI_10_FONT_ID, deviceLine.c_str(), pageWidth - metrics.contentSidePadding * 2);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, nameY, deviceLine.c_str(), true);
    listOffsetY = renderer.getLineHeight(UI_10_FONT_ID) + 4;
  }

  // Use GUI.drawList for consistent formatting with main settings
  const char* items[] = {
    btMgr && btMgr->isEnabled() ? "Disable Bluetooth" : "Enable Bluetooth",
    "Reconnect Bonded Remote",
    "Scan for Devices",
    "Learn Page-Turn Keys",
    "Clear Learned Keys",
    "Forget Bonded Remote"
  };

  std::vector<std::string> itemLabels;
  for (int i = 0; i < 6; i++) {
    itemLabels.push_back(items[i]);
  }

  GUI.drawList(
      renderer,
      Rect{0,
           metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing + listOffsetY,
           pageWidth,
           pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.buttonHintsHeight +
                         metrics.verticalSpacing * 2 + listOffsetY)},
      6, selectedIndex,
      [&itemLabels](int index) { return itemLabels[index]; }, nullptr, nullptr,
      [this](int i) {
        if (i == 0) {
          return std::string(btMgr && btMgr->isEnabled() ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
        }
        if (i == 1 && SETTINGS.bleBondedDeviceName[0] != '\0') {
          return renderer.truncatedText(UI_10_FONT_ID, SETTINGS.bleBondedDeviceName,
                                        renderer.getScreenWidth() - UITheme::getInstance().getMetrics().contentSidePadding * 4);
        }
        return std::string("");
      },
      true);

  if (!lastError.empty()) {
    std::string statusText = renderer.truncatedText(UI_10_FONT_ID, lastError.c_str(),
                                                    pageWidth - metrics.contentSidePadding * 2);
    const int statusY = pageHeight - metrics.buttonHintsHeight - metrics.contentSidePadding - renderer.getLineHeight(UI_10_FONT_ID);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, statusY, statusText.c_str(), true);
  }

  // Button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void BluetoothSettingsActivity::renderDeviceList() {
  auto metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  if (!btMgr) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Bluetooth error");
    return;
  }

  const auto& devices = btMgr->getDiscoveredDevices();
  const auto& connectedDevices = btMgr->getConnectedDevices();

  // Header with device count
  char countStr[32];
  snprintf(countStr, sizeof(countStr), btMgr->isScanning() ? tr(STR_SCANNING) : "Found %zu", devices.size());
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, 
                 tr(STR_BLUETOOTH), countStr);

  // Subheader with scan status
  std::string subheaderText;
  if (btMgr->isScanning()) {
    subheaderText = "Searching for devices...";
  } else {
    if (devices.empty()) {
      subheaderText = "No devices found";
    } else {
      char buf[64];
      snprintf(buf, sizeof(buf), "%d device(s) available", (int)devices.size());
      subheaderText = buf;
    }
  }
  
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    subheaderText.c_str());

  // Build device list labels
  std::vector<std::string> deviceLabels;
  std::vector<std::string> deviceValues;
  char buf[128];
  int deviceCount = 0;
  
  if (!devices.empty()) {
    for (const auto& device : devices) {
      bool connected = btMgr->isConnected(device.address);
      
      // Device name with indicators
      const char* connSymbol = connected ? "[*] " : "";
      const char* hidSymbol = device.isHID ? "[HID] " : "";
      snprintf(buf, sizeof(buf), "%s%s%s", connSymbol, hidSymbol, device.name.c_str());
      deviceLabels.push_back(buf);
      
      // RSSI/signal strength
      std::string signalBars = getSignalStrengthIndicator(device.rssi);
      snprintf(buf, sizeof(buf), "%s (%d dBm)", signalBars.c_str(), device.rssi);
      deviceValues.push_back(buf);
      
      deviceCount++;
      
      // Limit to reasonable number of devices to show
      if (deviceCount >= 8) break;
    }
  }
  
  // Add action buttons
  if (deviceCount < (int)devices.size()) {
    deviceLabels.push_back("...");
    deviceValues.push_back("");
  }
  
  deviceLabels.push_back("< Rescan >");
  deviceValues.push_back("");
  
  if (!connectedDevices.empty()) {
    deviceLabels.push_back("< Disconnect All >");
    deviceValues.push_back("");
  }
  
  // Render the list using GUI.drawList for consistency
  GUI.drawList(
      renderer,
      Rect{0, metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.buttonHintsHeight +
                         metrics.verticalSpacing * 2)},
      deviceLabels.size(), selectedIndex,
      [&deviceLabels](int index) { return deviceLabels[index]; }, nullptr, nullptr,
      [&deviceValues](int i) { return i < (int)deviceValues.size() ? deviceValues[i] : std::string(""); },
      true);

  // Help text
  GUI.drawHelpText(renderer,
                   Rect{0, pageHeight - metrics.buttonHintsHeight - metrics.contentSidePadding - 15, pageWidth, 20},
                   "Up/Down: Select | Right: Rescan");

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONNECT), tr(STR_DIR_LEFT), tr(STR_RETRY));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

std::string BluetoothSettingsActivity::getSignalStrengthIndicator(const int32_t rssi) const {
  // Convert RSSI to signal bars representation (matching WiFi scanner style)
  if (rssi >= -50) {
    return "||||";  // Excellent
  }
  if (rssi >= -60) {
    return " |||";  // Good
  }
  if (rssi >= -70) {
    return "  ||";  // Fair
  }
  return "   |";  // Very weak
}

void BluetoothSettingsActivity::renderLearnKeys() {
  auto metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Learn Page-Turn Keys");

  const char* stepText = "Press PREVIOUS PAGE button";
  if (learnStep == LearnStep::WAIT_NEXT) {
    stepText = "Press NEXT PAGE button";
  } else if (learnStep == LearnStep::DONE) {
    stepText = "Learning complete";
  }

  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    stepText);

  char line1[64];
  char line2[64];
  snprintf(line1, sizeof(line1), "Prev key: %s", learnedPrevKey ? "captured" : "waiting");
  snprintf(line2, sizeof(line2), "Next key: %s", learnedNextKey ? "captured" : "waiting");

  renderer.drawCenteredText(UI_12_FONT_ID, metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + 32,
                            line1);
  renderer.drawCenteredText(UI_12_FONT_ID, metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + 56,
                            line2);

  if (learnedPrevKey) {
    char line3[48];
    snprintf(line3, sizeof(line3), "Report byte: [%u]", static_cast<unsigned>(learnedReportIndex));
    renderer.drawCenteredText(UI_10_FONT_ID, metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + 80,
                              line3);
  }

  if (!lastError.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight - metrics.buttonHintsHeight - 16, lastError.c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK),
                                            learnStep == LearnStep::DONE ? tr(STR_SELECT) : "",
                                            "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}