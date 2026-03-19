#include "GameActivity.h"

#include <HalDisplay.h>

#include <algorithm>
#include <cstring>

#include "GameMenuActivity.h"
#include "MappedInputManager.h"
#include "game/GameSave.h"

// --- FOV ---

namespace {
constexpr int FOV_RADIUS = 8;

// Bresenham line-of-sight: returns true if no wall blocks the line from (x0,y0) to (x1,y1)
bool hasLineOfSight(const game::Tile* tiles, int x0, int y0, int x1, int y1) {
  int dx = abs(x1 - x0);
  int dy = -abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1;
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  int cx = x0, cy = y0;
  while (true) {
    // Skip the starting tile check
    if ((cx != x0 || cy != y0) && (cx != x1 || cy != y1)) {
      if (tiles[cy * game::MAP_WIDTH + cx] == game::Tile::Wall) {
        return false;
      }
    }
    if (cx == x1 && cy == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      cx += sx;
    }
    if (e2 <= dx) {
      err += dx;
      cy += sy;
    }
  }
  return true;
}

// Calculate total attack bonus from equipped weapons
int equippedAttackBonus() {
  int bonus = 0;
  for (uint8_t i = 0; i < GAME_STATE.inventoryCount; i++) {
    const auto& item = GAME_STATE.inventory[i];
    if (item.flags & static_cast<uint8_t>(game::ItemFlag::Equipped)) {
      for (int d = 0; d < game::ITEM_DEF_COUNT; d++) {
        if (game::ITEM_DEFS[d].type == item.type && game::ITEM_DEFS[d].subtype == item.subtype) {
          bonus += game::ITEM_DEFS[d].attack + item.enchantment;
          break;
        }
      }
    }
  }
  return bonus;
}

// Calculate total defense bonus from equipped armor/shields
int equippedDefenseBonus() {
  int bonus = 0;
  for (uint8_t i = 0; i < GAME_STATE.inventoryCount; i++) {
    const auto& item = GAME_STATE.inventory[i];
    if (item.flags & static_cast<uint8_t>(game::ItemFlag::Equipped)) {
      for (int d = 0; d < game::ITEM_DEF_COUNT; d++) {
        if (game::ITEM_DEFS[d].type == item.type && game::ITEM_DEFS[d].subtype == item.subtype) {
          bonus += game::ITEM_DEFS[d].defense + item.enchantment;
          break;
        }
      }
    }
  }
  return bonus;
}

// Check if a tile is walkable for monsters
bool isWalkable(game::Tile tile) {
  return tile == game::Tile::Floor || tile == game::Tile::DoorOpen || tile == game::Tile::StairsUp ||
         tile == game::Tile::StairsDown || tile == game::Tile::Rubble;
}

}  // namespace

// --- Lifecycle ---

void GameActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  gameRenderer.init(renderer);

  // Load save or start new game
  if (GAME_STATE.hasSaveFile()) {
    GAME_STATE.loadFromFile();
  }
  // If no save was loaded, newGame() was already called before entering this activity

  loadOrGenerateLevel();
  computeVisibility();
  updateRequired = true;

  xTaskCreate(&GameActivity::taskTrampoline, "GameTask", 8192, this, 1, &displayTaskHandle);
}

void GameActivity::onExit() {
  ActivityWithSubactivity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

// --- Render Task ---

void GameActivity::taskTrampoline(void* param) {
  static_cast<GameActivity*>(param)->displayTaskLoop();
}

void GameActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void GameActivity::render() {
  gameRenderer.draw(renderer, tiles, fogOfWar, monsters, monsterCount, levelItems, itemCount, visible);
}

// --- Input ---

void GameActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  using Button = MappedInputManager::Button;

  if (mappedInput.wasReleased(Button::Up)) {
    handleMove(0, -1);
  } else if (mappedInput.wasReleased(Button::Down)) {
    handleMove(0, 1);
  } else if (mappedInput.wasReleased(Button::Left)) {
    handleMove(-1, 0);
  } else if (mappedInput.wasReleased(Button::Right)) {
    handleMove(1, 0);
  } else if (mappedInput.wasReleased(Button::Confirm)) {
    handleAction();
  } else if (mappedInput.wasReleased(Button::Back)) {
    openGameMenu();
  }
}

// --- Game Menu ---

void GameActivity::openGameMenu() {
  auto onResume = [this] {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    exitActivity();
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
  };

  auto onSaveQuit = [this] {
    saveCurrentLevel();
    GAME_STATE.saveToFile();
    GAME_STATE.addMessage("Game saved.");
    onGoHome();
  };

  auto onAbandon = [this] {
    GameSave::deleteAll();
    GAME_STATE.deleteSaveFile();
    onGoHome();
  };

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  enterNewActivity(new GameMenuActivity(renderer, mappedInput, onResume, onSaveQuit, onAbandon));
  xSemaphoreGive(renderingMutex);
}

// --- Movement ---

void GameActivity::handleMove(int dx, int dy) {
  auto& p = GAME_STATE.player;

  // Dead players don't move
  if (p.hp == 0) return;

  int newX = p.x + dx;
  int newY = p.y + dy;

  // Bounds check
  if (newX < 0 || newX >= game::MAP_WIDTH || newY < 0 || newY >= game::MAP_HEIGHT) return;

  game::Tile target = tiles[newY * game::MAP_WIDTH + newX];

  // Wall blocks movement
  if (target == game::Tile::Wall) return;
  if (target == game::Tile::DoorClosed) {
    // Open the door
    tiles[newY * game::MAP_WIDTH + newX] = game::Tile::DoorOpen;
    GAME_STATE.addMessage("You open the door.");
    p.turnCount++;
    processMonsterTurns();
    computeVisibility();
    updateRequired = true;
    return;
  }

  // Check for monster at target position
  for (uint8_t i = 0; i < monsterCount; i++) {
    if (monsters[i].x == newX && monsters[i].y == newY && monsters[i].hp > 0) {
      // Melee attack (strength + weapon bonus vs monster defense)
      const auto& def = game::MONSTER_DEFS[monsters[i].type];
      int atkPower = static_cast<int>(p.strength) + equippedAttackBonus();
      int damage = std::max(1, atkPower - static_cast<int>(def.defense));
      // Add some variance
      game::Rng rng(p.turnCount ^ (p.x * 31 + p.y * 37));
      damage = std::max(1, damage + rng.nextRangeInclusive(-damage / 4, damage / 4));

      monsters[i].hp = (damage >= monsters[i].hp) ? 0 : monsters[i].hp - damage;

      char msgBuf[64];
      if (monsters[i].hp == 0) {
        snprintf(msgBuf, sizeof(msgBuf), "You slay the %s! (+%uXP)", def.name, def.expValue);
        GAME_STATE.addMessage(msgBuf);
        p.experience += def.expValue;
        checkLevelUp();

        // Boss drops the Ring of Power
        if (monsters[i].type == game::BOSS_MONSTER_TYPE && itemCount < game::MAX_ITEMS_PER_LEVEL) {
          auto& ring = levelItems[itemCount];
          ring.x = monsters[i].x;
          ring.y = monsters[i].y;
          ring.type = game::ITEM_DEFS[game::RING_OF_POWER_DEF].type;
          ring.subtype = game::ITEM_DEFS[game::RING_OF_POWER_DEF].subtype;
          ring.count = 1;
          ring.enchantment = 0;
          ring.flags = 0;
          itemCount++;
          GAME_STATE.addMessage("Something glints on the ground...");
        }
      } else {
        snprintf(msgBuf, sizeof(msgBuf), "You hit the %s for %d.", def.name, damage);
        GAME_STATE.addMessage(msgBuf);
      }

      // Monster becomes hostile
      monsters[i].state = static_cast<uint8_t>(game::MonsterState::Hostile);

      p.turnCount++;
      processMonsterTurns();
      computeVisibility();
      updateRequired = true;
      return;
    }
  }

  // Move player
  p.x = static_cast<int16_t>(newX);
  p.y = static_cast<int16_t>(newY);
  p.turnCount++;

  processMonsterTurns();
  computeVisibility();
  updateRequired = true;
}

// --- Action (Confirm button) ---

void GameActivity::handleAction() {
  auto& p = GAME_STATE.player;

  // Dead players can't act
  if (p.hp == 0) {
    handlePlayerDeath();
    return;
  }

  int mapIdx = p.y * game::MAP_WIDTH + p.x;
  game::Tile here = tiles[mapIdx];

  // Stairs
  if (here == game::Tile::StairsDown) {
    if (p.dungeonDepth >= game::MAX_DEPTH) {
      GAME_STATE.addMessage("The stairs are blocked by rubble.");
      updateRequired = true;
      return;
    }
    saveCurrentLevel();
    p.dungeonDepth++;
    GAME_STATE.addMessage("You descend deeper...");
    loadOrGenerateLevel();
    // Place player at stairs up on new level
    for (int i = 0; i < game::MAP_SIZE; i++) {
      if (tiles[i] == game::Tile::StairsUp) {
        p.x = static_cast<int16_t>(i % game::MAP_WIDTH);
        p.y = static_cast<int16_t>(i / game::MAP_WIDTH);
        break;
      }
    }
    computeVisibility();
    updateRequired = true;
    return;
  }

  if (here == game::Tile::StairsUp) {
    if (p.dungeonDepth <= 1) {
      GAME_STATE.addMessage("You see daylight above... but the mines call.");
      updateRequired = true;
      return;
    }
    saveCurrentLevel();
    p.dungeonDepth--;
    GAME_STATE.addMessage("You ascend...");
    loadOrGenerateLevel();
    // Place player at stairs down on previous level
    for (int i = 0; i < game::MAP_SIZE; i++) {
      if (tiles[i] == game::Tile::StairsDown) {
        p.x = static_cast<int16_t>(i % game::MAP_WIDTH);
        p.y = static_cast<int16_t>(i / game::MAP_WIDTH);
        break;
      }
    }
    computeVisibility();
    updateRequired = true;
    return;
  }

  // Pick up items
  for (uint8_t i = 0; i < itemCount; i++) {
    if (levelItems[i].x == p.x && levelItems[i].y == p.y) {
      // Check if this is the Ring of Power — victory!
      const auto& ringDef = game::ITEM_DEFS[game::RING_OF_POWER_DEF];
      if (levelItems[i].type == ringDef.type && levelItems[i].subtype == ringDef.subtype) {
        GAME_STATE.addMessage("You claim the Ring of Power!");
        GAME_STATE.addMessage("The mines tremble... You have won!");
        handleVictory();
        return;
      }

      // Gold goes straight to purse
      if (static_cast<game::ItemType>(levelItems[i].type) == game::ItemType::Gold) {
        uint16_t amount = levelItems[i].count * 10;
        p.gold += amount;
        char msgBuf[48];
        snprintf(msgBuf, sizeof(msgBuf), "You pick up %u gold.", amount);
        GAME_STATE.addMessage(msgBuf);
      } else {
        if (GAME_STATE.inventoryCount >= game::MAX_INVENTORY) {
          GAME_STATE.addMessage("Your pack is full!");
          updateRequired = true;
          return;
        }
        // Move item to inventory
        GAME_STATE.inventory[GAME_STATE.inventoryCount] = levelItems[i];
        GAME_STATE.inventory[GAME_STATE.inventoryCount].x = -1;
        GAME_STATE.inventory[GAME_STATE.inventoryCount].y = -1;
        GAME_STATE.inventoryCount++;

        // Find matching item def for message
        char msgBuf[64];
        for (int d = 0; d < game::ITEM_DEF_COUNT; d++) {
          if (game::ITEM_DEFS[d].type == levelItems[i].type && game::ITEM_DEFS[d].subtype == levelItems[i].subtype) {
            snprintf(msgBuf, sizeof(msgBuf), "You pick up %s.", game::ITEM_DEFS[d].name);
            GAME_STATE.addMessage(msgBuf);
            break;
          }
        }
      }

      // Remove from level by swapping with last
      levelItems[i] = levelItems[itemCount - 1];
      itemCount--;

      p.turnCount++;
      processMonsterTurns();
      updateRequired = true;
      return;
    }
  }

  GAME_STATE.addMessage("Nothing to do here.");
  updateRequired = true;
}

// --- Monster AI ---

void GameActivity::processMonsterTurns() {
  auto& p = GAME_STATE.player;
  if (p.hp == 0) return;

  for (uint8_t i = 0; i < monsterCount; i++) {
    auto& m = monsters[i];
    if (m.hp == 0) continue;

    int dx = static_cast<int>(p.x) - m.x;
    int dy = static_cast<int>(p.y) - m.y;
    int dist2 = dx * dx + dy * dy;

    // State transitions
    auto state = static_cast<game::MonsterState>(m.state);

    if (state == game::MonsterState::Asleep) {
      // Wake up if player is nearby and visible
      if (dist2 <= FOV_RADIUS * FOV_RADIUS && visible[m.y * game::MAP_WIDTH + m.x]) {
        // Wake chance based on distance — closer = more likely
        game::Rng rng(p.turnCount ^ (m.x * 17 + m.y * 13 + i));
        int wakeChance = 80 - dist2;  // Very likely when close
        if (static_cast<int>(rng.nextRange(100)) < wakeChance) {
          m.state = static_cast<uint8_t>(game::MonsterState::Hostile);
          state = game::MonsterState::Hostile;
        }
      }
      continue;  // Asleep monsters don't act
    }

    if (state == game::MonsterState::Wandering) {
      // Become hostile if player is visible and close
      if (dist2 <= FOV_RADIUS * FOV_RADIUS && visible[m.y * game::MAP_WIDTH + m.x]) {
        m.state = static_cast<uint8_t>(game::MonsterState::Hostile);
        state = game::MonsterState::Hostile;
      } else {
        // Random wander
        game::Rng rng(p.turnCount ^ (m.x * 23 + m.y * 29 + i * 7));
        int dir = rng.nextRange(4);
        int wmx = m.x + ((dir == 0) ? 1 : (dir == 1) ? -1 : 0);
        int wmy = m.y + ((dir == 2) ? 1 : (dir == 3) ? -1 : 0);

        if (wmx >= 0 && wmx < game::MAP_WIDTH && wmy >= 0 && wmy < game::MAP_HEIGHT) {
          if (isWalkable(tiles[wmy * game::MAP_WIDTH + wmx])) {
            // Don't walk onto player or other monsters
            bool blocked = (wmx == p.x && wmy == p.y);
            if (!blocked) {
              for (uint8_t j = 0; j < monsterCount && !blocked; j++) {
                if (j != i && monsters[j].hp > 0 && monsters[j].x == wmx && monsters[j].y == wmy) {
                  blocked = true;
                }
              }
            }
            if (!blocked) {
              m.x = static_cast<int16_t>(wmx);
              m.y = static_cast<int16_t>(wmy);
            }
          }
        }
        continue;
      }
    }

    // Hostile: move toward player or attack
    if (state == game::MonsterState::Hostile) {
      // Adjacent to player? Attack!
      if (abs(dx) <= 1 && abs(dy) <= 1 && dist2 <= 2) {
        monsterAttackPlayer(m);
        if (p.hp == 0) return;  // Player died
        continue;
      }

      // Move toward player (simple greedy pathfinding)
      int bestX = m.x;
      int bestY = m.y;
      int bestDist = dist2;

      // Try the 4 cardinal directions
      static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (const auto& d : dirs) {
        int nx = m.x + d[0];
        int ny = m.y + d[1];

        if (nx < 0 || nx >= game::MAP_WIDTH || ny < 0 || ny >= game::MAP_HEIGHT) continue;
        if (!isWalkable(tiles[ny * game::MAP_WIDTH + nx])) continue;

        // Don't walk onto player (would need to attack instead, handled above)
        if (nx == p.x && ny == p.y) continue;

        // Don't walk onto other monsters
        bool occupied = false;
        for (uint8_t j = 0; j < monsterCount; j++) {
          if (j != i && monsters[j].hp > 0 && monsters[j].x == nx && monsters[j].y == ny) {
            occupied = true;
            break;
          }
        }
        if (occupied) continue;

        int ndx = static_cast<int>(p.x) - nx;
        int ndy = static_cast<int>(p.y) - ny;
        int nd = ndx * ndx + ndy * ndy;
        if (nd < bestDist) {
          bestDist = nd;
          bestX = nx;
          bestY = ny;
        }
      }

      m.x = static_cast<int16_t>(bestX);
      m.y = static_cast<int16_t>(bestY);
    }
  }

  // Natural regeneration: heal 1 HP every (20 - CON/2) turns, minimum every 5 turns
  int regenRate = std::max(5, 20 - static_cast<int>(p.constitution) / 2);
  if (p.hp > 0 && p.hp < p.maxHp && p.turnCount % regenRate == 0) {
    p.hp++;
  }
  // MP regenerates a bit slower
  if (p.mp < p.maxMp && p.turnCount % (regenRate + 5) == 0) {
    p.mp++;
  }
}

void GameActivity::monsterAttackPlayer(game::Monster& m) {
  auto& p = GAME_STATE.player;
  const auto& def = game::MONSTER_DEFS[m.type];

  // Monster attack vs player dexterity + armor bonus
  int playerDef = static_cast<int>(p.dexterity / 3) + equippedDefenseBonus();
  int damage = std::max(1, static_cast<int>(def.attack) - playerDef);
  game::Rng rng(p.turnCount ^ (m.x * 41 + m.y * 43));
  damage = std::max(1, damage + rng.nextRangeInclusive(-damage / 4, damage / 4));

  // Apply damage
  p.hp = (static_cast<uint16_t>(damage) >= p.hp) ? 0 : p.hp - static_cast<uint16_t>(damage);

  char msgBuf[64];
  if (p.hp == 0) {
    snprintf(msgBuf, sizeof(msgBuf), "The %s kills you!", def.name);
    GAME_STATE.addMessage(msgBuf);
  } else {
    snprintf(msgBuf, sizeof(msgBuf), "The %s hits you for %d.", def.name, damage);
    GAME_STATE.addMessage(msgBuf);
  }
}

// --- Level Up ---

void GameActivity::checkLevelUp() {
  auto& p = GAME_STATE.player;

  while (p.charLevel < 50 && p.experience >= game::xpForLevel(p.charLevel + 1)) {
    p.charLevel++;

    // Stat gains
    uint16_t hpGain = 4 + p.constitution / 4;
    uint16_t mpGain = 2 + p.intelligence / 5;
    p.maxHp += hpGain;
    p.maxMp += mpGain;
    p.hp = p.maxHp;  // Full heal on level up
    p.mp = p.maxMp;
    p.strength += 1;
    p.dexterity += 1;

    char msgBuf[48];
    snprintf(msgBuf, sizeof(msgBuf), "Welcome to level %u!", p.charLevel);
    GAME_STATE.addMessage(msgBuf);
  }
}

// --- Player Death ---

void GameActivity::handlePlayerDeath() {
  GAME_STATE.addMessage("Press any key to return...");
  updateRequired = true;

  // Delete save data — permadeath!
  GameSave::deleteAll();
  GAME_STATE.deleteSaveFile();

  onGoHome();
}

// --- Victory ---

void GameActivity::handleVictory() {
  auto& p = GAME_STATE.player;

  char msgBuf[64];
  snprintf(msgBuf, sizeof(msgBuf), "Victory! Depth %u, Level %u, %u turns.", p.dungeonDepth, p.charLevel, p.turnCount);
  GAME_STATE.addMessage(msgBuf);
  updateRequired = true;

  // Clear save — the quest is complete
  GameSave::deleteAll();
  GAME_STATE.deleteSaveFile();

  onGoHome();
}

// --- Level Management ---

void GameActivity::loadOrGenerateLevel() {
  auto& p = GAME_STATE.player;

  // Always regenerate from seed (deterministic)
  auto result = DungeonGenerator::generate(p.gameSeed, p.dungeonDepth, tiles, monsters, levelItems);
  monsterCount = result.monsterCount;
  itemCount = result.itemCount;

  // Clear fog
  memset(fogOfWar, 0, sizeof(fogOfWar));
  memset(visible, 0, sizeof(visible));

  // If we have saved state for this level, overlay it
  if (GameSave::hasLevel(p.dungeonDepth)) {
    // Load saved fog, monsters, and items (overrides generated state)
    GameSave::loadLevel(p.dungeonDepth, fogOfWar, monsters, monsterCount, levelItems, itemCount);
  } else {
    // First visit — place player at stairs up
    p.x = result.stairsUpX;
    p.y = result.stairsUpY;
  }
}

void GameActivity::saveCurrentLevel() {
  GameSave::saveLevel(GAME_STATE.player.dungeonDepth, fogOfWar, monsters, monsterCount, levelItems, itemCount);
}

// --- Visibility ---

void GameActivity::computeVisibility() {
  auto& p = GAME_STATE.player;

  memset(visible, 0, sizeof(visible));

  // For each tile within FOV_RADIUS, check line of sight
  int startX = std::max(0, static_cast<int>(p.x) - FOV_RADIUS);
  int endX = std::min(game::MAP_WIDTH - 1, static_cast<int>(p.x) + FOV_RADIUS);
  int startY = std::max(0, static_cast<int>(p.y) - FOV_RADIUS);
  int endY = std::min(game::MAP_HEIGHT - 1, static_cast<int>(p.y) + FOV_RADIUS);

  for (int y = startY; y <= endY; y++) {
    for (int x = startX; x <= endX; x++) {
      int dx = x - p.x;
      int dy = y - p.y;
      if (dx * dx + dy * dy > FOV_RADIUS * FOV_RADIUS) continue;

      if (hasLineOfSight(tiles, p.x, p.y, x, y)) {
        visible[y * game::MAP_WIDTH + x] = true;
        game::fogSetExplored(fogOfWar, x, y);
      }
    }
  }
}
