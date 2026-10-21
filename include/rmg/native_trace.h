#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace rmg::native_trace {

// Function IDs and event kinds intentionally match YRMod's RFT1 oracle.
enum FunctionId : std::uint16_t {
    ProcessAllRegions = 1,
    GenerateRandomMap = 2,
    GaussianRandom = 3,
    CompareAngle = 4,
    Randomizer = 5,
    InitializeMapRegions = 6,
    FixCliffSlopeTransitions = 7,
    CalculateLevelsAndZones = 8,
    RandomizeCliffTileVariants = 9,
    GenerateMapZone = 10,
    BuildRegionNeighborList = 11,
    ProcessRegionBridgesAndCliffs = 12,
    PlaceCliffAtCell = 13,
    GetCliffNeighborMask = 14,
    PlaceCliffCorner = 15,
    RegionLevelDecision = 16,
    CollectZoneBoundaryCells = 17,
    PlaceCliffCornerType6 = 18,
    PlaceCliffCornerComplexType3 = 19,
    PlaceCliffCornerType8 = 20,
    PlaceCliffCornerComplexType1 = 21,
    PlaceCliffLine = 22,
    PlaceCliffCornerComplexType2 = 23,
    PlaceCliffCornerType5 = 24,
    PlaceCliffCornerType7 = 25,

    // Stable core IDs are 256 plus the zero-based manifest ordinal.
    CalculateCellLevelRecursive = 264,
    AssignMapZones = 292,
    AssignZonesToRegions = 293,
    CollectBorderCellsForRegion = 298,
    ExpandRegionBorder = 299,
    AssignCounterclockwiseBorder = 300,
    GenerateTerrain = 329,
    SubdivideRegionIntoRects = 330,
    GenerateTerrainType0 = 331,
    GenerateTerrainType1 = 332,
    GenerateTerrainType2 = 333,
    ExpandGreenTiles = 334,
    ComputeTerrainDistance = 335,
    IsCellInsideRegion = 336,
    FloodFillTerrainPatch = 337,
    DecoratePonds = 339,
};

enum EventKind : std::uint8_t {
    Enter = 1,
    Exit = 2,
    CallSite = 3,
};

enum ResultKind : std::uint16_t {
    ResultLegacy = 0,
    ResultNone = 1,
    ResultBinary64 = 2,
};

enum DetailKind : std::uint8_t {
    DetailNone = 0,
    DetailOrderedPackedCells = 1,
    DetailOrderedCellState = 2,
    DetailOrderedTerrainWorkspace = 3,
    DetailOrderedRects = 4,
    DetailOrderedIsoTiles = 5,
    DetailOrderedZoneWorkspace = 6,
};

struct EventData {
    std::array<std::uint32_t, 6> args{};
    std::array<std::uint32_t, 4> result{};
    std::int32_t regionIndex = -1;
    std::int32_t regionId = -1;
    std::int32_t regionCount = 0;
    std::int32_t regionCellCount = 0;
    std::int16_t cellX = -1;
    std::int16_t cellY = -1;
    std::uint16_t resultKind = ResultLegacy;
};

bool active() noexcept;
bool coreFunctionEnabled(std::uint16_t functionId) noexcept;
bool detailRequested() noexcept;
bool detailRequestedForFunction(std::uint16_t functionId) noexcept;
std::uint16_t currentFunctionId() noexcept;
void detailCurrent(std::uint8_t detailKind, std::span<const std::uint32_t> detailValues);
void beginFromEnvironment();
void finish();
void setRandomizerState(std::uint32_t next1, std::uint32_t next2) noexcept;
void setGaussianState(bool hasSpare, std::uint64_t spareBits) noexcept;

void enter(std::uint32_t site, std::uint16_t functionId, int x = -1, int y = -1);
void exit(
    std::uint32_t site,
    std::uint16_t functionId,
    std::uint64_t resultBits = 0,
    int x = -1,
    int y = -1);
void call(
    std::uint32_t site,
    std::uint16_t functionId,
    int x = -1,
    int y = -1,
    std::uint32_t raw = 0,
    std::uint32_t next1 = 0,
    std::uint32_t next2 = 0,
    std::uint64_t resultBits = 0);
void enterDetailed(std::uint32_t site, std::uint16_t functionId, const EventData& data);
void exitDetailed(
    std::uint32_t site,
    std::uint16_t functionId,
    const EventData& data,
    std::uint8_t detailKind = DetailNone,
    std::span<const std::uint32_t> detailValues = {});
void callDetailed(std::uint32_t site, std::uint16_t functionId, const EventData& data);
void callWithDetail(
    std::uint32_t site,
    std::uint16_t functionId,
    std::uint8_t detailKind,
    std::span<const std::uint32_t> detailValues);

// Records the point inside Randomizer::Random after the table value has been
// produced.  It is a single leaf event rather than an enter/exit pair, which
// keeps the fixed-size trace useful for maps with many random draws.
void randomizerRaw(
    std::uint32_t raw,
    std::uint32_t next1,
    std::uint32_t next2);

// 0x598008 is the wrapper callsite used by GaussianRandom before each raw
// Randomizer draw in the original executable.
void gaussianRawCallSite();

} // namespace rmg::native_trace
