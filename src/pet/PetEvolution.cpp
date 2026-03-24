#include "PetEvolution.h"

#include <I18n.h>

namespace {

static uint8_t determineVariant(const PetState& state) {
  const uint8_t stageIdx = static_cast<uint8_t>(state.stage) > 0 ? static_cast<uint8_t>(state.stage) - 1 : 0;
  const uint16_t minPages = PetConfig::EVOLUTION[stageIdx].minPages;
  const uint16_t scholarThreshold = static_cast<uint16_t>(minPages + (minPages / 2));

  if (state.currentStreak >= 7 && state.booksFinished >= 1 && state.totalPagesRead >= scholarThreshold) {
    return 0;
  }
  if (state.currentStreak < 3 && state.totalPagesRead <= static_cast<uint32_t>(minPages + 50)) {
    return 2;
  }
  return 1;
}

}  // namespace

namespace PetEvolution {

void checkEvolution(PetState& state) {
  const uint8_t stageIdx = static_cast<uint8_t>(state.stage);
  if (stageIdx >= static_cast<uint8_t>(PetStage::Adult)) {
    return;
  }

  const auto& req = PetConfig::EVOLUTION[stageIdx];
  const bool daysMet = (stageIdx == static_cast<uint8_t>(PetStage::Egg)) || (state.daysAtStage >= req.minDays);
  if (!daysMet || state.totalPagesRead < req.minPages || state.hunger < req.minAvgHunger) {
    return;
  }

  if (stageIdx == 3 && (state.currentStreak < 7 || state.booksFinished < 1)) {
    return;
  }

  state.stage = static_cast<PetStage>(stageIdx + 1);
  state.daysAtStage = 0;

  if (state.stage == PetStage::Child || state.stage == PetStage::Teen) {
    state.evolutionVariant = determineVariant(state);
  }
}

const char* variantStageName(PetStage stage, uint8_t variant) {
  switch (stage) {
    case PetStage::Egg:
      return tr(STR_PET_STAGE_EGG);
    case PetStage::Baby:
      return tr(STR_PET_STAGE_BABY);
    case PetStage::Child:
      return variant == 2 ? "Wild Child" : (variant == 0 ? "Scholarly Child" : tr(STR_PET_STAGE_CHILD));
    case PetStage::Teen:
      return variant == 2 ? "Wild Companion" : (variant == 0 ? "Scholar" : tr(STR_PET_STAGE_TEEN));
    case PetStage::Adult:
      return tr(STR_PET_STAGE_ADULT);
    case PetStage::Dead:
      return tr(STR_PET_MOOD_DEAD);
    default:
      return tr(STR_PET_STAGE_EGG);
  }
}

const char* typeName(uint8_t type) {
  return PetTypeNames::get(type % PetTypeNames::COUNT);
}

}  // namespace PetEvolution
