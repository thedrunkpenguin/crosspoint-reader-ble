#pragma once

#include <array>
#include <functional>

#include "../Activity.h"

class GolfZeroActivity final : public Activity {
  enum class PlayStyle : uint8_t { Dice = 0, Club = 1 };
  enum class HoleLengthPreset : uint8_t { Short = 0, Mixed = 1, Long = 2 };
  enum class ClubType : uint8_t { Driver = 0, Iron = 1, Wedge = 2, Putter = 3 };

  enum class Terrain : uint8_t {
    Rough = 0,
    Fairway = 1,
    Green = 2,
    Sand = 3,
    Water = 4,
    Trees = 5,
    Cactus = 6,
    Mud = 7,
    Bridge = 8,
    Chance = 9,
    SlopeN = 10,
    SlopeNE = 11,
    SlopeE = 12,
    SlopeSE = 13,
    SlopeS = 14,
    SlopeSW = 15,
    SlopeW = 16,
    SlopeNW = 17
  };

  enum class TurnMode : uint8_t {
    MainMenu,
    RulesPage,
    KeyPage,
    AwaitRoll,
    Aim,
    HoleComplete,
    CourseComplete,
    PauseMenu,
    Scorecard
  };

  struct CourseDef {
    const char* name;
    uint32_t baseSeed;
  };

  struct HoleDef {
    uint8_t startX;
    uint8_t startY;
    uint8_t cupX;
    uint8_t cupY;
    uint32_t holeSeed;
    static constexpr uint8_t MAX_WP = 8;
    uint8_t wpX[MAX_WP];
    uint8_t wpY[MAX_WP];
    uint8_t wpCount;
  };

  struct ShotSegment {
    uint8_t fromX;
    uint8_t fromY;
    uint8_t toX;
    uint8_t toY;
  };

  static constexpr uint8_t GRID_W = 9;
  static constexpr uint8_t GRID_H = 22;
  static constexpr uint8_t COURSE_COUNT = 6;
  static constexpr uint8_t HOLES_PER_COURSE = 9;
  static constexpr uint8_t PAR_PER_HOLE = 6;
  static constexpr uint8_t MAX_MULLIGANS = 6;
  static constexpr uint16_t MAX_SEGMENTS = 180;
  static constexpr uint8_t SAVE_VERSION = 3;

  static constexpr uint8_t DIRECTION_COUNT = 12;
  // Display vectors for the 6 edge directions (for arrows and slope glyphs).
  static constexpr std::array<int8_t, 6> EDGE_VEC_X = {1, 1, -1, -1, -1, 1};
  static constexpr std::array<int8_t, 6> EDGE_VEC_Y = {0, -1, -1, 0, 1, 1};

  static constexpr std::array<CourseDef, COURSE_COUNT> COURSES = {
      CourseDef{"Sunforge Basin", 0xA31F00D1u},
      CourseDef{"Brasswood Run", 0x77BC2E5Fu},
      CourseDef{"Red Mesa Loop", 0x19E4B8A3u},
      CourseDef{"Gullspire Reach", 0x62AB4C11u},
      CourseDef{"Dustglass Hollow", 0xC93E7D2Au},
      CourseDef{"North Ember Links", 0x14D8B56Eu},
  };

  const std::function<void()> onBack;

  // Runtime state
  TurnMode turnMode = TurnMode::AwaitRoll;
  uint8_t currentCourse = 0;
  uint8_t selectedCourse = 0;
  uint8_t currentHole = 0;
  uint8_t menuIndex = 0;
  uint8_t mulligansUsed = 0;
  uint8_t strokesThisHole = 0;
  uint8_t strokesTotal = 0;
  uint8_t dieRoll = 0;
  uint8_t previewDir = 0;
  PlayStyle playStyle = PlayStyle::Dice;
  HoleLengthPreset lengthPreset = HoleLengthPreset::Mixed;
  ClubType selectedClub = ClubType::Iron;
  bool usePutt = false;
  bool rerolledTeeShot = false;
  bool loadedFromSave = false;
  bool hasSaveFile = false;
  bool scorecardAdvanceAfterConfirm = false;
  TurnMode resumeMode = TurnMode::AwaitRoll;
  TurnMode pauseReturnMode = TurnMode::AwaitRoll;
  TurnMode scorecardReturnMode = TurnMode::PauseMenu;

  uint8_t ballX = 0;
  uint8_t ballY = 0;

  std::array<uint8_t, HOLES_PER_COURSE> holeScores{};
  std::array<ShotSegment, MAX_SEGMENTS> segments{};
  uint16_t segmentCount = 0;

  // Layout cache
  int gridLeft = 0;
  int gridTop = 0;
  int cellSize = 12;
  int scrollY = 0;

  static constexpr const char* SAVE_DIR = "/.crosspoint/golfzero";
  static constexpr const char* SAVE_PATH = "/.crosspoint/golfzero/save.bin";

  void startFreshGame();
  HoleDef buildHoleDef(uint8_t courseIdx, uint8_t holeIdx) const;
  void setupHole();
  void finishHole();
  void advanceHoleOrCourse();

  uint32_t hashCoord(uint32_t seed, int x, int y, uint8_t salt) const;
  Terrain terrainAt(int x, int y, const HoleDef& hole) const;
  bool isFairwayLike(Terrain t) const;
  bool isSlope(Terrain t) const;
  int slopeDir(Terrain t) const;

  bool computePreviewLanding(uint8_t& outX, uint8_t& outY, bool& outInCup, bool& outPathValid) const;
  bool pathCrossesTrees(const HoleDef& hole, int fromX, int fromY, int toX, int toY, bool fromFairway) const;
  bool advanceHexStep(int& x, int& y, uint8_t edgeDir) const;
  void applySlopeRoll(const HoleDef& hole, uint8_t& x, uint8_t& y) const;

  void rollDie();
  int movementDistance(const HoleDef& hole) const;
  int clubDistanceFromTerrain(const HoleDef& hole) const;
  bool canUseSelectedClub(const HoleDef& hole) const;
  const char* clubName() const;
  const char* playStyleName() const;
  const char* lengthPresetName() const;
  void rotateDirection(int delta);
  bool hasValidDirection(uint8_t dir) const;
  void snapToValidDirection();
  void togglePuttMode();
  void cycleClub(int delta);
  void confirmShot();
  void rerollIfAllowed();

  bool saveGame(bool showMessage);
  bool loadGame();
  void clearSave();

  void drawGrid(const HoleDef& hole);
  void drawHexCell(int cx, int cy, int radius, bool state) const;
  void fillHexCellSolid(int cx, int cy, int radius, bool state) const;
  void fillHexCellPattern(int cx, int cy, int radius, uint8_t spacing) const;
  void drawSegments();
  void drawBallAndCup(const HoleDef& hole, uint8_t previewX, uint8_t previewY, bool inAim, bool previewValid);
  void drawHud(const HoleDef& hole);
  void drawSideRails(const HoleDef& hole);
  void drawInCourseKey();
  void drawPauseMenu();
  void drawScorecard();
  void drawMainMenu();
  void drawRulesPage();
  void drawKeyPage();

  void renderMessage(const char* msg);
  int sx(int gridX, int gridY) const;
  int sy(int gridX, int gridY) const;

 public:
  explicit GolfZeroActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack)
      : Activity("GolfZero", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
