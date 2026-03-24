#pragma once

#include <functional>
#include <string>

#include "../ActivityWithSubactivity.h"
#include "pet/PetSpriteRenderer.h"
#include "pet/PetState.h"

class VirtualPetActivity final : public ActivityWithSubactivity {
  int selectorIndex = 0;
  int menuStartIndex = 0;
  int typeSelectIndex = 0;
  bool inTypeSelect = false;
  bool changingType = false;
  bool swallowBackRelease = false;
  bool swallowConfirmRelease = false;
  const std::function<void()> onBack;

  static constexpr int actionCount = 12;
  static constexpr int visibleCommandRows = 6;

  const char* moodText() const;
  const char* petTypeName(int type) const;
  void applyAction(int index);
  void openRenameKeyboard();
  void keepSelectionVisible();

 public:
  explicit VirtualPetActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack)
      : ActivityWithSubactivity("VirtualPet", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;
};
