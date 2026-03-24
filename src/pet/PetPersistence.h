#pragma once

#include "PetState.h"

namespace pet {

bool loadState(PetState& state);
bool saveState(const PetState& state);
bool clearState();

}  // namespace pet
