#include "GameSave.h"

#include <HalStorage.h>
#include <HardwareSerial.h>
#include <Serialization.h>

#include <cstdio>

namespace {
constexpr uint8_t LEVEL_FILE_VERSION = 1;
constexpr char SAVE_DIR[] = "/.crosspoint/game";

void levelPath(uint8_t depth, char* buf, size_t bufSize) {
  snprintf(buf, bufSize, "/.crosspoint/game/level_%02u.bin", depth);
}
}  // namespace

bool GameSave::saveLevel(uint8_t depth, const uint8_t* fogOfWar, const game::Monster* monsters, uint8_t monsterCount,
                         const game::Item* items, uint8_t itemCount) {
  Storage.mkdir(SAVE_DIR);

  char path[48];
  levelPath(depth, path, sizeof(path));

  FsFile file;
  if (!Storage.openFileForWrite("DM", path, file)) {
    Serial.printf("[%lu] [DM ] Failed to write level %u\n", millis(), depth);
    return false;
  }

  serialization::writePod(file, LEVEL_FILE_VERSION);
  serialization::writePod(file, depth);

  // Fog of war bitmap
  file.write(fogOfWar, game::FOG_SIZE);

  // Monsters
  serialization::writePod(file, monsterCount);
  for (uint8_t i = 0; i < monsterCount; i++) {
    serialization::writePod(file, monsters[i]);
  }

  // Items
  serialization::writePod(file, itemCount);
  for (uint8_t i = 0; i < itemCount; i++) {
    serialization::writePod(file, items[i]);
  }

  file.close();
  Serial.printf("[%lu] [DM ] Level %u saved (%u monsters, %u items)\n", millis(), depth, monsterCount, itemCount);
  return true;
}

bool GameSave::loadLevel(uint8_t depth, uint8_t* fogOfWar, game::Monster* monsters, uint8_t& monsterCount,
                         game::Item* items, uint8_t& itemCount) {
  char path[48];
  levelPath(depth, path, sizeof(path));

  FsFile file;
  if (!Storage.openFileForRead("DM", path, file)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version > LEVEL_FILE_VERSION) {
    Serial.printf("[%lu] [DM ] Unknown level version %u\n", millis(), version);
    file.close();
    return false;
  }

  uint8_t savedDepth;
  serialization::readPod(file, savedDepth);

  // Fog of war
  file.read(fogOfWar, game::FOG_SIZE);

  // Monsters
  serialization::readPod(file, monsterCount);
  if (monsterCount > game::MAX_MONSTERS) {
    monsterCount = game::MAX_MONSTERS;
  }
  for (uint8_t i = 0; i < monsterCount; i++) {
    serialization::readPod(file, monsters[i]);
  }

  // Items
  serialization::readPod(file, itemCount);
  if (itemCount > game::MAX_ITEMS_PER_LEVEL) {
    itemCount = game::MAX_ITEMS_PER_LEVEL;
  }
  for (uint8_t i = 0; i < itemCount; i++) {
    serialization::readPod(file, items[i]);
  }

  file.close();
  Serial.printf("[%lu] [DM ] Level %u loaded (%u monsters, %u items)\n", millis(), depth, monsterCount, itemCount);
  return true;
}

bool GameSave::hasLevel(uint8_t depth) {
  char path[48];
  levelPath(depth, path, sizeof(path));
  return Storage.exists(path);
}

void GameSave::deleteLevel(uint8_t depth) {
  char path[48];
  levelPath(depth, path, sizeof(path));
  Storage.remove(path);
}

void GameSave::deleteAll() {
  // Delete save file
  Storage.remove("/.crosspoint/game/save.bin");

  // Delete all level files
  for (uint8_t i = 1; i <= game::MAX_DEPTH; i++) {
    char path[48];
    levelPath(i, path, sizeof(path));
    Storage.remove(path);
  }

  Serial.printf("[%lu] [DM ] All save data deleted\n", millis());
}
