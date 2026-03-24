#include "PetPersistence.h"

#include <HalStorage.h>
#include <HardwareSerial.h>

#include "Serialization.h"

namespace {
constexpr const char* kPetDir = "/.crosspoint/pet";
constexpr const char* kPetStatePath = "/.crosspoint/pet/state.bin";
constexpr uint32_t kPetSaveMagic = 0x31544550;  // PET1
constexpr uint8_t kPetSaveVersion = 1;

bool isReasonablePetNameChar(char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '-' ||
         ch == '_' || ch == '\'';
}

void sanitizePetState(PetState& state) {
  state.petName[sizeof(state.petName) - 1] = '\0';

  bool validName = true;
  bool anyVisible = false;
  for (size_t i = 0; i < sizeof(state.petName) && state.petName[i] != '\0'; ++i) {
    const char ch = state.petName[i];
    if (!isReasonablePetNameChar(ch)) {
      validName = false;
      break;
    }
    if (ch != ' ') {
      anyVisible = true;
    }
  }
  if (!validName || !anyVisible) {
    state.petName[0] = '\0';
  }

  if (state.petType >= PetTypeNames::COUNT) {
    state.petType = 0;
  }

  if (static_cast<uint8_t>(state.stage) > static_cast<uint8_t>(PetStage::Dead)) {
    state.stage = state.alive ? PetStage::Egg : PetStage::Dead;
  }

  if (static_cast<uint8_t>(state.mood) > static_cast<uint8_t>(PetMood::Refusing)) {
    state.mood = state.alive ? PetMood::Normal : PetMood::Dead;
  }

  state.hunger = std::max(0, std::min(100, state.hunger));
  state.happiness = std::max(0, std::min(100, state.happiness));
  state.health = std::max(0, std::min(100, state.health));
  state.energy = std::max(0, std::min(100, state.energy));
  state.discipline = std::max(0, std::min(100, state.discipline));
  state.weight = std::max(0, std::min(100, state.weight));
}
}

namespace pet {

bool loadState(PetState& state) {
  FsFile f;
  if (!Storage.openFileForRead("PET", kPetStatePath, f)) {
    Serial.printf("[PET] No save file found at %s - starting fresh\n", kPetStatePath);
    return false;
  }
  Serial.printf("[PET] Loading pet state from %s\n", kPetStatePath);

  state = PetState{};

  uint32_t header = 0;
  serialization::readPod(f, header);

  if (header == kPetSaveMagic) {
    uint8_t version = 0;
    serialization::readPod(f, version);
    if (version != kPetSaveVersion) {
      f.close();
      Serial.printf("[PET] Unsupported save version %u - starting fresh\n", static_cast<unsigned>(version));
      return false;
    }

    serialization::readPod(f, state.initialized);
    serialization::readPod(f, state.alive);
    serialization::readPod(f, state.stage);
    serialization::readPod(f, state.mood);
  } else {
    const uint8_t* legacy = reinterpret_cast<const uint8_t*>(&header);
    state.initialized = (legacy[0] != 0);
    state.alive = (legacy[1] != 0);
    state.stage = static_cast<PetStage>(legacy[2]);
    state.mood = static_cast<PetMood>(legacy[3]);
  }

  state.petType = 0;
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
  sanitizePetState(state);
  Serial.printf("[PET] Pet state loaded (alive=%d stage=%d hunger=%d)\n",
                static_cast<int>(state.alive), static_cast<int>(state.stage),
                static_cast<int>(state.hunger));
  return true;
}

bool saveState(const PetState& state) {
  Storage.mkdir(kPetDir);

  FsFile f;
  if (!Storage.openFileForWrite("PET", kPetStatePath, f)) {
    Serial.printf("[%lu] [PET] ERROR: Failed to open save file for writing\n", millis());
    return false;
  }
  Serial.printf("[PET] Saving pet state (alive=%d stage=%d hunger=%d)\n",
                static_cast<int>(state.alive), static_cast<int>(state.stage),
                static_cast<int>(state.hunger));

  serialization::writePod(f, kPetSaveMagic);
  serialization::writePod(f, kPetSaveVersion);

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
  Serial.printf("[PET] Pet state saved successfully\n");
  return true;
}

bool clearState() {
  if (Storage.exists(kPetStatePath)) {
    const bool removed = Storage.remove(kPetStatePath);
    Serial.printf("[PET] Clear pet state file: %s\n", removed ? "removed" : "failed");
    return removed;
  }
  return true;
}

}  // namespace pet
