#pragma once

#include "PetState.h"

namespace PetEvolution {
void checkEvolution(PetState& state);
const char* variantStageName(PetStage stage, uint8_t variant);
const char* typeName(uint8_t type);
}
