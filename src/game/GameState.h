#pragma once

#include <string>

#include "GameTypes.h"

class GameState {
  static GameState instance;

 public:
  game::Player player;
  game::Item inventory[game::MAX_INVENTORY];
  uint8_t inventoryCount = 0;

  // Message log (ring buffer of recent messages)
  std::string messages[game::MAX_MESSAGES];
  uint8_t messageCount = 0;
  uint8_t messageHead = 0;  // Index of oldest message in ring buffer

  ~GameState() = default;

  static GameState& getInstance() { return instance; }

  // Reset to new game defaults
  void newGame(uint32_t seed);

  // Add a message to the log
  void addMessage(const char* msg);

  // Get the Nth most recent message (0 = most recent)
  const std::string& getMessage(int recencyIndex) const;

  // Persistence
  bool saveToFile() const;
  bool loadFromFile();
  bool hasSaveFile() const;
  void deleteSaveFile() const;
};

#define GAME_STATE GameState::getInstance()
