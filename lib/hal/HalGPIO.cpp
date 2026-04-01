#include <HalGPIO.h>
#include <SPI.h>

void HalGPIO::begin() {
  inputMgr.begin();
  SPI.begin(EPD_SCLK, SPI_MISO, EPD_MOSI, EPD_CS);
  pinMode(UART0_RXD, INPUT);
}

void HalGPIO::update() { 
  previousVirtualButtonState = virtualButtonState;
  virtualButtonState = desiredVirtualButtonState;

  inputMgr.update();
}

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
    // Cap at last BLE activity time so a short physical press doesn't accumulate
    // stale-window time toward the long-press chapter-skip threshold.
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
  const uint8_t mask = (1 << buttonIndex);
  const bool wasPressed = (desiredVirtualButtonState & mask) != 0;
  if (pressed == wasPressed) {
    return;
  }

  if (pressed) {
    desiredVirtualButtonState |= mask;
    virtualPressStart[buttonIndex] = millis();
  } else {
    desiredVirtualButtonState &= ~mask;
    // Use last BLE activity time so getHeldTime() reflects actual physical hold duration.
    // `>=` matters here because the first valid HID report can land in the same millisecond
    // as the synthetic press injection.
    virtualPressFinish[buttonIndex] = (virtualLastActivityTime[buttonIndex] >= virtualPressStart[buttonIndex])
                        ? virtualLastActivityTime[buttonIndex]
                        : millis();
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