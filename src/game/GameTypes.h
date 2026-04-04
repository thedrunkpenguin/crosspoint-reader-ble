#pragma once

#include <cstdint>

namespace game {

// --- Map Constants ---

constexpr int MAP_WIDTH = 80;
constexpr int MAP_HEIGHT = 50;
constexpr int MAP_SIZE = MAP_WIDTH * MAP_HEIGHT;            // 4000 bytes
constexpr int FOG_SIZE = (MAP_SIZE + 7) / 8;                // 500 bytes (bitfield)
constexpr int MAX_MONSTERS = 30;
constexpr int MAX_ITEMS_PER_LEVEL = 40;
constexpr int MAX_INVENTORY = 20;
constexpr int MAX_MESSAGES = 10;
constexpr int MAX_DEPTH = 26;

// --- Enums ---

enum class Tile : uint8_t {
  Wall,
  Floor,
  DoorClosed,
  DoorOpen,
  StairsUp,
  StairsDown,
  Rubble,
  Water,
  TileCount
};

enum class MonsterState : uint8_t { Asleep, Wandering, Hostile };

enum class ItemType : uint8_t {
  Weapon,
  Armor,
  Shield,
  Potion,
  Scroll,
  Food,
  Gold,
  Ring,
  Amulet,
  ItemTypeCount
};

enum class ItemFlag : uint8_t {
  None = 0,
  Identified = 1 << 0,
  Cursed = 1 << 1,
  Equipped = 1 << 2,
};

enum class Direction : uint8_t { North, South, East, West };

// --- Structs ---

struct Player {
  int16_t x = 0;
  int16_t y = 0;
  uint16_t hp = 20;
  uint16_t maxHp = 20;
  uint16_t mp = 5;
  uint16_t maxMp = 5;
  uint16_t strength = 10;
  uint16_t dexterity = 10;
  uint16_t constitution = 10;
  uint16_t intelligence = 10;
  uint16_t charLevel = 1;
  uint32_t experience = 0;
  uint16_t gold = 0;
  uint8_t dungeonDepth = 1;
  uint32_t gameSeed = 0;
  uint16_t turnCount = 0;
};

struct Monster {
  int16_t x = 0;
  int16_t y = 0;
  uint8_t type = 0;
  uint16_t hp = 0;
  uint8_t state = static_cast<uint8_t>(MonsterState::Asleep);
};

struct Item {
  int16_t x = -1;
  int16_t y = -1;
  uint8_t type = 0;
  uint8_t subtype = 0;
  uint8_t count = 1;
  uint8_t enchantment = 0;
  uint8_t flags = 0;
};

// --- Definition Tables (const, stored in flash) ---

struct MonsterDef {
  const char* name;
  char glyph;
  uint8_t minDepth;
  uint16_t baseHp;
  uint8_t attack;
  uint8_t defense;
  uint16_t expValue;
};

// Tolkien-inspired bestiary
static const MonsterDef MONSTER_DEFS[] = {
    {"Giant Rat", 'r', 1, 4, 2, 0, 5},
    {"Bat", 'b', 1, 3, 1, 0, 3},
    {"Kobold", 'k', 1, 6, 3, 1, 8},
    {"Grid Bug", 'x', 1, 2, 1, 0, 2},
    {"Goblin", 'g', 2, 8, 4, 2, 12},
    {"Orc", 'o', 3, 12, 6, 3, 20},
    {"Warg", 'w', 3, 10, 7, 2, 18},
    {"Large Spider", 'S', 4, 14, 5, 2, 22},
    {"Skeleton", 's', 4, 10, 5, 4, 15},
    {"Uruk-hai", 'U', 5, 18, 8, 5, 30},
    {"Cave Troll", 'T', 6, 30, 10, 6, 50},
    {"Wight", 'W', 7, 20, 9, 5, 40},
    {"Shade", 'G', 8, 16, 11, 3, 45},
    {"Oliphaunt", 'O', 9, 50, 12, 8, 80},
    {"Olog-hai", 'P', 10, 40, 14, 9, 70},
    {"Fire Drake", 'd', 12, 35, 13, 7, 90},
    {"Nazgul", 'N', 15, 60, 18, 12, 150},
    {"Young Dragon", 'D', 18, 80, 20, 14, 200},
    {"Balrog", 'B', 22, 120, 25, 16, 500},
    {"Ancient Dragon", 'D', 25, 150, 30, 20, 1000},
    // Boss — The Necromancer (Sauron in fair form), always on the deepest level
    {"The Necromancer", 'p', 26, 250, 35, 22, 5000},
};
static constexpr int MONSTER_DEF_COUNT = sizeof(MONSTER_DEFS) / sizeof(MONSTER_DEFS[0]);
static constexpr int BOSS_MONSTER_TYPE = MONSTER_DEF_COUNT - 1;  // Index of The Necromancer

struct ItemDef {
  const char* name;
  char glyph;
  uint8_t type;    // ItemType
  uint8_t subtype;
  uint16_t value;  // Base gold value
  int8_t attack;   // Bonus if weapon/armor
  int8_t defense;  // Bonus if armor/shield
};

static const ItemDef ITEM_DEFS[] = {
    // Weapons
    {"Dagger", '/', static_cast<uint8_t>(ItemType::Weapon), 0, 5, 2, 0},
    {"Short Sword", '/', static_cast<uint8_t>(ItemType::Weapon), 1, 15, 4, 0},
    {"Long Sword", '/', static_cast<uint8_t>(ItemType::Weapon), 2, 30, 6, 0},
    {"Battle Axe", '/', static_cast<uint8_t>(ItemType::Weapon), 3, 50, 8, 0},
    {"Mithril Blade", '/', static_cast<uint8_t>(ItemType::Weapon), 4, 200, 12, 0},
    // Armor
    {"Leather Armor", '[', static_cast<uint8_t>(ItemType::Armor), 0, 10, 0, 2},
    {"Chain Mail", '[', static_cast<uint8_t>(ItemType::Armor), 1, 30, 0, 4},
    {"Plate Mail", '[', static_cast<uint8_t>(ItemType::Armor), 2, 60, 0, 6},
    {"Mithril Coat", '[', static_cast<uint8_t>(ItemType::Armor), 3, 300, 0, 10},
    // Shields
    {"Wooden Shield", ')', static_cast<uint8_t>(ItemType::Shield), 0, 8, 0, 1},
    {"Iron Shield", ')', static_cast<uint8_t>(ItemType::Shield), 1, 25, 0, 3},
    // Potions
    {"Potion of Healing", '!', static_cast<uint8_t>(ItemType::Potion), 0, 20, 0, 0},
    {"Potion of Mana", '!', static_cast<uint8_t>(ItemType::Potion), 1, 25, 0, 0},
    {"Potion of Strength", '!', static_cast<uint8_t>(ItemType::Potion), 2, 50, 0, 0},
    // Scrolls
    {"Scroll of Identify", '?', static_cast<uint8_t>(ItemType::Scroll), 0, 15, 0, 0},
    {"Scroll of Teleport", '?', static_cast<uint8_t>(ItemType::Scroll), 1, 30, 0, 0},
    {"Scroll of Mapping", '?', static_cast<uint8_t>(ItemType::Scroll), 2, 40, 0, 0},
    // Food
    {"Rations", '%', static_cast<uint8_t>(ItemType::Food), 0, 5, 0, 0},
    {"Lembas Bread", '%', static_cast<uint8_t>(ItemType::Food), 1, 30, 0, 0},
    // Gold
    {"Gold Coins", '$', static_cast<uint8_t>(ItemType::Gold), 0, 1, 0, 0},
    // Quest item — dropped by The Necromancer
    {"Ring of Power", '=', static_cast<uint8_t>(ItemType::Ring), 0, 999, 0, 0},
};
static constexpr int ITEM_DEF_COUNT = sizeof(ITEM_DEFS) / sizeof(ITEM_DEFS[0]);
static constexpr int RING_OF_POWER_DEF = ITEM_DEF_COUNT - 1;  // Index of Ring of Power

// --- Glyph lookup helpers ---

inline char tileGlyph(Tile tile) {
  switch (tile) {
    case Tile::Wall: return '#';
    case Tile::Floor: return '.';
    case Tile::DoorClosed: return '+';
    case Tile::DoorOpen: return '\'';
    case Tile::StairsUp: return '<';
    case Tile::StairsDown: return '>';
    case Tile::Rubble: return ':';
    case Tile::Water: return '~';
    default: return '?';
  }
}

inline char itemGlyph(uint8_t type) {
  switch (static_cast<ItemType>(type)) {
    case ItemType::Weapon: return '/';
    case ItemType::Armor: return '[';
    case ItemType::Shield: return ')';
    case ItemType::Potion: return '!';
    case ItemType::Scroll: return '?';
    case ItemType::Food: return '%';
    case ItemType::Gold: return '$';
    case ItemType::Ring: return '=';
    case ItemType::Amulet: return '"';
    default: return '*';
  }
}

// --- Fog of War helpers ---

inline bool fogIsExplored(const uint8_t* fog, int x, int y) {
  int idx = y * MAP_WIDTH + x;
  return (fog[idx / 8] >> (idx % 8)) & 1;
}

inline void fogSetExplored(uint8_t* fog, int x, int y) {
  int idx = y * MAP_WIDTH + x;
  fog[idx / 8] |= (1 << (idx % 8));
}

// --- XorShift32 RNG (deterministic, 4 bytes state) ---

struct Rng {
  uint32_t state;

  explicit Rng(uint32_t seed) : state(seed ? seed : 1) {}

  uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }

  // Returns value in [0, max)
  uint32_t nextRange(uint32_t max) { return next() % max; }

  // Returns value in [min, max]
  int nextRangeInclusive(int min, int max) { return min + static_cast<int>(next() % (max - min + 1)); }
};

// --- Level-up XP thresholds ---

// XP required for each character level (index = level, value = cumulative XP needed)
inline uint32_t xpForLevel(uint16_t level) {
  if (level <= 1) return 0;
  // Roughly: 20, 60, 140, 300, 600, 1100, 1900, 3200, 5200, 8200...
  uint32_t xp = 0;
  for (uint16_t i = 2; i <= level; i++) {
    xp += 10u * i * i;
  }
  return xp;
}

// --- Level seed derivation ---

inline uint32_t levelSeed(uint32_t gameSeed, uint8_t depth) {
  return gameSeed ^ (depth * 2654435761u);
}

}  // namespace game
