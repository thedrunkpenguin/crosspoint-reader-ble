#include "GolfZeroActivity.h"

#include <Arduino.h>
#include <Serialization.h>
#include <esp_random.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include <HalStorage.h>

namespace {
constexpr int STATUS_HEIGHT = 142;
constexpr int DOT_R = 1;
constexpr int CUP_R = 4;
constexpr int BALL_R = 3;

constexpr uint16_t CONIFER_MASK[11] = {
  0b000010000, 0b000111000, 0b001111100, 0b000111000, 0b001111100, 0b011111110,
  0b001111100, 0b011111110, 0b111111111, 0b000010000, 0b000010000,
};
constexpr uint16_t BROADLEAF_MASK[11] = {
  0b001111100, 0b011111110, 0b111111111, 0b111111111, 0b111111111, 0b011111110,
  0b001111100, 0b000010000, 0b000010000, 0b000010000, 0b000010000,
};
// Saguaro-style cactus: center trunk, left arm upper, right arm lower.
constexpr uint16_t CACTUS_MASK[11] = {
  0b000010000,  // trunk tip
  0b000010000,
  0b000010100,  // right arm start
  0b000011100,  // right arm joint
  0b000010000,  // trunk
  0b001110000,  // left arm joint
  0b001010000,  // left arm top
  0b000010000,  // trunk
  0b000010000,
  0b000010000,
  0b000010000,  // trunk base
};

inline int iabs(int v) { return v < 0 ? -v : v; }
}  // namespace

void GolfZeroActivity::onEnter() {
  Activity::onEnter();
  hasSaveFile = loadGame();
  if (!hasSaveFile) {
    startFreshGame();
    selectedCourse = currentCourse;
    resumeMode = TurnMode::AwaitRoll;
  } else {
    selectedCourse = currentCourse;
    resumeMode = turnMode;
    if (resumeMode == TurnMode::PauseMenu || resumeMode == TurnMode::Scorecard || resumeMode == TurnMode::MainMenu ||
        resumeMode == TurnMode::RulesPage || resumeMode == TurnMode::KeyPage) {
      resumeMode = TurnMode::AwaitRoll;
    }
  }
  turnMode = TurnMode::MainMenu;
  menuIndex = 0;
  requestUpdate();
}

void GolfZeroActivity::startFreshGame() {
  currentCourse = static_cast<uint8_t>(selectedCourse % COURSE_COUNT);
  currentHole = 0;
  mulligansUsed = 0;
  strokesTotal = 0;
  dieRoll = 0;
  previewDir = 0;
  usePutt = false;
  loadedFromSave = false;
  hasSaveFile = false;
  holeScores.fill(0);
  segmentCount = 0;
  setupHole();
}

GolfZeroActivity::HoleDef GolfZeroActivity::buildHoleDef(uint8_t courseIdx, uint8_t holeIdx) const {
  const CourseDef& course = COURSES[courseIdx % COURSE_COUNT];
  const uint32_t seed = course.baseSeed ^ (0x9E3779B9u * (static_cast<uint32_t>(holeIdx) + 1u));

  // Tee near bottom of grid, cup near top.
  uint8_t sy0 = static_cast<uint8_t>(GRID_H - 2 - (hashCoord(seed, 2, 3, 5) % 2));
  uint8_t cy  = static_cast<uint8_t>(1 + (hashCoord(seed, 6, 7, 11) % 2));
  if (lengthPreset == HoleLengthPreset::Short) {
    sy0 = static_cast<uint8_t>(GRID_H - 4 - (hashCoord(seed, 11, 12, 9) % 2));
    cy  = static_cast<uint8_t>(6 + (hashCoord(seed, 13, 14, 15) % 3));
  } else if (lengthPreset == HoleLengthPreset::Mixed) {
    cy  = static_cast<uint8_t>(3 + (hashCoord(seed, 15, 16, 17) % 3));
  }

  // Number of bends (2-4 intermediate waypoints between tee and cup).
  const uint8_t numBends = 2 + static_cast<uint8_t>(hashCoord(seed, 20, 21, 31) % 3);
  const uint8_t wpCount  = static_cast<uint8_t>(numBends + 2u);  // +tee +cup

  HoleDef hole{};
  hole.holeSeed = seed;
  hole.wpCount  = wpCount;

  const int totalY = static_cast<int>(sy0) - static_cast<int>(cy);  // rows top to bottom
  const int yStep  = std::max(2, totalY / (wpCount - 1));

  // Pick alternating left/right columns for each bend.
  // "Left zone": columns 1..GRID_W/2-1, "Right zone": GRID_W/2+1..GRID_W-2
  const int leftLo  = 1;
  const int leftHi  = GRID_W / 2 - 1;   // 1..3
  const int rightLo = GRID_W / 2 + 1;   // 5..7
  const int rightHi = GRID_W - 2;

  // Tee and cup X can be anywhere reasonable.
  uint8_t sx0 = static_cast<uint8_t>(2 + (hashCoord(seed, 1, 2, 3) % (GRID_W - 4)));
  uint8_t cx  = static_cast<uint8_t>(2 + (hashCoord(seed, 4, 5, 7) % (GRID_W - 4)));

  hole.wpX[0] = sx0;  hole.wpY[0] = sy0;

  // First bend side is determined by seed.
  bool goLeft = (hashCoord(seed, 40, 41, 43) & 1u) != 0u;

  for (uint8_t i = 1; i < wpCount - 1; ++i) {
    const int bY = static_cast<int>(sy0) - i * yStep;
    uint8_t bX;
    if (goLeft) {
      bX = static_cast<uint8_t>(leftLo + (hashCoord(seed, 50 + i, 51 + i, 53) % (leftHi - leftLo + 1)));
    } else {
      bX = static_cast<uint8_t>(rightLo + (hashCoord(seed, 60 + i, 61 + i, 67) % (rightHi - rightLo + 1)));
    }
    goLeft = !goLeft;
    hole.wpX[i] = bX;
    hole.wpY[i] = static_cast<uint8_t>(std::max(static_cast<int>(cy) + 2, bY));
  }

  hole.wpX[wpCount - 1] = cx;
  hole.wpY[wpCount - 1] = cy;
  hole.startX = sx0;
  hole.startY = sy0;
  hole.cupX   = cx;
  hole.cupY   = cy;

  return hole;
}

uint32_t GolfZeroActivity::hashCoord(uint32_t seed, int x, int y, uint8_t salt) const {
  uint32_t v = seed ^ (static_cast<uint32_t>(x) * 0x45d9f3bu) ^ (static_cast<uint32_t>(y) * 0x119de1f3u) ^ salt;
  v ^= v >> 16;
  v *= 0x7feb352du;
  v ^= v >> 15;
  v *= 0x846ca68bu;
  v ^= v >> 16;
  return v;
}

GolfZeroActivity::Terrain GolfZeroActivity::terrainAt(int x, int y, const HoleDef& hole) const {
  if (x < 0 || y < 0 || x >= GRID_W || y >= GRID_H) {
    return Terrain::Trees;
  }

  // Find the corridor centre-X at this row by interpolating through waypoints.
  // Waypoints are ordered from tee (high Y) to cup (low Y).
  // We scan segments until we find the one that spans row y.
  auto lerpCenterX = [&](int segY) -> int {
    for (int i = 0; i < static_cast<int>(hole.wpCount) - 1; ++i) {
      const int yA = static_cast<int>(hole.wpY[i]);
      const int yB = static_cast<int>(hole.wpY[i + 1]);
      // Segment may slope either direction; accept whichever spans segY.
      const int yLo = std::min(yA, yB);
      const int yHi = std::max(yA, yB);
      if (segY >= yLo && segY <= yHi) {
        const int span = yHi - yLo;
        if (span == 0) return static_cast<int>(hole.wpX[i]);
        const int xA = static_cast<int>(hole.wpX[i]);
        const int xB = static_cast<int>(hole.wpX[i + 1]);
        // Interpolate: t goes from yA toward yB.
        const int t = (yA > yB) ? (yA - segY) : (segY - yA);
        return xA + (xB - xA) * t / std::max(1, iabs(yA - yB));
      }
    }
    // Above cup or below tee — use nearest endpoint.
    if (segY > static_cast<int>(hole.wpY[0])) return static_cast<int>(hole.wpX[0]);
    return static_cast<int>(hole.wpX[hole.wpCount - 1]);
  };

  const int centerX = lerpCenterX(y);
  const int dist = iabs(x - centerX);

  // Within 1 cell of any waypoint: wider junction zone so bends are navigable.
  bool nearWaypoint = false;
  for (int i = 0; i < static_cast<int>(hole.wpCount); ++i) {
    if (iabs(y - static_cast<int>(hole.wpY[i])) <= 1 &&
        iabs(x - static_cast<int>(hole.wpX[i])) <= 2) {
      nearWaypoint = true;
      break;
    }
  }

  const int laneHalfWidth = nearWaypoint ? 2 : 1;

  // Outside the corridor = out of bounds (Trees).
  if (dist > laneHalfWidth) {
    return Terrain::Trees;
  }

  Terrain t = Terrain::Fairway;

  // Green around cup.
  const int cupDx = x - static_cast<int>(hole.cupX);
  const int cupDy = y - static_cast<int>(hole.cupY);
  if ((cupDx * cupDx + cupDy * cupDy) <= 4) {
    t = Terrain::Green;
  }

  // Tee and cup cells are always playable Green.
  if ((x == static_cast<int>(hole.startX) && y == static_cast<int>(hole.startY)) ||
      (x == static_cast<int>(hole.cupX)   && y == static_cast<int>(hole.cupY))) {
    return Terrain::Green;
  }

  if (t != Terrain::Green) {
    // Hazard scatter within the fairway corridor.
    const uint32_t h = hashCoord(hole.holeSeed, x, y, 9);
    if      ((h % 43u) == 0u) { t = Terrain::Sand; }
    else if ((h % 61u) == 0u) { t = Terrain::Mud; }
    else if ((h % 73u) == 0u) { t = Terrain::Chance; }
    else if ((h % 79u) == 0u) { t = Terrain::Water; }
    else if ((h % 89u) == 0u) { t = Terrain::Cactus; }
    else {
      // Slope accents.
      const uint32_t s = hashCoord(hole.holeSeed, x, y, 17);
      if ((s % 41u) == 0u) {
        const uint8_t dir = static_cast<uint8_t>(s % 6u);
        return static_cast<Terrain>(static_cast<uint8_t>(Terrain::SlopeN) + dir);
      }
    }
  } else {
    // Slopes appear on greens too (tricky putting).
    const uint32_t s = hashCoord(hole.holeSeed, x, y, 23);
    if ((s % 19u) == 0u) {
      const uint8_t dir = static_cast<uint8_t>(s % 6u);
      return static_cast<Terrain>(static_cast<uint8_t>(Terrain::SlopeN) + dir);
    }
  }

  return t;
}

bool GolfZeroActivity::isFairwayLike(Terrain t) const {
  return t == Terrain::Fairway || t == Terrain::Green;
}

bool GolfZeroActivity::isSlope(Terrain t) const {
  return t >= Terrain::SlopeN && t <= Terrain::SlopeNW;
}

int GolfZeroActivity::slopeDir(Terrain t) const {
  if (!isSlope(t)) return -1;
  return static_cast<int>(static_cast<uint8_t>(t) - static_cast<uint8_t>(Terrain::SlopeN)) % 6;
}

void GolfZeroActivity::setupHole() {
  const HoleDef hole = buildHoleDef(currentCourse, currentHole);
  ballX = hole.startX;
  ballY = hole.startY;
  strokesThisHole = 0;
  dieRoll = 0;
  previewDir = 0;
  selectedClub = ClubType::Iron;
  usePutt = false;
  rerolledTeeShot = false;
  segmentCount = 0;
  scorecardAdvanceAfterConfirm = false;
  pauseReturnMode = TurnMode::AwaitRoll;
  scorecardReturnMode = TurnMode::PauseMenu;
  resumeMode = TurnMode::AwaitRoll;
  turnMode = TurnMode::AwaitRoll;
  scrollY = 0;
}

int GolfZeroActivity::movementDistance(const HoleDef& hole) const {
  if (playStyle == PlayStyle::Club) {
    return clubDistanceFromTerrain(hole);
  }

  if (usePutt) {
    return 1;
  }

  int dist = static_cast<int>(dieRoll);
  const Terrain t = terrainAt(ballX, ballY, hole);
  if (t == Terrain::Sand) dist -= 1;
  if (dist < 1) dist = 1;
  return dist;
}

int GolfZeroActivity::clubDistanceFromTerrain(const HoleDef& hole) const {
  const Terrain t = terrainAt(ballX, ballY, hole);
  switch (selectedClub) {
    case ClubType::Putter:
      return 1;
    case ClubType::Driver:
      if (!isFairwayLike(t)) return 0;
      return 5;
    case ClubType::Iron:
      if (t == Terrain::Sand) return 2;
      return isFairwayLike(t) ? 3 : 2;
    case ClubType::Wedge:
      if (t == Terrain::Sand) return 2;
      return isFairwayLike(t) ? 2 : 1;
  }
  return 0;
}

bool GolfZeroActivity::canUseSelectedClub(const HoleDef& hole) const {
  return clubDistanceFromTerrain(hole) > 0;
}

const char* GolfZeroActivity::clubName() const {
  switch (selectedClub) {
    case ClubType::Driver:
      return "Driver";
    case ClubType::Iron:
      return "Iron";
    case ClubType::Wedge:
      return "Wedge";
    default:
      return "Putter";
  }
}

const char* GolfZeroActivity::playStyleName() const {
  return playStyle == PlayStyle::Dice ? "Dice" : "Club";
}

const char* GolfZeroActivity::lengthPresetName() const {
  switch (lengthPreset) {
    case HoleLengthPreset::Short:
      return "Short";
    case HoleLengthPreset::Long:
      return "Long";
    default:
      return "Mixed";
  }
}

void GolfZeroActivity::cycleClub(int delta) {
  int c = static_cast<int>(selectedClub) + delta;
  while (c < 0) c += 4;
  while (c >= 4) c -= 4;
  selectedClub = static_cast<ClubType>(c);
}

bool GolfZeroActivity::pathCrossesTrees(const HoleDef& hole, int fromX, int fromY, int toX, int toY, bool fromFairway) const {
  if (fromFairway) {
    return false;
  }

  const int dx = (toX > fromX) ? 1 : ((toX < fromX) ? -1 : 0);
  const int dy = (toY > fromY) ? 1 : ((toY < fromY) ? -1 : 0);
  int x = fromX;
  int y = fromY;
  while (x != toX || y != toY) {
    x += dx;
    y += dy;
    if (terrainAt(x, y, hole) == Terrain::Trees) {
      return true;
    }
  }
  return false;
}

bool GolfZeroActivity::advanceHexStep(int& x, int& y, uint8_t edgeDir) const {
  const bool odd = (y & 1) != 0;
  int nx = x;
  int ny = y;
  switch (edgeDir % 6) {
    case 0:  // E
      nx += 1;
      break;
    case 1:  // NE
      nx += odd ? 1 : 0;
      ny -= 1;
      break;
    case 2:  // NW
      nx += odd ? 0 : -1;
      ny -= 1;
      break;
    case 3:  // W
      nx -= 1;
      break;
    case 4:  // SW
      nx += odd ? 0 : -1;
      ny += 1;
      break;
    case 5:  // SE
      nx += odd ? 1 : 0;
      ny += 1;
      break;
  }
  if (nx < 0 || ny < 0 || nx >= GRID_W || ny >= GRID_H) {
    return false;
  }
  x = nx;
  y = ny;
  return true;
}

void GolfZeroActivity::applySlopeRoll(const HoleDef& hole, uint8_t& x, uint8_t& y) const {
  for (int i = 0; i < 8; ++i) {
    Terrain t = terrainAt(x, y, hole);
    if (!isSlope(t)) {
      return;
    }

    const int d = slopeDir(t);
    int nx = static_cast<int>(x);
    int ny = static_cast<int>(y);
    if (!advanceHexStep(nx, ny, static_cast<uint8_t>(d))) {
      return;
    }
    if (terrainAt(nx, ny, hole) == Terrain::Water) {
      return;
    }

    x = static_cast<uint8_t>(nx);
    y = static_cast<uint8_t>(ny);
  }
}

bool GolfZeroActivity::computePreviewLanding(uint8_t& outX, uint8_t& outY, bool& outInCup, bool& outPathValid) const {
  const HoleDef hole = buildHoleDef(currentCourse, currentHole);
  const int dist = movementDistance(hole);
  if (dist <= 0) {
    outPathValid = false;
    outInCup = false;
    outX = ballX;
    outY = ballY;
    return false;
  }

  int x = ballX;
  int y = ballY;
  int cupStep = -1;
  int cactusX = -1;
  int cactusY = -1;
  const bool fromFairway = isFairwayLike(terrainAt(ballX, ballY, hole));

  for (int step = 1; step <= dist; ++step) {
    uint8_t edgeDir;
    if (previewDir < 6) {
      edgeDir = previewDir;
    } else {
      const uint8_t corner = static_cast<uint8_t>(previewDir - 6);
      edgeDir = static_cast<uint8_t>((step & 1) ? corner : ((corner + 1) % 6));
    }

    if (!advanceHexStep(x, y, edgeDir)) {
      outPathValid = false;
      outInCup = false;
      outX = ballX;
      outY = ballY;
      return false;
    }

    const Terrain stepTerrain = terrainAt(x, y, hole);
    if (!fromFairway && stepTerrain == Terrain::Trees) {
      outPathValid = false;
      outInCup = false;
      outX = ballX;
      outY = ballY;
      return false;
    }

    if (stepTerrain == Terrain::Cactus && cactusX < 0) {
      cactusX = x;
      cactusY = y;
    }

    if (x == hole.cupX && y == hole.cupY && cupStep < 0) {
      cupStep = step;
    }
  }

  if (cactusX >= 0) {
    x = cactusX;
    y = cactusY;
  }

  Terrain land = terrainAt(x, y, hole);
  if (land == Terrain::Water || land == Terrain::Trees) {
    outPathValid = false;
    outInCup = false;
    outX = ballX;
    outY = ballY;
    return false;
  }

  outInCup = false;
  if (cupStep > 0 && (dist - cupStep) <= 1) {
    x = hole.cupX;
    y = hole.cupY;
    outInCup = true;
  }

  uint8_t rx = static_cast<uint8_t>(x);
  uint8_t ry = static_cast<uint8_t>(y);
  if (!outInCup) {
    applySlopeRoll(hole, rx, ry);
    outInCup = (rx == hole.cupX && ry == hole.cupY);
  }

  outPathValid = true;
  outX = rx;
  outY = ry;
  return true;
}

void GolfZeroActivity::rollDie() {
  if (playStyle != PlayStyle::Dice) {
    return;
  }
  dieRoll = static_cast<uint8_t>((esp_random() % 6u) + 1u);
  usePutt = false;
  turnMode = TurnMode::Aim;
}

void GolfZeroActivity::rotateDirection(int delta) {
  const uint8_t original = previewDir;
  int d = static_cast<int>(previewDir);
  for (int i = 0; i < DIRECTION_COUNT; ++i) {
    d += delta;
    while (d < 0) d += DIRECTION_COUNT;
    while (d >= DIRECTION_COUNT) d -= DIRECTION_COUNT;
    const uint8_t cand = static_cast<uint8_t>(d);
    if (hasValidDirection(cand)) {
      previewDir = cand;
      return;
    }
  }
  previewDir = original;
}

bool GolfZeroActivity::hasValidDirection(uint8_t dir) const {
  const uint8_t old = previewDir;
  auto* self = const_cast<GolfZeroActivity*>(this);
  self->previewDir = static_cast<uint8_t>(dir % DIRECTION_COUNT);
  uint8_t px = ballX;
  uint8_t py = ballY;
  bool inCup = false;
  bool valid = false;
  const bool ok = computePreviewLanding(px, py, inCup, valid);
  self->previewDir = old;
  return ok && valid;
}

void GolfZeroActivity::snapToValidDirection() {
  if (hasValidDirection(previewDir)) {
    return;
  }

  const uint8_t original = previewDir;
  for (int i = 1; i < DIRECTION_COUNT; ++i) {
    const uint8_t cand = static_cast<uint8_t>((static_cast<int>(original) + i) % DIRECTION_COUNT);
    if (hasValidDirection(cand)) {
      previewDir = cand;
      return;
    }
  }
}

void GolfZeroActivity::togglePuttMode() {
  if (turnMode != TurnMode::Aim || playStyle != PlayStyle::Dice) return;
  usePutt = !usePutt;
}

void GolfZeroActivity::finishHole() {
  holeScores[currentHole] = strokesThisHole;
  strokesTotal = 0;
  for (uint8_t i = 0; i < HOLES_PER_COURSE; ++i) {
    strokesTotal = static_cast<uint8_t>(strokesTotal + holeScores[i]);
  }
  turnMode = TurnMode::HoleComplete;
}

void GolfZeroActivity::advanceHoleOrCourse() {
  if (currentHole + 1 < HOLES_PER_COURSE) {
    currentHole++;
    setupHole();
    return;
  }

  if (currentCourse + 1 < COURSE_COUNT) {
    currentCourse++;
    currentHole = 0;
    holeScores.fill(0);
    mulligansUsed = 0;
    setupHole();
    return;
  }

  turnMode = TurnMode::CourseComplete;
  clearSave();
}

void GolfZeroActivity::confirmShot() {
  if (turnMode != TurnMode::Aim) {
    return;
  }

  const HoleDef holeCtx = buildHoleDef(currentCourse, currentHole);
  if (playStyle == PlayStyle::Club && !canUseSelectedClub(holeCtx)) {
    renderMessage("That club cannot be used from this lie.");
    requestUpdate();
    return;
  }

  uint8_t nx = ballX;
  uint8_t ny = ballY;
  bool inCup = false;
  bool valid = false;
  if (!computePreviewLanding(nx, ny, inCup, valid) || !valid) {
    renderMessage("Blocked path. Choose another line.");
    requestUpdate();
    return;
  }

  if (segmentCount < MAX_SEGMENTS) {
    segments[segmentCount++] = ShotSegment{ballX, ballY, nx, ny};
  }

  ballX = nx;
  ballY = ny;
  strokesThisHole = static_cast<uint8_t>(strokesThisHole + 1);

  const HoleDef hole = buildHoleDef(currentCourse, currentHole);
  const Terrain land = terrainAt(ballX, ballY, hole);
  if (land == Terrain::Cactus) {
    strokesThisHole = static_cast<uint8_t>(strokesThisHole + 1);
    renderMessage("Cactus! +1 penalty stroke.");
  } else if (land == Terrain::Mud) {
    // Mud penalty with rollback to prior safe ball position.
    if (segmentCount > 0) {
      const ShotSegment& s = segments[segmentCount - 1];
      ballX = s.fromX;
      ballY = s.fromY;
    }
    strokesThisHole = static_cast<uint8_t>(strokesThisHole + 1);
    renderMessage("Mud! +1 stroke and drop back.");
  } else if (land == Terrain::Chance) {
    if ((esp_random() & 1u) == 0u) {
      if (mulligansUsed > 0) {
        mulligansUsed--;
      }
      renderMessage("Chance: Lucky bounce! Mulligan refunded.");
    } else {
      mulligansUsed = static_cast<uint8_t>(std::min<int>(MAX_MULLIGANS, mulligansUsed + 1));
      renderMessage("Chance: Tough break. Mulligan lost.");
    }
  } else if (land == Terrain::Bridge) {
    if (mulligansUsed > 0) {
      mulligansUsed--;
    }
    renderMessage("Bridge bonus! Mulligan refunded.");
  }

  if (inCup) {
    finishHole();
  } else {
    dieRoll = 0;
    usePutt = false;
    turnMode = TurnMode::AwaitRoll;
  }
}

void GolfZeroActivity::rerollIfAllowed() {
  if (turnMode != TurnMode::Aim || playStyle != PlayStyle::Dice) {
    return;
  }

  const bool isTee = (strokesThisHole == 0);
  if (isTee && !rerolledTeeShot) {
    rerolledTeeShot = true;
    rollDie();
    return;
  }

  if (mulligansUsed < MAX_MULLIGANS) {
    mulligansUsed++;
    rollDie();
    return;
  }

  renderMessage("No mulligans left.");
}

bool GolfZeroActivity::saveGame(bool showMessage) {
  Storage.mkdir(SAVE_DIR);

  FsFile f;
  if (!Storage.openFileForWrite("GZF", SAVE_PATH, f)) {
    if (showMessage) {
      renderMessage("Save failed");
    }
    return false;
  }

  serialization::writePod(f, SAVE_VERSION);
  serialization::writePod(f, currentCourse);
  serialization::writePod(f, currentHole);
  serialization::writePod(f, mulligansUsed);
  serialization::writePod(f, strokesThisHole);
  serialization::writePod(f, strokesTotal);
  serialization::writePod(f, dieRoll);
  serialization::writePod(f, previewDir);
  serialization::writePod(f, playStyle);
  serialization::writePod(f, lengthPreset);
  serialization::writePod(f, selectedClub);
  serialization::writePod(f, usePutt);
  serialization::writePod(f, rerolledTeeShot);
  serialization::writePod(f, ballX);
  serialization::writePod(f, ballY);
  // Persist gameplay mode, not transient overlays.
  TurnMode modeToSave = turnMode;
  if (turnMode == TurnMode::PauseMenu || turnMode == TurnMode::Scorecard) {
    modeToSave = pauseReturnMode;
  }
  serialization::writePod(f, modeToSave);
  serialization::writePod(f, holeScores);
  serialization::writePod(f, segmentCount);
  for (uint16_t i = 0; i < segmentCount && i < MAX_SEGMENTS; ++i) {
    serialization::writePod(f, segments[i]);
  }
  f.close();

  loadedFromSave = true;
  hasSaveFile = true;
  if (showMessage) {
    renderMessage("Progress saved");
  }
  return true;
}

bool GolfZeroActivity::loadGame() {
  FsFile f;
  if (!Storage.openFileForRead("GZF", SAVE_PATH, f)) {
    return false;
  }

  uint8_t version = 0;
  serialization::readPod(f, version);
  if (version != SAVE_VERSION) {
    f.close();
    return false;
  }

  serialization::readPod(f, currentCourse);
  serialization::readPod(f, currentHole);
  serialization::readPod(f, mulligansUsed);
  serialization::readPod(f, strokesThisHole);
  serialization::readPod(f, strokesTotal);
  serialization::readPod(f, dieRoll);
  serialization::readPod(f, previewDir);
  if (version >= 2) {
    serialization::readPod(f, playStyle);
    serialization::readPod(f, lengthPreset);
    serialization::readPod(f, selectedClub);
  } else {
    playStyle = PlayStyle::Dice;
    lengthPreset = HoleLengthPreset::Mixed;
    selectedClub = ClubType::Iron;
  }
  serialization::readPod(f, usePutt);
  serialization::readPod(f, rerolledTeeShot);
  serialization::readPod(f, ballX);
  serialization::readPod(f, ballY);
  serialization::readPod(f, turnMode);
  serialization::readPod(f, holeScores);
  serialization::readPod(f, segmentCount);

  if (currentCourse >= COURSE_COUNT) currentCourse = 0;
  if (currentHole >= HOLES_PER_COURSE) currentHole = 0;
  if (segmentCount > MAX_SEGMENTS) segmentCount = MAX_SEGMENTS;
  for (uint16_t i = 0; i < segmentCount; ++i) {
    serialization::readPod(f, segments[i]);
  }

  f.close();
  loadedFromSave = true;
  return true;
}

void GolfZeroActivity::clearSave() {
  Storage.remove(SAVE_PATH);
  hasSaveFile = false;
  loadedFromSave = false;
}

int GolfZeroActivity::sx(int gridX, int gridY) const {
  (void)gridY;
  const int stepX = (cellSize * 3) / 2;
  return gridLeft + gridX * stepX;
}

int GolfZeroActivity::sy(int gridX, int gridY) const {
  const int stepY = std::max(6, (cellSize * 1732) / 1000);
  const int colOffset = (gridX & 1) ? (stepY / 2) : 0;
  return gridTop + gridY * stepY + colOffset - scrollY;
}

void GolfZeroActivity::drawHexCell(int cx, int cy, int radius, bool state) const {
  const int rx = std::max(3, radius);
  const int ry = std::max(3, (radius * 866) / 1000);
  const int xPts[6] = {cx - rx / 2, cx + rx / 2, cx + rx, cx + rx / 2, cx - rx / 2, cx - rx};
  const int yPts[6] = {cy - ry, cy - ry, cy, cy + ry, cy + ry, cy};

  for (int i = 0; i < 6; ++i) {
    const int j = (i + 1) % 6;
    renderer.drawLine(xPts[i], yPts[i], xPts[j], yPts[j], state);
  }

  // Add an inset outline to make the hex field read denser on e-ink.
  if (radius >= 6) {
    const int irx = std::max(2, rx - 2);
    const int iry = std::max(2, ry - 2);
    const int ixPts[6] = {cx - irx / 2, cx + irx / 2, cx + irx, cx + irx / 2, cx - irx / 2, cx - irx};
    const int iyPts[6] = {cy - iry, cy - iry, cy, cy + iry, cy + iry, cy};
    for (int i = 0; i < 6; ++i) {
      const int j = (i + 1) % 6;
      renderer.drawLine(ixPts[i], iyPts[i], ixPts[j], iyPts[j], state);
    }
  }
}

void GolfZeroActivity::fillHexCellSolid(int cx, int cy, int radius, bool state) const {
  const int rx = std::max(3, radius - 1);
  const int ry = std::max(3, (radius * 866) / 1000 - 1);

  for (int dy = -ry; dy <= ry; ++dy) {
    const int ay = iabs(dy);
    const int half = std::max(1, rx - (rx * ay) / (2 * std::max(1, ry)));
    renderer.drawLine(cx - half, cy + dy, cx + half, cy + dy, state);
  }
}

void GolfZeroActivity::fillHexCellPattern(int cx, int cy, int radius, uint8_t spacing) const {
  const int rx = std::max(3, radius - 1);
  const int ry = std::max(3, (radius * 866) / 1000 - 1);
  const int gap = std::max<int>(1, spacing);

  for (int dy = -ry; dy <= ry; ++dy) {
    if (((dy + ry) % gap) != 0) {
      continue;
    }
    const int ay = iabs(dy);
    const int half = std::max(1, rx - (rx * ay) / (2 * std::max(1, ry)));
    renderer.drawLine(cx - half, cy + dy, cx + half, cy + dy, true);
  }
}

void GolfZeroActivity::drawSideRails(const HoleDef& hole) {
  (void)hole;

  const int w = renderer.getScreenWidth();
  const int top = STATUS_HEIGHT + 2;
  const int bottom = renderer.getScreenHeight() - 4;
  const int leftRailX = 4;
  const int rightRailX = w - 20;
  const int railW = 16;

  renderer.drawRect(leftRailX, top, railW, bottom - top, true);
  renderer.drawRect(rightRailX, top, railW, bottom - top, true);

  for (int y = top + 4; y < bottom - 4; y += 10) {
    renderer.drawLine(leftRailX + 3, y, leftRailX + railW - 4, y + 4, true);
    renderer.drawLine(rightRailX + 3, y, rightRailX + railW - 4, y + 4, true);
  }
}

void GolfZeroActivity::drawGrid(const HoleDef& hole) {
  auto drawTreeMask = [&](int centerX, int topY, const uint16_t* rows, int rowCount, int scale) {
    constexpr int maskW = 9;
    const int px = std::max(1, scale);
    auto cellSet = [&](int r, int c) -> bool {
      if (r < 0 || r >= rowCount || c < 0 || c >= maskW) {
        return false;
      }
      const uint16_t bit = static_cast<uint16_t>(1u << (maskW - 1 - c));
      return (rows[r] & bit) != 0u;
    };

    // Fill interior with very dark gray.
    for (int r = 0; r < rowCount; ++r) {
      const uint16_t bits = rows[r];
      for (int c = 0; c < maskW; ++c) {
        const uint16_t bit = static_cast<uint16_t>(1u << (maskW - 1 - c));
        if ((bits & bit) != 0u) {
          renderer.fillRectDither(centerX + (c - 4) * px, topY + r * px, px, px, Color::DarkGray);
        }
      }
    }

    // Add black outline along the glyph perimeter.
    for (int r = 0; r < rowCount; ++r) {
      for (int c = 0; c < maskW; ++c) {
        if (!cellSet(r, c)) {
          continue;
        }
        if (!cellSet(r - 1, c) || !cellSet(r + 1, c) || !cellSet(r, c - 1) || !cellSet(r, c + 1)) {
          if (px == 1) {
            renderer.fillRect(centerX + (c - 4), topY + r, 1, 1, true);
          } else {
            renderer.drawRect(centerX + (c - 4) * px, topY + r * px, px, px, true);
          }
        }
      }
    }
  };

  auto drawHexWithNatureIcon = [&](int cx, int cy, int iconR, bool cactusVariant, uint8_t treeStyle) {
    if (!cactusVariant) {
      const int u = std::max(1, iconR / 6);
      const int baseY = cy + std::max(1, u);  // start lower in tile
      if ((treeStyle & 1u) == 0u) {
        // Conifer icon: fixed bitmap mask.
        drawTreeMask(cx, baseY - 10 * u, CONIFER_MASK, 11, u);
      } else {
        // Broadleaf icon: fixed bitmap mask.
        drawTreeMask(cx, baseY - 10 * u, BROADLEAF_MASK, 11, u);
      }
    } else {
      // Cactus icon: saguaro bitmap mask, same rendering pipeline as trees.
      const int u = std::max(1, iconR / 6);
      const int baseY = cy + std::max(1, u);
      drawTreeMask(cx, baseY - 10 * u, CACTUS_MASK, 11, u);
    }
  };

  for (int y = 0; y < GRID_H; ++y) {
    for (int x = 0; x < GRID_W; ++x) {
      const int px = sx(x, y);
      const int py = sy(x, y);
      const Terrain t = terrainAt(x, y, hole);
      const int s = std::max(4, cellSize / 3);

      drawHexCell(px, py, cellSize, true);

      if (t == Terrain::Fairway) {
        // Lighter hatch so fairway is visually distinct from rough/trees.
        fillHexCellPattern(px, py, cellSize, 4);
      } else if (t == Terrain::Green) {
        // Very light: sparse hatch.
        fillHexCellPattern(px, py, cellSize, 8);
      } else if (t == Terrain::Sand) {
        // Sand: slanted grain marks (distinct from water and fairway hatch).
        renderer.drawLine(px - s, py + s / 2, px - s / 3, py - s / 3, 2, true);
        renderer.drawLine(px - s / 4, py + s, px + s / 2, py, 2, true);
        renderer.drawLine(px + s / 4, py + s / 2, px + s, py - s / 2, 2, true);
      } else if (t == Terrain::Water) {
        // Water uses a larger stylized crest/trough motif over the plain tile.
        const int rx = std::max(3, cellSize - 1);
        const int ry = std::max(3, (cellSize * 866) / 1000 - 1);
        const int waveRows[2] = {-ry / 3, ry / 3};
        for (int row = 0; row < 2; ++row) {
          const int dy = waveRows[row];
          const int ay = iabs(dy);
          const int half = std::max(4, rx - (rx * ay) / (2 * std::max(1, ry)));
          const int wx0 = px - half + 2;
          const int wx1 = px + half - 11;
          for (int wx = wx0; wx <= wx1; wx += 12) {
            renderer.drawLine(wx, py + dy, wx + 2, py + dy - 2, 2, true);
            renderer.drawLine(wx + 2, py + dy - 2, wx + 4, py + dy, 2, true);
            renderer.drawLine(wx + 4, py + dy, wx + 6, py + dy, 2, true);
            renderer.drawLine(wx + 6, py + dy, wx + 8, py + dy - 2, 2, true);
            renderer.drawLine(wx + 8, py + dy - 2, wx + 10, py + dy, 2, true);
          }
        }
      } else if (t == Terrain::Bridge) {
        renderer.drawLine(px - s, py - s, px + s, py + s, 2, true);
        renderer.drawLine(px - s, py + s, px + s, py - s, 2, true);
        renderer.drawRect(px - s, py - s, s * 2, s * 2, true);
      } else if (t == Terrain::Mud) {
        fillHexCellPattern(px, py, cellSize - 1, 3);
        renderer.drawLine(px - s, py, px + s, py, 2, true);
      } else if (t == Terrain::Chance) {
        renderer.drawText(UI_10_FONT_ID, px - 5, py - 6, "?", true, EpdFontFamily::BOLD);
      } else if (isSlope(t)) {
        const int d = slopeDir(t);
        renderer.drawLine(px, py, px + EDGE_VEC_X[d] * (s + 2), py + EDGE_VEC_Y[d] * (s + 2), 2, true);
      }

      // Keep all hex borders crisp black after interior/icon rendering.
      drawHexCell(px, py, cellSize, true);
    }
  }

  // Second pass: draw trees and cactus on top of hex borders so they appear "standing"
  for (int y = 0; y < GRID_H; ++y) {
    for (int x = 0; x < GRID_W; ++x) {
      const int px = sx(x, y);
      const int py = sy(x, y);
      const Terrain t = terrainAt(x, y, hole);
      const int s = std::max(4, cellSize / 3);

      if (t == Terrain::Trees) {
        const uint32_t th = hashCoord(hole.holeSeed, x, y, 29);
        // Keep style simple and stable: upper half tends round trees, lower half conifers.
        uint8_t treeStyle = (y < (GRID_H / 2)) ? 1 : 0;
        if ((th % 5u) == 0u) {
          treeStyle = static_cast<uint8_t>(1u - treeStyle);
        }
        drawHexWithNatureIcon(px, py, std::max(8, cellSize), false, treeStyle);
      } else if (t == Terrain::Cactus) {
        drawHexWithNatureIcon(px, py, std::max(4, s), true, 0);
      }
    }
  }
}

void GolfZeroActivity::drawSegments() {
  for (uint16_t i = 0; i < segmentCount; ++i) {
    const ShotSegment& s = segments[i];
    // Haloed stroke: white underlay keeps the path visible over dark fairway.
    renderer.drawLine(sx(s.fromX, s.fromY), sy(s.fromX, s.fromY), sx(s.toX, s.toY), sy(s.toX, s.toY), 5, false);
    renderer.drawLine(sx(s.fromX, s.fromY), sy(s.fromX, s.fromY), sx(s.toX, s.toY), sy(s.toX, s.toY), 3, true);
  }
}

void GolfZeroActivity::drawBallAndCup(const HoleDef& hole, uint8_t previewX, uint8_t previewY, bool inAim, bool previewValid) {
  const int cx = sx(hole.cupX, hole.cupY);
  const int cy = sy(hole.cupX, hole.cupY);
  renderer.fillRect(cx - 6, cy - 6, 12, 12, false);
  renderer.drawRoundedRect(cx - 7, cy - 7, 14, 14, 1, 2, true);
  renderer.fillRect(cx - 2, cy - 1, 4, 2, true);   // hole opening
  renderer.drawLine(cx, cy - 20, cx, cy - 1, 3, true);  // flag pole centered over cup
  renderer.fillRect(cx + 2, cy - 18, 12, 8, true);      // solid, prominent flag
  renderer.fillRect(sx(ballX, ballY) - BALL_R, sy(ballX, ballY) - BALL_R, BALL_R * 2, BALL_R * 2, true);

  if (inAim) {
    const int bx = sx(ballX, ballY);
    const int by = sy(ballX, ballY);
    const int ex = sx(previewX, previewY);
    const int ey = sy(previewX, previewY);
    renderer.drawLine(bx, by, ex, ey, 5, false);
    renderer.drawLine(bx, by, ex, ey, 3, true);
    renderer.drawRect(ex - 4, ey - 4, 8, 8, true);
    if (!previewValid) {
      renderer.drawLine(ex - 5, ey - 5, ex + 5, ey + 5, true);
      renderer.drawLine(ex - 5, ey + 5, ex + 5, ey - 5, true);
    }
  }
}

void GolfZeroActivity::drawHud(const HoleDef& hole) {
  const int keyW = 212;
  const int keyX = renderer.getScreenWidth() - keyW - 6;
  const int panelX = 4;
  const int panelY = 8;
  const int panelW = std::max(150, keyX - panelX - 6);
  const int panelH = 108;

  // Keep HUD text readable even when course scrolls behind the top area.
  renderer.fillRect(panelX, panelY, panelW, panelH, false);
  renderer.drawRect(panelX, panelY, panelW, panelH, true);

  char line1[64] = {};
  snprintf(line1, sizeof(line1), "%s", COURSES[currentCourse].name);
  renderer.drawText(UI_10_FONT_ID, panelX + 6, panelY + 8, line1, true, EpdFontFamily::BOLD);

  char holeInfo[32] = {};
  snprintf(holeInfo, sizeof(holeInfo), "Hole %d/%d", currentHole + 1, HOLES_PER_COURSE);
  renderer.drawText(SMALL_FONT_ID, panelX + 6, panelY + 22, holeInfo, true);

  char line2[96] = {};
  snprintf(line2, sizeof(line2), "Strokes %u  Par %u  %s/%s", strokesThisHole, PAR_PER_HOLE, playStyleName(),
           lengthPresetName());
  renderer.drawText(SMALL_FONT_ID, panelX + 6, panelY + 34, line2, true);

  if (turnMode == TurnMode::AwaitRoll) {
    if (playStyle == PlayStyle::Dice) {
      char line3[96] = {};
      snprintf(line3, sizeof(line3), "Mulligans %u/%u  Confirm: Roll d6", mulligansUsed, MAX_MULLIGANS);
      renderer.drawText(SMALL_FONT_ID, panelX + 6, panelY + 50, line3, true);
    } else {
      char line3[96] = {};
      snprintf(line3, sizeof(line3), "Club: %s  Confirm: Aim shot", clubName());
      renderer.drawText(SMALL_FONT_ID, panelX + 6, panelY + 50, line3, true);
      renderer.drawText(SMALL_FONT_ID, panelX + 6, panelY + 66, "Up/Down: club  Left/Right: scroll", true);
    }
  } else if (turnMode == TurnMode::Aim) {
    char line3[96] = {};
    if (playStyle == PlayStyle::Dice) {
      snprintf(line3, sizeof(line3), "Roll=%u  Move=%d  %s", dieRoll, movementDistance(hole),
               usePutt ? "Putt" : "Shot");
    } else {
      snprintf(line3, sizeof(line3), "Club=%s  Move=%d", clubName(), movementDistance(hole));
    }
    renderer.drawText(SMALL_FONT_ID, panelX + 6, panelY + 50, line3, true);
    renderer.drawText(SMALL_FONT_ID, panelX + 6, panelY + 66, "Arrows: 12-dir hex shot  Confirm: place", true);
    if (playStyle == PlayStyle::Dice) {
      renderer.drawText(SMALL_FONT_ID, panelX + 6, panelY + 82, "Up: toggle putt  Down: reroll", true);
    } else {
      renderer.drawText(SMALL_FONT_ID, panelX + 6, panelY + 82, "Up/Down: club  Back: menu", true);
    }
  } else if (turnMode == TurnMode::HoleComplete) {
    char done[80] = {};
    snprintf(done, sizeof(done), "Holed in %u strokes. Confirm: next hole", strokesThisHole);
    renderer.drawText(SMALL_FONT_ID, panelX + 6, panelY + 50, done, true);
  } else if (turnMode == TurnMode::CourseComplete) {
    char done[80] = {};
    snprintf(done, sizeof(done), "Round complete. Total strokes: %u", strokesTotal);
    renderer.drawText(SMALL_FONT_ID, panelX + 6, panelY + 50, done, true);
    renderer.drawText(SMALL_FONT_ID, panelX + 6, panelY + 66, "Confirm: restart  Back: exit", true);
  }

  if (loadedFromSave) {
    renderer.drawText(SMALL_FONT_ID, panelX + panelW - 48, panelY + 10, "Saved", true);
  }
}

void GolfZeroActivity::drawInCourseKey() {
  const int w = 212;
  const int h = 126;
  const int x = renderer.getScreenWidth() - w - 6;
  const int y = 4;

  renderer.fillRect(x, y, w, h, false);
  renderer.drawRect(x, y, w, h, true);
  renderer.drawText(SMALL_FONT_ID, x + 92, y + 4, "Key", true, EpdFontFamily::BOLD);

  const int leftIconX = x + 8;
  const int leftTextX = x + 30;
  const int rightIconX = x + 108;
  const int rightTextX = x + 130;
  const int row0 = y + 22;
  const int rowStep = 18;
  const int iconYOffset = 8;

  // Left column rows (icon and description aligned per row).
  renderer.fillRectDither(leftIconX, row0 + iconYOffset, 14, 14, Color::DarkGray);
  renderer.drawRect(leftIconX, row0 + iconYOffset, 14, 14, true);
  renderer.drawText(SMALL_FONT_ID, leftTextX, row0 + 3, "Fairway", true);

  renderer.fillRectDither(leftIconX, row0 + rowStep + iconYOffset, 14, 14, Color::LightGray);
  renderer.drawRect(leftIconX, row0 + rowStep + iconYOffset, 14, 14, true);
  renderer.drawText(SMALL_FONT_ID, leftTextX, row0 + rowStep + 3, "Green", true);

  renderer.drawLine(leftIconX, row0 + rowStep * 2 + 18, leftIconX + 5, row0 + rowStep * 2 + 12, 2, true);
  renderer.drawLine(leftIconX + 4, row0 + rowStep * 2 + 22, leftIconX + 10, row0 + rowStep * 2 + 15, 2, true);
  renderer.drawLine(leftIconX + 8, row0 + rowStep * 2 + 18, leftIconX + 14, row0 + rowStep * 2 + 12, 2, true);
  renderer.drawText(SMALL_FONT_ID, leftTextX, row0 + rowStep * 2 + 3, "Sand", true);

  renderer.drawText(UI_10_FONT_ID, leftIconX + 4, row0 + rowStep * 3 + 9, "?", true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, leftTextX, row0 + rowStep * 3 + 3, "Chance", true);

  renderer.drawLine(leftIconX, row0 + rowStep * 4 + 19, leftIconX + 14, row0 + rowStep * 4 + 19, 2, true);
  renderer.drawText(SMALL_FONT_ID, leftTextX, row0 + rowStep * 4 + 3, "Mud", true);

  // Right column rows (icon and description aligned per row).
  renderer.fillRectDither(rightIconX, row0 + iconYOffset, 14, 14, Color::LightGray);
  renderer.drawRoundedRect(rightIconX, row0 + iconYOffset, 14, 14, 1, 3, true);
  renderer.drawLine(rightIconX + 2, row0 + 13, rightIconX + 5, row0 + 12, 2, true);
  renderer.drawLine(rightIconX + 5, row0 + 12, rightIconX + 8, row0 + 13, 2, true);
  renderer.drawLine(rightIconX + 8, row0 + 13, rightIconX + 11, row0 + 12, 2, true);
  renderer.drawLine(rightIconX + 2, row0 + 16, rightIconX + 5, row0 + 15, 2, true);
  renderer.drawLine(rightIconX + 5, row0 + 15, rightIconX + 8, row0 + 16, 2, true);
  renderer.drawLine(rightIconX + 8, row0 + 16, rightIconX + 11, row0 + 15, 2, true);
  renderer.drawLine(rightIconX + 2, row0 + 19, rightIconX + 5, row0 + 18, 2, true);
  renderer.drawLine(rightIconX + 5, row0 + 18, rightIconX + 8, row0 + 19, 2, true);
  renderer.drawLine(rightIconX + 8, row0 + 19, rightIconX + 11, row0 + 18, 2, true);
  renderer.drawText(SMALL_FONT_ID, rightTextX, row0 + 3, "Water", true);

  drawHexCell(rightIconX + 7, row0 + rowStep + 15, 7, true);
  // Trees sample: exact same fixed glyph masks used on the board.
  auto drawLegendTreeMask = [&](int centerX, int topY, const uint16_t* rows) {
    constexpr int maskW = 9;
    auto cellSet = [&](int r, int c) -> bool {
      if (r < 0 || r >= 11 || c < 0 || c >= maskW) {
        return false;
      }
      const uint16_t bit = static_cast<uint16_t>(1u << (maskW - 1 - c));
      return (rows[r] & bit) != 0u;
    };

    for (int r = 0; r < 11; ++r) {
      const uint16_t bits = rows[r];
      for (int c = 0; c < maskW; ++c) {
        if ((bits & static_cast<uint16_t>(1u << (maskW - 1 - c))) != 0u) {
          renderer.fillRectDither(centerX + (c - 4), topY + r, 1, 1, Color::DarkGray);
        }
      }
    }

    for (int r = 0; r < 11; ++r) {
      for (int c = 0; c < maskW; ++c) {
        if (!cellSet(r, c)) {
          continue;
        }
        if (!cellSet(r - 1, c) || !cellSet(r + 1, c) || !cellSet(r, c - 1) || !cellSet(r, c + 1)) {
          renderer.fillRect(centerX + (c - 4), topY + r, 1, 1, true);
        }
      }
    }
  };
  drawLegendTreeMask(rightIconX + 5, row0 + rowStep + 11, CONIFER_MASK);
  drawLegendTreeMask(rightIconX + 10, row0 + rowStep + 11, BROADLEAF_MASK);
  renderer.drawText(SMALL_FONT_ID, rightTextX, row0 + rowStep + 3, "Trees", true);

  drawHexCell(rightIconX + 7, row0 + rowStep * 2 + 15, 6, true);
  renderer.drawLine(rightIconX + 7, row0 + rowStep * 2 + 11, rightIconX + 7, row0 + rowStep * 2 + 23, 2, true);
  renderer.drawLine(rightIconX + 2, row0 + rowStep * 2 + 14, rightIconX + 6, row0 + rowStep * 2 + 14, 2, true);
  renderer.drawLine(rightIconX + 8, row0 + rowStep * 2 + 19, rightIconX + 12, row0 + rowStep * 2 + 19, 2, true);
  renderer.drawText(SMALL_FONT_ID, rightTextX, row0 + rowStep * 2 + 3, "Cactus", true);

  renderer.drawRoundedRect(rightIconX, row0 + rowStep * 3 + iconYOffset, 14, 14, 1, 2, true);
  renderer.drawLine(rightIconX + 8, row0 + rowStep * 3 + 10, rightIconX + 8, row0 + rowStep * 3 + 19, true);
  renderer.drawLine(rightIconX + 8, row0 + rowStep * 3 + 10, rightIconX + 12, row0 + rowStep * 3 + 12, true);
  renderer.drawLine(rightIconX + 8, row0 + rowStep * 3 + 12, rightIconX + 12, row0 + rowStep * 3 + 12, true);
  renderer.drawText(SMALL_FONT_ID, rightTextX, row0 + rowStep * 3 + 3, "Cup", true);
}

void GolfZeroActivity::drawMainMenu() {
  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, 10, 24, "Golf", true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, 10, 44, "X4 edition", true);

  const int startY = 72;
  const bool hasResume = hasSaveFile;
  const int menuCount = hasResume ? 8 : 7;
  if (menuIndex >= menuCount) menuIndex = 0;

  for (int i = 0; i < menuCount; ++i) {
    const int y = startY + i * 24;
    char label[96] = {};
    if (hasResume) {
      switch (i) {
        case 0:
          snprintf(label, sizeof(label), "Resume Saved Round");
          break;
        case 1:
          snprintf(label, sizeof(label), "Start New Round");
          break;
        case 2:
          snprintf(label, sizeof(label), "Course: %s", COURSES[selectedCourse].name);
          break;
        case 3:
          snprintf(label, sizeof(label), "Mode: %s", playStyleName());
          break;
        case 4:
          snprintf(label, sizeof(label), "Length: %s", lengthPresetName());
          break;
        case 5:
          snprintf(label, sizeof(label), "Rules & Controls");
          break;
        case 6:
          snprintf(label, sizeof(label), "Map Key");
          break;
        default:
          snprintf(label, sizeof(label), "Exit");
          break;
      }
    } else {
      switch (i) {
        case 0:
          snprintf(label, sizeof(label), "Start Round");
          break;
        case 1:
          snprintf(label, sizeof(label), "Course: %s", COURSES[selectedCourse].name);
          break;
        case 2:
          snprintf(label, sizeof(label), "Mode: %s", playStyleName());
          break;
        case 3:
          snprintf(label, sizeof(label), "Length: %s", lengthPresetName());
          break;
        case 4:
          snprintf(label, sizeof(label), "Rules & Controls");
          break;
        case 5:
          snprintf(label, sizeof(label), "Map Key");
          break;
        default:
          snprintf(label, sizeof(label), "Exit");
          break;
      }
    }

    if (menuIndex == i) {
      renderer.fillRect(8, y - 2, renderer.getScreenWidth() - 16, 20, true);
      renderer.drawText(UI_10_FONT_ID, 14, y, label, false);
    } else {
      renderer.drawText(UI_10_FONT_ID, 14, y, label, true);
    }
  }

  renderer.drawText(SMALL_FONT_ID, 10, renderer.getScreenHeight() - 20, "Up/Down: select  Confirm: open", true);
}

void GolfZeroActivity::drawRulesPage() {
  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, 10, 20, "Rules & Controls", true, EpdFontFamily::BOLD);
  static const char* lines[] = {
      "1) Confirm rolls d6",
      "2) Fairway +1, Sand -1",
      "3) Left/Right rotate 12-direction",
      "4) Up toggles putt (move 1)",
      "5) Down rerolls (tee once, then mulligans)",
      "6) Confirm places line/shot",
      "7) Water/Trees blocked",
      "8) Cactus: +1 penalty, stop",
      "9) Mud: +1 and drop back",
      "10) Chance/Bridge: random effect",
      "11) Slopes roll one hex",
  };
  int y = 44;
  for (const char* line : lines) {
    renderer.drawText(SMALL_FONT_ID, 10, y, line, true);
    y += 20;  // one full blank line between rules
  }
  renderer.drawText(SMALL_FONT_ID, 10, renderer.getScreenHeight() - 20, "Back/Confirm: return", true,
                    EpdFontFamily::BOLD);
}

void GolfZeroActivity::drawKeyPage() {
  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, 10, 20, "Map Key", true, EpdFontFamily::BOLD);

  int y = 52;
  renderer.drawRect(12, y, 16, 16, true);
  renderer.drawText(SMALL_FONT_ID, 36, y + 3, "Fairway (+1 roll)", true);
  y += 26;

  renderer.fillRectDither(12, y, 16, 16, Color::LightGray);
  renderer.drawRect(12, y, 16, 16, true);
  renderer.drawText(SMALL_FONT_ID, 36, y + 3, "Green (+1 / putting zone)", true);
  y += 26;

  renderer.drawLine(12, y, 28, y + 16, true);
  renderer.drawLine(12, y + 16, 28, y, true);
  renderer.drawText(SMALL_FONT_ID, 36, y + 3, "Sand (-1 roll)", true);
  y += 26;

  renderer.drawRoundedRect(12, y, 16, 16, 1, 4, true);
  renderer.drawText(SMALL_FONT_ID, 36, y + 3, "Water (cannot land)", true);
  y += 26;

  renderer.fillRect(12, y, 16, 16, true);
  renderer.drawText(SMALL_FONT_ID, 36, y + 3, "Trees (cannot land)", true);
  y += 26;

  renderer.drawLine(20, y + 2, 20, y + 14, true);
  renderer.drawLine(14, y + 8, 26, y + 8, true);
  renderer.drawText(SMALL_FONT_ID, 36, y + 3, "Cactus (+1, stop)", true);
  y += 26;

  renderer.fillRectDither(12, y, 16, 16, Color::LightGray);
  renderer.drawText(SMALL_FONT_ID, 36, y + 3, "Mud (+1 and drop back)", true);
  y += 26;

  renderer.drawText(SMALL_FONT_ID, 12, y + 3, "?", true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, 36, y + 3, "Chance/Bridge (random)", true);
  y += 26;

  renderer.drawLine(20, y + 8, 30, y + 8, true);
  renderer.drawText(SMALL_FONT_ID, 36, y + 3, "Slope (ball rolls)", true);
  y += 26;

  renderer.drawRoundedRect(12, y, 12, 12, 1, 2, true);
  renderer.drawText(SMALL_FONT_ID, 36, y + 1, "Cup", true);
  renderer.fillRect(12, y + 20, 12, 12, true);
  renderer.drawText(SMALL_FONT_ID, 36, y + 21, "Ball", true);

  renderer.drawText(SMALL_FONT_ID, 10, renderer.getScreenHeight() - 20, "Back/Confirm: return", true,
                    EpdFontFamily::BOLD);
}

void GolfZeroActivity::drawPauseMenu() {
  const int w = 176;
  const int h = 122;
  const int x = (renderer.getScreenWidth() - w) / 2;
  const int y = (renderer.getScreenHeight() - h) / 2;
  renderer.fillRect(x, y, w, h, false);
  renderer.drawRect(x, y, w, h, true);
  renderer.drawText(UI_10_FONT_ID, x + 8, y + 8, "Pause", true, EpdFontFamily::BOLD);

  static const char* items[] = {"Resume", "Scorecard", "Save", "Save & Exit", "Exit"};
  for (int i = 0; i < 5; ++i) {
    const int iy = y + 26 + i * 17;
    if (menuIndex == i) {
      renderer.fillRect(x + 6, iy + 1, w - 12, 17, true);
      renderer.drawText(UI_10_FONT_ID, x + 10, iy, items[i], false);
    } else {
      renderer.drawText(UI_10_FONT_ID, x + 10, iy, items[i], true);
    }
  }
}

void GolfZeroActivity::drawScorecard() {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();

  renderer.fillRect(0, 0, w, h, false);
  renderer.drawText(UI_12_FONT_ID, 8, 4, "Scorecard", true, EpdFontFamily::BOLD);

  char header[96] = {};
  snprintf(header, sizeof(header), "%s  Par %d x %d", COURSES[currentCourse].name, PAR_PER_HOLE, HOLES_PER_COURSE);
  renderer.drawText(SMALL_FONT_ID, 8, 20, header, true);
  renderer.drawLine(8, 40, w - 8, 40, true);

  int y = 48;
  int finishedHoles = 0;
  int total = 0;

  for (uint8_t i = 0; i < HOLES_PER_COURSE; ++i) {
    int shown = static_cast<int>(holeScores[i]);
    bool inProgress = false;
    if (shown == 0 && i == currentHole && (turnMode == TurnMode::Scorecard) && strokesThisHole > 0) {
      shown = strokesThisHole;
      inProgress = true;
    }

    if (holeScores[i] > 0) {
      finishedHoles++;
      total += holeScores[i];
    }

    char line[96] = {};
    if (shown > 0) {
      const int delta = shown - PAR_PER_HOLE;
      snprintf(line, sizeof(line), "H%d  %d/%d  %+d%s", i + 1, shown, PAR_PER_HOLE, delta, inProgress ? " *" : "");
    } else {
      snprintf(line, sizeof(line), "H%d  -/%d", i + 1, PAR_PER_HOLE);
    }
    renderer.drawText(SMALL_FONT_ID, 10, y, line, true);
    y += 15;
  }

  const int parToDate = finishedHoles * PAR_PER_HOLE;
  const int overUnder = total - parToDate;
  char totals[96] = {};
  snprintf(totals, sizeof(totals), "Total %d   Through %d   %+d", total, finishedHoles, overUnder);
  renderer.drawText(UI_10_FONT_ID, 8, h - 48, totals, true, EpdFontFamily::BOLD);

  if (scorecardAdvanceAfterConfirm) {
    renderer.drawText(SMALL_FONT_ID, 8, h - 30, "Confirm: next hole  Back: return", true);
  } else {
    renderer.drawText(SMALL_FONT_ID, 8, h - 30, "Confirm/Back: return", true);
  }
}

void GolfZeroActivity::renderMessage(const char* msg) {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2 - 8, msg, true, EpdFontFamily::BOLD);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void GolfZeroActivity::loop() {
  using Button = MappedInputManager::Button;

  if (turnMode == TurnMode::MainMenu) {
    const int menuCount = hasSaveFile ? 8 : 7;
    if (mappedInput.wasReleased(Button::Up) || mappedInput.wasReleased(Button::Left)) {
      menuIndex = static_cast<uint8_t>((menuIndex + menuCount - 1) % menuCount);
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(Button::Down) || mappedInput.wasReleased(Button::Right)) {
      menuIndex = static_cast<uint8_t>((menuIndex + 1) % menuCount);
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(Button::Back)) {
      if (onBack) onBack();
      finish();
      return;
    }
    if (mappedInput.wasReleased(Button::Confirm)) {
      if (hasSaveFile) {
        if (menuIndex == 0) {
          turnMode = resumeMode;
        } else if (menuIndex == 1) {
          clearSave();
          startFreshGame();
          turnMode = TurnMode::AwaitRoll;
        } else if (menuIndex == 2) {
          selectedCourse = static_cast<uint8_t>((selectedCourse + 1) % COURSE_COUNT);
        } else if (menuIndex == 3) {
          playStyle = (playStyle == PlayStyle::Dice) ? PlayStyle::Club : PlayStyle::Dice;
        } else if (menuIndex == 4) {
          lengthPreset = static_cast<HoleLengthPreset>((static_cast<int>(lengthPreset) + 1) % 3);
        } else if (menuIndex == 5) {
          turnMode = TurnMode::RulesPage;
        } else if (menuIndex == 6) {
          turnMode = TurnMode::KeyPage;
        } else {
          if (onBack) onBack();
          finish();
          return;
        }
      } else {
        if (menuIndex == 0) {
          startFreshGame();
          turnMode = TurnMode::AwaitRoll;
        } else if (menuIndex == 1) {
          selectedCourse = static_cast<uint8_t>((selectedCourse + 1) % COURSE_COUNT);
        } else if (menuIndex == 2) {
          playStyle = (playStyle == PlayStyle::Dice) ? PlayStyle::Club : PlayStyle::Dice;
        } else if (menuIndex == 3) {
          lengthPreset = static_cast<HoleLengthPreset>((static_cast<int>(lengthPreset) + 1) % 3);
        } else if (menuIndex == 4) {
          turnMode = TurnMode::RulesPage;
        } else if (menuIndex == 5) {
          turnMode = TurnMode::KeyPage;
        } else {
          if (onBack) onBack();
          finish();
          return;
        }
      }
      requestUpdate();
      return;
    }
    return;
  }

  if (turnMode == TurnMode::RulesPage || turnMode == TurnMode::KeyPage) {
    if (mappedInput.wasReleased(Button::Back) || mappedInput.wasReleased(Button::Confirm)) {
      turnMode = TurnMode::MainMenu;
      requestUpdate();
    }
    return;
  }

  if (turnMode == TurnMode::Scorecard) {
    if (mappedInput.wasReleased(Button::Back)) {
      turnMode = scorecardReturnMode;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(Button::Confirm)) {
      if (scorecardAdvanceAfterConfirm) {
        scorecardAdvanceAfterConfirm = false;
        advanceHoleOrCourse();
      } else {
        turnMode = scorecardReturnMode;
      }
      requestUpdate();
      return;
    }
    return;
  }

  if (turnMode == TurnMode::PauseMenu) {
    if (mappedInput.wasReleased(Button::Up) || mappedInput.wasReleased(Button::Left)) {
      menuIndex = static_cast<uint8_t>((menuIndex + 4) % 5);
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(Button::Down) || mappedInput.wasReleased(Button::Right)) {
      menuIndex = static_cast<uint8_t>((menuIndex + 1) % 5);
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(Button::Back)) {
      turnMode = pauseReturnMode;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(Button::Confirm)) {
      if (menuIndex == 0) {
        turnMode = pauseReturnMode;
      } else if (menuIndex == 1) {
        scorecardReturnMode = TurnMode::PauseMenu;
        scorecardAdvanceAfterConfirm = false;
        turnMode = TurnMode::Scorecard;
      } else if (menuIndex == 2) {
        saveGame(true);
        turnMode = pauseReturnMode;
      } else if (menuIndex == 3) {
        saveGame(false);
        if (onBack) onBack();
        finish();
        return;
      } else {
        if (onBack) onBack();
        finish();
        return;
      }
      requestUpdate();
      return;
    }
    return;
  }

  if (mappedInput.wasReleased(Button::Back)) {
    pauseReturnMode = turnMode;
    turnMode = TurnMode::PauseMenu;
    menuIndex = 0;
    requestUpdate();
    return;
  }

  if (turnMode == TurnMode::AwaitRoll) {
    if (mappedInput.wasReleased(Button::Confirm)) {
      if (playStyle == PlayStyle::Dice) {
        rollDie();
      } else {
        usePutt = (selectedClub == ClubType::Putter);
        turnMode = TurnMode::Aim;
      }
      resumeMode = TurnMode::Aim;
      requestUpdate();
    }
    if (playStyle == PlayStyle::Club) {
      if (mappedInput.wasReleased(Button::Up)) {
        cycleClub(-1);
        requestUpdate();
      }
      if (mappedInput.wasReleased(Button::Down)) {
        cycleClub(1);
        requestUpdate();
      }
      // Left/Right scroll the course view in club mode.
      const int stepY = std::max(6, (cellSize * 1732) / 1000);
      if (mappedInput.wasReleased(Button::Left)) {
        scrollY = std::max(0, scrollY - stepY * 3);
        requestUpdate();
      }
      if (mappedInput.wasReleased(Button::Right)) {
        scrollY += stepY * 3;
        requestUpdate();
      }
    } else {
      // Dice mode: Up/Down manually scroll the course view.
      const int stepY = std::max(6, (cellSize * 1732) / 1000);
      if (mappedInput.wasReleased(Button::Up)) {
        scrollY = std::max(0, scrollY - stepY * 3);
        requestUpdate();
      }
      if (mappedInput.wasReleased(Button::Down)) {
        scrollY += stepY * 3;
        requestUpdate();
      }
    }
    return;
  }

  if (turnMode == TurnMode::Aim) {
    snapToValidDirection();

    if (mappedInput.wasReleased(Button::Left)) {
      rotateDirection(-1);
      requestUpdate();
    }
    if (mappedInput.wasReleased(Button::Right)) {
      rotateDirection(1);
      requestUpdate();
    }
    if (mappedInput.wasReleased(Button::Up)) {
      if (playStyle == PlayStyle::Dice) {
        togglePuttMode();
      } else {
        cycleClub(-1);
      }
      requestUpdate();
    }
    if (mappedInput.wasReleased(Button::Down)) {
      if (playStyle == PlayStyle::Dice) {
        rerollIfAllowed();
      } else {
        cycleClub(1);
      }
      requestUpdate();
    }
    if (mappedInput.wasReleased(Button::Confirm)) {
      confirmShot();
      if (turnMode == TurnMode::AwaitRoll || turnMode == TurnMode::Aim || turnMode == TurnMode::HoleComplete) {
        resumeMode = turnMode;
      }
      requestUpdate();
    }
    return;
  }

  if (turnMode == TurnMode::HoleComplete) {
    if (mappedInput.wasReleased(Button::Confirm)) {
      scorecardReturnMode = TurnMode::HoleComplete;
      scorecardAdvanceAfterConfirm = true;
      turnMode = TurnMode::Scorecard;
      requestUpdate();
    }
    return;
  }

  if (turnMode == TurnMode::CourseComplete) {
    if (mappedInput.wasReleased(Button::Confirm)) {
      startFreshGame();
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(Button::Back)) {
      if (onBack) onBack();
      finish();
      return;
    }
  }
}

void GolfZeroActivity::render(RenderLock&&) {
  if (turnMode == TurnMode::MainMenu) {
    drawMainMenu();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  if (turnMode == TurnMode::RulesPage) {
    drawRulesPage();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  if (turnMode == TurnMode::KeyPage) {
    drawKeyPage();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  if (turnMode == TurnMode::Scorecard) {
    drawScorecard();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  renderer.clearScreen();

  const HoleDef hole = buildHoleDef(currentCourse, currentHole);

  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const int availH = h - STATUS_HEIGHT - 8;
  // Fill width below the top HUD/key band while keeping small side margins.
  const int widthBudget = std::max(40, w - 6);
  const int fullSize = std::max(6, (widthBudget * 2) / (3 * (GRID_W - 1) + 4));
  cellSize = std::max(6, (fullSize * 9) / 10);

  const int stepX = (cellSize * 3) / 2;
  const int stepY = std::max(6, (cellSize * 1732) / 1000);
  const int hexRy = std::max(3, stepY / 2);
  const int boardW = (GRID_W - 1) * stepX + cellSize * 2 + 4;
  const int boardH = GRID_H * stepY + stepY / 2 + 4;
  gridLeft = (w - boardW) / 2 + cellSize + 2;
  gridTop = STATUS_HEIGHT + hexRy + 2;  // pin top; scroll handles vertical position

  const int maxScroll = std::max(0, boardH - availH);
  if (turnMode != TurnMode::AwaitRoll || playStyle == PlayStyle::Club) {
    // Auto-scroll while aiming/playing; leave dice await-roll free for manual scan.
    const int ballPxY = gridTop + ballY * stepY + ((ballX & 1) ? stepY / 2 : 0);
    const int targetScroll = ballPxY - (STATUS_HEIGHT + availH / 2);
    scrollY = std::max(0, std::min(targetScroll, maxScroll));
  } else {
    scrollY = std::max(0, std::min(scrollY, maxScroll));
  }

  drawSideRails(hole);
  drawGrid(hole);
  drawSegments();

  uint8_t previewX = ballX;
  uint8_t previewY = ballY;
  bool inCup = false;
  bool valid = true;
  if (turnMode == TurnMode::Aim) {
    computePreviewLanding(previewX, previewY, inCup, valid);
  }

  drawBallAndCup(hole, previewX, previewY, turnMode == TurnMode::Aim, valid);
  drawHud(hole);
  drawInCourseKey();

  if (turnMode == TurnMode::PauseMenu) {
    drawPauseMenu();
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
