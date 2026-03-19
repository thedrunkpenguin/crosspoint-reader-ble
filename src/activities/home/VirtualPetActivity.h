#pragma once

#include <functional>

#include "../Activity.h"
#include "pet/PetSpriteRenderer.h"
#include "pet/PetState.h"

class VirtualPetActivity final : public Activity {
  int selectorIndex = 0;
  int typeSelectIndex = 0;
  bool inTypeSelect = false;
  bool changingType = false;
  const std::function<void()> onBack;

  const char* moodText() const;
  const char* petTypeName(int type) const;
  void applyAction(int index);

 public:
  explicit VirtualPetActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack)
      : Activity("VirtualPet", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;
};
