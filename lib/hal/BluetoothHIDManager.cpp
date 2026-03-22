#include "BluetoothHIDManager.h"
#include <Logging.h>
#include <NimBLEDevice.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <WiFi.h>

// HID Service and characteristic UUIDs
static const char* HID_SERVICE_UUID = "1812";
static const char* HID_REPORT_UUID = "2A4D";
static const char* HID_INFO_UUID = "2A4A";

struct ExtractedHIDKey {
  uint8_t keycode = 0x00;
  uint8_t reportIndex = 0xFF;
};

static ExtractedHIDKey extractGenericPageTurnKeycode(const uint8_t* report, size_t length) {
  ExtractedHIDKey result;

  if (!report || length == 0) {
    return result;
  }

  // First pass: prefer known page-turn keycodes anywhere in short reports.
  const size_t scanLen = length < 8 ? length : 8;
  for (size_t i = 0; i < scanLen; i++) {
    const uint8_t code = report[i];
    if (DeviceProfiles::isCommonPageTurnCode(code)) {
      result.keycode = code;
      result.reportIndex = static_cast<uint8_t>(i);
      return result;
    }
  }

  // Second pass: typical keyboard report key slots (bytes 2..7)
  for (size_t i = 2; i < scanLen; i++) {
    if (report[i] != 0x00) {
      result.keycode = report[i];
      result.reportIndex = static_cast<uint8_t>(i);
      return result;
    }
  }

  // Final fallback for non-keyboard HID layouts: first non-zero byte.
  for (size_t i = 0; i < scanLen; i++) {
    if (report[i] != 0x00) {
      result.keycode = report[i];
      result.reportIndex = static_cast<uint8_t>(i);
      return result;
    }
  }

  return result;
}

// Global static for singleton
static BluetoothHIDManager* g_instance = nullptr;

// Scan callbacks for NimBLE 2.x - keep as static to ensure it stays alive
class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    if (g_instance) {
      // onScanResult expects non-const pointer, need to cast
      g_instance->onScanResult(const_cast<NimBLEAdvertisedDevice*>(advertisedDevice));
    } else {
      LOG_ERR("BT", "onResult called but g_instance is NULL!");
    }
  }
  
  void onScanEnd(const NimBLEScanResults& results, int reason) override {
    (void)results;
    (void)reason;
  }
};

// Static instance to keep callbacks alive during scan
static ScanCallbacks scanCallbacks;

// Client connection callbacks
class ClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pClient) override {
    LOG_INF("BT", "Client connected: %s", pClient->getPeerAddress().toString().c_str());
  }
  
  void onDisconnect(NimBLEClient* pClient, int reason) override {
    LOG_ERR("BT", "Client disconnected: %s (reason: %d)", pClient->getPeerAddress().toString().c_str(), reason);
  }
};

BluetoothHIDManager& BluetoothHIDManager::getInstance() {
  if (!g_instance) {
    g_instance = new BluetoothHIDManager();
    LOG_INF("BT", "BluetoothHIDManager instance created");
  }
  return *g_instance;
}

BluetoothHIDManager::BluetoothHIDManager() {
  LOG_DBG("BT", "BluetoothHIDManager constructor");
}

BluetoothHIDManager::~BluetoothHIDManager() {
  cleanup();
}

void BluetoothHIDManager::cleanup() {
  if (_enabled) {
    disable();
  }
}

bool BluetoothHIDManager::enable() {
  if (_enabled) {
    LOG_DBG("BT", "Already enabled");
    return true;
  }
  
  LOG_INF("BT", "Enabling Bluetooth...");
  
  // CRITICAL: Disable WiFi when enabling Bluetooth
  // ESP32-C3 cannot have both WiFi and BLE enabled simultaneously
  if (WiFi.getMode() != WIFI_OFF) {
    LOG_INF("BT", "Disabling WiFi to enable Bluetooth (mutual exclusion)");
    WiFi.disconnect(true);  // true = turn off WiFi radio
    WiFi.mode(WIFI_OFF);
    delay(100);  // Brief delay to ensure WiFi is fully powered down
  }
  
  try {
    // Initialize NimBLE stack
    NimBLEDevice::init("CrossPoint");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // +9dBm
    NimBLEDevice::setSecurityAuth(true, false, true);
    
    _enabled = true;
    lastError = "";
    
    LOG_INF("BT", "Bluetooth enabled successfully");
    loadState();
    return true;
  } catch (const std::exception& e) {
    LOG_ERR("BT", "Failed to enable Bluetooth: %s", e.what());
    lastError = std::string("Init failed: ") + e.what();
    _enabled = false;
    return false;
  } catch (...) {
    LOG_ERR("BT", "Failed to enable Bluetooth: unknown error");
    lastError = "Init failed: unknown error";
    _enabled = false;
    return false;
  }
}

bool BluetoothHIDManager::disable() {
  if (!_enabled) {
    LOG_DBG("BT", "Already disabled");
    return true;
  }
  
  LOG_INF("BT", "Disabling Bluetooth...");
  
  if (_scanning) {
    stopScan();
  }
  
  // Disconnect all devices
  while (!_connectedDevices.empty()) {
    disconnectFromDevice(_connectedDevices[0].address);
  }
  
  // Deinitialize NimBLE stack
  NimBLEDevice::deinit(false);
  
  _enabled = false;
  lastError = "";
  
  LOG_INF("BT", "Bluetooth disabled");
  return true;
}

void BluetoothHIDManager::startScan(uint32_t durationMs) {
  if (!_enabled || _scanning) {
    LOG_DBG("BT", "Cannot scan: enabled=%d scanning=%d", _enabled, _scanning);
    return;
  }
  
  LOG_INF("BT", "Starting BLE scan for %lu ms", durationMs);
  _scanning = true;
  _discoveredDevices.clear();
  
  try {
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (!pScan) {
      LOG_ERR("BT", "Failed to get scan object");
      _scanning = false;
      return;
    }
    
    // Use static callbacks object to ensure it stays alive
    pScan->setScanCallbacks(&scanCallbacks, false);
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
    
    // In NimBLE 2.x, duration=0 means scan continuously until stop() is called
    // Parameter 1: 0 = continuous scan
    // Parameter 2: isContinue (false = clear old results)
    bool started = pScan->start(0, false);
    
    if (!started) {
      LOG_ERR("BT", "Failed to start scan!");
      _scanning = false;
      return;
    }
    
    // Wait for the specified duration
    delay(durationMs);
    
    // Stop the scan
    pScan->stop();
    
    _scanning = false;
    LOG_INF("BT", "Scan complete, found %d devices", _discoveredDevices.size());
  } catch (const std::exception& e) {
    LOG_ERR("BT", "Scan failed: %s", e.what());
    _scanning = false;
    lastError = std::string("Scan failed: ") + e.what();
  }
}

void BluetoothHIDManager::stopScan() {
  if (!_scanning) return;
  
  LOG_INF("BT", "Stopping scan");
  
  try {
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (pScan) {
      pScan->stop();
    }
  } catch (...) {
    LOG_ERR("BT", "Error stopping scan");
  }
  
  _scanning = false;
}

void BluetoothHIDManager::onScanResult(NimBLEAdvertisedDevice* advertisedDevice) {
  if (!advertisedDevice) return;
  
  std::string address = advertisedDevice->getAddress().toString();
  std::string name = advertisedDevice->getName();
  int rssi = advertisedDevice->getRSSI();
  
  // Check if device advertises HID service
  bool isHID = advertisedDevice->isAdvertisingService(NimBLEUUID(HID_SERVICE_UUID));
  
  // Check if we already have this device
  for (auto& dev : _discoveredDevices) {
    if (dev.address == address) {
      dev.rssi = rssi; // Update RSSI
      if (isHID) dev.isHID = true;
      return;
    }
  }
  
  // Add new device
  BluetoothDevice device;
  device.address = address;
  device.name = name.empty() ? "Unknown" : name;
  device.rssi = rssi;
  device.isHID = isHID;
  
  _discoveredDevices.push_back(device);

  LOG_DBG("BT", "Found device: %s (%s) RSSI:%d HID:%d", 
          device.name.c_str(), device.address.c_str(), rssi, isHID);
}

bool BluetoothHIDManager::connectToDevice(const std::string& address) {
  if (!_enabled) {
    LOG_ERR("BT", "Cannot connect: Bluetooth not enabled");
    lastError = "Bluetooth not enabled";
    return false;
  }
  
  // Check if already connected
  if (isConnected(address)) {
    LOG_INF("BT", "Already connected to %s", address.c_str());
    return true;
  }
  
  LOG_INF("BT", "Connecting to device %s", address.c_str());
  
  try {
    NimBLEAddress bleAddress(address, BLE_ADDR_PUBLIC);

    // Reuse existing disconnected client objects to avoid NimBLE deleteClient() on this target.
    NimBLEClient* pClient = NimBLEDevice::getClientByPeerAddress(bleAddress);
    const bool hadExistingClient = (pClient != nullptr);
    if (!pClient) {
      pClient = NimBLEDevice::getDisconnectedClient();
      if (pClient) {
        pClient->setPeerAddress(bleAddress);
      }
    }
    if (!pClient) {
      pClient = NimBLEDevice::createClient(bleAddress);
    }

    if (!pClient) {
      lastError = "Failed to create BLE client";
      LOG_ERR("BT", "Failed to create BLE client");
      return false;
    }

    // Keep client lifetime under manager control so disconnect callbacks do not free it in NimBLE context.
    pClient->setSelfDelete(false, false);

    if (!pClient->isConnected()) {
      pClient->deleteServices();
    }
    
    // Set connection callbacks
    static ClientCallbacks clientCallbacks;
    pClient->setClientCallbacks(&clientCallbacks);
    
    // Connect to device
    if (!pClient->connect(bleAddress)) {
      if (hadExistingClient) {
        LOG_INF("BT", "Reconnect with existing client failed for %s, retrying with fresh client", address.c_str());
        NimBLEClient* freshClient = NimBLEDevice::createClient(bleAddress);
        if (freshClient) {
          pClient = freshClient;
          pClient->setSelfDelete(false, false);
          pClient->setClientCallbacks(&clientCallbacks);
        }
      }

      if (!pClient->connect(bleAddress)) {
        lastError = "Connection failed";
        LOG_ERR("BT", "Failed to connect to %s", address.c_str());
        return false;
      }
    }
    
    // Get HID service
    NimBLERemoteService* pService = pClient->getService(HID_SERVICE_UUID);
    if (!pService) {
      lastError = "HID service not found";
      LOG_ERR("BT", "Device %s doesn't have HID service", address.c_str());
      pClient->disconnect();
      return false;
    }
    
    LOG_INF("BT", "Found HID service, enumerating report characteristics...");
    
    // BLE HID has multiple report characteristics (input, output, feature)
    // We need to find one that supports NOTIFY or INDICATE (input report)
    // In NimBLE 2.x, getCharacteristics() returns std::vector<NimBLERemoteCharacteristic*>
    auto pCharacteristics = pService->getCharacteristics(true);
    NimBLERemoteCharacteristic* pReportChar = nullptr;
    
    int reportCount = 0;
    std::vector<NimBLERemoteCharacteristic*> reportChars;
    
    for (auto it = pCharacteristics.begin(); it != pCharacteristics.end(); ++it) {
      auto* pChar = *it;
      LOG_DBG("BT", "Characteristic UUID: %s, canRead:%d canWrite:%d canNotify:%d canIndicate:%d",
              pChar->getUUID().toString().c_str(),
              pChar->canRead(), pChar->canWrite(), pChar->canNotify(), pChar->canIndicate());
      
      if (pChar->getUUID().equals(NimBLEUUID(HID_REPORT_UUID))) {
        reportCount++;
        
        // Check if this report supports notify or indicate (input report)
        if (pChar->canNotify() || pChar->canIndicate()) {
          reportChars.push_back(pChar);
          LOG_INF("BT", "Added Report char #%d for subscription", reportCount);
        }
      }
    }
    
    if (reportChars.empty()) {
      lastError = "No input report characteristic found";
      LOG_ERR("BT", "No Report characteristic with notify/indicate found");
      pClient->disconnect();
      return false;
    }
    
    // Subscribe to ALL Report characteristics with notify capability
    LOG_INF("BT", "Subscribing to %d Report characteristics...", reportChars.size());
    size_t successfulSubscriptions = 0;
    
    for (size_t i = 0; i < reportChars.size(); i++) {
      auto* pChar = reportChars[i];
      
      // Subscribe with callback
      bool subResult = pChar->subscribe(true, onHIDNotify);
      LOG_INF("BT", "Report char #%d subscribe result: %d", i + 1, subResult);
      if (subResult) {
        successfulSubscriptions++;
      }
      
      if (!subResult) {
        LOG_INF("BT", "Failed to subscribe to Report char #%d (continuing)", i + 1);
      }
    }

    if (successfulSubscriptions == 0) {
      lastError = "Failed to subscribe to input reports";
      LOG_ERR("BT", "No HID report subscriptions succeeded for %s", address.c_str());
      pClient->disconnect();
      return false;
    }
    
    LOG_INF("BT", "Subscribed to %u/%u HID Report characteristics",
            static_cast<unsigned>(successfulSubscriptions), static_cast<unsigned>(reportChars.size()));
    
    // Save connection with activity timestamp
    ConnectedDevice connDev;
    connDev.address = address;
    connDev.client = pClient;
    connDev.reportChars = reportChars;
    connDev.connectedTime = millis();
    connDev.subscribed = true;
    connDev.lastActivityTime = millis();  // Initialize activity timer
    connDev.wasConnected = true;  // Mark for auto-reconnect if disconnected
    
    // Detect device profile
    // First, try to find the device in scan results to get its name
    bool foundInScan = false;
    for (const auto& dev : _discoveredDevices) {
      if (dev.address == address) {
        connDev.name = dev.name;
        foundInScan = true;
        LOG_INF("BT", "Device found in scan results: %s (%s)", dev.name.c_str(), address.c_str());
        break;
      }
    }
    
    if (!foundInScan) {
      LOG_INF("BT", "Device not in scan results (may be previously paired): %s", address.c_str());
    }
    
    // Profile matching priority:
    //  1. MAC-prefix exact match  (hardware ID, precise – always wins)
    //  2. User-learned custom profile (explicitly taught by the user)
    //     → only if the name-matched known profile is NOT marked strictProfile
    //  3. Fuzzy name-pattern match  (last resort – can produce false positives)
    connDev.profile = DeviceProfiles::findDeviceProfile(address.c_str(), nullptr);

    if (!connDev.profile) {
      // Check if a name-matched profile exists and whether it is strict.
      const DeviceProfiles::DeviceProfile* nameMatch =
          DeviceProfiles::findDeviceProfile(nullptr, connDev.name.c_str());
      const bool nameMatchIsStrict = nameMatch && nameMatch->strictProfile;

      // Prefer the user's learned mapping over a non-strict name-based guess.
      const auto* customProfile = DeviceProfiles::getCustomProfile();
      if (customProfile && !nameMatchIsStrict) {
        connDev.profile = customProfile;
        LOG_INF("BT", "Using learned custom profile (overrides non-strict name match): up=0x%02X dn=0x%02X",
                customProfile->pageUpCode, customProfile->pageDownCode);
      } else if (nameMatch) {
        connDev.profile = nameMatch;
        if (nameMatchIsStrict) {
          LOG_INF("BT", "Using strict name-matched profile '%s' (custom profile bypassed)",
                  nameMatch->name);
        } else {
          LOG_INF("BT", "Using name-matched profile '%s' (no custom profile set)", nameMatch->name);
        }
      }
    }
    
    if (connDev.profile) {
      LOG_INF("BT", "✓ Using device profile: %s (byte[%d] for keycode)", 
              connDev.profile->name, connDev.profile->reportByteIndex);
    } else {
      LOG_INF("BT", "No known profile matched for %s, will auto-detect from HID codes", address.c_str());
    }
    
    auto existing = std::find_if(_connectedDevices.begin(), _connectedDevices.end(),
                                 [&address](const ConnectedDevice& dev) { return dev.address == address; });
    if (existing != _connectedDevices.end()) {
      *existing = connDev;
    } else {
      _connectedDevices.push_back(connDev);
    }
    
    LOG_INF("BT", "Successfully connected to %s", address.c_str());
    lastError = "Connected";
    return true;
    
  } catch (const std::exception& e) {
    lastError = std::string("Connection error: ") + e.what();
    LOG_ERR("BT", "%s", lastError.c_str());
    return false;
  } catch (...) {
    lastError = "Unknown connection error";
    LOG_ERR("BT", "%s", lastError.c_str());
    return false;
  }
}

bool BluetoothHIDManager::disconnectFromDevice(const std::string& address) {
  LOG_INF("BT", "Disconnecting from device %s", address.c_str());
  
  auto it = std::find_if(_connectedDevices.begin(), _connectedDevices.end(),
    [&address](const ConnectedDevice& dev) { return dev.address == address; });
  
  if (it != _connectedDevices.end()) {
    if (_buttonInjector && it->activeInjectedButton != 0xFF) {
      _buttonInjector(it->activeInjectedButton, false);
    }
    NimBLEClient* client = it->client;

    // Ensure normal CPU speed during BLE termination to avoid WDT in low-power mode.
    if (client && client->isConnected()) {
      try {
        HalPowerManager::Lock lock;
        client->disconnect();
      } catch (const std::exception& e) {
        LOG_ERR("BT", "Error during disconnect: %s", e.what());
      } catch (...) {
        LOG_ERR("BT", "Unknown error during disconnect");
      }
    }

    // Remove from our list
    _connectedDevices.erase(it);
    LOG_INF("BT", "Disconnected from %s", address.c_str());
    return true;
  }
  
  LOG_INF("BT", "Device %s not in connected list", address.c_str());
  return false;
}

bool BluetoothHIDManager::isConnected(const std::string& address) const {
  return std::find_if(_connectedDevices.begin(), _connectedDevices.end(), [&address](const ConnectedDevice& dev) {
           return dev.address == address && dev.client && dev.client->isConnected();
         }) != _connectedDevices.end();
}

std::vector<std::string> BluetoothHIDManager::getConnectedDevices() const {
  std::vector<std::string> addresses;
  for (const auto& dev : _connectedDevices) {
    if (dev.client && dev.client->isConnected()) {
      addresses.push_back(dev.address);
    }
  }
  return addresses;
}

ConnectedDevice* BluetoothHIDManager::findConnectedDevice(const std::string& address) {
  auto it = std::find_if(_connectedDevices.begin(), _connectedDevices.end(),
    [&address](const ConnectedDevice& dev) { return dev.address == address; });
  
  if (it != _connectedDevices.end()) {
    return &(*it);
  }
  return nullptr;
}

void BluetoothHIDManager::processInputEvents() {
  // Input events are processed via notifications callback
  // This method is kept for potential polling-based implementations
}

void BluetoothHIDManager::setInputCallback(std::function<void(uint16_t)> callback) {
  _inputCallback = callback;
  LOG_DBG("BT", "Input callback registered");
}

void BluetoothHIDManager::setLearnInputCallback(std::function<void(uint8_t, uint8_t)> callback) {
  _learnInputCallback = callback;
  LOG_DBG("BT", "Learn input callback registered");
}

void BluetoothHIDManager::setButtonInjector(std::function<void(uint8_t, bool)> injector) {
  _buttonInjector = injector;
  LOG_DBG("BT", "Button injector registered");
}

void BluetoothHIDManager::setBondedDevice(const std::string& address, const std::string& name) {
  _bondedDeviceAddress = address;
  _bondedDeviceName = name;
  LOG_INF("BT", "Bonded device set: %s (%s)", _bondedDeviceAddress.c_str(), _bondedDeviceName.c_str());
}

bool BluetoothHIDManager::hasRecentActivity() const {
  // Check if any connected device has had activity in the last 4 minutes
  // This prevents power sleep while using BLE controller
  unsigned long now = millis();
  for (const auto& device : _connectedDevices) {
    if (device.lastActivityTime > 0) {
      unsigned long timeSinceActivity = now - device.lastActivityTime;
      if (timeSinceActivity < 240000) {  // 4 minute (240 second) threshold to keep BLE alive
        return true;
      }
    }
  }
  return false;
}

// Static callback for HID notifications
void BluetoothHIDManager::onHIDNotify(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  if (!g_instance || !pData || length == 0) return;
  
  // Get the device address and find the connected device
  ConnectedDevice* device = nullptr;
  if (pChar && pChar->getRemoteService()) {
    auto client = pChar->getRemoteService()->getClient();
    if (client) {
      std::string deviceAddr = client->getPeerAddress().toString();
      device = g_instance->findConnectedDevice(deviceAddr);
    }
  }
  
  if (!device) return;
  
  // Update activity timestamp to keep connection alive
  device->lastActivityTime = millis();

  auto releaseInjectedButton = [&]() {
    if (g_instance->_buttonInjector && device->activeInjectedButton != 0xFF) {
      g_instance->_buttonInjector(device->activeInjectedButton, false);
    }
    device->activeInjectedButton = 0xFF;
  };
  
  // Extract keycode based on device profile or auto-detect
  uint8_t keycode = 0xFF;
  uint8_t keycodeIndex = 0xFF;
  bool isPressed = false;
  bool isGameBrickProfile = false;
  
  if (length < 1) {
    LOG_DBG("BT", "HID report empty, ignoring");
    return;
  }
  
  // Determine keycode source and press state based on device profile
  if (device->profile) {
    // Use device profile's byte index for keycode
    if (length >= device->profile->reportByteIndex + 1) {
      keycode = pData[device->profile->reportByteIndex];
      keycodeIndex = device->profile->reportByteIndex;
    }

    // For custom/learned profiles: if the fixed-index byte is not one of the learned
    // keycodes, scan the entire report.  This handles remotes where the prev/next buttons
    // send their keycodes at different byte positions, or where they arrive on separate
    // HID report characteristics with their own frame layouts.
    const bool isCustomProfile = (strcmp(device->profile->name, "Custom BLE Remote") == 0);
    if (isCustomProfile &&
        keycode != device->profile->pageUpCode &&
        keycode != device->profile->pageDownCode) {
      for (size_t bi = 0; bi < length && bi < 8; bi++) {
        const uint8_t b = pData[bi];
        if (b == device->profile->pageUpCode || b == device->profile->pageDownCode) {
          keycode = b;
          keycodeIndex = static_cast<uint8_t>(bi);
          LOG_DBG("BT", "Custom profile: found learned code 0x%02X at byte[%u] (vs fixed idx %u)",
                  keycode, static_cast<unsigned>(bi),
                  static_cast<unsigned>(device->profile->reportByteIndex));
          break;
        }
      }
    }

    // For Game Brick: press state from byte[0] bit 0
    // For standard HID keyboards: press state from keycode (non-zero = pressed)
    if (strcmp(device->profile->name, "IINE Game Brick") == 0) {
      isGameBrickProfile = true;
      // Game Brick: accept only stable digital-button report family (0x1x).
      // Ignore noisy transitional frames (commonly 0x2x/0x3x) that can trigger false presses.
      const bool stableButtonReport = (pData[0] & 0xF0) == 0x10;
      if (!stableButtonReport) {
        LOG_DBG("BT", "Game Brick: ignoring transitional report byte[0]=0x%02X, keycode=0x%02X", pData[0], keycode);
        // Keep the previous button state intact while skipping transitional frames.
        // Resetting state here can create a duplicate "new press" on the next stable
        // frame, which shows up as a double page-turn.
        return;
      }

      // Game Brick: byte[0] LSB indicates press state
      isPressed = (pData[0] & 0x01) != 0;

      // Prevent initial stale pressed frame right after subscribe from triggering navigation.
      // Only allow presses after at least one clean release frame has been seen.
      if (!device->hasSeenRelease) {
        if (!isPressed) {
          device->hasSeenRelease = true;
        } else {
          LOG_DBG("BT", "Game Brick: ignoring startup pressed frame keycode=0x%02X", keycode);
          device->lastButtonState = true;
          device->lastHIDKeycode = keycode;
          return;
        }
      }

      LOG_DBG("BT", "Game Brick: byte[0]=0x%02X, keycode=0x%02X, pressed=%d", pData[0], keycode, isPressed);
    } else {
      // Standard HID keyboards/custom profiles: keycode non-zero = pressed.
      // Normalise 0xFF (= "nothing found in report") to 0x00 so that short
      // release frames (e.g. 1-byte consumer control [0x00]) are treated as
      // a key-release rather than a phantom press.
      if (keycode == 0xFF) {
        keycode = 0x00;
      }
      isPressed = (keycode != 0x00);
      LOG_DBG("BT", "Device %s: keycode=0x%02X, pressed=%d", device->profile->name, keycode, isPressed);
    }
  } else {
    // Auto-detect mode: support a wider range of generic HID remotes.
    const ExtractedHIDKey extracted = extractGenericPageTurnKeycode(pData, length);
    keycode = extracted.keycode;
    keycodeIndex = extracted.reportIndex;

    // Keep existing GameBrick bit0 press-state behavior when applicable.
    if (length >= 5 && (keycode == 0x07 || keycode == 0x09)) {
      isPressed = ((pData[0] & 0x01) != 0) || (keycode != 0x00);
      LOG_DBG("BT", "Auto-detect (GameBrick-like): keycode=0x%02X, pressed=%d", keycode, isPressed);
    } else {
      isPressed = (keycode != 0x00);
      LOG_DBG("BT", "Auto-detect (generic HID): keycode=0x%02X, pressed=%d", keycode, isPressed);
    }
  }

  // Update release state for startup noise gate
  // When we see the first release (isPressed = false), we enable button injection
  if (!isPressed && !device->hasSeenRelease) {
    device->hasSeenRelease = true;
  }
  
  // Ignore if no valid keycode detected
  if (keycode == 0x00 || keycode == 0xFF) {
    releaseInjectedButton();
    // Track state for transition detection
    device->lastButtonState = isPressed;
    device->lastHIDKeycode = keycode;
    return;
  }
  
  // CRITICAL GATE: Don't inject any buttons until we've seen the first release
  // This prevents startup transient noise from being interpreted as button presses
  if (!device->hasSeenRelease) {
    releaseInjectedButton();
    device->lastButtonState = isPressed;
    device->lastHIDKeycode = keycode;
    return;
  }

  // Detect button PRESS transition.
  // For most remotes, key changes while held are treated as a new press event.
  // For Game Brick, ignore key-change retriggers while held to avoid duplicate events.
  const bool isNewPressEvent =
      isPressed && (!device->lastButtonState || (!isGameBrickProfile && keycode != device->lastHIDKeycode));
  if (isNewPressEvent) {
    LOG_INF("BT", ">>> BUTTON PRESSED: keycode=0x%02X <<<", keycode);

    if (g_instance->_learnInputCallback && keycode != 0x00 && keycode != 0xFF && keycodeIndex != 0xFF) {
      g_instance->_learnInputCallback(keycode, keycodeIndex);
    }
    
    // Also call original callback if set
    if (g_instance->_inputCallback) {
      g_instance->_inputCallback(keycode);
    }
  }

  const uint8_t mappedButton = isPressed ? g_instance->mapKeycodeToButton(keycode, device->profile) : 0xFF;

  if (!isPressed || mappedButton == 0xFF) {
    releaseInjectedButton();
  } else {
    if (device->activeInjectedButton != 0xFF && device->activeInjectedButton != mappedButton) {
      releaseInjectedButton();
    }

    if (g_instance->_buttonInjector && device->activeInjectedButton == 0xFF) {
      if (isGameBrickProfile && device->lastInjectedKeycode == keycode &&
          (millis() - device->lastInjectionTime) < 180) {
        LOG_DBG("BT", "Game Brick: debouncing duplicate key 0x%02X (%lu ms)", keycode,
                millis() - device->lastInjectionTime);
      } else {
      String buttonName = (mappedButton == HalGPIO::BTN_DOWN) ? "PageForward" : "PageBack";
      LOG_INF("BT", "Mapped key 0x%02X -> %s", keycode, buttonName.c_str());
      g_instance->_buttonInjector(mappedButton, true);
      device->activeInjectedButton = mappedButton;
      device->lastInjectionTime = millis();
      device->lastInjectedKeycode = keycode;
      }
    }
  }
  
  // Track the button state and keycode for next time
  device->lastButtonState = isPressed;
  device->lastHIDKeycode = keycode;
}

uint16_t BluetoothHIDManager::parseHIDReport(uint8_t* data, size_t length) {
  if (length < 3) {
    LOG_ERR("BT", "Invalid HID report length: %d", length);
    return 0;
  }
  
  uint8_t modifier = data[0];
  uint8_t keycode = data[2]; // First key in the report
  
  // If no key pressed (all zeros), return 0
  if (keycode == 0 && modifier == 0) {
    return 0;
  }
  
  // Log non-empty reports
  LOG_INF("BT", "HID Report: mod=0x%02X key=0x%02X", modifier, keycode);
  
  // Combine modifier and keycode (modifier in upper byte, keycode in lower)
  uint16_t combined = (static_cast<uint16_t>(modifier) << 8) | keycode;
  
  return combined;
}

// Map HID keycodes to navigator buttons based on device profile
// Only maps keycodes that match the current device's profile to prevent
// unwanted D-pad or other button inputs from triggering page turns
uint8_t BluetoothHIDManager::mapKeycodeToButton(uint8_t keycode, const DeviceProfiles::DeviceProfile* profile) {
  // Log keycode for debugging
  if (keycode != 0x00) {
    LOG_DBG("BT", "mapKeycodeToButton() called with keycode: 0x%02X", keycode);
  }
  
  // If we have a device profile, ONLY map keycodes specific to that profile
  if (profile) {
    if (keycode == profile->pageUpCode) {
      LOG_INF("BT", "Matched profile pageUpCode 0x%02X (%s) -> PageBack", keycode, profile->name);
      return HalGPIO::BTN_UP;
    } else if (keycode == profile->pageDownCode) {
      LOG_INF("BT", "Matched profile pageDownCode 0x%02X (%s) -> PageForward", keycode, profile->name);
      return HalGPIO::BTN_DOWN;
    }

    // The known profile didn't recognise this keycode. For non-strict (standard layout)
    // profiles, also consult the user-learned custom mapping as a fallback. This covers
    // the common case where a device partially matches a known profile (e.g. its back
    // button matches MINI_KEYBOARD but its forward button uses a different code).
    const bool isStrict = profile->strictProfile;
    if (!isStrict) {
      if (const auto* learned = DeviceProfiles::getCustomProfile()) {
        if (keycode == learned->pageUpCode) {
          LOG_INF("BT", "Custom-fallback: 0x%02X -> PageBack (profile=%s)", keycode, profile->name);
          return HalGPIO::BTN_UP;
        }
        if (keycode == learned->pageDownCode) {
          LOG_INF("BT", "Custom-fallback: 0x%02X -> PageForward (profile=%s)", keycode, profile->name);
          return HalGPIO::BTN_DOWN;
        }
      }
    }

    // Not matched by profile or fallback - ignore
    LOG_DBG("BT", "Keycode 0x%02X not in profile %s (expecting 0x%02X/0x%02X), ignoring",
            keycode, profile->name, profile->pageUpCode, profile->pageDownCode);
    return 0xFF;
  }

  // Learned mappings are only used for unknown devices.
  if (const auto* customProfile = DeviceProfiles::getCustomProfile()) {
    if (keycode == customProfile->pageUpCode) {
      LOG_INF("BT", "Mapped learned key 0x%02X -> PageBack", keycode);
      return HalGPIO::BTN_UP;
    }
    if (keycode == customProfile->pageDownCode) {
      LOG_INF("BT", "Mapped learned key 0x%02X -> PageForward", keycode);
      return HalGPIO::BTN_DOWN;
    }
  }

  // No profile match - use broad common-key mapping for generic remotes/keyboards.
  bool pageForward = false;
  if (DeviceProfiles::mapCommonCodeToDirection(keycode, pageForward)) {
    if (pageForward) {
      LOG_INF("BT", "Mapped generic key 0x%02X -> PageForward", keycode);
      return HalGPIO::BTN_DOWN;
    }
    LOG_INF("BT", "Mapped generic key 0x%02X -> PageBack", keycode);
    return HalGPIO::BTN_UP;
  }

  if (keycode == 0x00) {
    return 0xFF;
  }

  // Unknown key for generic device; ignore safely.
  LOG_DBG("BT", "Unmapped keycode: 0x%02X (no profile)", keycode);
  return 0xFF;
}

void BluetoothHIDManager::updateActivity() {
  // Check inactivity timeouts every 10 seconds
  unsigned long now = millis();
  if (now - lastMaintenanceCheck < 10000) {
    return;
  }
  lastMaintenanceCheck = now;

  // Check for one inactive connection and disconnect it in-place.
  std::string inactiveAddress;
  unsigned long inactiveTimeMs = 0;
  for (const auto& device : _connectedDevices) {
    if (device.lastActivityTime == 0) {
      continue;
    }

    unsigned long inactiveTime = now - device.lastActivityTime;
    if (inactiveTime > INACTIVITY_TIMEOUT_MS) {
      inactiveAddress = device.address;
      inactiveTimeMs = inactiveTime;
      break;
    }
  }

  if (!inactiveAddress.empty()) {
    LOG_INF("BT", "Device %s inactive for %lu ms, disconnecting", inactiveAddress.c_str(), inactiveTimeMs);
    disconnectFromDevice(inactiveAddress);
  }
}

void BluetoothHIDManager::checkAutoReconnect(bool userInputDetected) {
  if (!_enabled) {
    return;
  }

  static unsigned long lastReconnectCheck = 0;
  static unsigned long lastReconnectAttempt = 0;
  unsigned long now = millis();
  
  // Only check every 5 seconds to avoid hammering
  if (now - lastReconnectCheck < 5000) {
    return;
  }
  lastReconnectCheck = now;

  // Remove stale disconnected clients from active list.
  for (auto it = _connectedDevices.begin(); it != _connectedDevices.end();) {
    if (!it->client || !it->client->isConnected()) {
      if (_buttonInjector && it->activeInjectedButton != 0xFF) {
        _buttonInjector(it->activeInjectedButton, false);
      }
      LOG_DBG("BT", "Pruning stale disconnected client entry: %s client=%p", it->address.c_str(), it->client);
      it = _connectedDevices.erase(it);
    } else {
      ++it;
    }
  }

  // Already connected.
  if (!_connectedDevices.empty()) {
    LOG_DBG("BT", "AutoReconnect skipped: already connected");
    return;
  }

  // Reconnect is user-driven while reading: require a local button event.
  if (!userInputDetected) {
    LOG_DBG("BT", "AutoReconnect skipped: no local user input");
    return;
  }

  // Avoid reconnect storms.
  if (now - lastReconnectAttempt < 2000) {
    LOG_DBG("BT", "AutoReconnect skipped: cooldown active (%lu ms)", now - lastReconnectAttempt);
    return;
  }
  lastReconnectAttempt = now;

  if (_bondedDeviceAddress.empty()) {
    LOG_DBG("BT", "AutoReconnect skipped: no bonded device configured");
    return;
  }

  LOG_INF("BT", "Button activity detected while disconnected, reconnecting to bonded device %s",
          _bondedDeviceAddress.c_str());

  if (connectToDevice(_bondedDeviceAddress)) {
    LOG_INF("BT", "Reconnected to bonded device %s", _bondedDeviceAddress.c_str());
  } else {
    LOG_ERR("BT", "Reconnect to bonded device %s failed: %s", _bondedDeviceAddress.c_str(), lastError.c_str());
  }
}

void BluetoothHIDManager::saveState() {
  LOG_DBG("BT", "Saving state (stub)");
  // Stub: would save paired devices to file
}

void BluetoothHIDManager::loadState() {
  LOG_DBG("BT", "Loading state (stub)");
  // Stub: would load paired devices from file
}

