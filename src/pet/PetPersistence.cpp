#include "PetPersistence.h"

#include <HalStorage.h>
#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <algorithm>

#include "Serialization.h"

namespace {
constexpr const char* kPetDir = "/.crosspoint/pet";
constexpr const char* kPetStatePath = "/.crosspoint/pet/state.bin";
constexpr uint32_t kPetSaveMagic = 0x31544550;  // PET1
constexpr uint8_t kPetSaveVersion = 1;

SemaphoreHandle_t getPetPersistenceMutex() {
  static SemaphoreHandle_t s_mutex = xSemaphoreCreateMutex();
  return s_mutex;
}

class ScopedPetPersistenceLock {
 public:
  ScopedPetPersistenceLock() : mutex_(getPetPersistenceMutex()) {
    if (mutex_ != nullptr) {
      locked_ = (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE);
    }
  }

  ~ScopedPetPersistenceLock() {
    if (locked_ && mutex_ != nullptr) {
      xSemaphoreGive(mutex_);
    }
  }

  bool locked() const { return locked_; }

 private:
  SemaphoreHandle_t mutex_ = nullptr;
  bool locked_ = false;
};

template <typename T>
bool readPodChecked(FsFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

template <typename T>
bool writePodChecked(FsFile& file, const T& value) {
  return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

bool isReasonablePetNameChar(char ch) {
  // Keep names user-friendly but resilient: allow printable ASCII.
  return ch >= 32 && ch <= 126;
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
  ScopedPetPersistenceLock lock;
  if (!lock.locked()) {
    Serial.printf("[PET] Failed to acquire persistence lock for load\n");
    return false;
  }

  FsFile f;
  if (!Storage.openFileForRead("PET", kPetStatePath, f)) {
    Serial.printf("[PET] No save file found at %s - starting fresh\n", kPetStatePath);
    return false;
  }
  Serial.printf("[PET] Loading pet state from %s\n", kPetStatePath);

  state = PetState{};

  uint32_t header = 0;
  if (!readPodChecked(f, header)) {
    f.close();
    Serial.printf("[PET] Failed reading save header - starting fresh\n");
    return false;
  }

  if (header == kPetSaveMagic) {
    uint8_t version = 0;
    if (!readPodChecked(f, version)) {
      f.close();
      Serial.printf("[PET] Failed reading save version - starting fresh\n");
      return false;
    }
    if (version != kPetSaveVersion) {
      f.close();
      Serial.printf("[PET] Unsupported save version %u - starting fresh\n", static_cast<unsigned>(version));
      return false;
    }

    if (!readPodChecked(f, state.initialized) || !readPodChecked(f, state.alive) || !readPodChecked(f, state.stage) ||
        !readPodChecked(f, state.mood)) {
      f.close();
      Serial.printf("[PET] Save file truncated in core state - starting fresh\n");
      return false;
    }
  } else {
    const uint8_t* legacy = reinterpret_cast<const uint8_t*>(&header);
    state.initialized = (legacy[0] != 0);
    state.alive = (legacy[1] != 0);
    state.stage = static_cast<PetStage>(legacy[2]);
    state.mood = static_cast<PetMood>(legacy[3]);
  }

  state.petType = 0;
  if (!readPodChecked(f, state.petType) || !readPodChecked(f, state.petName)) {
    f.close();
    Serial.printf("[PET] Save file truncated in identity fields - starting fresh\n");
    return false;
  }

  if (!readPodChecked(f, state.hunger) || !readPodChecked(f, state.happiness) || !readPodChecked(f, state.health) ||
      !readPodChecked(f, state.energy) || !readPodChecked(f, state.discipline) || !readPodChecked(f, state.weight)) {
    f.close();
    Serial.printf("[PET] Save file truncated in stats - starting fresh\n");
    return false;
  }

  if (!readPodChecked(f, state.birthTime) || !readPodChecked(f, state.lastTickTime) ||
      !readPodChecked(f, state.lastDecayMs) || !readPodChecked(f, state.readPages) ||
      !readPodChecked(f, state.totalPagesRead) || !readPodChecked(f, state.mealsFromReading) ||
      !readPodChecked(f, state.booksFinished) || !readPodChecked(f, state.chaptersFinished)) {
    f.close();
    Serial.printf("[PET] Save file truncated in progress fields - starting fresh\n");
    return false;
  }

  if (f.available() > 0 && !readPodChecked(f, state.currentStreak)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.daysAtStage)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.lastReadDay)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.pageAccumulator)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.missionDay)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.missionPagesRead)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.missionPetCount)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.isSick)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.sicknessTimer)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.wasteCount)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.mealsSinceClean)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.attentionCall)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.isFakeCall)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.currentNeed)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.lastCallTime)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.isSleeping)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.lightsOff)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.totalAge)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.careMistakes)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.avgCareScore)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.evolutionVariant)) {
    f.close();
    return false;
  }
  if (f.available() > 0 && !readPodChecked(f, state.streakTier)) {
    f.close();
    return false;
  }

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
  ScopedPetPersistenceLock lock;
  if (!lock.locked()) {
    Serial.printf("[PET] Failed to acquire persistence lock for save\n");
    return false;
  }

  Storage.mkdir(kPetDir);

  FsFile f;
  if (!Storage.openFileForWrite("PET", kPetStatePath, f)) {
    Serial.printf("[%lu] [PET] ERROR: Failed to open save file for writing\n", millis());
    return false;
  }
  Serial.printf("[PET] Saving pet state (alive=%d stage=%d hunger=%d)\n",
                static_cast<int>(state.alive), static_cast<int>(state.stage),
                static_cast<int>(state.hunger));

  bool ok = true;
  ok = ok && writePodChecked(f, kPetSaveMagic);
  ok = ok && writePodChecked(f, kPetSaveVersion);

  ok = ok && writePodChecked(f, state.initialized);
  ok = ok && writePodChecked(f, state.alive);
  ok = ok && writePodChecked(f, state.stage);
  ok = ok && writePodChecked(f, state.mood);
  ok = ok && writePodChecked(f, state.petType);
  ok = ok && writePodChecked(f, state.petName);

  ok = ok && writePodChecked(f, state.hunger);
  ok = ok && writePodChecked(f, state.happiness);
  ok = ok && writePodChecked(f, state.health);
  ok = ok && writePodChecked(f, state.energy);
  ok = ok && writePodChecked(f, state.discipline);
  ok = ok && writePodChecked(f, state.weight);

  ok = ok && writePodChecked(f, state.birthTime);
  ok = ok && writePodChecked(f, state.lastTickTime);
  ok = ok && writePodChecked(f, state.lastDecayMs);
  ok = ok && writePodChecked(f, state.readPages);
  ok = ok && writePodChecked(f, state.totalPagesRead);
  ok = ok && writePodChecked(f, state.mealsFromReading);
  ok = ok && writePodChecked(f, state.booksFinished);
  ok = ok && writePodChecked(f, state.chaptersFinished);

  ok = ok && writePodChecked(f, state.currentStreak);
  ok = ok && writePodChecked(f, state.daysAtStage);
  ok = ok && writePodChecked(f, state.lastReadDay);
  ok = ok && writePodChecked(f, state.pageAccumulator);
  ok = ok && writePodChecked(f, state.missionDay);
  ok = ok && writePodChecked(f, state.missionPagesRead);
  ok = ok && writePodChecked(f, state.missionPetCount);
  ok = ok && writePodChecked(f, state.isSick);
  ok = ok && writePodChecked(f, state.sicknessTimer);
  ok = ok && writePodChecked(f, state.wasteCount);
  ok = ok && writePodChecked(f, state.mealsSinceClean);
  ok = ok && writePodChecked(f, state.attentionCall);
  ok = ok && writePodChecked(f, state.isFakeCall);
  ok = ok && writePodChecked(f, state.currentNeed);
  ok = ok && writePodChecked(f, state.lastCallTime);
  ok = ok && writePodChecked(f, state.isSleeping);
  ok = ok && writePodChecked(f, state.lightsOff);
  ok = ok && writePodChecked(f, state.totalAge);
  ok = ok && writePodChecked(f, state.careMistakes);
  ok = ok && writePodChecked(f, state.avgCareScore);
  ok = ok && writePodChecked(f, state.evolutionVariant);
  ok = ok && writePodChecked(f, state.streakTier);

  if (!ok) {
    f.close();
    Serial.printf("[PET] ERROR: Failed while writing pet save\n");
    return false;
  }

  f.close();
  Serial.printf("[PET] Pet state saved successfully\n");
  return true;
}

bool clearState() {
  ScopedPetPersistenceLock lock;
  if (!lock.locked()) {
    Serial.printf("[PET] Failed to acquire persistence lock for clear\n");
    return false;
  }

  if (Storage.exists(kPetStatePath)) {
    const bool removed = Storage.remove(kPetStatePath);
    Serial.printf("[PET] Clear pet state file: %s\n", removed ? "removed" : "failed");
    return removed;
  }
  return true;
}

}  // namespace pet
