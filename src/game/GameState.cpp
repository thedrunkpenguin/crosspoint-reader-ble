#include "GameState.h"

#include <HalStorage.h>
#include <HardwareSerial.h>
#include <Serialization.h>

#include <cstring>

namespace {
constexpr uint8_t SAVE_FILE_VERSION = 1;
constexpr char SAVE_DIR[] = "/.crosspoint/game";
constexpr char SAVE_FILE[] = "/.crosspoint/game/save.bin";
}  // namespace

GameState GameState::instance;

void GameState::newGame(uint32_t seed) {
  player = game::Player{};
  player.gameSeed = seed;
  player.hp = 20;
  player.maxHp = 20;
  player.mp = 5;
  player.maxMp = 5;
  player.strength = 10;
  player.dexterity = 10;
  player.constitution = 10;
  player.intelligence = 10;
  player.charLevel = 1;
  player.experience = 0;
  player.gold = 0;
  player.dungeonDepth = 1;
  player.turnCount = 0;

  inventoryCount = 0;
  memset(inventory, 0, sizeof(inventory));

  messageCount = 0;
  messageHead = 0;
  for (auto& msg : messages) {
    msg.clear();
  }

  addMessage("You enter the Deep Mines...");
}

void GameState::addMessage(const char* msg) {
  if (messageCount < game::MAX_MESSAGES) {
    // Buffer not full yet — append at next slot
    int idx = (messageHead + messageCount) % game::MAX_MESSAGES;
    messages[idx] = msg;
    messageCount++;
  } else {
    // Buffer full — overwrite oldest, advance head
    messages[messageHead] = msg;
    messageHead = (messageHead + 1) % game::MAX_MESSAGES;
  }
}

const std::string& GameState::getMessage(int recencyIndex) const {
  static const std::string empty;
  if (recencyIndex < 0 || recencyIndex >= messageCount) {
    return empty;
  }
  // Most recent is at (head + count - 1), go backwards by recencyIndex
  int idx = (messageHead + messageCount - 1 - recencyIndex) % game::MAX_MESSAGES;
  return messages[idx];
}

bool GameState::saveToFile() const {
  Storage.mkdir(SAVE_DIR);

  FsFile file;
  if (!Storage.openFileForWrite("DM", SAVE_FILE, file)) {
    Serial.printf("[%lu] [DM ] Failed to open save file for writing\n", millis());
    return false;
  }

  serialization::writePod(file, SAVE_FILE_VERSION);

  // Player struct (written as raw POD)
  serialization::writePod(file, player);

  // Inventory
  serialization::writePod(file, inventoryCount);
  for (uint8_t i = 0; i < inventoryCount; i++) {
    serialization::writePod(file, inventory[i]);
  }

  // Message log
  serialization::writePod(file, messageCount);
  serialization::writePod(file, messageHead);
  for (uint8_t i = 0; i < messageCount; i++) {
    int idx = (messageHead + i) % game::MAX_MESSAGES;
    serialization::writeString(file, messages[idx]);
  }

  file.close();
  Serial.printf("[%lu] [DM ] Game saved (depth %u, turn %u)\n", millis(), player.dungeonDepth, player.turnCount);
  return true;
}

bool GameState::loadFromFile() {
  FsFile file;
  if (!Storage.openFileForRead("DM", SAVE_FILE, file)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version > SAVE_FILE_VERSION) {
    Serial.printf("[%lu] [DM ] Unknown save version %u\n", millis(), version);
    file.close();
    return false;
  }

  // Player
  serialization::readPod(file, player);

  // Inventory
  serialization::readPod(file, inventoryCount);
  if (inventoryCount > game::MAX_INVENTORY) {
    inventoryCount = game::MAX_INVENTORY;
  }
  for (uint8_t i = 0; i < inventoryCount; i++) {
    serialization::readPod(file, inventory[i]);
  }

  // Message log
  serialization::readPod(file, messageCount);
  serialization::readPod(file, messageHead);
  if (messageCount > game::MAX_MESSAGES) {
    messageCount = game::MAX_MESSAGES;
  }
  for (auto& msg : messages) {
    msg.clear();
  }
  for (uint8_t i = 0; i < messageCount; i++) {
    int idx = (messageHead + i) % game::MAX_MESSAGES;
    serialization::readString(file, messages[idx]);
  }

  file.close();
  Serial.printf("[%lu] [DM ] Game loaded (depth %u, turn %u)\n", millis(), player.dungeonDepth, player.turnCount);
  return true;
}

bool GameState::hasSaveFile() const {
  return Storage.exists(SAVE_FILE);
}

void GameState::deleteSaveFile() const {
  Storage.remove(SAVE_FILE);
  Serial.printf("[%lu] [DM ] Save file deleted\n", millis());
}
