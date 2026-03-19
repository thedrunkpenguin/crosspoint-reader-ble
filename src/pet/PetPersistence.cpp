#include "PetPersistence.h"

#include <HalStorage.h>

#include "Serialization.h"

namespace {
constexpr const char* kPetDir = "/.crosspoint/pet";
constexpr const char* kPetStatePath = "/.crosspoint/pet/state.bin";
}

namespace pet {

bool loadState(PetState& state) {
  FsFile f;
  if (!Storage.openFileForRead("PET", kPetStatePath, f)) {
    return false;
  }

  state = PetState{};

  serialization::readPod(f, state.initialized);
  state.petType = 0;
  serialization::readPod(f, state.alive);
  serialization::readPod(f, state.stage);
  serialization::readPod(f, state.mood);
  serialization::readPod(f, state.petType);
  serialization::readPod(f, state.petName);

  serialization::readPod(f, state.hunger);
  serialization::readPod(f, state.happiness);
  serialization::readPod(f, state.health);
  serialization::readPod(f, state.energy);
  serialization::readPod(f, state.discipline);
  serialization::readPod(f, state.weight);

  serialization::readPod(f, state.birthTime);
  serialization::readPod(f, state.lastTickTime);
  serialization::readPod(f, state.lastDecayMs);
  serialization::readPod(f, state.readPages);
  serialization::readPod(f, state.totalPagesRead);
  serialization::readPod(f, state.mealsFromReading);
  serialization::readPod(f, state.booksFinished);
  serialization::readPod(f, state.chaptersFinished);

  if (f.available() > 0) serialization::readPod(f, state.currentStreak);
  if (f.available() > 0) serialization::readPod(f, state.daysAtStage);
  if (f.available() > 0) serialization::readPod(f, state.lastReadDay);
  if (f.available() > 0) serialization::readPod(f, state.pageAccumulator);
  if (f.available() > 0) serialization::readPod(f, state.missionDay);
  if (f.available() > 0) serialization::readPod(f, state.missionPagesRead);
  if (f.available() > 0) serialization::readPod(f, state.missionPetCount);
  if (f.available() > 0) serialization::readPod(f, state.isSick);
  if (f.available() > 0) serialization::readPod(f, state.sicknessTimer);
  if (f.available() > 0) serialization::readPod(f, state.wasteCount);
  if (f.available() > 0) serialization::readPod(f, state.mealsSinceClean);
  if (f.available() > 0) serialization::readPod(f, state.attentionCall);
  if (f.available() > 0) serialization::readPod(f, state.isFakeCall);
  if (f.available() > 0) serialization::readPod(f, state.currentNeed);
  if (f.available() > 0) serialization::readPod(f, state.lastCallTime);
  if (f.available() > 0) serialization::readPod(f, state.isSleeping);
  if (f.available() > 0) serialization::readPod(f, state.lightsOff);
  if (f.available() > 0) serialization::readPod(f, state.totalAge);
  if (f.available() > 0) serialization::readPod(f, state.careMistakes);
  if (f.available() > 0) serialization::readPod(f, state.avgCareScore);
  if (f.available() > 0) serialization::readPod(f, state.evolutionVariant);
  if (f.available() > 0) serialization::readPod(f, state.streakTier);

  f.close();
  if (!state.initialized && state.alive) {
    state.initialized = true;
  }
  if (state.totalPagesRead == 0) {
    state.totalPagesRead = state.readPages;
  }
  if (state.stage == PetStage::Dead) {
    state.alive = false;
  }
  return true;
}

bool saveState(const PetState& state) {
  if (!Storage.exists(kPetDir)) {
    Storage.mkdir(kPetDir);
  }

  FsFile f;
  if (!Storage.openFileForWrite("PET", kPetStatePath, f)) {
    return false;
  }

  serialization::writePod(f, state.initialized);
  serialization::writePod(f, state.alive);
  serialization::writePod(f, state.stage);
  serialization::writePod(f, state.mood);
  serialization::writePod(f, state.petType);
  serialization::writePod(f, state.petName);

  serialization::writePod(f, state.hunger);
  serialization::writePod(f, state.happiness);
  serialization::writePod(f, state.health);
  serialization::writePod(f, state.energy);
  serialization::writePod(f, state.discipline);
  serialization::writePod(f, state.weight);

  serialization::writePod(f, state.birthTime);
  serialization::writePod(f, state.lastTickTime);
  serialization::writePod(f, state.lastDecayMs);
  serialization::writePod(f, state.readPages);
  serialization::writePod(f, state.totalPagesRead);
  serialization::writePod(f, state.mealsFromReading);
  serialization::writePod(f, state.booksFinished);
  serialization::writePod(f, state.chaptersFinished);

  serialization::writePod(f, state.currentStreak);
  serialization::writePod(f, state.daysAtStage);
  serialization::writePod(f, state.lastReadDay);
  serialization::writePod(f, state.pageAccumulator);
  serialization::writePod(f, state.missionDay);
  serialization::writePod(f, state.missionPagesRead);
  serialization::writePod(f, state.missionPetCount);
  serialization::writePod(f, state.isSick);
  serialization::writePod(f, state.sicknessTimer);
  serialization::writePod(f, state.wasteCount);
  serialization::writePod(f, state.mealsSinceClean);
  serialization::writePod(f, state.attentionCall);
  serialization::writePod(f, state.isFakeCall);
  serialization::writePod(f, state.currentNeed);
  serialization::writePod(f, state.lastCallTime);
  serialization::writePod(f, state.isSleeping);
  serialization::writePod(f, state.lightsOff);
  serialization::writePod(f, state.totalAge);
  serialization::writePod(f, state.careMistakes);
  serialization::writePod(f, state.avgCareScore);
  serialization::writePod(f, state.evolutionVariant);
  serialization::writePod(f, state.streakTier);

  f.close();
  return true;
}

}  // namespace pet
