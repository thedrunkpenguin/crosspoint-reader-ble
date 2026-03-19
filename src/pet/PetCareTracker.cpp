#include "PetCareTracker.h"

#include <Arduino.h>

namespace {

static inline uint8_t clampSub(uint8_t val, uint8_t amount) {
  return (val > amount) ? (val - amount) : 0;
}

static inline uint8_t clampAdd(uint8_t val, uint8_t amount) {
  uint16_t result = static_cast<uint16_t>(val) + amount;
  return (result > PetConfig::MAX_STAT) ? PetConfig::MAX_STAT : static_cast<uint8_t>(result);
}

static PetNeed detectNeed(const PetState& state) {
  if (state.isSick) return PetNeed::NeedsMedicine;
  if (state.hunger < 30) return PetNeed::Hungry;
  if (state.wasteCount > 0) return PetNeed::Dirty;
  if (state.happiness < 30) return PetNeed::Bored;
  return PetNeed::None;
}

}  // namespace

namespace PetCareTracker {

void checkCareMistakes(PetState& state, uint32_t elapsedHours) {
  if (state.hunger == 0 && elapsedHours >= 6 && state.careMistakes < 255) {
    state.careMistakes++;
  }
  if (state.isSick && elapsedHours >= 12 && state.careMistakes < 255) {
    state.careMistakes++;
  }
  if (state.wasteCount > 0 && elapsedHours >= 4 && state.careMistakes < 255) {
    state.careMistakes++;
  }
}

void generateAttentionCall(PetState& state, uint32_t nowSec) {
  if (state.attentionCall || state.isSleeping) return;
  if (state.lastCallTime > 0 && nowSec > state.lastCallTime &&
      (nowSec - state.lastCallTime) < PetConfig::ATTENTION_CALL_INTERVAL_SEC) {
    return;
  }

  const PetNeed need = detectNeed(state);
  if (need == PetNeed::None) {
    if (random(100) >= PetConfig::FAKE_CALL_CHANCE_PERCENT) {
      return;
    }
    state.attentionCall = true;
    state.isFakeCall = true;
    state.currentNeed = PetNeed::None;
  } else {
    state.attentionCall = true;
    state.isFakeCall = false;
    state.currentNeed = need;
  }
  state.lastCallTime = nowSec;
}

void expireAttentionCall(PetState& state, uint32_t nowSec) {
  if (!state.attentionCall || state.lastCallTime == 0 || nowSec <= state.lastCallTime) return;
  if ((nowSec - state.lastCallTime) < PetConfig::ATTENTION_CALL_EXPIRE_SEC) return;

  if (!state.isFakeCall) {
    if (state.careMistakes < 255) state.careMistakes++;
    state.happiness = clampSub(static_cast<uint8_t>(state.happiness), 10);
  } else {
    state.discipline = clampAdd(static_cast<uint8_t>(state.discipline), PetConfig::DISCIPLINE_PER_IGNORE_FAKE);
  }

  state.attentionCall = false;
  state.isFakeCall = false;
  state.currentNeed = PetNeed::None;
}

void updateCareScore(PetState& state) {
  const uint8_t daily = static_cast<uint8_t>((state.hunger + state.happiness + state.health) / 3);
  state.avgCareScore = static_cast<uint8_t>((state.avgCareScore * 6 + daily * 4) / 10);
}

}  // namespace PetCareTracker
