#include "ActivityWithSubactivity.h"

#include <HalPowerManager.h>

void ActivityWithSubactivity::renderTaskLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (renderTaskExitRequested) {
      break;
    }
    {
      HalPowerManager::Lock powerLock;  // Ensure we don't go into low-power mode while rendering
      RenderLock lock(*this);
      if (renderTaskExitRequested) {
        break;
      }
      if (!subActivity) {
        render(std::move(lock));
      }
      // If subActivity is set, consume the notification but skip parent render
      // Note: the sub-activity will call its render() from its own display task
    }
  }

  if (renderTaskExitSignal) {
    xSemaphoreGive(renderTaskExitSignal);
  }
  renderTaskHandle = nullptr;
  vTaskDelete(nullptr);
  while (true) {
  }
}

void ActivityWithSubactivity::exitActivity() {
  // No need to lock, since onExit() already acquires its own lock
  if (subActivity) {
    LOG_DBG("ACT", "Exiting subactivity...");
    subActivity->onExit();
    subActivity.reset();
  }
}

void ActivityWithSubactivity::enterNewActivity(Activity* activity) {
  // Acquire lock to avoid 2 activities rendering at the same time during transition
  RenderLock lock(*this);
  subActivity.reset(activity);
  subActivity->onEnter();
}

void ActivityWithSubactivity::loop() {
  if (subActivity) {
    subActivity->loop();
  }
}

void ActivityWithSubactivity::requestUpdate() {
  if (!subActivity) {
    Activity::requestUpdate();
  }
  // Sub-activity should call their own requestUpdate() from their loop() function
}

void ActivityWithSubactivity::onExit() {
  // No need to lock, onExit() already acquires its own lock
  exitActivity();
  Activity::onExit();
}
