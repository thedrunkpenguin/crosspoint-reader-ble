#include "Activity.h"

#include <HalPowerManager.h>

void Activity::renderTaskTrampoline(void* param) {
  auto* self = static_cast<Activity*>(param);
  self->renderTaskLoop();
}

void Activity::renderTaskLoop() {
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
      render(std::move(lock));
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

void Activity::onEnter() {
  renderTaskExitRequested = false;
  if (renderTaskExitSignal) {
    while (xSemaphoreTake(renderTaskExitSignal, 0) == pdTRUE) {
    }
  }
  xTaskCreate(&renderTaskTrampoline, name.c_str(),
              8192,              // Stack size
              this,              // Parameters
              1,                 // Priority
              &renderTaskHandle  // Task handle
  );
  assert(renderTaskHandle != nullptr && "Failed to create render task");
  LOG_DBG("ACT", "Entering activity: %s", name.c_str());
}

void Activity::onExit() {
  if (renderTaskHandle) {
    if (xTaskGetCurrentTaskHandle() == renderTaskHandle) {
      renderTaskExitRequested = true;
      return;
    }

    renderTaskExitRequested = true;
    xTaskNotify(renderTaskHandle, 1, eIncrement);
    bool exitedCleanly = false;

    if (renderTaskExitSignal) {
      const BaseType_t signaled = xSemaphoreTake(renderTaskExitSignal, pdMS_TO_TICKS(8000));
      if (signaled != pdTRUE) {
        LOG_ERR("ACT", "Timeout waiting for render task exit: %s", name.c_str());
      } else {
        exitedCleanly = true;
      }
    } else {
      const TickType_t startTick = xTaskGetTickCount();
      const TickType_t timeoutTicks = pdMS_TO_TICKS(8000);
      while (renderTaskHandle && (xTaskGetTickCount() - startTick) < timeoutTicks) {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      exitedCleanly = (renderTaskHandle == nullptr);
    }

    if (!exitedCleanly && renderTaskHandle) {
      vTaskDelete(renderTaskHandle);
    }

    renderTaskHandle = nullptr;
  }

  LOG_DBG("ACT", "Exiting activity: %s", name.c_str());
}

void Activity::requestUpdate() {
  // Using direct notification to signal the render task to update
  // Increment counter so multiple rapid calls won't be lost
  if (renderTaskHandle) {
    xTaskNotify(renderTaskHandle, 1, eIncrement);
  }
}

void Activity::requestUpdateAndWait() {
  // FIXME @ngxson : properly implement this using freeRTOS notification
  delay(100);
}

// RenderLock

Activity::RenderLock::RenderLock(Activity& activity) : activity(activity) {
  xSemaphoreTake(activity.renderingMutex, portMAX_DELAY);
}

Activity::RenderLock::~RenderLock() { xSemaphoreGive(activity.renderingMutex); }
