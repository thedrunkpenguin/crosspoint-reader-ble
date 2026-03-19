#include "PetDecayEngine.h"

namespace {

static inline bool isSleepHour(uint8_t hour) {
  return hour >= PetConfig::SLEEP_HOUR || hour < PetConfig::WAKE_HOUR;
}

static inline uint8_t clampSub(uint8_t val, uint8_t amount) {
  return (val > amount) ? (val - amount) : 0;
}

static inline uint8_t clampAdd(uint8_t val, uint8_t amount) {
  uint16_t result = static_cast<uint16_t>(val) + amount;
  return (result > PetConfig::MAX_STAT) ? PetConfig::MAX_STAT : static_cast<uint8_t>(result);
}

static void applyOneHour(PetState& state, uint8_t hour, uint32_t tickHour) {
  if (isSleepHour(hour)) {
    state.isSleeping = true;
    if (state.lightsOff == 0 && tickHour % 2 == 0) {
      state.happiness = clampSub(static_cast<uint8_t>(state.happiness), 1);
    }
    return;
  }

  state.isSleeping = false;
  state.hunger = clampSub(static_cast<uint8_t>(state.hunger), PetConfig::HUNGER_DECAY_PER_HOUR);

  if (tickHour % 2 == 0) {
    uint8_t decay = PetConfig::HAPPINESS_DECAY_PER_HOUR;
    if (state.isSick) decay = static_cast<uint8_t>(decay + PetConfig::SICK_HAPPINESS_PENALTY);
    if (state.wasteCount > 0) decay = static_cast<uint8_t>(decay + state.wasteCount * PetConfig::WASTE_HAPPINESS_PENALTY);
    state.happiness = clampSub(static_cast<uint8_t>(state.happiness), decay);
  }

  if (state.hunger == 0) {
    state.health = clampSub(static_cast<uint8_t>(state.health), PetConfig::HEALTH_DECAY_PER_HOUR);
  }
  if (state.weight < PetConfig::UNDERWEIGHT_THRESHOLD && tickHour % 4 == 0) {
    state.health = clampSub(static_cast<uint8_t>(state.health), 1);
  }

  if (tickHour % 12 == 0) {
    if (state.weight > PetConfig::NORMAL_WEIGHT) {
      state.weight = clampSub(static_cast<uint8_t>(state.weight), 1);
    } else if (state.weight < PetConfig::NORMAL_WEIGHT) {
      state.weight = clampAdd(static_cast<uint8_t>(state.weight), 1);
    }
  }

  if (state.isSick) {
    if (state.sicknessTimer > 0) {
      state.sicknessTimer--;
    } else {
      state.isSick = false;
    }
  }
}

}  // namespace

namespace PetDecayEngine {

void applyDecay(PetState& state, uint32_t elapsedHours, uint8_t startHour) {
  if (elapsedHours > 720) elapsedHours = 720;

  for (uint32_t h = 0; h < elapsedHours; ++h) {
    const uint8_t currentHour = static_cast<uint8_t>((startHour + h) % 24);
    applyOneHour(state, currentHour, h);

    if (state.health <= 0) {
      state.health = 0;
      state.alive = false;
      state.stage = PetStage::Dead;
      state.mood = PetMood::Dead;
      return;
    }
  }
}

}  // namespace PetDecayEngine
