#include "RPGEncounter.h"

namespace rpg {

// Enemy definitions — Inspired by D&D 5e Basic Rules, Dragonlance, and Neverwinter Saga
// Stats: id, name, level, maxHp, hp, AC, damage, experienceReward, goldReward
Enemy ENEMIES[] = {
    {1, "Kobold",       1,   5,   5,  12,  2,   50,   8},   // CR 1/8 — dragon-army scout
    {2, "Goblin",       1,   7,   7,  15,  4,  100,  12},   // CR 1/4 — Blackwood raider
    {3, "Draconian",    2,  22,  22,  14,  7,  200,  30},   // Baaz draconian, Dragon Army
    {4, "Skeleton",     2,  13,  13,  13,  5,  100,  15},   // Undead remnant, necrotic
    {5, "Hobgoblin",    3,  27,  27,  18,  8,  300,  45},   // CR 1 — disciplined soldier
    {6, "Wight",        4,  45,  45,  14, 10,  700, 100},   // CR 3 — undead from Neverwinter
    {7, "Drow Scout",   5,  60,  60,  16, 12, 1200, 175},   // Salvatore's dark elves
    {8, "Young Dragon", 6, 110, 110,  17, 16, 2500, 500},   // Dragonlance — chromatic dragon
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

// Encounter choices
const Choice TAVERN_CHOICES[] = {
  {"Set out for the Darken Wood",    2, 0x00000001},
  {"Visit the crossroads merchant",  3, 0x00000002},
  {"Rest by the inn hearthfire",     4, 0x00000004},
};

const Choice FOREST_CHOICES[] = {
  {"Press forward through the wood", 5, 0x00000010},
  {"Make camp and watch the dark",   4, 0x00000020},
  {"Return to Solace",               1, 0x00000040},
};

const Choice GOBLIN_CHOICES[] = {
  {"Search the fallen foe's pack",   8, 0x00000100},
  {"Show quarter to the wounded",    7, 0x00000200},
};

const Choice MERCY_CHOICES[] = {
  {"Follow the hidden trail",        2, 0x00000400},
  {"Return to Solace",               1, 0x00000800},
};

const Choice LOOT_CHOICES[] = {
  {"Return to Solace with the spoils", 1, 0x00001000},
  {"Press deeper into the wood",       2, 0x00002000},
};

Encounter ENCOUNTERS[] = {
  {1,
   "Inn of the Last Home",
   EncounterType::Story,
   "Autumn rain drums on the vallenwood canopy while old companions share dark rumors.\n"
   "A hand-drawn map of the Darken Wood rests beside a cooling mug.\n\n"
   "The Dragon Army marches east. Your journey begins tonight.",
   TAVERN_CHOICES,
   3,
   nullptr,
   nullptr,
   nullptr},
  {2,
   "The Darken Wood",
   EncounterType::Story,
   "Ancient pines close overhead along the old elven road to Qualinesti.\n"
   "Somewhere ahead, armored footfalls echo through mist-wrapped shadows.",
   FOREST_CHOICES,
   3,
   nullptr,
   nullptr,
   nullptr},
  {3,
   "Crossroads Merchant",
   EncounterType::Merchant,
   "Under a patched cloak beside a battered wagon, a traveling merchant mutters:\n"
   "\"War makes fine customers. Potions for silver, scrolls for gold.\"",
   nullptr,
   0,
   nullptr,
   nullptr,
   nullptr},
  {4,
   "Waystone Rest",
   EncounterType::RestSite,
   "You shelter beside an ancient waystone carved with Solamnic script.\n"
   "Warmth returns as the night wind calls through the pines.",
   nullptr,
   0,
   nullptr,
   nullptr,
   nullptr},
  {5,
   "Ambush in the Wood",
   EncounterType::Combat,
   "Steel glints in the shadows. The creature charges from the treeline!",
   nullptr,
   0,
   (Enemy*)&ENEMIES[0],
   nullptr,
   nullptr},
  {6,
   "After the Skirmish",
   EncounterType::Story,
   "Your foe staggers and falls onto autumn leaves. A worn pack tumbles free,\n"
   "heavy with coins and the smell of alchemist's glass.",
   GOBLIN_CHOICES,
   2,
   nullptr,
   nullptr,
   nullptr},
  {7,
   "A Moment of Mercy",
   EncounterType::Story,
   "You lower your blade. The creature stares in stunned disbelief, then stumbles\n"
   "away into the pines, pointing toward a hidden trail before vanishing.",
   MERCY_CHOICES,
   2,
   nullptr,
   nullptr,
   nullptr},
  {8,
   "Spoils of the Wood",
   EncounterType::Story,
   "Inside the fallen pack: tarnished coins, a healing draught corked in blue wax,\n"
   "and a strange tonic sealed with dark pitch.",
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
