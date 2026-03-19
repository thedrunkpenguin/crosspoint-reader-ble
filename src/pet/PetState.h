#pragma once

#include <cstdint>

enum class PetStage : uint8_t {
  Egg = 0,
  Baby = 1,
  Child = 2,
  Teen = 3,
  Adult = 4,
  Dead = 5,
  EGG = Egg,
  HATCHLING = Baby,
  YOUNGSTER = Child,
  COMPANION = Teen,
  ELDER = Adult,
  DEAD = Dead
};

enum class PetMood : uint8_t {
  Happy = 0,
  Normal = 1,
  Sad = 2,
  Sick = 3,
  Sleeping = 4,
  Dead = 5,
  Needy = 6,
  Refusing = 7,
  HAPPY = Happy,
  NEUTRAL = Normal,
  SAD = Sad,
  SICK = Sick,
  SLEEPING = Sleeping,
  DEAD = Dead,
  NEEDY = Needy,
  REFUSING = Refusing
};

enum class PetNeed : uint8_t {
  None = 0,
  Hungry = 1,
  NeedsMedicine = 2,
  Dirty = 3,
  Bored = 4,
  Sleepy = 5,
  NONE = None,
  HUNGRY = Hungry,
  SICK = NeedsMedicine,
  DIRTY = Dirty,
  BORED = Bored,
  SLEEPY = Sleepy
};

namespace PetTypeNames {
constexpr const char* NAMES[] = {"Chicken", "Cat", "Dog", "Dragon", "Bunny"};
constexpr uint8_t COUNT = 5;
inline const char* get(uint8_t type) { return (type < COUNT) ? NAMES[type] : NAMES[0]; }
}  // namespace PetTypeNames

namespace PetConfig {
constexpr uint8_t MAX_STAT = 100;
constexpr uint16_t PAGES_PER_MEAL = 20;
constexpr uint32_t PET_COOLDOWN_MS = 30000;
constexpr uint32_t EXERCISE_COOLDOWN_MS = 3600000;
constexpr uint8_t HUNGER_PER_MEAL = 25;
constexpr uint8_t HAPPINESS_PER_PET = 10;
constexpr uint8_t HAPPINESS_PER_SNACK = 15;
constexpr uint8_t WEIGHT_PER_MEAL = 5;
constexpr uint8_t WEIGHT_PER_SNACK = 3;
constexpr uint8_t WEIGHT_PER_EXERCISE = 8;
constexpr uint8_t OVERWEIGHT_THRESHOLD = 80;
constexpr uint8_t UNDERWEIGHT_THRESHOLD = 20;
constexpr uint8_t NORMAL_WEIGHT = 50;
constexpr uint8_t HUNGER_DECAY_PER_HOUR = 1;
constexpr uint8_t HAPPINESS_DECAY_PER_HOUR = 1;
constexpr uint8_t HEALTH_DECAY_PER_HOUR = 2;
constexpr uint8_t SICK_RECOVERY_HOURS = 24;
constexpr uint8_t SICK_HAPPINESS_PENALTY = 2;
constexpr uint8_t MEALS_UNTIL_WASTE = 3;
constexpr uint8_t MAX_WASTE = 4;
constexpr uint8_t WASTE_HAPPINESS_PENALTY = 1;
constexpr uint8_t DISCIPLINE_PER_SCOLD = 10;
constexpr uint8_t DISCIPLINE_PER_IGNORE_FAKE = 5;
constexpr uint8_t FAKE_CALL_CHANCE_PERCENT = 30;
constexpr uint32_t ATTENTION_CALL_INTERVAL_SEC = 14400;
constexpr uint32_t ATTENTION_CALL_EXPIRE_SEC = 7200;
constexpr uint8_t SLEEP_HOUR = 22;
constexpr uint8_t WAKE_HOUR = 7;
constexpr uint16_t ELDER_LIFESPAN_DAYS = 20;
constexpr uint8_t CHAPTER_COMPLETE_HAPPINESS = 5;
constexpr uint8_t BOOK_FINISH_HAPPINESS = 40;
constexpr uint8_t BOOK_FINISH_HUNGER = 20;
constexpr uint8_t POMODORO_HAPPINESS = 15;
constexpr uint8_t DAILY_GOAL_PAGES = 20;
constexpr uint8_t DAILY_GOAL_HEALTH = 10;
constexpr uint8_t DAILY_GOAL_HAPPINESS = 5;

struct EvolutionReq {
  uint8_t minDays;
  uint16_t minPages;
  uint8_t minAvgHunger;
};

constexpr EvolutionReq EVOLUTION[] = {
    {1, 20, 0},
    {3, 100, 40},
    {7, 500, 50},
    {14, 1500, 60},
};

constexpr uint16_t STREAK_PAGES_PER_MEAL[] = {20, 16, 13, 10};
}  // namespace PetConfig

struct PetState {
  bool initialized = false;
  bool alive = false;
  PetStage stage = PetStage::Egg;
  PetMood mood = PetMood::Normal;
  uint8_t petType = 0;
  char petName[20] = {};

  int hunger = 80;
  int happiness = 80;
  int health = 100;
  int energy = 60;
  int discipline = 50;
  int weight = 50;

  uint32_t birthTime = 0;
  uint32_t lastTickTime = 0;
  uint32_t lastDecayMs = 0;
  uint32_t readPages = 0;
  uint32_t totalPagesRead = 0;
  uint32_t mealsFromReading = 0;
  uint32_t booksFinished = 0;
  uint32_t chaptersFinished = 0;

  uint16_t currentStreak = 0;
  uint8_t daysAtStage = 0;
  uint16_t lastReadDay = 0;
  uint16_t pageAccumulator = 0;

  uint16_t missionDay = 0;
  uint8_t missionPagesRead = 0;
  uint8_t missionPetCount = 0;

  bool isSick = false;
  uint8_t sicknessTimer = 0;
  uint8_t wasteCount = 0;
  uint8_t mealsSinceClean = 0;

  bool attentionCall = false;
  bool isFakeCall = false;
  PetNeed currentNeed = PetNeed::None;
  uint32_t lastCallTime = 0;

  bool isSleeping = false;
  uint8_t lightsOff = 0;

  uint16_t totalAge = 0;
  uint8_t careMistakes = 0;
  uint8_t avgCareScore = 50;
  uint8_t evolutionVariant = 0;
  uint8_t streakTier = 0;

  bool exists() const { return initialized; }
  bool isAlive() const { return stage != PetStage::Dead && alive; }
};
