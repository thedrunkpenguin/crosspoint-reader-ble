#pragma once

#include <cstdint>

#include "PetState.h"

namespace PetDecayEngine {
void applyDecay(PetState& state, uint32_t elapsedHours, uint8_t startHour);
}
