#pragma once

#include "rmg/map_evaluation.h"
#include "rmg/map_types.h"
#include "rmg/native_theater_catalog.h"
#include "rmg/randomizer.h"
#include "rmg/rmg_config.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rmg {

struct MapSeedOptions {
    int theater = 0;
    int mapType = 1;
    int resources = 1;
    int ruggedness = 1;
    int time = 1;
    int waterAmount = 1;
    int numPlayers = 2;
    int tiberium = 4;
    int tiberiumLayout = 0;
    int vegetation = 2;
    int urbanPresence = 0;
    int width = 80;
    int height = 80;
    int accessibility = 0;
    int regionSize = 1;
    std::uint32_t seed = 1;
    int terrainTypeOverride = -1;
    bool nativeWidthInterpolation = false;
    bool nativeHeightInterpolation = false;
    int nativeSeedWidth = -1;
    int nativeSeedHeight = -1;
    // ScenarioClass::UniqueID immediately before the first neutral-tech
    // BuildingClass is constructed. It is process state, not a map option.
    int nativeScenarioUniqueId = -1;
};

int nativeInterpolatedPlayableSize(int seedSize, int players, bool highResourceTerrain = false) noexcept;

struct NativeStageSnapshot {
    int index = 0;
    std::string name;
    bool valid = true;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int cells = 0;
    int nonNullCells = 0;
    std::uint64_t rngDraws = 0;
    std::string rngState;
    std::string mapHash;
    std::string terrainHash;
    std::string overlayHash;
    std::string objectHash;
    std::string heightHash;
    std::string flagsHash;
    int landCells = 0;
    int waterCells = 0;
    int shoreCells = 0;
    int dirtCells = 0;
    int roughCells = 0;
    int paveCells = 0;
    int greenCells = 0;
    int cliffCells = 0;
    int passableCells = 0;
    int buildableCells = 0;
    int wetCells = 0;
    int heightCells = 0;
    int encodedShoreCells = 0;
    int encodedShoreWaterCells = 0;
    int encodedShoreLandCells = 0;
    int encodedShoreNonZeroSubTiles = 0;
    int encodedShoreWaterNonZeroSubTiles = 0;
    int encodedShoreLandNonZeroSubTiles = 0;
    int levelNonZeroCells = 0;
    int slopeCells = 0;
    int spawnExclusionCells = 0;
    int cliffShoreExclusionCells = 0;
    int regionTaggedCells = 0;
    int tempTaggedCells = 0;
    int urbanMarkedCells = 0;
    int oreCells = 0;
    int gemCells = 0;
    int treeOverlayCells = 0;
    int techOverlayCells = 0;
    int startOverlayCells = 0;
    int neutralBuildingCells = 0;
    int neutralUnitCells = 0;
    int terrainChainCells = 0;
    int grassCells = 0;
    int tiberiumTreeObjects = 0;
    int orePatchLampObjects = 0;
    int terrainType = 0;
    int terrainPatches = 0;
    int floodFillAttempts = 0;
    int cliffLines = 0;
    int waterChains = 0;
    int regions = 0;
    int connectedRegions = 0;
    int regionProcessCalls = 0;
    int startsPlaced = 0;
    int techBuildings = 0;
    int tiberiumPatches = 0;
    int pond2x2Tiles = 0;
    int pondEdgeTiles = 0;
    int pondTemplateCells = 0;
    int shoreTilesPlaced = 0;
    int heightNoiseCells = 0;
    int slopePatternMatches = 0;
    int surfaceBlobs = 0;
    int surfaceCells = 0;
    int treeClusters = 0;
    int treeCells = 0;
    int decorationEvents = 0;
    int terrainChains = 0;
    int urbanRegions = 0;
    int isoTileOverrides = 0;
};

struct NativeObjectOverride {
    Point iso;
    int objectAbstractType = -1;
    int objectTypeIndex = -1;
    int altObjectAbstractType = -1;
    int altObjectTypeIndex = -1;
};

struct NativeStructureOverride {
    Point iso;
    int nativeTechIndex = -1;
    int nativeUniqueId = -1;
    std::string nativeSaveKey;
    std::string nativeSaveValue;
};

struct NativeOverlayOverride {
    Point iso;
    int overlayTypeIndex = -1;
    int overlayData = 0;
    int landTypeOverride = -1;
};

struct NativeGenerationReport {
    int terrainType = 0;
    int terrainPatches = 0;
    int floodFillAttempts = 0;
    int floodFillFailures = 0;
    int floodFillMinimumClamps = 0;
    int floodFillFringeCells = 0;
    int cliffLines = 0;
    int cliffLineAttempts = 0;
    int cliffLineSteps = 0;
    int cliffLineTouchedCells = 0;
    int cliffLineAngleAdjustments = 0;
    int cliffLineWidthAdjustments = 0;
    int cliffLineBranchAttempts = 0;
    int cliffLineBranchSuccesses = 0;
    int cliffLineRaisedCells = 0;
    int cliffLineRollbacks = 0;
    int cliffPlateauAttempts = 0;
    int cliffPlateauAccepted = 0;
    int cliffPlateauRejectedBounds = 0;
    int cliffPlateauRejectedOccupied = 0;
    int cliffPlateauRaisedCells = 0;
    int cliffPlateauCliffCells = 0;
    int cliffPlateauWaterCells = 0;
    int cliffPlateauShoreCells = 0;
    int cliffPlateauOutwardCells = 0;
    int cliffPlateauRollbacks = 0;
    int zoneOutwardCalls = 0;
    int zoneOutwardPasses = 0;
    int zoneOutwardBorderCells = 0;
    int zoneOutwardCandidateChecks = 0;
    int zoneOutwardAcceptedCells = 0;
    int zoneOutwardRejectedCells = 0;
    int zoneOutwardRaisedCells = 0;
    int zoneOutwardShoreCells = 0;
    int zoneDirectionalCalls = 0;
    int zoneDirectionalBorderCells = 0;
    int zoneDirectionalHeapPushes = 0;
    int zoneDirectionalHeapPops = 0;
    int zoneDirectionalCandidateChecks = 0;
    int zoneDirectionalAcceptedCells = 0;
    int zoneDirectionalRejectedCells = 0;
    int zoneDirectionalFinalizedCells = 0;
    int zoneDirectionalAngleDrifts = 0;
    int waterChains = 0;
    int oreChainBudget = 0;
    int oreChainTargetLength = 0;
    int oreChainMaskBuilds = 0;
    int oreChainMaskPasses = 0;
    int oreChainMaskAllowedCells = 0;
    int oreChainMaskRejectedCells = 0;
    int oreChainSeedSearches = 0;
    int oreChainHeapPushes = 0;
    int oreChainHeapPops = 0;
    int oreChainCandidateChecks = 0;
    int oreChainRejectedByMask = 0;
    int oreChainRejectedCells = 0;
    int oreChainCommittedCells = 0;
    int oreChainShoreCells = 0;
    int oreChainRollbacks = 0;
    int regionCandidates = 0;
    int regionMinimumSize = 0;
    int regionFilteredSmall = 0;
    int regions = 0;
    int connectedRegions = 0;
    int regionBorderExpansionCalls = 0;
    int regionBorderExpansionPasses = 0;
    int regionBorderExpansionBoundaryCells = 0;
    int regionBorderExpansionCandidateChecks = 0;
    int regionBorderExpansionAcceptedCells = 0;
    int regionBorderExpansionConflictFailures = 0;
    int regionBorderExpansionAborts = 0;
    int regionBorderExpansionRejectedWater = 0;
    int regionBorderExpansionRejectedTerrain = 0;
    int regionBorderExpansionLevelAdjustments = 0;
    int regionProcessCalls = 0;
    int regionProcessMinimumSize = 0;
    int regionProcessSkippedSmall = 0;
    int regionProcessInputCells = 0;
    int regionProcessSubregions = 0;
    int regionProcessOutputRegions = 0;
    int regionProcessOutputCells = 0;
    int regionProcessBorderCells = 0;
    int regionProcessNeighborLinks = 0;
    int regionProcessCandidateLevels = 0;
    int regionProcessSmallMerges = 0;
    int regionProcessLevelAdjustments = 0;
    int regionNeighborBuildCalls = 0;
    int regionNeighborBoundaryCells = 0;
    int regionNeighborLinks = 0;
    int regionBridgeCliffCalls = 0;
    int regionBridgePairsChecked = 0;
    int regionBridgeAttempts = 0;
    int regionBridgesPlaced = 0;
    int regionCliffPairsChecked = 0;
    int regionCliffAttempts = 0;
    int regionCliffsPlaced = 0;
    int regionCliffMaskChecks = 0;
    int regionCliffMaskFailures = 0;
    int regionCliffAttemptLimitHits = 0;
    int startsPlaced = 0;
    int spawnExclusionCells = 0;
    int spawnExclusionStarts = 0;
    int spawnExclusionHeapPushes = 0;
    int spawnExclusionHeapPops = 0;
    int spawnExclusionCandidateChecks = 0;
    int spawnExclusionRejectedNonBlank = 0;
    int spawnExclusionLimitHits = 0;
    int cliffShoreExclusionCells = 0;
    int techBuildingPlacementMode = 0;
    int techBuildingRounds = 0;
    int techBuildingEligibleRegions = 0;
    int techBuildingTargets = 0;
    int techBuildingRegionCalls = 0;
    int techBuildingCandidateSamples = 0;
    int techBuildingBlankSearchRejects = 0;
    int techBuildingGlobalFallbacks = 0;
    int techBuildingFootprintChecks = 0;
    int techBuildingFootprintFailures = 0;
    int techBuildingRejectedObjects = 0;
    int techBuildingRejectedNonBlank = 0;
    int techBuildingRejectedLevel = 0;
    int techBuildingRejectedRock = 0;
    int techBuildingRejectedUsableArea = 0;
    int techBuildingRejectedExclusion = 0;
    int techBuildingUnlimboSuccesses = 0;
    int techBuildings = 0;
    int tiberiumPatches = 0;
    int rmgMinimumTiberium = 0;
    int rmgMaximumTiberium = 0;
    int tiberiumBudget = 0;
    int tiberiumBudgetCells = 0;
    int regionWaypointQuotaTotal = 0;
    int regionStartQuotaTotal = 0;
    int nativeWaypointsSelected = 0;
    int nativeSpawnCandidateCells = 0;
    int nativeSpawnPoolCells = 0;
    int nativeSpawnExtraCells = 0;
    int tiberiumRegionSeeds = 0;
    int tiberiumWaypointSeeds = 0;
    int tiberiumSpecialPatchSeeds = 0;
    int richTiberiumPatches = 0;
    int tiberiumPatchHeapPushes = 0;
    int tiberiumPatchHeapPops = 0;
    int tiberiumPatchCandidateChecks = 0;
    int tiberiumPatchRejectedCells = 0;
    int tiberiumPatchExistingOverlayIncrements = 0;
    int tiberiumPatchFirstCellResets = 0;
    int tiberiumPatchInitialRetries = 0;
    int tiberiumPatchUnfilled = 0;
    int landCells = 0;
    int dirtCells = 0;
    int roughCells = 0;
    int greenCells = 0;
    int waterCells = 0;
    int shoreCells = 0;
    int terrainPostWaterChecks = 0;
    int terrainPostWaterCleared = 0;
    int terrainPostShoreAnchors = 0;
    int terrainPostShoreCandidates = 0;
    int terrainPostShoreTiles = 0;
    int shoreInitialCandidates = 0;
    int shoreTargetTiles = 0;
    int shoreTilesPlaced = 0;
    int shoreExpansionCandidates = 0;
    int pond2x2Tiles = 0;
    int pondEdgeTiles = 0;
    int pondTemplateCells = 0;
    int shoreSlopeVariants = 0;
    int cliffSlopeVariants = 0;
    int heightNoiseCells = 0;
    int heightNoiseSkippedLowRuggedness = 0;
    int heightNoiseGaussianSamples = 0;
    int heightNoiseExclusionClamps = 0;
    int heightNoiseRaisedCells = 0;
    int heightNoiseLoweredCells = 0;
    int heightNoiseTotalRaiseSteps = 0;
    int heightNoiseTotalLowerSteps = 0;
    int slopePatternChecks = 0;
    int slopePatternMatches = 0;
    int slopePatternClearedCells = 0;
    int slopePatternRaisedLevels = 0;
    int oreCells = 0;
    int gemCells = 0;
    int oreValue = 0;
    int surfaceBlobs = 0;
    int surfaceCells = 0;
    int surfaceMode = 0;
    int surfaceSimpleDensityCells = 0;
    int surfaceSimpleRolls = 0;
    int surfaceSimpleHits = 0;
    int surfaceSimpleSizeSamples = 0;
    int surfaceSimpleTreeTarget = 0;
    int vegetationDensityCells = 0;
    int vegetationDensityBoosts = 0;
    int vegetationDensityRolls = 0;
    int vegetationDensityHits = 0;
    int grassTarget = 0;
    int grassAttemptBudget = 0;
    int grassAttempts = 0;
    int grassCandidateSamples = 0;
    int grassRejectedOccupied = 0;
    int grassRejectedTerrain = 0;
    int grassPlaced = 0;
    int heapSurfaceBlobs = 0;
    int heapSurfaceCells = 0;
    int groundBlobHeapPushes = 0;
    int groundBlobHeapPops = 0;
    int groundBlobCandidateChecks = 0;
    int groundBlobRejectedCells = 0;
    int groundBlobTaggedCells = 0;
    int decorationCentersTried = 0;
    int decorationCentersAccepted = 0;
    int decorationTargetEvents = 0;
    int decorationEvents = 0;
    int decorationUrbanAttempts = 0;
    int decorationUrbanSkips = 0;
    int urbanPatchRects = 0;
    int urbanPatchRectCells = 0;
    int decorationFootprintChecks = 0;
    int decorationFootprintFailures = 0;
    int decorationFootprintRejectedBounds = 0;
    int decorationFootprintRejectedTerrain = 0;
    int decorationFootprintRejectedOccupied = 0;
    int decorationFootprintRejectedLevel = 0;
    int decorationFootprintRejectedRock = 0;
    int decorationFootprintRejectedExclusion = 0;
    int urbanRegionCells = 0;
    int urbanRegionBoundaryCells = 0;
    int urbanRegionRejected = 0;
    int urbanRegionRejectedSmall = 0;
    int urbanRegionRejectedSparse = 0;
    int urbanRegionHeapPushes = 0;
    int urbanRegionHeapPops = 0;
    int urbanRegionMarkedCells = 0;
    int urbanRegionAnchorPavedCells = 0;
    int urbanRegionInitialPaveAttempts = 0;
    int urbanRegionInitialPavedCells = 0;
    int urbanRegionInitialPave2x2 = 0;
    int urbanCliffEdgeCandidates = 0;
    int urbanCliffEdgeSegments = 0;
    int urbanCliffEdgeCells = 0;
    int urbanCliffEdgeEndpointVariants = 0;
    int urbanCliffEdgeRandomShortens = 0;
    int urbanCliffEdgeAreaFailures = 0;
    int urbanCliffEdgeDecorTiles = 0;
    int urbanRegions = 0;
    int urbanPavedCells = 0;
    int civilianBuildingTargets = 0;
    int civilianBuildingAttempts = 0;
    int civilianBuildingFootprintFailures = 0;
    int civilianBuildings = 0;
    int civilianUnitTargets = 0;
    int civilianUnitAttempts = 0;
    int civilianUnitRejectedBounds = 0;
    int civilianUnitRejectedOccupied = 0;
    int civilianUnitRejectedExclusion = 0;
    int civilianInfantry = 0;
    int civilianVehicleUnits = 0;
    int civilianUnits = 0;
    int terrainChains = 0;
    int terrainChainCells = 0;
    int terrainChainFrontierSteps = 0;
    int terrainChainBranchSteps = 0;
    int terrainChainEntriesQueued = 0;
    int terrainChainEntriesProcessed = 0;
    int terrainChainQueueMax = 0;
    int terrainChainRejectedPlacements = 0;
    int terrainChainTerrainSelections = 0;
    int terrainChainSelectionFailures = 0;
    int terrainChainGaussianSizeSamples = 0;
    int terrainChainSizeClassFallbacks = 0;
    int terrainChainTileAttempts = 0;
    int terrainChainConnectionSkips = 0;
    int terrainChainQueueSaturationRejects = 0;
    int terrainChainTailRestores = 0;
    int terrainChainSize2Skips = 0;
    int terrainChainFoundationCellsChecked = 0;
    int terrainChainRejectedBounds = 0;
    int terrainChainRejectedTerrain = 0;
    int terrainChainRejectedOccupied = 0;
    int terrainChainRejectedSlope = 0;
    int terrainChainRejectedMarkedFootprints = 0;
    int terrainChainFootprintsMarked = 0;
    int terrainChainFootprintsCleared = 0;
    int terrainChainFootprintCellsMarked = 0;
    int terrainChainFootprintCellsCleared = 0;
    int terrainChainTilesCleared = 0;
    int terrainChainTerminalFallbacks = 0;
    int neutralDecorationBuildingTargets = 0;
    int neutralDecorationBuildingAttempts = 0;
    int neutralDecorationBuildingFootprintFailures = 0;
    int neutralDecorationBuildings = 0;
    int neutralDecorationUnitTargets = 0;
    int neutralDecorationUnitAttempts = 0;
    int neutralDecorationUnitRejectedBounds = 0;
    int neutralDecorationUnitRejectedOccupied = 0;
    int neutralDecorationUnitRejectedExclusion = 0;
    int neutralDecorationInfantry = 0;
    int neutralDecorationVehicleUnits = 0;
    int neutralDecorationUnits = 0;
    int maxTrees = 0;
    int vegetationMinimum = 0;
    int vegetationMaximum = 0;
    int treePlacementPhase = 0;
    int treeClusterTarget = 0;
    int treeClusters = 0;
    int treeCells = 0;
    int heapTreeClusters = 0;
    int heapTreeCells = 0;
    int treePatchHeapPushes = 0;
    int treePatchHeapPops = 0;
    int treePatchCandidateChecks = 0;
    int treePatchRejectedCells = 0;
    int treePatchTaggedCells = 0;
    int treePatchPlacementChecks = 0;
    int treePatchRejectedPlacements = 0;
    int tiberiumTrees = 0;
    int orePatchLamps = 0;
    int minStartResourceValue = 0;
    int maxStartResourceValue = 0;
    int startResourceValueSpread = 0;
    int minStartResourceDistance = 0;
    int maxStartResourceDistance = 0;
    int startResourceDistanceSpread = 0;
    int minBuildableNearStart = 0;
    int maxBuildableNearStart = 0;
    int buildableNearStartSpread = 0;
    std::vector<int> startResourceValues;
    std::vector<int> startResourceDistances;
    std::vector<int> buildableNearStarts;
    std::vector<IsoTileOverride> isoTileOverrides;
    std::vector<NativeOverlayOverride> nativeOverlayOverrides;
    std::vector<NativeObjectOverride> nativeObjectOverrides;
    std::vector<NativeStructureOverride> nativeStructureOverrides;
    std::vector<Point> nativeWaypoints;
    std::vector<NativeStageSnapshot> stageSnapshots;
    std::vector<std::string> notes;
    MapEvaluationReport evaluation;
};

class NativeMapGenerator {
public:
    explicit NativeMapGenerator(const NativeTheaterCatalog* theaterCatalog = nullptr) noexcept
        : nativeTheaterCatalog_(theaterCatalog)
    {
    }

    MapGrid generate(const MapSeedOptions& options, const RmgConfig& config = {});
    const NativeGenerationReport& report() const noexcept { return report_; }

private:
    struct FloodOptions {
        int targetCells = 0;
        int minimumTargetCells = 400;
        int nativeMapWidth = 0;
        Rect region;
        Point mapOffset;
        Point seed;
        Point distanceAnchor;
        bool ellipseRegion = false;
        bool distanceToRegion = false;
        bool hasDistanceAnchor = false;
        bool nativeCellCardinalNeighbors = false;
        bool nativeCellWorkspace = false;
        double noiseScale = 0.5;
        bool commitFringe = true;
    };

    struct NativeRegion {
        int id = 0;
        int nativeBaseZoneId = -1;
        std::vector<Point> cells;
        std::vector<Point> nativeCells;
        std::vector<Point> nativeWaypoints;
        std::vector<Point> nativeResourceSeeds;
        Rect bounds;
        Rect nativeBounds;
        int level = 4;
        int nativeConstructedCellCount = 0;
        bool wet = false;
        bool processed = false;
        int nativeWaypointQuota = 0;
        int startQuota = 0;
        std::vector<int> neighbors;
    };

    struct SpawnSelection {
        std::vector<Point> selected;
        int candidateCount = 0;
        int targetCount = 0;
    };

    struct UrbanRegion {
        Point center;
        Rect bounds;
        std::vector<Point> cells;
        std::vector<Point> boundaryCells;
    };

    struct ZoneExpansionResult {
        int borderCells = 0;
        int candidateChecks = 0;
        int acceptedCells = 0;
        int rejectedCells = 0;
        int raisedCells = 0;
        int shoreCells = 0;
        int heapPushes = 0;
        int heapPops = 0;
        int finalizedCells = 0;
        int angleDrifts = 0;
        std::vector<Point> accepted;
    };

    std::vector<Rect> subdivideRegion(Rect region, int requestedCount, NativeRandomizer& rng);
    int floodFillTerrainPatch(MapGrid& map, NativeRandomizer& rng, const FloodOptions& options, int tag);
    bool placeCliffsAndOre(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng);
    bool placeCliffLine(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, Point* endPoint, bool allowBranch = false, Point seed = {0, 0}, double forcedAngle = 0.0);
    bool placeCliffPlateau(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, Point anchor, int direction, double lineWidth, double* x, double* y);
    void initializeHighResourceNativeWorkspace(const MapGrid& map);
    void synchronizeHighResourceWorkspaceToMap(MapGrid& map);
    void synchronizeHighResourceMapToWorkspace(const MapGrid& map, bool preserveNativePieces);
    bool recalculateHighResourceCliffLevels(
        MapGrid& map,
        NativeRandomizer& rng,
        bool includeRandomizedSmoothPasses);
    std::vector<unsigned char> buildOreChainEligibilityMask(const MapGrid& map);
    std::vector<Point> collectNativeZoneBorderCells(const MapGrid& map, int zoneId) const;
    bool expandNativeZoneOutward(
        const MapGrid& map,
        int zoneId,
        int passes,
        Rect bounds,
        bool allowPreviousZone,
        int previousZoneBlankLevel,
        std::vector<Point>* acceptedCells = nullptr);
    bool expandNativeZoneDirectional(
        const MapGrid& map,
        NativeRandomizer& rng,
        int zoneId,
        float factor,
        Rect bounds,
        Point center,
        bool finalize,
        ZoneExpansionResult* expansionResult = nullptr);
    void convertNativeZoneBlankToGreen(int zoneId);
    bool expandNativeOreZone(const MapGrid& map, int zoneId, std::vector<Point>* acceptedCells);
    bool placeOreChain(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, Point seed);
    std::vector<Point> collectZoneBorderCells(const MapGrid& map, const std::vector<Point>& zoneCells) const;
    ZoneExpansionResult expandZoneOutward(MapGrid& map, const std::vector<Point>& zoneCells, int passes, Rect bounds, int targetLevel, bool shoreMode);
    ZoneExpansionResult expandZoneDirectional(MapGrid& map, NativeRandomizer& rng, const std::vector<Point>& zoneCells, Point center, Rect bounds, double factor, bool finalize, int targetLevel);
    void generateTerrainType0(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng);
    void generateTerrainType1(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng);
    void generateTerrainType2(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng);
    void generateTerrainNativeWorkspace(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, int terrainType);
    int floodFillTerrainPatchNativeCellWorkspace(MapGrid& map, NativeRandomizer& rng, const FloodOptions& options, int tag);
    void rebuildWaterShorePieces(MapGrid& map);
    void seedGreenTilesAroundShorePieces(MapGrid& map, bool countTerrainPostMetrics);
    void postProcessGeneratedTerrain(MapGrid& map);
    void placeGreenTilesNearShore(MapGrid& map, NativeRandomizer& rng);
    void recalculateNativeHighResourceLandTypes(MapGrid& map);
    void decoratePonds(MapGrid& map, NativeRandomizer& rng);
    void decorateNativeFramePonds(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng);
    std::vector<NativeRegion> buildPassableRegions(
        MapGrid& map,
        const MapSeedOptions& options,
        const char* tracePrefix = nullptr);
    void expandNativeRegionBorders(MapGrid& map, const MapSeedOptions& options, std::vector<NativeRegion>& regions);
    void buildRegionNeighborLists(const MapGrid& map, std::vector<NativeRegion>& regions);
    void processRegionBridgesAndCliffs(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, std::vector<NativeRegion>& regions);
    bool placeBridgeBetweenRegions(
        MapGrid& map,
        NativeRandomizer& rng,
        const NativeRegion& wet,
        const NativeRegion& firstBank,
        const NativeRegion& secondBank);
    bool placeRegionCliffAtCell(
        MapGrid& map,
        NativeRandomizer& rng,
        Point nativeCell,
        int higherRegionId,
        int lowerRegionId,
        float fallbackBias);
    bool placeCliffCornerType6(
        MapGrid& map,
        NativeRandomizer& rng,
        Point from,
        Point to,
        int higherRegionId,
        int lowerRegionId);
    bool placeCliffCornerType5(
        MapGrid& map,
        NativeRandomizer& rng,
        Point from,
        Point to,
        int higherRegionId,
        int lowerRegionId);
    bool placeCliffCornerType7(
        MapGrid& map,
        NativeRandomizer& rng,
        Point from,
        Point to,
        int higherRegionId,
        int lowerRegionId);
    bool placeCliffCornerType8(
        MapGrid& map,
        NativeRandomizer& rng,
        Point from,
        Point to,
        int higherRegionId,
        int lowerRegionId);
    bool placeCliffCornerComplexType3(
        MapGrid& map,
        Point from,
        Point to,
        int higherRegionId,
        int lowerRegionId);
    bool placeCliffCornerComplexType1(
        MapGrid& map,
        Point from,
        Point to,
        int higherRegionId,
        int lowerRegionId);
    bool placeCliffCornerComplexType2(
        MapGrid& map,
        Point from,
        Point to,
        int higherRegionId,
        int lowerRegionId);
    bool placeNativeIsoTile(
        MapGrid& map,
        int tileTypeIndex,
        Point anchor,
        int zoneId,
        int baseLevel);
    void processNativeRegionWorkspace(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, std::vector<NativeRegion>& regions);
    void processNativeRegions(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, std::vector<NativeRegion>& regions);
    bool calculateNativeMapgenLevelsRecursive(MapGrid& map);
    void fixNativeCliffSlopeTransitions(MapGrid& map, NativeRandomizer& rng);
    void calculateNativeFlatCellLevels(MapGrid& map, NativeRandomizer& rng);
    void randomizeNativeCliffTileVariants(MapGrid& map, NativeRandomizer& rng);
    void connectNativeRegions(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, const std::vector<NativeRegion>& regions);
    void resetRegionSelectionTrace() const;
    void dumpRegionSelectionTrace(const char* event, const MapSeedOptions& options, const std::vector<NativeRegion>& regions) const;
    SpawnSelection selectSpawnCells(const MapGrid& map, const NativeRegion& region, int desiredCount, const MapSeedOptions& options, NativeRandomizer& rng, bool consumeRandomOffset = true) const;
    std::vector<Point> placeStartPoints(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, std::vector<NativeRegion>& regions);
    void evaluateStartFairness(const MapGrid& map, const std::vector<Point>& starts);
    void markSpawnExclusionZones(MapGrid& map, const std::vector<Point>& starts);
    bool isAreaSuitableForPlacement(const MapGrid& map, Point center, int radius) const;
    void spawnNeutralTechBuildings(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, const std::vector<NativeRegion>& regions, bool initialZeroQuotaRegions = false);
    void distributeTiberium(MapGrid& map, const MapSeedOptions& options, const RmgConfig& config, NativeRandomizer& rng, const std::vector<NativeRegion>& regions, const std::vector<Point>& starts);
    void placeTiberiumPatch(MapGrid& map, const MapSeedOptions& options, const RmgConfig& config, NativeRandomizer& rng, Point seed, int amount, bool rich, const char* traceBranch = nullptr, bool seedIsNative = false, double amountJitter = 0.0, int amountRawCount = 0, std::uint32_t amountRaw0 = 0, std::uint32_t amountRaw1 = 0, int nativeScratchTag = 0);
    void markCliffShoreExclusion(MapGrid& map);
    std::vector<int> generateHeightNoise(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng);
    void smoothHeightsAndAssignSlopes(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng);
    void cleanNativeSlopePatterns(MapGrid& map);
    int placeGroundTileBlob(MapGrid& map, NativeRandomizer& rng, Point center, Terrain terrain, int targetCells, bool allowPave);
    int placeGroundTileBlobNative(MapGrid& map, NativeRandomizer& rng, Point center, Terrain terrain, int targetCells, bool allowPave);
    void refreshNativeSurfaceLatOverrides(MapGrid& map);
    void placeSurfaceBlobs(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng);
    void placeSurfaceAndVegetation(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng);
    void placeSurfaceGrass(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng);
    int placeTreePatch(MapGrid& map, NativeRandomizer& rng, Point center, int targetTrees, double placementChance);
    int placeTreePatchNative(MapGrid& map, NativeRandomizer& rng, Point center, int targetTrees, double placementChance);
    void placeTrees(MapGrid& map, const MapSeedOptions& options, const RmgConfig& config, NativeRandomizer& rng);
    void placeNativeDecorations(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, const std::vector<NativeRegion>& regions);
    bool isDecorationCandidateCell(const MapGrid& map, Point p, bool allowExistingPaving = false) const;
    bool isPavedAndEmptyRect(const MapGrid& map, Point topLeft, int width, int height) const;
    bool canPlaceDecorationFootprint(const MapGrid& map, Point anchor, int width, int height, bool requirePave, bool allowExistingPaving, int requiredLevel = -1);
    void stampDecorationFootprint(MapGrid& map, Point anchor, int width, int height, Terrain terrain);
    UrbanRegion createUrbanRegion(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, Point center, double targetScale = 1.0);
    bool selectDecorationCenter(const MapGrid& map, NativeRandomizer& rng, const std::vector<NativeRegion>& regions, Point* center);
    void placeCliffEdgeTiles(MapGrid& map, NativeRandomizer& rng, const UrbanRegion& urban);
    void placeUrbanPatches(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, const UrbanRegion& urban);
    void placeRegionBuildings(MapGrid& map, NativeRandomizer& rng, const UrbanRegion& urban);
    void placeRegionUnits(MapGrid& map, NativeRandomizer& rng, const UrbanRegion& urban);
    void placeTerrainChain(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, Point center);
    void placeNeutralDecorationBuildings(MapGrid& map, NativeRandomizer& rng, Point center, int target);
    void placeNeutralDecorationUnits(MapGrid& map, NativeRandomizer& rng, Point center, int target);
    void captureStage(const std::string& name, const MapGrid& map, const NativeRandomizer& rng, bool initializedClear = false);
    void initializeNativeMapgenData();
    void resetNativeZonesAndRegionsScratch();
    void cleanupNativeMapgenData();
    void writeNativeRegionScratch(const std::vector<NativeRegion>& regions);
    void writeNativeInitialMapZonesScratch(const MapGrid& map);
    void expandNativeMapZonesScratch(const MapGrid& map, const MapSeedOptions& options);
    void assignRemainingNativeMapZonesScratch(MapGrid& map, NativeRandomizer& rng);
    std::vector<NativeRegion> buildNativeMapgenRegions(const MapGrid& map, bool committedOnly = false) const;

    NativeGenerationReport report_;
    std::vector<Point> nativeStartsForExclusion_;
    std::unordered_set<std::uint64_t> nativeSpawnExclusionKeys_;
    std::unordered_set<std::uint64_t> nativeCliffShoreExclusionKeys_;
    std::unordered_set<std::uint64_t> nativeTreeCandidateKeys_;
    std::unordered_map<std::uint64_t, int> nativeSurfacePreservedLandTypes_;
    std::unordered_set<std::uint64_t> nativePlacedIsoTileKeys_;
    std::unordered_set<std::uint64_t> nativeIsoTileTemplateFootprintKeys_;
    std::unordered_set<std::uint64_t> nativeMapgenSurfaceBlockedKeys_;
    std::unordered_set<std::uint64_t> nativeMapgenHalfHeightKeys_;
    std::unordered_map<std::uint64_t, int> nativeInitialCliffZoneIds_;
    std::unordered_map<std::uint64_t, int> nativeInitialCliffTiles_;
    std::unordered_map<std::uint64_t, int> nativeMapgenTempTags_;
    std::unordered_map<std::uint64_t, int> nativeMapgenZoneIds_;
    std::unordered_map<std::uint64_t, int> nativeMapgenCacheFlags_;
    std::vector<NativeRegion> nativeRegionScratchRegions_;
    std::vector<int> nativeMapgenScratchZoneLevels_;
    std::vector<int> nativeMapgenScratchZoneCellCounts_;
    std::vector<unsigned char> nativeMapgenScratchZoneWetFlags_;
    bool nativeMapgenDataActive_ = false;
    // MapType=3/4 keeps cliff/ore edits in the native mapgen workspace until
    // the region-processing boundary.  The visible MapGrid remains Blank
    // during the earlier Oracle stages.
    bool nativeHighResourceWorkspaceActive_ = false;
    bool nativeHighResourceVisibleProjectionReady_ = false;
    bool nativeHighResourceLatMissingNeighborSame_ = false;
    bool nativeCliffPlateauEnabled_ = false;
    // Origin's PlaceCliffsAndOre owns a monotonically increasing active zone
    // id.  It is separate from the later region scratch reset because the
    // cliff/ore workspace must retain these ids through terrain_generated.
    int nativeHighResourceActiveZone_ = 0;
    int nativeHighResourceLevel_ = 4;
    int nativeHighResourcePlacedCellCount_ = 0;
    int nativeOreChainAttempt_ = 0;
    int nativeScenarioUniqueId_ = -1;
    int nativeMapgenDefaultTempTag_ = 0;
    int nativeMapgenDefaultZoneId_ = 0;
    int nativeMapgenDefaultCacheFlags_ = -1;
    Point nativeInvalidCellPoint_{};
    int nativeInvalidCellTile_ = 0xFFFF;
    const NativeTheaterCatalog* nativeTheaterCatalog_ = nullptr;
};

std::string describeNativeStageReport(const NativeGenerationReport& report);

} // namespace rmg
