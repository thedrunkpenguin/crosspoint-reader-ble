#include "RPGEncounter.h"

namespace rpg {

// Enemy definitions
Enemy ENEMIES[] = {
    {1, "Goblin", 1, 7, 7, 12, 4, 100, 15},
    {2, "Orc", 2, 15, 15, 11, 6, 200, 30},
    {3, "Troll", 3, 30, 30, 13, 8, 500, 75},
    {4, "Skeleton", 1, 5, 5, 10, 3, 50, 10},
    {5, "Vampire", 4, 40, 40, 15, 10, 1000, 150},
    {6, "Dragon", 5, 100, 100, 18, 15, 2000, 500},
};

int ENEMY_COUNT = sizeof(ENEMIES) / sizeof(ENEMIES[0]);

Enemy* getEnemy(uint8_t enemyId) {
  for (int i = 0; i < ENEMY_COUNT; i++) {
    if (ENEMIES[i].id == enemyId) {
      ENEMIES[i].hp = ENEMIES[i].maxHp;  // Reset HP
      return &ENEMIES[i];
    }
  }
  return nullptr;
}

// Encounter definitions
const Choice TAVERN_CHOICES[] = {
  {"Head toward the Blackwood", 2, 0x00000001},
  {"Visit the market merchant", 3, 0x00000002},
  {"Rest by the hearth", 4, 0x00000004},
};

const Choice FOREST_CHOICES[] = {
  {"Follow the rustling trail", 5, 0x00000010},
  {"Set up a cautious camp", 4, 0x00000020},
  {"Return to the tavern", 1, 0x00000040},
};

const Choice GOBLIN_CHOICES[] = {
  {"Take the goblin satchel", 8, 0x00000100},
  {"Spare the wounded goblin", 7, 0x00000200},
};

const Choice MERCY_CHOICES[] = {
  {"Track the goblin's hidden path", 2, 0x00000400},
  {"Return to town", 1, 0x00000800},
};

const Choice LOOT_CHOICES[] = {
  {"Return to town with loot", 1, 0x00001000},
  {"Press deeper into Blackwood", 2, 0x00002000},
};

Encounter ENCOUNTERS[] = {
  {1,
   "Ashwick Tavern",
   EncounterType::Story,
   "Rain taps against oak shutters while old adventurers mutter over stale ale.\n"
   "A hand-drawn map of Blackwood lies beside your mug, marked with clawed symbols.\n\n"
   "Tonight, your story begins.",
   TAVERN_CHOICES,
   3,
   nullptr,
   nullptr,
   nullptr},
  {2,
   "Blackwood Edge",
   EncounterType::Story,
   "Mist curls between ancient pines. The scent of wet soil and smoke lingers in the air.\n"
   "Somewhere ahead, something small and dangerous is moving through the brush.",
   FOREST_CHOICES,
   3,
   nullptr,
   nullptr,
   nullptr},
  {3,
   "Wandering Merchant",
   EncounterType::Merchant,
   "Under a patched crimson awning, a merchant polishes steel and whispers:\n"
   "\"Coin for survival, friend. Potions for blood. Scrolls for fire.\"",
   nullptr,
   0,
   nullptr,
   nullptr,
   nullptr},
  {4,
   "Campfire Rest",
   EncounterType::RestSite,
   "You build a small fire beside an old rune-carved stone.\n"
   "Warmth returns to your hands as night birds call beyond the trees.",
   nullptr,
   0,
   nullptr,
   nullptr,
   nullptr},
  {5,
   "Goblin Ambush",
   EncounterType::Combat,
   "A jagged blade flashes from the brush. A goblin raider hisses and charges!",
   nullptr,
   0,
   (Enemy*)&ENEMIES[0],
   nullptr,
   nullptr},
  {6,
   "After the Clash",
   EncounterType::Story,
   "The goblin collapses in the mud. A small satchel tumbles free, packed with rough coins and bone charms.",
   GOBLIN_CHOICES,
   2,
   nullptr,
   nullptr,
   nullptr},
  {7,
   "Merciful Path",
   EncounterType::Story,
   "You lower your blade. The goblin stares in disbelief, then gestures to a hidden trail before slipping away.",
   MERCY_CHOICES,
   2,
   nullptr,
   nullptr,
   nullptr},
  {8,
   "Spoils of Blackwood",
   EncounterType::Story,
   "Inside the satchel you find old silver, a cracked ring, and a charcoal map fragment pointing deeper into the forest.",
   LOOT_CHOICES,
   2,
   nullptr,
   nullptr,
   nullptr},
};

int ENCOUNTER_COUNT = sizeof(ENCOUNTERS) / sizeof(ENCOUNTERS[0]);

const Encounter* getEncounter(uint8_t encounterId) {
  for (int i = 0; i < ENCOUNTER_COUNT; i++) {
    if (ENCOUNTERS[i].id == encounterId) {
      return &ENCOUNTERS[i];
    }
  }
  return nullptr;
}

}  // namespace rpg
