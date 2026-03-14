#include "BluetoothHIDManager.h"
#include <Logging.h>
#include <NimBLEDevice.h>
#include <HalGPIO.h>
#include <WiFi.h>

// HID Service and characteristic UUIDs
static const char* HID_SERVICE_UUID = "1812";
static const char* HID_REPORT_UUID = "2A4D";
static const char* HID_INFO_UUID = "2A4A";

// Global static for singleton
static BluetoothHIDManager* g_instance = nullptr;

// Scan callbacks for NimBLE 2.x - keep as static to ensure it stays alive
class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    LOG_DBG("BT", "onResult callback triggered!");
    if (g_instance) {
      // onScanResult expects non-const pointer, need to cast
      g_instance->onScanResult(const_cast<NimBLEAdvertisedDevice*>(advertisedDevice));
    } else {
      LOG_ERR("BT", "onResult called but g_instance is NULL!");
    }
  }
  
  void onScanEnd(const NimBLEScanResults& results, int reason) override {
    LOG_INF("BT", "onScanEnd callback: %d devices, reason: %d", results.getCount(), reason);
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
    // Could trigger auto-reconnect here
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
  NimBLEDevice::deinit(true);
  
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
    
    LOG_DBG("BT", "Setting up scan callbacks...");
    // Use static callbacks object to ensure it stays alive
    pScan->setScanCallbacks(&scanCallbacks, false);
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
    
    LOG_DBG("BT", "Starting continuous scan (duration: 0 = continuous)...");
    // In NimBLE 2.x, duration=0 means scan continuously until stop() is called
    // Parameter 1: 0 = continuous scan
    // Parameter 2: isContinue (false = clear old results)
    bool started = pScan->start(0, false);
    
    if (!started) {
      LOG_ERR("BT", "Failed to start scan!");
      _scanning = false;
      return;
    }
    
    LOG_DBG("BT", "Scan started, waiting %lu ms...", durationMs);
    // Wait for the specified duration
    delay(durationMs);
    
    LOG_DBG("BT", "Stopping scan after %lu ms...", durationMs);
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

    const std::string prefix = (address.size() >= 8) ? address.substr(0, 8) : address;
    LOG_INF("BT", "Scan device: %s (%s) prefix=%s RSSI:%d HID:%d",
      device.name.c_str(), device.address.c_str(), prefix.c_str(), rssi, isHID);
  
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
    // Create client
    NimBLEClient* pClient = NimBLEDevice::createClient();
    if (!pClient) {
      lastError = "Failed to create client";
      LOG_ERR("BT", "Failed to create BLE client");
      return false;
    }
    
    // Set connection callbacks
    static ClientCallbacks clientCallbacks;
    pClient->setClientCallbacks(&clientCallbacks);
    
    // Connect to device
    // In NimBLE 2.x, NimBLEAddress needs a type parameter (default is PUBLIC)
    NimBLEAddress bleAddress(address, BLE_ADDR_PUBLIC);
    if (!pClient->connect(bleAddress)) {
      lastError = "Connection failed";
      LOG_ERR("BT", "Failed to connect to %s", address.c_str());
      NimBLEDevice::deleteClient(pClient);
      return false;
    }
    
    LOG_INF("BT", "Connected, discovering services...");
    
    // Get HID service
    NimBLERemoteService* pService = pClient->getService(HID_SERVICE_UUID);
    if (!pService) {
      lastError = "HID service not found";
      LOG_ERR("BT", "Device %s doesn't have HID service", address.c_str());
      pClient->disconnect();
      NimBLEDevice::deleteClient(pClient);
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
        LOG_INF("BT", "Found Report char #%d, notify:%d indicate:%d UUID:%s", 
                reportCount, pChar->canNotify(), pChar->canIndicate(),
                pChar->getUUID().toString().c_str());
        
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
    
    for (size_t i = 0; i < reportChars.size(); i++) {
      auto* pChar = reportChars[i];
      LOG_INF("BT", "Subscribing to Report char #%d...", i + 1);
      
      // Subscribe with callback
      bool subResult = pChar->subscribe(true, onHIDNotify);
      LOG_INF("BT", "Report char #%d subscribe result: %d", i + 1, subResult);
      
      if (!subResult) {
        LOG_INF("BT", "Failed to subscribe to Report char #%d (continuing)", i + 1);
      }
    }
    
    LOG_INF("BT", "Subscribed to %d HID Report characteristics", reportChars.size());
    
    // Save connection with activity timestamp
    ConnectedDevice connDev;
    connDev.address = address;
    connDev.client = pClient;
    connDev.reportChars = reportChars;
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
    
    // Always attempt profile matching by MAC address (and name if available)
    connDev.profile = DeviceProfiles::findDeviceProfile(address.c_str(), connDev.name.c_str());
    
    if (connDev.profile) {
      LOG_INF("BT", "✓ Using device profile: %s (byte[%d] for keycode)", 
              connDev.profile->name, connDev.profile->reportByteIndex);
    } else {
      LOG_INF("BT", "No known profile matched for %s, will auto-detect from HID codes", address.c_str());
    }
    
    _connectedDevices.push_back(connDev);
    
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
    // Just disconnect - don't delete the client as NimBLE manages it
    // and the disconnect callback will be called
    if (it->client && it->client->isConnected()) {
      try {
        LOG_DBG("BT", "Calling disconnect on client...");
        it->client->disconnect();
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
  return std::find_if(_connectedDevices.begin(), _connectedDevices.end(),
    [&address](const ConnectedDevice& dev) { return dev.address == address; }) != _connectedDevices.end();
}

std::vector<std::string> BluetoothHIDManager::getConnectedDevices() const {
  std::vector<std::string> addresses;
  for (const auto& dev : _connectedDevices) {
    addresses.push_back(dev.address);
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

void BluetoothHIDManager::setButtonInjector(std::function<void(uint8_t)> injector) {
  _buttonInjector = injector;
  LOG_DBG("BT", "Button injector registered");
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
  
  // Log raw data for debugging
  char hexStr[128] = {0};
  int offset = 0;
  for (size_t i = 0; i < length && i < 16; i++) {
    offset += snprintf(hexStr + offset, sizeof(hexStr) - offset, "%02X ", pData[i]);
  }
  LOG_DBG("BT", "HID Report (%d bytes): %s", length, hexStr);
  
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
  
  // Extract keycode based on device profile or auto-detect
  uint8_t keycode = (length > profile.reportByteIndex) ? pData[profile.reportByteIndex] : pData[0];
  bool isPressed = (keycode != 0) || (pData[0] != 0);
  
  if (length < 2) {
    LOG_DBG("BT", "HID report too short (%d bytes)", length);
    return;
  }
  
  // Determine keycode source and press state based on device profile
  if (device->profile) {
    // Use device profile's byte index for keycode
    if (length >= device->profile->reportByteIndex + 1) {
      keycode = pData[device->profile->reportByteIndex];
    }
    
    // For Game Brick: press state from byte[0] bit 0
    // For standard HID keyboards: press state from keycode (non-zero = pressed)
    if (strcmp(device->profile->name, "IINE Game Brick") == 0) {
      // Game Brick: byte[0] LSB indicates press state
      isPressed = (pData[0] & 0x01) != 0;
      LOG_DBG("BT", "Game Brick: byte[0]=0x%02X, keycode=0x%02X, pressed=%d", pData[0], keycode, isPressed);
    } else {
      // Standard HID keyboards: keycode presence indicates press
      // 0x00 = not pressed, any other value = pressed
      // isPressed = (keycode != 0x00);
      isPressed = (pData[0] != 0);
      LOG_DBG("BT", "Device %s: keycode=0x%02X, pressed=%d", device->profile->name, keycode, isPressed);
    }
  } else {
    // Auto-detect mode: try to intelligently detect report format
    // Priority 1: Check for GameBrick-style codes in byte[4] (A/B buttons 0x07, 0x09)
    if (length >= 5) {
      uint8_t byte4 = pData[4];
      // If we see GameBrick button codes (0x07 or 0x09), use GameBrick format
      if (byte4 == 0x07 || byte4 == 0x09) {
        keycode = byte4;
        isPressed = (pData[0] & 0x01) != 0;
        LOG_DBG("BT", "Auto-detect (GameBrick codes detected): byte[4]=0x%02X, pressed=%d", keycode, isPressed);
      } else if (byte4 == 0x00 && (pData[0] & 0x01)) {
        // Button pressed (byte[0] bit set) but byte[4] is zero - might be in transition
        // Fall through to standard keyboard check
        keycode = length > 2 ? pData[2] : 0x00;
        isPressed = (keycode != 0x00);
        LOG_DBG("BT", "Auto-detect (standard keyboard): keycode=0x%02X, pressed=%d", keycode, isPressed);
      } else {
        // No GameBrick codes in byte[4], try standard keyboard byte[2]
        keycode = length > 2 ? pData[2] : 0x00;
        isPressed = (keycode != 0x00);
        LOG_DBG("BT", "Auto-detect (standard keyboard): keycode=0x%02X, pressed=%d", keycode, isPressed);
      }
    } else {
      // Report too short for GameBrick, use standard keyboard format
      keycode = length > 2 ? pData[2] : 0x00;
      isPressed = (keycode != 0x00);
      LOG_DBG("BT", "Auto-detect (standard keyboard): keycode=0x%02X, pressed=%d", keycode, isPressed);
    }
  }
  
  // Ignore if no valid keycode detected
  if (keycode == 0x00 || keycode == 0xFF) {
    // Track state for transition detection
    device->lastButtonState = isPressed;
    device->lastHIDKeycode = keycode;
    return;
  }
  
  // Detect button PRESS transition: keycode appeared (not was before)
  if (isPressed && !device->lastButtonState) {
    LOG_INF("BT", ">>> BUTTON PRESSED: keycode=0x%02X <<<", keycode);
    
    // Try to map to button and inject with cooldown
    if (g_instance->_buttonInjector) {
      uint8_t btn = g_instance->mapKeycodeToButton(keycode, device->profile);
      if (btn != 0xFF) {
        // Add 150ms cooldown between button injections to prevent flooding
        unsigned long now = millis();
        if (now - device->lastInjectionTime >= 150) {
          String buttonName = (btn == HalGPIO::BTN_DOWN) ? "PageForward" : "PageBack";
          LOG_INF("BT", "Mapped key 0x%02X -> %s", keycode, buttonName.c_str());
          LOG_DBG("BT", "Injecting button: %d", btn);
          g_instance->_buttonInjector(btn);
          device->lastInjectionTime = now;
        } else {
          LOG_DBG("BT", "Button injection throttled (cooldown: %u ms)", now - device->lastInjectionTime);
        }
      }
    }
    
    // Also call original callback if set
    if (g_instance->_inputCallback) {
      g_instance->_inputCallback(keycode);
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
    } else {
      // Not a profile-mapped keycode - ignore it
      LOG_DBG("BT", "Keycode 0x%02X not in profile %s (expecting 0x%02X/0x%02X), ignoring", 
              keycode, profile->name, profile->pageUpCode, profile->pageDownCode);
      return 0xFF;
    }
  }
  
  // No profile - fall back to generic HID consumer codes only
  switch (keycode) {
    case 0xE9:   // Consumer Page Up
      LOG_INF("BT", "Mapped key 0xE9 (Consumer PageUp) -> PageForward");
      return HalGPIO::BTN_DOWN;
    case 0xEA:   // Consumer Page Down
      LOG_INF("BT", "Mapped key 0xEA (Consumer PageDown) -> PageBack");
      return HalGPIO::BTN_UP;
    
    case 0x00:   // Key release/idle
      return 0xFF;
    
    default:
      // Ignore all other keycodes without a profile
      LOG_DBG("BT", "Unmapped keycode: 0x%02X (no profile)", keycode);
      return 0xFF;
  }
}

void BluetoothHIDManager::updateActivity() {
  // Check inactivity timeouts every 10 seconds
  unsigned long now = millis();
  if (now - lastMaintenanceCheck < 10000) {
    return;
  }
  lastMaintenanceCheck = now;
  
  // Check for inactive connections
  for (auto& device : _connectedDevices) {
    if (device.lastActivityTime > 0) {
      unsigned long inactiveTime = now - device.lastActivityTime;
      if (inactiveTime > INACTIVITY_TIMEOUT_MS) {
        LOG_INF("BT", "Device %s inactive for %lu ms, disconnecting", device.address.c_str(), inactiveTime);
        disconnectFromDevice(device.address);
        break;  // List modified, exit loop
      }
    }
  }
}

void BluetoothHIDManager::checkAutoReconnect() {
  // Check for devices that were previously connected but are now disconnected
  // Attempt to reconnect to them automatically
  static unsigned long lastReconnectCheck = 0;
  unsigned long now = millis();
  
  // Only check every 5 seconds to avoid hammering
  if (now - lastReconnectCheck < 5000) {
    return;
  }
  lastReconnectCheck = now;
  
  // Look for devices that should be auto-reconnected
  for (auto& device : _connectedDevices) {
    if (device.wasConnected && device.client) {
      // Check if client is still connected
      if (!device.client->isConnected()) {
        LOG_INF("BT", "Device %s was disconnected, attempting auto-reconnect...", device.address.c_str());
        
        // Remove from list and reconnect
        std::string addr = device.address;
        disconnectFromDevice(addr);
        
        // Attempt reconnection
        if (connectToDevice(addr)) {
          LOG_INF("BT", "Auto-reconnected to %s", addr.c_str());
        } else {
          LOG_ERR("BT", "Auto-reconnect to %s failed: %s", addr.c_str(), lastError.c_str());
        }
        break;  // Only reconnect one device per check
      }
    }
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

