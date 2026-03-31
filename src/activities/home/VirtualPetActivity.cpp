#include "VirtualPetActivity.h"

#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "pet/PetManager.h"
#include "pet/PetState.h"

namespace {
bool isDisplayablePetName(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }

  if (value[0] == '/' || value[0] == '.' || value[0] == '\\') {
    return false;
  }

  for (size_t i = 0; value[i] != '\0'; ++i) {
    const char ch = value[i];
    if (ch == '/' || ch == '\\') {
      return false;
    }
  }

  return true;
}
}

const char* VirtualPetActivity::petTypeName(int type) const {
  switch (type % 5) {
    case 0:
      return "Chicken";
    case 1:
      return "Cat";
    case 2:
      return "Dog";
    case 3:
      return "Dragon";
    case 4:
      return "Bunny";
    default:
      return "Chicken";
  }
}

const char* VirtualPetActivity::moodText() const {
  switch (PET_MANAGER.state().mood) {
    case PetMood::Happy:
      return tr(STR_PET_MOOD_HAPPY);
    case PetMood::Normal:
      return tr(STR_PET_MOOD_NORMAL);
    case PetMood::Sad:
      return tr(STR_PET_MOOD_SAD);
    case PetMood::Sick:
      return tr(STR_PET_MOOD_SICK);
    case PetMood::Sleeping:
      return tr(STR_PET_MOOD_SLEEPING);
    case PetMood::Dead:
      return tr(STR_PET_MOOD_DEAD);
    case PetMood::Needy:
      return "Needy";
    case PetMood::Refusing:
      return "Refusing";
    default:
      return tr(STR_PET_MOOD_NORMAL);
  }
}

void VirtualPetActivity::keepSelectionVisible() {
  if (selectorIndex < menuStartIndex) {
    menuStartIndex = selectorIndex;
  } else if (selectorIndex >= menuStartIndex + visibleCommandRows) {
    menuStartIndex = selectorIndex - visibleCommandRows + 1;
  }

  const int maxStart = std::max(0, actionCount - visibleCommandRows);
  if (menuStartIndex > maxStart) menuStartIndex = maxStart;
  if (menuStartIndex < 0) menuStartIndex = 0;
}

void VirtualPetActivity::openRenameKeyboard() {
  std::string currentName = isDisplayablePetName(PET_MANAGER.state().petName) ? PET_MANAGER.state().petName : "";
  if (currentName.empty()) {
    currentName = petTypeName(PET_MANAGER.state().petType);
  }

  auto onComplete = [this](const std::string& value) {
    PET_MANAGER.renamePet(value.c_str());
    swallowBackRelease = true;
    swallowConfirmRelease = true;
    exitActivity();
    requestUpdate();
  };

  auto onCancel = [this]() {
    swallowBackRelease = true;
    swallowConfirmRelease = true;
    exitActivity();
    requestUpdate();
  };

  enterNewActivity(new KeyboardEntryActivity(renderer, mappedInput, "Pet Name", currentName, 18, false, onComplete, onCancel));
}

void VirtualPetActivity::applyAction(int index) {
  if (!PET_MANAGER.state().alive) {
    if (index == 0) {
      PET_MANAGER.hatch(static_cast<uint8_t>(typeSelectIndex));
      inTypeSelect = false;
      requestUpdate();
    }
    return;
  }

  switch (index) {
    case 0:
      PET_MANAGER.feedMeal();
      break;
    case 1:
      PET_MANAGER.feedSnack();
      break;
    case 2:
      PET_MANAGER.giveMedicine();
      break;
    case 3:
      PET_MANAGER.exercise();
      break;
    case 4:
      PET_MANAGER.cleanBathroom();
      break;
    case 5:
      PET_MANAGER.pet();
      break;
    case 6:
      PET_MANAGER.disciplinePet();
      break;
    case 7:
      PET_MANAGER.ignoreCry();
      break;
    case 8:
      PET_MANAGER.toggleLights();
      break;
    case 9:
      changingType = true;
      inTypeSelect = true;
      typeSelectIndex = PET_MANAGER.state().petType % 5;
      break;
    case 10:
      openRenameKeyboard();
      break;
    case 11:
      PET_MANAGER.resetPet();
      selectorIndex = 0;
      menuStartIndex = 0;
      typeSelectIndex = 0;
      inTypeSelect = false;
      changingType = false;
      break;
    default:
      break;
  }
  requestUpdate();
}

void VirtualPetActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  menuStartIndex = 0;
  typeSelectIndex = PET_MANAGER.state().petType % 5;
  inTypeSelect = false;
  changingType = false;
  swallowBackRelease = false;
  swallowConfirmRelease = false;
  PET_MANAGER.tick();
  requestUpdate();
}

void VirtualPetActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  bool consumedSwallowedRelease = false;

  if (swallowBackRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back)) {
      swallowBackRelease = false;
      consumedSwallowedRelease = true;
    }
  }

  if (swallowConfirmRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      swallowConfirmRelease = false;
      consumedSwallowedRelease = true;
    }
  }

  if (consumedSwallowedRelease) {
    requestUpdate();
    return;
  }

  PET_MANAGER.tick();

  if (inTypeSelect) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      inTypeSelect = false;
      changingType = false;
      requestUpdate();
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Up) || mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      typeSelectIndex = (typeSelectIndex + 4) % 5;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down) || mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      typeSelectIndex = (typeSelectIndex + 1) % 5;
      requestUpdate();
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (changingType && PET_MANAGER.state().alive) {
        PET_MANAGER.changeType(static_cast<uint8_t>(typeSelectIndex));
      } else {
        PET_MANAGER.hatch(static_cast<uint8_t>(typeSelectIndex));
      }
      inTypeSelect = false;
      changingType = false;
      requestUpdate();
    }
    return;
  }

  if (!swallowBackRelease && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  if (!PET_MANAGER.state().alive) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up) || mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      typeSelectIndex = (typeSelectIndex + 4) % 5;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down) || mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      typeSelectIndex = (typeSelectIndex + 1) % 5;
      requestUpdate();
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      PET_MANAGER.hatch(static_cast<uint8_t>(typeSelectIndex));
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up) || mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    selectorIndex = (selectorIndex - 1 + actionCount) % actionCount;
    keepSelectionVisible();
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
             mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selectorIndex = (selectorIndex + 1) % actionCount;
    keepSelectionVisible();
    requestUpdate();
  }

  if (!swallowConfirmRelease && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    applyAction(selectorIndex);
  }
}

void VirtualPetActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_VIRTUAL_PET));

  const auto& state = PET_MANAGER.state();

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;

  auto drawBar = [&](int x, int y, int w, const char* label, int value) {
    const int lh = renderer.getLineHeight(SMALL_FONT_ID);
    const int labelW = renderer.getTextWidth(SMALL_FONT_ID, label);
    renderer.drawText(SMALL_FONT_ID, x, y, label);
    const int bx = x + labelW + 8;
    const int bw = std::max(24, w - labelW - 8);
    const int by = y + (lh - 10) / 2;
    const int fill = ((bw - 2) * std::max(0, std::min(100, value))) / 100;
    renderer.drawRect(bx, by, bw, 10, true);
    if (fill > 0) {
      renderer.fillRect(bx + 1, by + 1, fill, 8, true);
    }
  };

  auto stageText = [&]() -> const char* {
    switch (state.stage) {
      case PetStage::Egg:
        return tr(STR_PET_STAGE_EGG);
      case PetStage::Baby:
        return tr(STR_PET_STAGE_BABY);
      case PetStage::Child:
        return tr(STR_PET_STAGE_CHILD);
      case PetStage::Teen:
        return tr(STR_PET_STAGE_TEEN);
      case PetStage::Adult:
        return tr(STR_PET_STAGE_ADULT);
      default:
        return tr(STR_PET_STAGE_EGG);
    }
  };

  const char* actionsAlive[actionCount] = {tr(STR_PET_FEED_MEAL), tr(STR_PET_FEED_SNACK), tr(STR_PET_MEDICINE),
                                           tr(STR_PET_EXERCISE), tr(STR_PET_CLEAN), tr(STR_PET_PRAISE),
                                           tr(STR_PET_DISCIPLINE_ACT), "Ignore", tr(STR_PET_LIGHTS), "Type", "Rename",
                                           "Start Over"};

  if (inTypeSelect) {
    const int centerY = contentTop + (contentBottom - contentTop) / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, centerY - 54, changingType ? "Change Animal Type" : "Choose Egg Type", true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - 24, petTypeName(typeSelectIndex), true, EpdFontFamily::BOLD);

    constexpr int previewScale = 2;
    constexpr int previewSize = PetSpriteRenderer::displaySize(previewScale);
    const int previewX = (pageWidth - previewSize) / 2;
    const int previewY = centerY - 4;
    PetSpriteRenderer::drawPet(renderer,
                   previewX,
                   previewY,
                   PetStage::Baby,
                   PetMood::Normal,
                   previewScale,
                   0,
                   static_cast<uint8_t>(typeSelectIndex),
                   0);

    renderer.drawCenteredText(SMALL_FONT_ID, previewY + previewSize + 18, "Up/Down: Change   Select: Confirm", true);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (!state.alive) {
    const int centerY = contentTop + (contentBottom - contentTop) / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - 48, tr(STR_PET_NO_PET));
    renderer.drawCenteredText(UI_12_FONT_ID, centerY - 18, "Choose Egg Type", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + 10, petTypeName(typeSelectIndex), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, centerY + 32, "Up/Down: Change", true);
    renderer.drawCenteredText(SMALL_FONT_ID, centerY + 48, "Select: Hatch", true);
  } else {
    constexpr int petScale = 2;
    constexpr int petSize = PetSpriteRenderer::displaySize(petScale);  // 96x96
    const int spriteX = (pageWidth - petSize) / 2;
    const int spriteY = contentTop + 8;
    PetSpriteRenderer::drawPet(renderer,
                   spriteX,
                   spriteY,
                   state.stage,
                   state.mood,
                   petScale,
                   state.evolutionVariant,
                   state.petType,
                   0);

    const char* activeName = isDisplayablePetName(state.petName) ? state.petName : petTypeName(state.petType);
        const int infoTop = spriteY + petSize + 10;
        renderer.drawCenteredText(UI_12_FONT_ID, infoTop, activeName, true, EpdFontFamily::BOLD);
        renderer.drawCenteredText(UI_10_FONT_ID, infoTop + 20, stageText(), true, EpdFontFamily::BOLD);
        renderer.drawCenteredText(UI_10_FONT_ID, infoTop + 36, moodText());

    const int level = PET_MANAGER.getPetLevel();
    const int levelProgress = PET_MANAGER.getLevelProgressPercent();
    char levelText[24];
    snprintf(levelText, sizeof(levelText), "Level %d", level);

    const int sideMargin = 28;
        const int statsY = infoTop + 54;
    const int barW = pageWidth - sideMargin * 2;
    const int barSpacing = renderer.getLineHeight(SMALL_FONT_ID) + 10;
    drawBar(sideMargin, statsY + barSpacing * 0, barW, tr(STR_PET_HUNGER), state.hunger);
    drawBar(sideMargin, statsY + barSpacing * 1, barW, tr(STR_PET_HAPPINESS), state.happiness);
    drawBar(sideMargin, statsY + barSpacing * 2, barW, tr(STR_PET_HEALTH), state.health);
    drawBar(sideMargin, statsY + barSpacing * 3, barW, tr(STR_PET_WEIGHT), state.weight);
    drawBar(sideMargin, statsY + barSpacing * 4, barW, tr(STR_PET_DISCIPLINE), state.discipline);

    const int readingPct = static_cast<int>((state.readPages % 10) * 10);
    drawBar(sideMargin, statsY + barSpacing * 5, barW, tr(STR_PET_PAGES_TO_FEED), readingPct);

    const int levelBarX = 10;
    const int levelBarY = contentBottom - 10;
    const int levelBarW = pageWidth - 20;
    const int levelLabelY = levelBarY - renderer.getLineHeight(SMALL_FONT_ID) - 2;

    renderer.drawCenteredText(SMALL_FONT_ID, levelLabelY, levelText, true, EpdFontFamily::BOLD);
    renderer.drawRect(levelBarX, levelBarY, levelBarW, 8, true);
    const int levelFill = ((levelBarW - 2) * std::max(0, std::min(100, levelProgress))) / 100;
    if (levelFill > 0) {
      renderer.fillRect(levelBarX + 1, levelBarY + 1, levelFill, 6, true);
    }

    const int menuY = statsY + barSpacing * 6 + 12;
    const int menuRowH = renderer.getLineHeight(SMALL_FONT_ID) + 3;
    const int menuW = pageWidth - sideMargin * 2;
    const int menuBottomLimit = levelLabelY - 4;
    const int maxRowsBySpace = std::max(1, (menuBottomLimit - menuY) / menuRowH);
    const int visible = std::min(std::min(visibleCommandRows, actionCount), maxRowsBySpace);
    for (int row = 0; row < visible; ++row) {
      const int i = menuStartIndex + row;
      if (i >= actionCount) break;
      const int y = menuY + row * menuRowH;
      const bool selected = selectorIndex == i;
      if (selected) {
        renderer.fillRect(sideMargin, y, menuW, menuRowH, true);
        renderer.drawText(SMALL_FONT_ID, sideMargin + 6, y + 3, actionsAlive[i], false, EpdFontFamily::BOLD);
      } else {
        renderer.drawText(SMALL_FONT_ID, sideMargin + 6, y + 3, actionsAlive[i], true);
      }
    }

    if (actionCount > visibleCommandRows) {
      const int indicatorY = menuY + visible * menuRowH + 2;
      char listInfo[20];
      snprintf(listInfo, sizeof(listInfo), "%d/%d", selectorIndex + 1, actionCount);
      renderer.drawCenteredText(SMALL_FONT_ID, indicatorY, listInfo);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
