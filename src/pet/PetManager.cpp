#include "PetManager.h"

#include <Arduino.h>

#include <I18n.h>

#include <algorithm>
#include <cstring>
#include <ctime>

#include "PetCareTracker.h"
#include "PetDecayEngine.h"
#include "PetEvolution.h"
#include "PetPersistence.h"

namespace {
constexpr uint8_t PET_MAX_LEVEL = 25;
constexpr uint32_t PET_LEVEL_BASE_PAGES = 20;
constexpr uint32_t PET_LEVEL_STEP_GROWTH = 12;
constexpr uint16_t PET_PAGE_TURN_SAVE_INTERVAL = 4;

struct PetLevelProgress {
  uint8_t level;
  uint8_t progress;
};

PetLevelProgress calculatePetLevelProgress(const uint32_t totalPagesRead) {
  uint8_t level = 1;
  uint32_t pagesIntoLevel = totalPagesRead;

  while (level < PET_MAX_LEVEL) {
    const uint32_t pagesNeededForNext = PET_LEVEL_BASE_PAGES + (static_cast<uint32_t>(level) - 1u) * PET_LEVEL_STEP_GROWTH;
    if (pagesIntoLevel < pagesNeededForNext) {
      const uint8_t progress = static_cast<uint8_t>((pagesIntoLevel * 100u) / pagesNeededForNext);
      return {level, progress};
    }
    pagesIntoLevel -= pagesNeededForNext;
    level++;
  }

  return {PET_MAX_LEVEL, 100};
}
}  // namespace

int PetManager::clamp(int value) {
  if (value < 0) return 0;
  if (value > PetConfig::MAX_STAT) return PetConfig::MAX_STAT;
  return value;
}

uint8_t PetManager::clampSub(uint8_t value, uint8_t amount) {
  return (value > amount) ? static_cast<uint8_t>(value - amount) : 0;
}

uint8_t PetManager::clampAdd(uint8_t value, uint8_t amount) {
  const uint16_t result = static_cast<uint16_t>(value) + amount;
  return (result > PetConfig::MAX_STAT) ? PetConfig::MAX_STAT : static_cast<uint8_t>(result);
}

PetManager& PetManager::getInstance() {
  static PetManager instance;
  return instance;
}

void PetManager::recalcMood() {
  if (!state_.isAlive()) {
    state_.mood = PetMood::Dead;
    return;
  }

  if (state_.isSleeping) {
    state_.mood = PetMood::Sleeping;
  } else if (state_.isSick) {
    state_.mood = PetMood::Sick;
  } else if (state_.attentionCall) {
    state_.mood = PetMood::Needy;
  } else if (state_.discipline < 30 && random(100) < 20) {
    state_.mood = PetMood::Refusing;
  } else if (state_.happiness > 70 && state_.hunger > 50) {
    state_.mood = PetMood::Happy;
  } else if (state_.happiness < 30 || state_.hunger < 25) {
    state_.mood = PetMood::Sad;
  } else if (state_.energy < 12) {
    state_.mood = PetMood::Sleeping;
  } else {
    state_.mood = PetMood::Normal;
  }
}

bool PetManager::load() {
  if (!pet::loadState(state_)) {
    state_ = PetState{};
    return true;
  }
  recalcMood();
  return true;
}

bool PetManager::save() {
  return pet::saveState(state_);
}

void PetManager::hatch(uint8_t type) {
  hatchNew(nullptr, type);
}

void PetManager::hatchNew(const char* name, uint8_t type) {
  state_ = PetState{};
  state_.initialized = true;
  state_.alive = true;
  state_.stage = PetStage::Egg;
  state_.hunger = 80;
  state_.happiness = 80;
  state_.health = 100;
  state_.energy = 70;
  state_.discipline = 50;
  state_.weight = 50;
  state_.petType = static_cast<uint8_t>(type % 5);
  if (name && name[0]) {
    strncpy(state_.petName, name, sizeof(state_.petName) - 1);
    state_.petName[sizeof(state_.petName) - 1] = '\0';
  }
  if (isTimeValid()) {
    state_.birthTime = getCurrentTime();
    state_.lastTickTime = state_.birthTime;
  }
  recalcMood();
  save();
}

void PetManager::changeType(uint8_t type) {
  if (!state_.exists()) return;
  state_.petType = static_cast<uint8_t>(type % 5);
  save();
}

bool PetManager::renamePet(const char* name) {
  if (!state_.exists()) return false;
  if (name && name[0]) {
    strncpy(state_.petName, name, sizeof(state_.petName) - 1);
    state_.petName[sizeof(state_.petName) - 1] = '\0';
  } else {
    state_.petName[0] = '\0';
  }
  return save();
}

bool PetManager::resetPet() {
  state_ = PetState{};
  lastPetTimeMs_ = 0;
  lastExerciseMs_ = 0;
  lastFeedback_ = nullptr;
  pendingMilestone_ = Milestone::NONE;
  lastMilestoneValue_ = 0;
  return pet::clearState();
}

void PetManager::updateStreak() {
  const uint16_t today = getDayOfYear();
  if (today == 0) return;
  if (state_.lastReadDay > 0) {
    const uint16_t diff = (today >= state_.lastReadDay) ? static_cast<uint16_t>(today - state_.lastReadDay) : 1;
    if (diff > 1) state_.currentStreak = 0;
  }
}

bool PetManager::isTimeValid() const {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 0)) return false;
  return timeInfo.tm_year >= 125;
}

uint32_t PetManager::getCurrentTime() const {
  return static_cast<uint32_t>(time(nullptr));
}

uint16_t PetManager::getDayOfYear() const {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 0)) return 0;
  if (timeInfo.tm_year < 125) return 0;
  return static_cast<uint16_t>(timeInfo.tm_yday + 1);
}

void PetManager::tick() {
  if (!state_.exists() || !state_.isAlive()) {
    return;
  }

  if (!isTimeValid()) {
    recalcMood();
    return;
  }

  const uint32_t now = getCurrentTime();
  if (state_.lastTickTime == 0) {
    state_.lastTickTime = now;
    save();
    return;
  }

  if (now <= state_.lastTickTime) {
    recalcMood();
    return;
  }

  const uint32_t elapsedSeconds = now - state_.lastTickTime;
  const uint32_t elapsedHours = elapsedSeconds / 3600;
  if (elapsedHours == 0) {
    recalcMood();
    return;
  }

  struct tm startTm;
  time_t startTime = static_cast<time_t>(state_.lastTickTime);
  localtime_r(&startTime, &startTm);
  const uint8_t startHour = static_cast<uint8_t>(startTm.tm_hour);

  PetDecayEngine::applyDecay(state_, elapsedHours, startHour);
  PetCareTracker::checkCareMistakes(state_, elapsedHours);
  PetCareTracker::expireAttentionCall(state_, now);
  PetCareTracker::generateAttentionCall(state_, now);

  state_.lastTickTime = now;
  state_.lastDecayMs = millis();

  const uint8_t elapsedDays = static_cast<uint8_t>(elapsedHours / 24);
  if (elapsedDays > 0) {
    state_.daysAtStage = static_cast<uint8_t>(state_.daysAtStage + elapsedDays);
    state_.totalAge = static_cast<uint16_t>(state_.totalAge + elapsedDays);
    PetCareTracker::updateCareScore(state_);
    PetEvolution::checkEvolution(state_);
    updateStreak();

    if (state_.stage == PetStage::Adult && state_.isAlive()) {
      const uint16_t lifespan = (state_.careMistakes < PetConfig::ELDER_LIFESPAN_DAYS)
                                    ? static_cast<uint16_t>(PetConfig::ELDER_LIFESPAN_DAYS - state_.careMistakes)
                                    : 1;
      if (state_.totalAge >= lifespan) {
        state_.alive = false;
        state_.stage = PetStage::Dead;
      }
    }
  }

  recalcMood();
  save();
}

uint16_t PetManager::getEffectivePagesPerMeal() const {
  const uint8_t tier = (state_.streakTier < 4) ? state_.streakTier : 3;
  return PetConfig::STREAK_PAGES_PER_MEAL[tier];
}

void PetManager::onPageTurn() {
  if (!state_.exists() || !state_.isAlive()) {
    return;
  }

  state_.readPages++;
  state_.totalPagesRead++;
  state_.pageAccumulator++;

  const uint16_t today = getDayOfYear();
  if (today > 0 && today != state_.missionDay) {
    state_.missionDay = today;
    state_.missionPagesRead = 0;
    state_.missionPetCount = 0;
  }

  if (state_.missionPagesRead < 20) {
    state_.missionPagesRead++;
  }

  if (today > 0 && today != state_.lastReadDay) {
    state_.lastReadDay = today;
    state_.currentStreak++;
    const uint8_t streakBonus = (state_.currentStreak > 5) ? 5 : static_cast<uint8_t>(state_.currentStreak);
    state_.happiness = clampAdd(static_cast<uint8_t>(state_.happiness), streakBonus);

    const uint8_t oldTier = state_.streakTier;
    if (state_.currentStreak >= 30) state_.streakTier = 3;
    else if (state_.currentStreak >= 14) state_.streakTier = 2;
    else if (state_.currentStreak >= 7) state_.streakTier = 1;
    else state_.streakTier = 0;

    if (state_.streakTier > oldTier) {
      pendingMilestone_ = Milestone::STREAK_UP;
      lastMilestoneValue_ = state_.currentStreak;
    }
  }

  if (state_.missionPagesRead == PetConfig::DAILY_GOAL_PAGES) {
    state_.health = clampAdd(static_cast<uint8_t>(state_.health), PetConfig::DAILY_GOAL_HEALTH);
    state_.happiness = clampAdd(static_cast<uint8_t>(state_.happiness), PetConfig::DAILY_GOAL_HAPPINESS);
    if (pendingMilestone_ == Milestone::NONE) {
      pendingMilestone_ = Milestone::DAILY_GOAL;
      lastMilestoneValue_ = PetConfig::DAILY_GOAL_PAGES;
    }
  }

  if (pendingMilestone_ == Milestone::NONE && state_.totalPagesRead > 0 && (state_.totalPagesRead % 100) == 0) {
    pendingMilestone_ = Milestone::PAGE_MILESTONE;
    lastMilestoneValue_ = static_cast<uint16_t>(state_.totalPagesRead > 65535 ? 65535 : state_.totalPagesRead);
  }

  const uint16_t pagesPerMeal = getEffectivePagesPerMeal();
  bool consumedMeal = false;
  if (state_.pageAccumulator >= pagesPerMeal) {
    state_.pageAccumulator = static_cast<uint16_t>(state_.pageAccumulator - pagesPerMeal);
    state_.mealsFromReading++;
    consumedMeal = true;
    (void)feedMeal();
  }

  PetEvolution::checkEvolution(state_);

  recalcMood();

  const bool shouldPersistNow =
      consumedMeal || pendingMilestone_ != Milestone::NONE || (state_.readPages % PET_PAGE_TURN_SAVE_INTERVAL) == 0;
  if (shouldPersistNow) {
    save();
  }
}

void PetManager::onChapterComplete() {
  if (!state_.exists() || !state_.isAlive()) {
    return;
  }
  state_.chaptersFinished++;
  state_.happiness = clampAdd(static_cast<uint8_t>(state_.happiness), PetConfig::CHAPTER_COMPLETE_HAPPINESS);
  recalcMood();
  save();
}

void PetManager::onBookFinished() {
  if (!state_.exists() || !state_.isAlive()) {
    return;
  }
  state_.booksFinished = static_cast<uint32_t>(state_.booksFinished + 1);
  state_.happiness = clampAdd(static_cast<uint8_t>(state_.happiness), PetConfig::BOOK_FINISH_HAPPINESS);
  state_.hunger = clampAdd(static_cast<uint8_t>(state_.hunger), PetConfig::BOOK_FINISH_HUNGER);
  recalcMood();
  save();
}

void PetManager::onPomodoroComplete() {
  if (!state_.exists() || !state_.isAlive()) {
    return;
  }
  state_.happiness = clampAdd(static_cast<uint8_t>(state_.happiness), PetConfig::POMODORO_HAPPINESS);
  recalcMood();
  save();
}

bool PetManager::pet() {
  if (!state_.exists() || !state_.isAlive()) {
    lastFeedback_ = tr(STR_PET_NO_PET);
    return false;
  }

  const unsigned long now = millis();
  if (now - lastPetTimeMs_ < PetConfig::PET_COOLDOWN_MS) {
    lastFeedback_ = "Cooldown";
    return false;
  }

  lastPetTimeMs_ = now;
  state_.happiness = clampAdd(static_cast<uint8_t>(state_.happiness), PetConfig::HAPPINESS_PER_PET);
  if (state_.missionPetCount < 3) state_.missionPetCount++;
  recalcMood();
  save();
  lastFeedback_ = tr(STR_PET_MSG_HAPPY);
  return true;
}

bool PetManager::feedMeal() {
  if (!state_.exists() || !state_.isAlive()) {
    lastFeedback_ = tr(STR_PET_NO_PET);
    return false;
  }
  if (state_.isSleeping) {
    lastFeedback_ = tr(STR_PET_BLOCKED_SLEEPING);
    return false;
  }
  if (state_.isSick) {
    lastFeedback_ = tr(STR_PET_BLOCKED_SICK);
    return false;
  }

  state_.hunger = clampAdd(static_cast<uint8_t>(state_.hunger), PetConfig::HUNGER_PER_MEAL);
  state_.weight = clampAdd(static_cast<uint8_t>(state_.weight), PetConfig::WEIGHT_PER_MEAL);
  if (state_.health < PetConfig::MAX_STAT && state_.hunger > 0) {
    state_.health = clampAdd(static_cast<uint8_t>(state_.health), 5);
  }
  state_.mealsSinceClean = static_cast<uint8_t>(state_.mealsSinceClean + 1);
  if (state_.mealsSinceClean >= PetConfig::MEALS_UNTIL_WASTE) {
    state_.mealsSinceClean = 0;
    if (state_.wasteCount < PetConfig::MAX_WASTE) state_.wasteCount++;
  }

  recalcMood();
  save();
  lastFeedback_ = tr(STR_PET_MSG_FED);
  return true;
}

bool PetManager::feedSnack() {
  if (!state_.exists() || !state_.isAlive()) {
    lastFeedback_ = tr(STR_PET_NO_PET);
    return false;
  }
  if (state_.isSleeping) {
    lastFeedback_ = tr(STR_PET_BLOCKED_SLEEPING);
    return false;
  }

  state_.happiness = clampAdd(static_cast<uint8_t>(state_.happiness), PetConfig::HAPPINESS_PER_SNACK);
  state_.weight = clampAdd(static_cast<uint8_t>(state_.weight), PetConfig::WEIGHT_PER_SNACK);
  recalcMood();
  save();
  lastFeedback_ = tr(STR_PET_MSG_HAPPY);
  return true;
}

bool PetManager::giveMedicine() {
  if (!state_.exists() || !state_.isAlive()) {
    lastFeedback_ = tr(STR_PET_NO_PET);
    return false;
  }
  if (state_.isSleeping) {
    lastFeedback_ = tr(STR_PET_BLOCKED_SLEEPING);
    return false;
  }
  if (!state_.isSick) {
    lastFeedback_ = "Not sick";
    return false;
  }

  state_.isSick = false;
  state_.sicknessTimer = 0;
  state_.health = clampAdd(static_cast<uint8_t>(state_.health), 12);
  recalcMood();
  save();
  lastFeedback_ = "Medicine";
  return true;
}

bool PetManager::exercise() {
  if (!state_.exists() || !state_.isAlive()) {
    lastFeedback_ = tr(STR_PET_NO_PET);
    return false;
  }
  if (state_.isSleeping) {
    lastFeedback_ = tr(STR_PET_BLOCKED_SLEEPING);
    return false;
  }
  if (state_.isSick) {
    lastFeedback_ = tr(STR_PET_BLOCKED_SICK);
    return false;
  }

  const unsigned long now = millis();
  if (now - lastExerciseMs_ < PetConfig::EXERCISE_COOLDOWN_MS) {
    lastFeedback_ = "Cooldown";
    return false;
  }
  lastExerciseMs_ = now;

  state_.weight = clampSub(static_cast<uint8_t>(state_.weight), PetConfig::WEIGHT_PER_EXERCISE);
  state_.happiness = clampAdd(static_cast<uint8_t>(state_.happiness), 10);
  state_.energy = clampSub(static_cast<uint8_t>(state_.energy), 6);
  recalcMood();
  save();
  lastFeedback_ = tr(STR_PET_MSG_EXERCISED);
  return true;
}

bool PetManager::cleanBathroom() {
  if (!state_.exists() || !state_.isAlive()) {
    lastFeedback_ = tr(STR_PET_NO_PET);
    return false;
  }

  state_.wasteCount = 0;
  state_.health = clampAdd(static_cast<uint8_t>(state_.health), 5);
  state_.happiness = clampAdd(static_cast<uint8_t>(state_.happiness), 1);
  recalcMood();
  save();
  lastFeedback_ = "Cleaned";
  return true;
}

bool PetManager::disciplinePet() {
  if (!state_.exists() || !state_.isAlive()) {
    lastFeedback_ = tr(STR_PET_NO_PET);
    return false;
  }

  if (state_.attentionCall) {
    if (state_.isFakeCall) {
      state_.discipline = clampAdd(static_cast<uint8_t>(state_.discipline), PetConfig::DISCIPLINE_PER_SCOLD);
      lastFeedback_ = "Disciplined";
    } else {
      state_.discipline = clampSub(static_cast<uint8_t>(state_.discipline), PetConfig::DISCIPLINE_PER_SCOLD);
      if (state_.careMistakes < 255) state_.careMistakes++;
      lastFeedback_ = "Too harsh";
    }
    state_.attentionCall = false;
    state_.isFakeCall = false;
    state_.currentNeed = PetNeed::None;
  } else {
    state_.discipline = clampSub(static_cast<uint8_t>(state_.discipline), 3);
    lastFeedback_ = "Unfair";
  }

  recalcMood();
  save();
  return true;
}

bool PetManager::ignoreCry() {
  if (!state_.exists() || !state_.isAlive()) {
    lastFeedback_ = tr(STR_PET_NO_PET);
    return false;
  }
  if (!state_.attentionCall) {
    lastFeedback_ = "No call";
    return false;
  }

  if (state_.isFakeCall) {
    state_.discipline = clampAdd(static_cast<uint8_t>(state_.discipline), PetConfig::DISCIPLINE_PER_IGNORE_FAKE);
    lastFeedback_ = "Ignored fake";
  } else {
    if (state_.careMistakes < 255) state_.careMistakes++;
    state_.happiness = clampSub(static_cast<uint8_t>(state_.happiness), 10);
    lastFeedback_ = "Ignored need";
  }

  state_.attentionCall = false;
  state_.isFakeCall = false;
  state_.currentNeed = PetNeed::None;
  recalcMood();
  save();
  return true;
}

bool PetManager::toggleLights() {
  if (!state_.exists() || !state_.isAlive()) {
    lastFeedback_ = tr(STR_PET_NO_PET);
    return false;
  }

  state_.lightsOff = state_.lightsOff ? 0 : 1;
  state_.energy = clampAdd(static_cast<uint8_t>(state_.energy), 8);
  recalcMood();
  save();
  return true;
}

PetManager::Milestone PetManager::consumePendingMilestone() {
  const Milestone milestone = pendingMilestone_;
  pendingMilestone_ = Milestone::NONE;
  return milestone;
}

PetMood PetManager::getMood() const {
  return state_.mood;
}

uint32_t PetManager::getDaysAlive() const {
  if (!state_.exists() || state_.birthTime == 0 || !isTimeValid()) return 0;
  const uint32_t now = getCurrentTime();
  if (now <= state_.birthTime) return 0;
  return (now - state_.birthTime) / 86400;
}

void PetManager::getMissions(PetMission out[3]) const {
  out[0] = {"Read 20 pages", state_.missionPagesRead, 20, state_.missionPagesRead >= 20};
  out[1] = {"Pet 3x", state_.missionPetCount, 3, state_.missionPetCount >= 3};
  out[2] = {"Keep fed", static_cast<uint8_t>(state_.hunger), 40, state_.hunger >= 40};
}

uint8_t PetManager::getPetLevel() const {
  if (!state_.exists() || !state_.isAlive()) return 1;
  return calculatePetLevelProgress(state_.totalPagesRead).level;
}

uint8_t PetManager::getLevelProgressPercent() const {
  if (!state_.exists() || !state_.isAlive()) return 0;
  return calculatePetLevelProgress(state_.totalPagesRead).progress;
}
