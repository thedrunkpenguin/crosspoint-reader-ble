#include "DeviceProfiles.h"

#include <cstring>
#include <Logging.h>

namespace DeviceProfiles {

const DeviceProfile* findDeviceProfile(const char* macAddress, const char* deviceName) {
  // First, try to find by MAC address prefix (case-insensitive comparison)
  if (macAddress) {
    for (int i = 0; i < KNOWN_DEVICES_COUNT; i++) {
      if (KNOWN_DEVICES[i].macPrefix) {
        // Case-insensitive MAC prefix check
        const char* prefix = KNOWN_DEVICES[i].macPrefix;
        size_t prefixLen = strlen(prefix);
        bool matches = true;
        
        for (size_t j = 0; j < prefixLen && macAddress[j] != '\0'; j++) {
          char macChar = macAddress[j];
          char prefixChar = prefix[j];
          // Convert both to lowercase for comparison
          if (macChar >= 'A' && macChar <= 'F') macChar = macChar - 'A' + 'a';
          if (prefixChar >= 'A' && prefixChar <= 'F') prefixChar = prefixChar - 'A' + 'a';
          
          if (macChar != prefixChar) {
            matches = false;
            break;
          }
        }
        
        if (matches) {
          LOG_INF("DEV", "✓ Matched device profile by MAC: %s -> %s", macAddress, KNOWN_DEVICES[i].name);
          return &KNOWN_DEVICES[i];
        }
      }
    }
    LOG_DBG("DEV", "No MAC match found for: %s", macAddress);
  }

  // Then try to find by device name (flexible matching)
  if (deviceName && strlen(deviceName) > 0) {
    for (int i = 0; i < KNOWN_DEVICES_COUNT; i++) {
      const char* profileName = KNOWN_DEVICES[i].name;
      
      // Try exact match first
      if (strcmp(deviceName, profileName) == 0) {
        LOG_INF("DEV", "✓ Matched device profile by exact name: %s", profileName);
        return &KNOWN_DEVICES[i];
      }
      
      // Try case-insensitive substring match for common patterns
      // This allows "Game Brick", "GameBrick", "IINE Game Brick", etc.
      if (strstr(deviceName, "Game") || strstr(deviceName, "game") || 
          strstr(deviceName, "GAME")) {
        if (strstr(deviceName, "Brick") || strstr(deviceName, "brick") || 
            strstr(deviceName, "BRICK")) {
          LOG_INF("DEV", "✓ Matched GameBrick by name pattern: %s -> IINE Game Brick", deviceName);
          return &KNOWN_DEVICES[0];  // Return GameBrick profile
        }
      }
      
      // Match MINI_KEYBOARD variants
      if (strstr(deviceName, "MINI") || strstr(deviceName, "mini") || 
          strstr(deviceName, "Mini")) {
        if (strstr(deviceName, "KEYBOARD") || strstr(deviceName, "keyboard") || 
            strstr(deviceName, "Keyboard")) {
          LOG_INF("DEV", "✓ Matched MINI_KEYBOARD by name pattern: %s", deviceName);
          return &KNOWN_DEVICES[1];  // Return MINI_KEYBOARD profile
        }
      }
      
      // Match Free 2 variants
      if (strstr(deviceName, "Free") || strstr(deviceName, "free") ||
          strstr(deviceName, "FREE")) {
        if (strstr(deviceName, "2")) {
          LOG_INF("DEV", "✓ Matched Free 2 by name pattern: %s", deviceName);
          return &KNOWN_DEVICES[4]; // Return Free 2 profile
        }
      }
    }
    
    LOG_DBG("DEV", "No profile match for device name: %s", deviceName);
  }

  return nullptr;
}

bool isStandardConsumerPageCode(uint8_t code) {
  // Standard HID Consumer Page codes for page navigation
  return code == STANDARD_PAGE_UP || code == STANDARD_PAGE_DOWN;
}

const DeviceProfile* getCustomProfile() {
  // TODO: Implement settings persistence for custom profiles
  // For now, return nullptr (no custom profile set)
  return nullptr;
}

void setCustomProfile(uint8_t pageUpCode, uint8_t pageDownCode, uint8_t reportByteIndex) {
  // TODO: Implement settings persistence for custom profiles
  LOG_INF("DEV", "Custom profile set: up=0x%02X down=0x%02X byte=%d", pageUpCode, pageDownCode, reportByteIndex);
}

void clearCustomProfile() {
  // TODO: Implement settings clearing
  LOG_INF("DEV", "Custom profile cleared");
}

}  // namespace DeviceProfiles
