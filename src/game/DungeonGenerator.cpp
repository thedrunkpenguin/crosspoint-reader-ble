#include "DungeonGenerator.h"

#include <HardwareSerial.h>

#include <algorithm>

using game::Item;
using game::MAP_HEIGHT;
using game::MAP_WIDTH;
using game::Monster;
using game::MonsterState;
using game::Rng;
using game::Tile;

namespace {

// --- BSP Tree Node (stack-allocated, max 32 leaves from 5 splits) ---

constexpr int MAX_BSP_NODES = 63;  // 2^6 - 1

struct BspNode {
  int16_t x, y, w, h;     // Partition bounds
  int16_t roomX, roomY;   // Room position within partition
  int16_t roomW, roomH;   // Room dimensions (0 = no room yet)
  int16_t left, right;    // Child indices (-1 = leaf)
};

struct BspTree {
  BspNode nodes[MAX_BSP_NODES];
  int count = 0;

  int addNode(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (count >= MAX_BSP_NODES) return -1;
    int idx = count++;
    nodes[idx] = {x, y, w, h, 0, 0, 0, 0, -1, -1};
    return idx;
  }
};

constexpr int MIN_PARTITION_SIZE = 8;  // Minimum partition dimension for splitting
constexpr int MIN_ROOM_SIZE = 3;       // Smallest room dimension
constexpr int ROOM_PADDING = 1;        // Wall padding inside partition

// --- Helpers ---

inline Tile& tileAt(Tile* tiles, int x, int y) {
  return tiles[y * MAP_WIDTH + x];
}

inline bool inBounds(int x, int y) {
  return x >= 0 && x < MAP_WIDTH && y >= 0 && y < MAP_HEIGHT;
}

inline bool isFloor(const Tile* tiles, int x, int y) {
  if (!inBounds(x, y)) return false;
  Tile t = tiles[y * MAP_WIDTH + x];
  return t == Tile::Floor || t == Tile::DoorClosed || t == Tile::DoorOpen || t == Tile::StairsUp ||
         t == Tile::StairsDown;
}

// --- BSP Split ---

void splitBsp(BspTree& tree, int nodeIdx, Rng& rng, int depth) {
  BspNode& node = tree.nodes[nodeIdx];

  // Stop if too small or deep enough
  if (depth >= 5) return;
  if (node.w < MIN_PARTITION_SIZE * 2 && node.h < MIN_PARTITION_SIZE * 2) return;
  if (tree.count >= MAX_BSP_NODES - 2) return;

  // Choose split direction: prefer splitting the longer axis
  bool splitH;
  if (node.w < MIN_PARTITION_SIZE * 2) {
    splitH = true;  // Must split horizontally
  } else if (node.h < MIN_PARTITION_SIZE * 2) {
    splitH = false;  // Must split vertically
  } else {
    splitH = (rng.next() % 2 == 0);
  }

  if (splitH) {
    // Horizontal split (top/bottom)
    int minSplit = MIN_PARTITION_SIZE;
    int maxSplit = node.h - MIN_PARTITION_SIZE;
    if (minSplit >= maxSplit) return;
    int split = rng.nextRangeInclusive(minSplit, maxSplit);

    node.left = tree.addNode(node.x, node.y, node.w, static_cast<int16_t>(split));
    node.right = tree.addNode(node.x, static_cast<int16_t>(node.y + split), node.w,
                              static_cast<int16_t>(node.h - split));
  } else {
    // Vertical split (left/right)
    int minSplit = MIN_PARTITION_SIZE;
    int maxSplit = node.w - MIN_PARTITION_SIZE;
    if (minSplit >= maxSplit) return;
    int split = rng.nextRangeInclusive(minSplit, maxSplit);

    node.left = tree.addNode(node.x, node.y, static_cast<int16_t>(split), node.h);
    node.right = tree.addNode(static_cast<int16_t>(node.x + split), node.y,
                              static_cast<int16_t>(node.w - split), node.h);
  }

  if (node.left >= 0) splitBsp(tree, node.left, rng, depth + 1);
  if (node.right >= 0) splitBsp(tree, node.right, rng, depth + 1);
}

// --- Room Placement ---

void placeRooms(BspTree& tree, int nodeIdx, Rng& rng, Tile* tiles) {
  BspNode& node = tree.nodes[nodeIdx];

  // If leaf node, place a room
  if (node.left == -1 && node.right == -1) {
    int maxW = node.w - ROOM_PADDING * 2;
    int maxH = node.h - ROOM_PADDING * 2;
    if (maxW < MIN_ROOM_SIZE) maxW = MIN_ROOM_SIZE;
    if (maxH < MIN_ROOM_SIZE) maxH = MIN_ROOM_SIZE;

    int roomW = rng.nextRangeInclusive(MIN_ROOM_SIZE, std::min(maxW, 12));
    int roomH = rng.nextRangeInclusive(MIN_ROOM_SIZE, std::min(maxH, 8));

    int maxRoomX = node.w - roomW - ROOM_PADDING;
    int maxRoomY = node.h - roomH - ROOM_PADDING;
    if (maxRoomX < ROOM_PADDING) maxRoomX = ROOM_PADDING;
    if (maxRoomY < ROOM_PADDING) maxRoomY = ROOM_PADDING;

    int roomX = node.x + rng.nextRangeInclusive(ROOM_PADDING, maxRoomX);
    int roomY = node.y + rng.nextRangeInclusive(ROOM_PADDING, maxRoomY);

    // Clamp to map bounds (leave 1-tile border)
    roomX = std::max(1, std::min(roomX, MAP_WIDTH - roomW - 1));
    roomY = std::max(1, std::min(roomY, MAP_HEIGHT - roomH - 1));
    roomW = std::min(roomW, MAP_WIDTH - roomX - 1);
    roomH = std::min(roomH, MAP_HEIGHT - roomY - 1);

    node.roomX = static_cast<int16_t>(roomX);
    node.roomY = static_cast<int16_t>(roomY);
    node.roomW = static_cast<int16_t>(roomW);
    node.roomH = static_cast<int16_t>(roomH);

    // Carve the room
    for (int ry = roomY; ry < roomY + roomH; ry++) {
      for (int rx = roomX; rx < roomX + roomW; rx++) {
        tileAt(tiles, rx, ry) = Tile::Floor;
      }
    }
    return;
  }

  // Recurse into children
  if (node.left >= 0) placeRooms(tree, node.left, rng, tiles);
  if (node.right >= 0) placeRooms(tree, node.right, rng, tiles);
}

// --- Get room center for a BSP subtree (finds a leaf) ---

void getRoomCenter(const BspTree& tree, int nodeIdx, int16_t& cx, int16_t& cy) {
  const BspNode& node = tree.nodes[nodeIdx];
  if (node.left == -1 && node.right == -1) {
    cx = static_cast<int16_t>(node.roomX + node.roomW / 2);
    cy = static_cast<int16_t>(node.roomY + node.roomH / 2);
    return;
  }
  // Pick left child to find a room center
  if (node.left >= 0) {
    getRoomCenter(tree, node.left, cx, cy);
  } else if (node.right >= 0) {
    getRoomCenter(tree, node.right, cx, cy);
  }
}

// --- Corridor Carving ---

void carveHCorridor(Tile* tiles, int x1, int x2, int y) {
  int startX = std::min(x1, x2);
  int endX = std::max(x1, x2);
  for (int x = startX; x <= endX; x++) {
    if (inBounds(x, y)) {
      tileAt(tiles, x, y) = Tile::Floor;
    }
  }
}

void carveVCorridor(Tile* tiles, int x, int y1, int y2) {
  int startY = std::min(y1, y2);
  int endY = std::max(y1, y2);
  for (int y = startY; y <= endY; y++) {
    if (inBounds(x, y)) {
      tileAt(tiles, x, y) = Tile::Floor;
    }
  }
}

void connectRooms(BspTree& tree, int nodeIdx, Rng& rng, Tile* tiles) {
  BspNode& node = tree.nodes[nodeIdx];
  if (node.left == -1 || node.right == -1) return;

  // Recurse first so children have rooms
  connectRooms(tree, node.left, rng, tiles);
  connectRooms(tree, node.right, rng, tiles);

  // Connect left subtree to right subtree with L-shaped corridor
  int16_t cx1, cy1, cx2, cy2;
  getRoomCenter(tree, node.left, cx1, cy1);
  getRoomCenter(tree, node.right, cx2, cy2);

  // Randomly choose L-bend direction
  if (rng.next() % 2 == 0) {
    carveHCorridor(tiles, cx1, cx2, cy1);
    carveVCorridor(tiles, cx2, cy1, cy2);
  } else {
    carveVCorridor(tiles, cx1, cy1, cy2);
    carveHCorridor(tiles, cx1, cx2, cy2);
  }
}

// --- Door Placement ---

void placeDoors(Tile* tiles, Rng& rng) {
  // Scan for corridor-room transitions: a floor tile with exactly 2 floor neighbors
  // on opposite sides (horizontal or vertical) and wall neighbors on the other axis
  for (int y = 1; y < MAP_HEIGHT - 1; y++) {
    for (int x = 1; x < MAP_WIDTH - 1; x++) {
      if (tileAt(tiles, x, y) != Tile::Floor) continue;

      bool floorN = isFloor(tiles, x, y - 1);
      bool floorS = isFloor(tiles, x, y + 1);
      bool floorE = isFloor(tiles, x + 1, y);
      bool floorW = isFloor(tiles, x - 1, y);

      bool isNarrowH = floorE && floorW && !floorN && !floorS;
      bool isNarrowV = floorN && floorS && !floorE && !floorW;

      if ((isNarrowH || isNarrowV) && rng.nextRange(4) == 0) {
        tileAt(tiles, x, y) = (rng.nextRange(3) == 0) ? Tile::DoorClosed : Tile::DoorOpen;
      }
    }
  }
}

// --- Scatter rubble for atmosphere ---

void placeRubble(Tile* tiles, Rng& rng, uint8_t depth) {
  int rubbleCount = 3 + depth;
  for (int i = 0; i < rubbleCount; i++) {
    int x = rng.nextRangeInclusive(1, MAP_WIDTH - 2);
    int y = rng.nextRangeInclusive(1, MAP_HEIGHT - 2);
    if (tileAt(tiles, x, y) == Tile::Floor) {
      tileAt(tiles, x, y) = Tile::Rubble;
    }
  }
}

// --- Find a random floor tile ---

bool findRandomFloor(const Tile* tiles, Rng& rng, int16_t& outX, int16_t& outY, int maxAttempts = 200) {
  for (int i = 0; i < maxAttempts; i++) {
    int x = rng.nextRangeInclusive(1, MAP_WIDTH - 2);
    int y = rng.nextRangeInclusive(1, MAP_HEIGHT - 2);
    if (tiles[y * MAP_WIDTH + x] == Tile::Floor) {
      outX = static_cast<int16_t>(x);
      outY = static_cast<int16_t>(y);
      return true;
    }
  }
  return false;
}

// --- Monster Placement ---

uint8_t placeMonsters(Tile* tiles, Monster* monsters, Rng& rng, uint8_t depth) {
  int count = std::min(3 + depth * 2, static_cast<int>(game::MAX_MONSTERS));
  uint8_t placed = 0;

  // Build list of eligible monster types for this depth
  uint8_t eligible[game::MONSTER_DEF_COUNT];
  uint8_t eligibleCount = 0;
  for (int i = 0; i < game::MONSTER_DEF_COUNT; i++) {
    if (game::MONSTER_DEFS[i].minDepth <= depth) {
      eligible[eligibleCount++] = static_cast<uint8_t>(i);
    }
  }
  if (eligibleCount == 0) return 0;

  for (int i = 0; i < count && placed < game::MAX_MONSTERS; i++) {
    int16_t mx, my;
    if (!findRandomFloor(tiles, rng, mx, my)) continue;

    // Check no other monster at this position
    bool occupied = false;
    for (uint8_t j = 0; j < placed; j++) {
      if (monsters[j].x == mx && monsters[j].y == my) {
        occupied = true;
        break;
      }
    }
    if (occupied) continue;

    // Pick a random eligible type, biased toward depth-appropriate monsters
    uint8_t typeIdx = eligible[rng.nextRange(eligibleCount)];
    const auto& def = game::MONSTER_DEFS[typeIdx];

    Monster& m = monsters[placed];
    m.x = mx;
    m.y = my;
    m.type = typeIdx;
    m.hp = def.baseHp + static_cast<uint16_t>(rng.nextRange(def.baseHp / 2 + 1));
    m.state = (rng.nextRange(3) == 0) ? static_cast<uint8_t>(MonsterState::Wandering)
                                      : static_cast<uint8_t>(MonsterState::Asleep);
    placed++;
  }

  return placed;
}

// --- Item Placement ---

uint8_t placeItems(Tile* tiles, Item* items, Rng& rng, uint8_t depth) {
  int count = std::min(2 + static_cast<int>(depth), static_cast<int>(game::MAX_ITEMS_PER_LEVEL));
  uint8_t placed = 0;

  for (int i = 0; i < count && placed < game::MAX_ITEMS_PER_LEVEL; i++) {
    int16_t ix, iy;
    if (!findRandomFloor(tiles, rng, ix, iy)) continue;

    // Pick a random item type
    uint8_t defIdx = static_cast<uint8_t>(rng.nextRange(game::ITEM_DEF_COUNT));
    const auto& def = game::ITEM_DEFS[defIdx];

    Item& item = items[placed];
    item.x = ix;
    item.y = iy;
    item.type = def.type;
    item.subtype = def.subtype;
    item.count = (def.type == static_cast<uint8_t>(game::ItemType::Gold))
                     ? static_cast<uint8_t>(rng.nextRangeInclusive(1, 10 + depth * 5))
                     : 1;
    item.enchantment = 0;
    item.flags = 0;

    // Small chance of enchantment on weapons/armor
    if ((def.type == static_cast<uint8_t>(game::ItemType::Weapon) ||
         def.type == static_cast<uint8_t>(game::ItemType::Armor)) &&
        rng.nextRange(4) == 0) {
      item.enchantment = static_cast<uint8_t>(rng.nextRangeInclusive(1, 3));
    }

    placed++;
  }

  return placed;
}

}  // anonymous namespace

// --- Public API ---

DungeonGenerator::Result DungeonGenerator::generate(uint32_t gameSeed, uint8_t depth, Tile* tiles, Monster* monsters,
                                                    Item* items) {
  Rng rng(game::levelSeed(gameSeed, depth));

  // Fill with walls
  for (int i = 0; i < game::MAP_SIZE; i++) {
    tiles[i] = Tile::Wall;
  }

  // BSP partition and room generation
  BspTree bsp;
  int root = bsp.addNode(0, 0, MAP_WIDTH, MAP_HEIGHT);
  splitBsp(bsp, root, rng, 0);
  placeRooms(bsp, root, rng, tiles);
  connectRooms(bsp, root, rng, tiles);

  // Place doors at narrow corridor points
  placeDoors(tiles, rng);

  // Scatter rubble
  placeRubble(tiles, rng, depth);

  // Place stairs
  Result result{};

  // StairsUp: find a floor tile in the first room (leftmost leaf)
  int16_t upX, upY;
  getRoomCenter(bsp, bsp.nodes[root].left >= 0 ? bsp.nodes[root].left : root, upX, upY);
  tileAt(tiles, upX, upY) = Tile::StairsUp;
  result.stairsUpX = upX;
  result.stairsUpY = upY;

  // StairsDown: find a floor tile in the last room (rightmost leaf)
  int16_t downX, downY;
  getRoomCenter(bsp, bsp.nodes[root].right >= 0 ? bsp.nodes[root].right : root, downX, downY);
  // Ensure stairs aren't on same tile
  if (downX == upX && downY == upY) {
    findRandomFloor(tiles, rng, downX, downY);
  }
  tileAt(tiles, downX, downY) = Tile::StairsDown;
  result.stairsDownX = downX;
  result.stairsDownY = downY;

  // Place monsters and items
  result.monsterCount = placeMonsters(tiles, monsters, rng, depth);
  result.itemCount = placeItems(tiles, items, rng, depth);

  // On the deepest level, place The Necromancer near the stairs down
  if (depth == game::MAX_DEPTH && result.monsterCount < game::MAX_MONSTERS) {
    int16_t bossX = downX;
    int16_t bossY = downY;
    // Find a floor tile near the stairs down for the boss
    for (int attempts = 0; attempts < 50; attempts++) {
      int16_t tx, ty;
      findRandomFloor(tiles, rng, tx, ty);
      // Prefer tiles in the same quadrant as stairs down
      int ddx = tx - downX;
      int ddy = ty - downY;
      if (ddx * ddx + ddy * ddy < 100) {
        bossX = tx;
        bossY = ty;
        break;
      }
    }
    Monster& boss = monsters[result.monsterCount];
    boss.x = bossX;
    boss.y = bossY;
    boss.type = game::BOSS_MONSTER_TYPE;
    boss.hp = game::MONSTER_DEFS[game::BOSS_MONSTER_TYPE].baseHp;
    boss.state = static_cast<uint8_t>(MonsterState::Hostile);  // Always hostile
    result.monsterCount++;
  }

  Serial.printf("[%lu] [DM ] Generated level %u (%u rooms, %u monsters, %u items)\n", millis(), depth, bsp.count,
                result.monsterCount, result.itemCount);

  return result;
}
