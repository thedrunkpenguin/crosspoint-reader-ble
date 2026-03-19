#pragma once

#include <cstdint>

#include "PetState.h"

namespace PetCareTracker {
void checkCareMistakes(PetState& state, uint32_t elapsedHours);
void generateAttentionCall(PetState& state, uint32_t nowSec);
void expireAttentionCall(PetState& state, uint32_t nowSec);
void updateCareScore(PetState& state);
}
