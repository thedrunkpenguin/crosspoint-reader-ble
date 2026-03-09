#include "BluetoothSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void BluetoothSettingsActivity::onEnter() {
  Activity::onEnter();
  
  selectedIndex = 0;
  viewMode = ViewMode::MAIN_MENU;
  lastError = "";
  lastScanTime = 0;
  
  // Get BLE manager instance
  try {
    btMgr = &BluetoothHIDManager::getInstance();
    LOG_INF("BT", "BluetoothHIDManager ready");
    
    // Restore Bluetooth persistent state on entry
    if (SETTINGS.bleEnabled && !btMgr->isEnabled()) {
      LOG_INF("BT", "Restoring Bluetooth from settings (enabled)");
      if (btMgr->enable()) {
        lastError = "Bluetooth restored";
        // Resume auto-reconnect if we have a bonded device
        if (strlen(SETTINGS.bleBondedDeviceAddr) > 0) {
          btMgr->autoReconnect(SETTINGS.bleBondedDeviceAddr, SETTINGS.bleBondedDeviceAddrType);
        }
      } else {
        lastError = "Failed to restore BT";
        SETTINGS.bleEnabled = 0;
      }
    } else if (!SETTINGS.bleEnabled && btMgr->isEnabled()) {
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
    } else {
      finish();
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
  } else {
    handleDeviceListInput();
  }
}

void BluetoothSettingsActivity::handleMainMenuInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedIndex = (selectedIndex > 0) ? selectedIndex - 1 : 1;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedIndex = (selectedIndex < 1) ? selectedIndex + 1 : 0;
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
            SETTINGS.bleEnabled = 0;
            SETTINGS.saveToFile();
          } else {
            lastError = "Failed to disable";
          }
        } else {
          LOG_INF("BT", "Enabling Bluetooth...");
          if (btMgr->enable()) {
            lastError = "Bluetooth enabled";
            SETTINGS.bleEnabled = 1;
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
    }
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
        lastError = std::string("Connected to ") + device.name;
        LOG_INF("BT", "Successfully connected to %s", device.name.c_str());
        // Save bonded device info to settings for auto-reconnect after sleep
        strncpy(SETTINGS.bleBondedDeviceAddr, device.address.c_str(), sizeof(SETTINGS.bleBondedDeviceAddr) - 1);
        SETTINGS.bleBondedDeviceAddr[sizeof(SETTINGS.bleBondedDeviceAddr) - 1] = '\0';
        strncpy(SETTINGS.bleBondedDeviceName, device.name.c_str(), sizeof(SETTINGS.bleBondedDeviceName) - 1);
        SETTINGS.bleBondedDeviceName[sizeof(SETTINGS.bleBondedDeviceName) - 1] = '\0';
        SETTINGS.saveToFile();
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
  } else {
    renderDeviceList();
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

  // Use GUI.drawList for consistent formatting with main settings
  const char* items[] = {
    btMgr && btMgr->isEnabled() ? "Disable Bluetooth" : "Enable Bluetooth",
    "Scan for Devices"
  };

  std::vector<std::string> itemLabels;
  for (int i = 0; i < 2; i++) {
    itemLabels.push_back(items[i]);
  }

  GUI.drawList(
      renderer,
      Rect{0, metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.buttonHintsHeight +
                         metrics.verticalSpacing * 2)},
      2, selectedIndex,
      [&itemLabels](int index) { return itemLabels[index]; }, nullptr, nullptr,
      [this](int i) {
        if (!lastError.empty()) {
          return lastError;
        }
        return std::string("");
      },
      true);

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