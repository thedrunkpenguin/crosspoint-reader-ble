#pragma once

#include "PetState.h"

namespace pet {

bool loadState(PetState& state);
bool saveState(const PetState& state);

}  // namespace pet
