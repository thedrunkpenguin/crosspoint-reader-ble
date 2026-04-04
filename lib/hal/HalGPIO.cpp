#include <HalGPIO.h>
#include <SPI.h>

namespace {
constexpr unsigned long VIRTUAL_BUTTON_REPRESS_DEBOUNCE_MS = 250;
}

void HalGPIO::begin() {
  inputMgr.begin();
  SPI.begin(EPD_SCLK, SPI_MISO, EPD_MOSI, EPD_CS);
  pinMode(UART0_RXD, INPUT);
}

void HalGPIO::update() {
  previousVirtualButtonState = virtualButtonState;
  virtualButtonState = desiredVirtualButtonState;

  inputMgr.update();
  const bool connected = isUsbConnected();
  usbStateChanged = (connected != lastUsbConnected);
  lastUsbConnected = connected;
}

bool HalGPIO::wasUsbStateChanged() const { return usbStateChanged; }

bool HalGPIO::isPressed(uint8_t buttonIndex) const {
  return inputMgr.isPressed(buttonIndex) || (virtualButtonState & (1 << buttonIndex));
}

bool HalGPIO::wasPressed(uint8_t buttonIndex) const {
  const uint8_t virtualPressed = virtualButtonState & ~previousVirtualButtonState;
  return inputMgr.wasPressed(buttonIndex) || (virtualPressed & (1 << buttonIndex));
}

bool HalGPIO::wasAnyPressed() const {
  const uint8_t virtualPressed = virtualButtonState & ~previousVirtualButtonState;
  return inputMgr.wasAnyPressed() || (virtualPressed > 0);
}

bool HalGPIO::wasReleased(uint8_t buttonIndex) const {
  const uint8_t virtualRelease = previousVirtualButtonState & ~virtualButtonState;
  return inputMgr.wasReleased(buttonIndex) || (virtualRelease & (1 << buttonIndex));
}

bool HalGPIO::wasAnyReleased() const {
  const uint8_t virtualRelease = previousVirtualButtonState & ~virtualButtonState;
  return inputMgr.wasAnyReleased() || (virtualRelease > 0);
}

unsigned long HalGPIO::getHeldTime() const {
  unsigned long heldTime = inputMgr.getHeldTime();

  for (uint8_t buttonIndex = 0; buttonIndex <= BTN_POWER; ++buttonIndex) {
    const unsigned long virtualHeldTime = getHeldTime(buttonIndex);
    if (virtualHeldTime > heldTime) {
      heldTime = virtualHeldTime;
    }
  }

  return heldTime;
}

unsigned long HalGPIO::getHeldTime(uint8_t buttonIndex) const {
  const uint8_t mask = (1 << buttonIndex);
  if (virtualButtonState & mask) {
    if (virtualLastActivityTime[buttonIndex] >= virtualPressStart[buttonIndex]) {
      return virtualLastActivityTime[buttonIndex] - virtualPressStart[buttonIndex];
    }
    return millis() - virtualPressStart[buttonIndex];
  }

  if ((previousVirtualButtonState & ~virtualButtonState) & mask) {
    return virtualPressFinish[buttonIndex] - virtualPressStart[buttonIndex];
  }

  if (inputMgr.isPressed(buttonIndex) || inputMgr.wasPressed(buttonIndex) || inputMgr.wasReleased(buttonIndex)) {
    return inputMgr.getHeldTime();
  }

  return 0;
}

void HalGPIO::setVirtualButtonState(uint8_t buttonIndex, bool pressed) {
  if (buttonIndex > BTN_POWER) {
    return;
  }

  const uint8_t mask = (1 << buttonIndex);
  const bool wasPressed = (desiredVirtualButtonState & mask) != 0;
  if (pressed == wasPressed) {
    return;
  }

  const unsigned long now = millis();

  if (pressed) {
    // BLE HID remotes can emit short release/press jitter for one physical click.
    // Suppress immediate re-presses so the reader doesn't render multiple fast
    // refreshes for what should be a single page turn.
    if (virtualPressFinish[buttonIndex] != 0 &&
        (now - virtualPressFinish[buttonIndex]) < VIRTUAL_BUTTON_REPRESS_DEBOUNCE_MS) {
      return;
    }

    desiredVirtualButtonState |= mask;
    virtualPressStart[buttonIndex] = now;
    virtualLastActivityTime[buttonIndex] = now;
  } else {
    desiredVirtualButtonState &= ~mask;
    virtualPressFinish[buttonIndex] = (virtualLastActivityTime[buttonIndex] >= virtualPressStart[buttonIndex])
                                          ? virtualLastActivityTime[buttonIndex]
                                          : now;
    virtualLastActivityTime[buttonIndex] = 0;
  }
}

void HalGPIO::injectButtonPress(uint8_t buttonIndex) {
  setVirtualButtonState(buttonIndex, true);
  setVirtualButtonState(buttonIndex, false);
}

void HalGPIO::updateVirtualButtonActivity(uint8_t buttonIndex) {
  if (buttonIndex < 7) {
    virtualLastActivityTime[buttonIndex] = millis();
  }
}

void HalGPIO::clearVirtualButtons() {
  virtualButtonState = 0;
  desiredVirtualButtonState = 0;
  previousVirtualButtonState = 0;
  for (uint8_t buttonIndex = 0; buttonIndex <= BTN_POWER; ++buttonIndex) {
    virtualPressStart[buttonIndex] = 0;
    virtualPressFinish[buttonIndex] = 0;
    virtualLastActivityTime[buttonIndex] = 0;
  }
}

bool HalGPIO::isUsbConnected() const {
  // U0RXD/GPIO20 reads HIGH when USB is connected
  return digitalRead(UART0_RXD) == HIGH;
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const bool usbConnected = isUsbConnected();
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  if ((wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && !usbConnected) ||
      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO && resetReason == ESP_RST_DEEPSLEEP && usbConnected)) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }
  return WakeupReason::Other;
}