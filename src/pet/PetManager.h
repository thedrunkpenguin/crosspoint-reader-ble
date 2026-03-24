#pragma once

#include "PetState.h"

struct PetMission {
  const char* label;
  uint8_t progress;
  uint8_t goal;
  bool done;
};

class PetManager {
 public:
  PetManager(const PetManager&) = delete;
  PetManager& operator=(const PetManager&) = delete;

  static PetManager& getInstance();

  bool load();
  bool save();

  const PetState& state() const { return state_; }
  const PetState& getState() const { return state_; }

  bool exists() const { return state_.exists(); }
  bool isAlive() const { return state_.isAlive(); }

  void hatch(uint8_t type = 0);
  void hatchNew(const char* name = nullptr, uint8_t type = 0);
  bool renamePet(const char* name);
  void changeType(uint8_t type);
  bool resetPet();

  void tick();

  void onPageTurn();
  void onChapterComplete();
  void onBookFinished();
  void onPomodoroComplete();
  uint16_t getEffectivePagesPerMeal() const;

  bool pet();

  bool feedMeal();
  bool feedSnack();
  bool giveMedicine();
  bool exercise();
  bool cleanBathroom();
  bool disciplinePet();
  bool ignoreCry();
  bool toggleLights();

  void clean() { (void)cleanBathroom(); }
  void praise() { (void)pet(); }
  void discipline() { (void)disciplinePet(); }

  enum class Milestone : uint8_t { NONE, DAILY_GOAL, STREAK_UP, PAGE_MILESTONE };
  Milestone consumePendingMilestone();
  uint16_t getLastMilestoneValue() const { return lastMilestoneValue_; }

  PetMood getMood() const;
  uint32_t getDaysAlive() const;
  const char* getLastFeedback() const { return lastFeedback_; }
  void getMissions(PetMission out[3]) const;
  uint8_t getPetLevel() const;
  uint8_t getLevelProgressPercent() const;

 private:
  PetState state_;
  unsigned long lastPetTimeMs_ = 0;
  unsigned long lastExerciseMs_ = 0;
  const char* lastFeedback_ = nullptr;
  Milestone pendingMilestone_ = Milestone::NONE;
  uint16_t lastMilestoneValue_ = 0;

  PetManager() = default;

  static uint8_t clampSub(uint8_t value, uint8_t amount);
  static uint8_t clampAdd(uint8_t value, uint8_t amount);
  static int clamp(int value);
  void recalcMood();
  void updateStreak();
  bool isTimeValid() const;
  uint32_t getCurrentTime() const;
  uint16_t getDayOfYear() const;
};

#define PET_MANAGER PetManager::getInstance()
