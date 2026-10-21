MapGrid NativeMapGenerator::generate(const MapSeedOptions& options, const RmgConfig& config)
{
    report_ = {};
    nativeScenarioUniqueId_ = options.nativeScenarioUniqueId;

    g_nativeTileTheater = options.theater;
    nativeStartsForExclusion_.clear();
    nativeSpawnExclusionKeys_.clear();
    nativeCliffShoreExclusionKeys_.clear();
    nativeTreeCandidateKeys_.clear();
    nativeSurfacePreservedLandTypes_.clear();
    nativePlacedIsoTileKeys_.clear();
    nativeIsoTileTemplateFootprintKeys_.clear();
    nativeInitialCliffZoneIds_.clear();
    nativeInitialCliffTiles_.clear();
    nativeHighResourceWorkspaceActive_ = false;
    nativeHighResourceVisibleProjectionReady_ = false;
    initializeNativeMapgenData();
    resetRegionSelectionTrace();
    report_.rmgMinimumTiberium = config.minimumTiberium;
    report_.rmgMaximumTiberium = config.maximumTiberium;
    report_.maxTrees = config.maxTrees;
    NativeRandomizer rng(options.seed);
    const bool highResourceTerrain = isNativeHighResourceMode(options);
    MapGrid map(
        resolvedPlayableSize(options.width, options.numPlayers, highResourceTerrain, options.nativeWidthInterpolation),
        resolvedPlayableSize(options.height, options.numPlayers, highResourceTerrain, options.nativeHeightInterpolation));
    if (options.nativeWidthInterpolation || options.nativeHeightInterpolation) {
        const int localWidth = map.width();
        const int localHeight = map.height();
        map.setNativeGeometry(
            {0, 0, localWidth + 4, localHeight + 12},
            {2, 5, localWidth, localHeight});
    }


    nativeCliffPlateauEnabled_ = rng.randomUnit() < 0.25;
    map.fill(Terrain::Water);
    captureStage("init_water", map, rng, true);
    // Origin starts the function trace at its init_water hook, after the
    // bootstrap draw and immediately before Type 3/4 cliff and ore work.
    native_trace::beginFromEnvironment();
    native_trace::setRandomizerState(rng.next1(), rng.next2());
    native_trace::setGaussianState(rng.hasGaussianSpare(), rng.gaussianSpareBits());

    int terrainType = options.terrainTypeOverride;
    if (terrainType < 0) {
        terrainType = std::clamp(options.mapType, 0, 2);
    }
    report_.terrainType = highResourceTerrain ? 3 : terrainType;
    nativeHighResourceWorkspaceActive_ = highResourceTerrain;
    nativeHighResourceLatMissingNeighborSame_ = highResourceTerrain && options.waterAmount > 20;

    if (highResourceTerrain) {
        map.fill(Terrain::Land);
        for (Cell& cell : map.cells()) {
            cell.level = kNativeBaseMapLevel;
        }
        initializeHighResourceNativeWorkspace(map);
        if (options.waterAmount != 0) {
            placeCliffsAndOre(map, options, rng);
        }
        nativeInitialCliffZoneIds_ = nativeMapgenZoneIds_;
        nativeInitialCliffTiles_.reserve(report_.isoTileOverrides.size());
        for (const IsoTileOverride& override : report_.isoTileOverrides) {
            nativeInitialCliffTiles_[nativeIsoKey(override.iso)] = nativeTileTypeIndexForOverride(override);
        }
        // The original generator keeps cliff/ore edits in g_MapgenData while
        // the visible CellClass remains Blank through the early region stages.
        // placeCliffLine uses MapGrid as a working surface, so restore the
        // visible projection before the first terrain snapshot.
        map.fill(Terrain::Land);
        captureStage("terrain_generated", map, rng);
    } else {
        if (native_trace::coreFunctionEnabled(native_trace::GenerateTerrain)) {
            const Rect traceVisibleRect = nativeLocalRect(map);
            const Rect traceMapRect = nativeSavedMapRect(map);
            native_trace::EventData traceData;
            traceData.args[0] = static_cast<std::uint32_t>(options.mapType);
            traceData.args[1] = static_cast<std::uint32_t>(options.resources);
            traceData.args[2] = static_cast<std::uint32_t>(options.waterAmount);
            traceData.args[3] = static_cast<std::uint32_t>(options.numPlayers);
            traceData.args[4] = static_cast<std::uint32_t>(traceVisibleRect.w);
            traceData.args[5] = static_cast<std::uint32_t>(traceVisibleRect.h);
            traceData.regionCount = traceMapRect.w;
            traceData.regionCellCount = traceMapRect.h;
            traceData.resultKind = native_trace::ResultNone;
            native_trace::enterDetailed(0x59A6C0, native_trace::GenerateTerrain, traceData);
        }
        switch (terrainType) {
        case 0:
            generateTerrainType0(map, options, rng);
            break;
        case 1:
            generateTerrainType1(map, options, rng);
            break;
        default:
            generateTerrainType2(map, options, rng);
            break;
        }
        if (terrainType == 2) {
            resetNativeZonesAndRegionsScratch();
            captureStage("terrain_generated", map, rng);
        }
        if (terrainType != 2 || !nativeType2FinalPostpassEnabled()) {
            postProcessGeneratedTerrain(map);
        }
    rebuildShorePieceTiles(map);
        if (terrainType != 2) {
            resetNativeZonesAndRegionsScratch();
            captureStage("terrain_generated", map, rng);
        }
    }
    captureStage("post_process_terrain", map, rng);

    const bool traceDecoratePonds = native_trace::coreFunctionEnabled(native_trace::DecoratePonds);
    native_trace::EventData decoratePondsTraceData;
    if (traceDecoratePonds) {
        const Rect visibleRect = nativeLocalRect(map);
        const Rect mapRect = nativeSavedMapRect(map);
        const int mapSize = mapRect.w + mapRect.h + 1;
        decoratePondsTraceData.args[0] = kNativeTileWaterBase;
        decoratePondsTraceData.args[1] = static_cast<std::uint32_t>(visibleRect.w);
        decoratePondsTraceData.args[2] = static_cast<std::uint32_t>(visibleRect.h);
        decoratePondsTraceData.args[3] = static_cast<std::uint32_t>(mapRect.w);
        decoratePondsTraceData.args[4] = static_cast<std::uint32_t>(mapRect.h);
        decoratePondsTraceData.args[5] = packNativeCell(nativeInvalidCellPoint_);
        decoratePondsTraceData.regionCount = mapSize;
        decoratePondsTraceData.regionCellCount = mapSize * mapSize;
        decoratePondsTraceData.resultKind = native_trace::ResultNone;
        native_trace::enterDetailed(0x59C630, native_trace::DecoratePonds, decoratePondsTraceData);
        if (native_trace::detailRequested()) {
            const NativePondTraceSnapshot snapshot = captureNativePondTraceSnapshot(
                map,
                report_.isoTileOverrides,
                nativeMapgenZoneIds_,
                nativeMapgenDefaultZoneId_,
                nativeMapgenDataActive_,
                nativeInvalidCellPoint_,
                nativeInvalidCellTile_,
                true);
            native_trace::detailCurrent(native_trace::DetailOrderedCellState, snapshot.detail);
        }
    }

    decorateNativeFramePonds(map, options, rng);

    if (traceDecoratePonds) {
        const NativePondTraceSnapshot snapshot = captureNativePondTraceSnapshot(
            map,
            report_.isoTileOverrides,
            nativeMapgenZoneIds_,
            nativeMapgenDefaultZoneId_,
            nativeMapgenDataActive_,
            nativeInvalidCellPoint_,
            nativeInvalidCellTile_,
            native_trace::detailRequested());
        decoratePondsTraceData.result[0] = snapshot.slotCount;
        decoratePondsTraceData.result[1] = static_cast<std::uint32_t>(snapshot.hash);
        decoratePondsTraceData.result[2] = static_cast<std::uint32_t>(snapshot.hash >> 32);
        decoratePondsTraceData.result[3] = static_cast<std::uint32_t>(nativeInvalidCellTile_);
        decoratePondsTraceData.resultKind = native_trace::ResultLegacy;
        native_trace::exitDetailed(
            0x59C630,
            native_trace::DecoratePonds,
            decoratePondsTraceData,
            native_trace::DetailOrderedCellState,
            snapshot.detail);
    }
    captureStage("decorate_ponds", map, rng);

    auto regions = buildPassableRegions(map, options, "initial");
    const bool traceAssignMapZones = native_trace::coreFunctionEnabled(native_trace::AssignMapZones);
    native_trace::EventData assignMapZonesTraceData;
    if (traceAssignMapZones) {
        const NativeZoneWorkspaceTraceSnapshot snapshot = captureNativeZoneWorkspaceTraceSnapshot(
            map,
            report_.isoTileOverrides,
            nativeMapgenTempTags_,
            nativeMapgenZoneIds_,
            nativeMapgenCacheFlags_,
            nativeMapgenDefaultTempTag_,
            nativeMapgenDefaultZoneId_,
            nativeMapgenDefaultCacheFlags_,
            nativeMapgenScratchZoneLevels_,
            nativeMapgenScratchZoneCellCounts_,
            nativeMapgenScratchZoneWetFlags_,
            nativeInvalidCellPoint_,
            nativeInvalidCellTile_,
            false);
        assignMapZonesTraceData.args[0] = static_cast<std::uint32_t>(nativeSavedMapRect(map).w + nativeSavedMapRect(map).h + 1);
        assignMapZonesTraceData.args[1] = snapshot.slotCount;
        assignMapZonesTraceData.args[2] = snapshot.regionCount;
        assignMapZonesTraceData.args[3] = snapshot.nextZoneId;
        assignMapZonesTraceData.args[4] = packNativeCell(nativeInvalidCellPoint_);
        assignMapZonesTraceData.args[5] = static_cast<std::uint32_t>(nativeInvalidCellTile_);
        assignMapZonesTraceData.regionCount = static_cast<std::int32_t>(snapshot.regionCount);
        assignMapZonesTraceData.regionCellCount = static_cast<std::int32_t>(snapshot.slotCount);
        assignMapZonesTraceData.resultKind = native_trace::ResultNone;
        native_trace::enterDetailed(0x58CF90, native_trace::AssignMapZones, assignMapZonesTraceData);
        if (native_trace::detailRequested()) {
            const NativeZoneWorkspaceTraceSnapshot detailSnapshot = captureNativeZoneWorkspaceTraceSnapshot(
                map,
                report_.isoTileOverrides,
                nativeMapgenTempTags_,
                nativeMapgenZoneIds_,
                nativeMapgenCacheFlags_,
                nativeMapgenDefaultTempTag_,
                nativeMapgenDefaultZoneId_,
                nativeMapgenDefaultCacheFlags_,
                nativeMapgenScratchZoneLevels_,
                nativeMapgenScratchZoneCellCounts_,
                nativeMapgenScratchZoneWetFlags_,
                nativeInvalidCellPoint_,
                nativeInvalidCellTile_,
                true);
            native_trace::detailCurrent(native_trace::DetailOrderedZoneWorkspace, detailSnapshot.detail);
        }
    }
    writeNativeInitialMapZonesScratch(map);
    if (traceAssignMapZones) {
        const NativeZoneWorkspaceTraceSnapshot snapshot = captureNativeZoneWorkspaceTraceSnapshot(
            map,
            report_.isoTileOverrides,
            nativeMapgenTempTags_,
            nativeMapgenZoneIds_,
            nativeMapgenCacheFlags_,
            nativeMapgenDefaultTempTag_,
            nativeMapgenDefaultZoneId_,
            nativeMapgenDefaultCacheFlags_,
            nativeMapgenScratchZoneLevels_,
            nativeMapgenScratchZoneCellCounts_,
            nativeMapgenScratchZoneWetFlags_,
            nativeInvalidCellPoint_,
            nativeInvalidCellTile_,
            native_trace::detailRequested());
        assignMapZonesTraceData.result[0] = snapshot.valueCount;
        assignMapZonesTraceData.result[1] = static_cast<std::uint32_t>(snapshot.hash);
        assignMapZonesTraceData.result[2] = static_cast<std::uint32_t>(snapshot.hash >> 32);
        assignMapZonesTraceData.result[3] = snapshot.nextZoneId;
        assignMapZonesTraceData.resultKind = native_trace::ResultLegacy;
        native_trace::exitDetailed(
            0x58CF90,
            native_trace::AssignMapZones,
            assignMapZonesTraceData,
            native_trace::DetailOrderedZoneWorkspace,
            snapshot.detail);
    }
    captureStage("regions_built", map, rng);
    expandNativeRegionBorders(map, options, regions);
    expandNativeMapZonesScratch(map, options);
    const bool traceAssignZonesToRegions = native_trace::coreFunctionEnabled(
        native_trace::AssignZonesToRegions);
    native_trace::EventData assignZonesToRegionsTraceData;
    if (traceAssignZonesToRegions) {
        const NativeZoneWorkspaceTraceSnapshot snapshot = captureNativeZoneWorkspaceTraceSnapshot(
            map,
            report_.isoTileOverrides,
            nativeMapgenTempTags_,
            nativeMapgenZoneIds_,
            nativeMapgenCacheFlags_,
            nativeMapgenDefaultTempTag_,
            nativeMapgenDefaultZoneId_,
            nativeMapgenDefaultCacheFlags_,
            nativeMapgenScratchZoneLevels_,
            nativeMapgenScratchZoneCellCounts_,
            nativeMapgenScratchZoneWetFlags_,
            nativeInvalidCellPoint_,
            nativeInvalidCellTile_,
            false);
        assignZonesToRegionsTraceData.args[0] = 0;
        assignZonesToRegionsTraceData.args[1] = static_cast<std::uint32_t>(
            nativeSavedMapRect(map).w + nativeSavedMapRect(map).h + 1);
        assignZonesToRegionsTraceData.args[2] = snapshot.slotCount;
        assignZonesToRegionsTraceData.args[3] = snapshot.regionCount;
        assignZonesToRegionsTraceData.args[4] = snapshot.nextZoneId;
        assignZonesToRegionsTraceData.args[5] = packNativeCell(nativeInvalidCellPoint_);
        assignZonesToRegionsTraceData.regionCount = static_cast<std::int32_t>(snapshot.regionCount);
        assignZonesToRegionsTraceData.regionCellCount = static_cast<std::int32_t>(snapshot.slotCount);
        assignZonesToRegionsTraceData.resultKind = native_trace::ResultNone;
        native_trace::enterDetailed(
            0x58D010,
            native_trace::AssignZonesToRegions,
            assignZonesToRegionsTraceData);
        if (native_trace::detailRequested()) {
            const NativeZoneWorkspaceTraceSnapshot detailSnapshot = captureNativeZoneWorkspaceTraceSnapshot(
                map,
                report_.isoTileOverrides,
                nativeMapgenTempTags_,
                nativeMapgenZoneIds_,
                nativeMapgenCacheFlags_,
                nativeMapgenDefaultTempTag_,
                nativeMapgenDefaultZoneId_,
                nativeMapgenDefaultCacheFlags_,
                nativeMapgenScratchZoneLevels_,
                nativeMapgenScratchZoneCellCounts_,
                nativeMapgenScratchZoneWetFlags_,
                nativeInvalidCellPoint_,
                nativeInvalidCellTile_,
                true);
            native_trace::detailCurrent(
                native_trace::DetailOrderedZoneWorkspace,
                detailSnapshot.detail);
        }
    }
    assignRemainingNativeMapZonesScratch(map, rng);
    if (traceAssignZonesToRegions) {
        const NativeZoneWorkspaceTraceSnapshot snapshot = captureNativeZoneWorkspaceTraceSnapshot(
            map,
            report_.isoTileOverrides,
            nativeMapgenTempTags_,
            nativeMapgenZoneIds_,
            nativeMapgenCacheFlags_,
            nativeMapgenDefaultTempTag_,
            nativeMapgenDefaultZoneId_,
            nativeMapgenDefaultCacheFlags_,
            nativeMapgenScratchZoneLevels_,
            nativeMapgenScratchZoneCellCounts_,
            nativeMapgenScratchZoneWetFlags_,
            nativeInvalidCellPoint_,
            nativeInvalidCellTile_,
            native_trace::detailRequested());
        assignZonesToRegionsTraceData.result[0] = snapshot.valueCount;
        assignZonesToRegionsTraceData.result[1] = static_cast<std::uint32_t>(snapshot.hash);
        assignZonesToRegionsTraceData.result[2] = static_cast<std::uint32_t>(snapshot.hash >> 32);
        assignZonesToRegionsTraceData.result[3] = snapshot.nextZoneId;
        assignZonesToRegionsTraceData.resultKind = native_trace::ResultLegacy;
        native_trace::exitDetailed(
            0x58D010,
            native_trace::AssignZonesToRegions,
            assignZonesToRegionsTraceData,
            native_trace::DetailOrderedZoneWorkspace,
            snapshot.detail);
    }
    captureStage("regions_expanded", map, rng);
    connectNativeRegions(map, options, rng, regions);
    captureStage("regions_connected", map, rng);
    if (report_.connectedRegions > 0) {
        regions = buildPassableRegions(map, options, "rebuild");
    }
    captureStage("regions_rebuilt", map, rng);
    if (highResourceTerrain) {
        const auto nativeRegionEnvEnabled = [](const char* name) {
            const char* value = std::getenv(name);
            return value != nullptr && value[0] == '1';
        };
        const auto traceMapgenCellBoundary = [&](std::uint32_t site, std::uint16_t functionId) {
            if (!native_trace::active()) {
                return;
            }
            if (!native_trace::detailRequestedForFunction(functionId)) {
                native_trace::call(site, functionId);
                return;
            }
            const NativePondTraceSnapshot snapshot = captureNativePondTraceSnapshot(
                map,
                report_.isoTileOverrides,
                nativeMapgenZoneIds_,
                nativeMapgenDefaultZoneId_,
                nativeMapgenDataActive_,
                nativeInvalidCellPoint_,
                nativeInvalidCellTile_,
                true);
            native_trace::callWithDetail(
                site,
                functionId,
                native_trace::DetailOrderedCellState,
                snapshot.detail);
        };
        auto mapgenRegions = buildNativeMapgenRegions(map, true);
        if (native_trace::active()) {
            native_trace::call(0x598D62, native_trace::ProcessAllRegions);
            native_trace::enter(0x58EBC0, native_trace::ProcessAllRegions);
        }
        dumpRegionSelectionTrace("mapgen_before_process", options, mapgenRegions);
        processNativeRegions(map, options, rng, mapgenRegions);
        dumpRegionSelectionTrace("mapgen_after_process", options, mapgenRegions);
        if (nativeRegionEnvEnabled("RMG_NATIVE_REGION_DEBUG_AFTER_PROCESS")) {
            captureStage("debug_after_process", map, rng);
        }
        if (native_trace::active()) {
            native_trace::exit(0x58EE90, native_trace::ProcessAllRegions);
            native_trace::call(0x598D67, native_trace::InitializeMapRegions);
            native_trace::EventData initializeData;
            initializeData.regionCount = static_cast<int>(mapgenRegions.size());
            native_trace::enterDetailed(
                0x58EF10,
                native_trace::InitializeMapRegions,
                initializeData);
        }

        // InitializeMapRegions is a separate native phase.  ProcessAllRegions
        // must finish before recursive levels, zone reset and the second flood
        // are performed; folding these calls into processNativeRegionWorkspace
        // changes the region split observed by the cliff pass.
        const bool initializeLevelPassSucceeded = calculateNativeMapgenLevelsRecursive(map);
        resetNativeZonesAndRegionsScratch();
        assignRemainingNativeMapZonesScratch(map, rng);
        mapgenRegions = buildNativeMapgenRegions(map);
        for (Cell& cell : map.cells()) {
            cell.regionTag = -1;
        }
        for (const NativeRegion& region : mapgenRegions) {
            for (Point source : region.cells) {
                if (map.inBounds(source)) {
                    map.at(source).regionTag = region.id;
                }
            }
        }
        if (initializeLevelPassSucceeded) {
            buildRegionNeighborLists(map, mapgenRegions);
        }
        if (initializeLevelPassSucceeded
            && !nativeRegionEnvEnabled("RMG_NATIVE_REGION_SKIP_BRIDGES")) {
            processRegionBridgesAndCliffs(map, options, rng, mapgenRegions);
        }
        if (native_trace::active()) {
            native_trace::EventData initializeData;
            initializeData.regionCount = static_cast<int>(mapgenRegions.size());
            initializeData.result[0] = initializeLevelPassSucceeded ? 1u : 0u;
            native_trace::exitDetailed(
                0x58F0AC,
                native_trace::InitializeMapRegions,
                initializeData);
            traceMapgenCellBoundary(0x598D6C, native_trace::FixCliffSlopeTransitions);
        }
        if (!nativeRegionEnvEnabled("RMG_NATIVE_REGION_SKIP_FIX")) {
            fixNativeCliffSlopeTransitions(map, rng);
        }
        traceMapgenCellBoundary(0x598D73, native_trace::CalculateLevelsAndZones);
        if (nativeRegionEnvEnabled("RMG_NATIVE_REGION_DEBUG_BEFORE_LEVELS")) {
            captureStage("debug_before_levels", map, rng);
        }
        if (!nativeRegionEnvEnabled("RMG_NATIVE_REGION_SKIP_LEVELS")) {
            calculateNativeMapgenLevelsRecursive(map);
        }
        if (!nativeRegionEnvEnabled("RMG_NATIVE_REGION_SKIP_FLAT")) {
            if (nativeRegionEnvEnabled("RMG_NATIVE_REGION_DEBUG_BEFORE_FLAT")) {
                captureStage("debug_before_flat", map, rng);
            }
            calculateNativeFlatCellLevels(map, rng);
        }
        traceMapgenCellBoundary(0x598D80, native_trace::RandomizeCliffTileVariants);
        randomizeNativeCliffTileVariants(map, rng);
        mapgenRegions = buildNativeMapgenRegions(map);
        dumpRegionSelectionTrace("mapgen_after_initialize", options, mapgenRegions);
        regions = std::move(mapgenRegions);
        buildRegionNeighborLists(map, regions);
    }
    if (native_trace::active()) {
        native_trace::call(0x598D87, native_trace::GenerateRandomMap);
    }
    if (highResourceTerrain && !nativeHighResourceVisibleProjectionReady_) {
        // RandomizeCliffTileVariants is the native boundary where the
        // temporary cliff/ore workspace first becomes visible on CellClass.
        synchronizeHighResourceWorkspaceToMap(map);
        nativeHighResourceVisibleProjectionReady_ = true;
    }
    captureStage("regions_processed", map, rng);
    placeGreenTilesNearShore(map, rng);
    if (highResourceTerrain) {
        recalculateNativeHighResourceLandTypes(map);
    }
    captureStage("green_near_shore", map, rng);

    // InitializeRegionsAndResources rebuilds the passable region list after
    // ExpandGreenTiles. The MapType 3/4 processing regions above describe
    // terrain subregions and do not yet carry spawn/resource quotas.
    regions = buildPassableRegions(map, options, "starts");
    nativeRegionScratchRegions_ = regions;
    const char* regionDumpPath = std::getenv(kNativeRegionDumpPathEnv);
    if (regionDumpPath && regionDumpPath[0] != '\0') {
        const auto savedReport = report_;
        MapGrid probeMap = map;
        buildPassableRegions(probeMap, options, "probe_before_starts");
        report_ = savedReport;
    }
    dumpRegionSelectionTrace("before_starts", options, regions);
    writeNativeRegionScratch(nativeRegionScratchRegions_.empty() ? regions : nativeRegionScratchRegions_);
    auto starts = placeStartPoints(map, options, rng, regions);
    if (options.mapType == 0) {
        spawnNeutralTechBuildings(map, options, rng, regions, true);
    }
    markSpawnExclusionZones(map, starts);
    captureStage("starts", map, rng);
    captureStage("spawn_exclusion", map, rng);
    if (options.mapType != 0) {
        spawnNeutralTechBuildings(map, options, rng, regions);
    }
    captureStage("tech", map, rng);
    distributeTiberium(map, options, config, rng, regions, starts);
    captureStage("tiberium", map, rng);
    resetNativeZonesAndRegionsScratch();
    smoothHeightsAndAssignSlopes(map, options, rng);
    captureStage("height_slope", map, rng);

    if (options.theater == 0) {
        placeSurfaceAndVegetation(map, options, rng);
        report_.treePlacementPhase = 1;
        placeTrees(map, options, config, rng);
        placeSurfaceGrass(map, options, rng);
        captureStage("surface", map, rng);
        captureStage("trees", map, rng);
        captureStage("grass", map, rng);
    } else {
        placeSurfaceBlobs(map, options, rng);
        report_.treePlacementPhase = 1;
        placeTrees(map, options, config, rng);
        captureStage("surface", map, rng);
        captureStage("trees", map, rng);
        captureStage("grass", map, rng);
    }
    // Origin GenerateMap captures surface/trees/grass/decorations at the same
    // semantic point; PlaceDecorations is not invoked on the strict native path.
    captureStage("decorations", map, rng);

    if (!highResourceTerrain) {
        rebuildShorePieceTiles(map);
        rebuildCliffPieceTiles(map, true, true);
    }
    report_.shoreSlopeVariants = static_cast<int>(std::count_if(map.cells().begin(), map.cells().end(), [](const Cell& cell) {
        if (cell.terrain != Terrain::Shore) {
            return false;
        }
        if (!isEncodedShorePiece(cell.slope)) {
            return cell.slope != 0;
        }
        const ShorePiece piece = decodeShorePiece(cell.slope);
        return piece.tile != 0 || piece.subTile != 0;
    }));
    report_.cliffSlopeVariants = static_cast<int>(std::count_if(map.cells().begin(), map.cells().end(), [](const Cell& cell) {
        if (cell.terrain != Terrain::Cliff) {
            return false;
        }
        return isEncodedCliffPiece(cell.slope)
            ? decodeCliffPiece(cell.slope).tile != 0
            : cell.slope != 0;
    }));

    refreshNativeLatOverridesForMap(
        map,
        report_.isoTileOverrides,
        true,
        nativeHighResourceLatMissingNeighborSame_,
        nativeInvalidCellTile_);
    if (nativeTheaterCatalog_ != nullptr) {
        std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
        overrideIndexByIso.reserve(report_.isoTileOverrides.size());
        for (std::size_t index = 0; index < report_.isoTileOverrides.size(); ++index) {
            overrideIndexByIso[nativeIsoKey(report_.isoTileOverrides[index].iso)] = index;
        }

        const std::vector<NativeIteratorCell> finalIteratorCells = buildNativeIteratorCells(
            map,
            report_.isoTileOverrides);
        std::unordered_map<std::uint64_t, int> finalLandTypeByIso;
        std::unordered_map<std::uint64_t, int> finalLevelByIso;
        finalLandTypeByIso.reserve(finalIteratorCells.size());
        finalLevelByIso.reserve(finalIteratorCells.size());

        // RecalcAttributes derives LandType from the selected TMP slot rather
        // than from the abstract terrain category used during generation.
        for (const NativeIteratorCell& iteratorCell : finalIteratorCells) {
            const NativeIsoTileTemplate* tile = nativeTheaterCatalog_->tileType(iteratorCell.isoTileTypeIndex);
            if (tile == nullptr || tile->slots.empty()) {
                finalLandTypeByIso[nativeIsoKey(iteratorCell.cell)] = iteratorCell.landType;
                finalLevelByIso[nativeIsoKey(iteratorCell.cell)] = iteratorCell.level;
                continue;
            }
            const std::size_t slotIndex = std::min<std::size_t>(
                static_cast<std::size_t>(std::max(0, iteratorCell.height)),
                tile->slots.size() - 1);
            const NativeIsoTileSlot& slot = tile->slots[slotIndex];
            const int landType = slot.present
                ? nativeLandTypeForTmpTerrainType(slot.terrainType)
                : iteratorCell.landType;
            finalLandTypeByIso[nativeIsoKey(iteratorCell.cell)] = landType;
            finalLevelByIso[nativeIsoKey(iteratorCell.cell)] = iteratorCell.level;
            if (!slot.present) {
                continue;
            }
            const auto overrideIt = overrideIndexByIso.find(nativeIsoKey(iteratorCell.cell));
            if (overrideIt != overrideIndexByIso.end()) {
                report_.isoTileOverrides[overrideIt->second].landType = landType;
            }
            const Point source = nativeCellToSourcePoint(map, iteratorCell.cell);
            if (map.inBounds(source) && sourcePointToNativeCell(map, source) == iteratorCell.cell) {
                map.at(source).landType = landType;
            }
        }

        // Rule.ini's CliffBackImpassability=2 marks clear/water/beach/ice
        // cells as Rock when one of the six native neighbors is four levels
        // higher. These are the exact offsets used by RecalcAttributes.
        constexpr std::array<Point, 6> kFinalCliffBackNeighbors{{
            {0, -1}, {-1, 0}, {2, 2}, {1, 1}, {-1, 1}, {1, -1},
        }};
        for (const NativeIteratorCell& iteratorCell : finalIteratorCells) {
            const std::uint64_t key = nativeIsoKey(iteratorCell.cell);
            const int landType = finalLandTypeByIso[key];
            if (landType != 0 && landType != 2 && landType != 6 && landType != 8) {
                continue;
            }
            bool cliffBack = false;
            for (const Point offset : kFinalCliffBackNeighbors) {
                const auto neighborLevelIt = finalLevelByIso.find(nativeIsoKey({
                    iteratorCell.cell.x + offset.x,
                    iteratorCell.cell.y + offset.y,
                }));
                if (neighborLevelIt != finalLevelByIso.end()
                    && neighborLevelIt->second >= iteratorCell.level + 4) {
                    cliffBack = true;
                    break;
                }
            }
            if (!cliffBack) {
                continue;
            }
            finalLandTypeByIso[key] = 3;
            const auto overrideIt = overrideIndexByIso.find(key);
            if (overrideIt != overrideIndexByIso.end()) {
                report_.isoTileOverrides[overrideIt->second].landType = 3;
            }
            const Point source = nativeCellToSourcePoint(map, iteratorCell.cell);
            if (map.inBounds(source) && sourcePointToNativeCell(map, source) == iteratorCell.cell) {
                map.at(source).landType = 3;
            }
        }
    }
    recalculateNativeTiberiumOverlayData(report_.nativeOverlayOverrides);
    cleanupNativeMapgenData();
    captureStage("final_rebuild", map, rng);

    report_.landCells = map.countTerrain(Terrain::Land)
        + map.countTerrain(Terrain::Dirt)
        + map.countTerrain(Terrain::Rough)
        + map.countTerrain(Terrain::Pave)
        + map.countTerrain(Terrain::Green);
    report_.dirtCells = map.countTerrain(Terrain::Dirt);
    report_.roughCells = map.countTerrain(Terrain::Rough);
    report_.greenCells = map.countTerrain(Terrain::Green);
    report_.waterCells = map.countTerrain(Terrain::Water);
    report_.shoreCells = map.countTerrain(Terrain::Shore);
    report_.oreCells = map.countOverlay(Overlay::Ore);
    report_.gemCells = map.countOverlay(Overlay::Gems);
    report_.treeCells = map.countOverlay(Overlay::Tree);
    report_.tiberiumTrees = static_cast<int>(std::count_if(
        report_.nativeObjectOverrides.begin(),
        report_.nativeObjectOverrides.end(),
        [](const NativeObjectOverride& object) {
            return object.objectAbstractType == 36;
        }));
    report_.orePatchLamps = map.countTerrainObject(TerrainObject::OrePatchLamp);
    report_.oreValue = std::accumulate(map.cells().begin(), map.cells().end(), 0, [](int total, const Cell& cell) {
        return total + overlayValue(cell);
    });
    evaluateStartFairness(map, starts);
    report_.evaluation = evaluateMap(map, starts, {
        options.seed,
        options.numPlayers,
        options.tiberium,
    });
    report_.notes.push_back("Native mode mirrors the original placement order; final shore/cliff pieces are remapped onto the local TMP catalog.");
    captureStage("final", map, rng);
    native_trace::finish();
    return map;
}
