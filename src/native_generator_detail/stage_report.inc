int nativeInterpolatedPlayableSize(int seedSize, int players, bool highResourceTerrain) noexcept
{
    return nativeInterpolatedPlayableSizeImpl(seedSize, players, highResourceTerrain);
}

void NativeMapGenerator::captureStage(const std::string& name, const MapGrid& map, const NativeRandomizer& rng, bool initializedClear)
{
    NativeStageSnapshot snapshot;
    snapshot.index = static_cast<int>(report_.stageSnapshots.size()) + 1;
    snapshot.name = name;
    const Rect mapRect = nativeSavedMapRect(map);
    snapshot.valid = true;
    snapshot.x = mapRect.x;
    snapshot.y = mapRect.y;
    snapshot.width = mapRect.w;
    snapshot.height = mapRect.h;
    snapshot.cells = mapRect.w * mapRect.h;
    snapshot.rngDraws = rng.drawCount();
    snapshot.rngState = hex64(rng.stateFingerprint());

    std::uint64_t mapHash = kFnvOffset;
    std::uint64_t terrainHash = kFnvOffset;
    std::uint64_t overlayHash = kFnvOffset;
    std::uint64_t objectHash = kFnvOffset;
    std::uint64_t heightHash = kFnvOffset;
    std::uint64_t flagsHash = kFnvOffset;
    for (std::uint64_t* hash : {&mapHash, &terrainHash, &overlayHash, &objectHash, &heightHash, &flagsHash}) {
        hashI32(*hash, snapshot.width);
        hashI32(*hash, snapshot.height);
        hashI32(*hash, snapshot.x);
        hashI32(*hash, snapshot.y);
        hashI32(*hash, snapshot.cells);
    }

    // GenerateTerrain/PlaceCliffsAndOre operate on the native map shell, not
    // only on the local playable rectangle.  The Origin stage dump therefore
    // includes the current g_MapgenData tile/height/level projection before
    // the later CellClass materialization boundary.
    const std::vector<IsoTileOverride>* stageIsoTileOverrides = &report_.isoTileOverrides;
    std::vector<NativeStageCell> nativeCells = buildNativeStageCells(
        map,
        initializedClear,
        stageIsoTileOverrides,
        &report_.nativeOverlayOverrides,
        &report_.nativeObjectOverrides,
        &nativeMapgenSurfaceBlockedKeys_,
        &nativeMapgenTempTags_,
        &nativeMapgenZoneIds_,
        nativeMapgenDataActive_,
        nativeMapgenDefaultTempTag_,
        nativeMapgenDefaultZoneId_);
    if (nativeHighResourceWorkspaceActive_
        && (name == "terrain_generated"
            || name == "post_process_terrain"
            || name == "decorate_ponds"
            || name == "regions_built"
            || name == "regions_expanded"
            || name == "regions_connected"
            || name == "regions_rebuilt"
            || name == "regions_processed")) {
        for (NativeStageCell& cell : nativeCells) {
            cell.landType = 0;
        }
    }
    dumpNativeStageCellsIfRequested(name, nativeCells);
    snapshot.nonNullCells = static_cast<int>(nativeCells.size());

    for (const NativeStageCell& nativeCell : nativeCells) {
        const Cell* sourceCell = nativeCell.source;
        const Cell fallbackCell{};
        const Cell& cell = sourceCell ? *sourceCell : fallbackCell;
        switch (cell.terrain) {
        case Terrain::Water:
            ++snapshot.waterCells;
            break;
        case Terrain::Land:
            ++snapshot.landCells;
            break;
        case Terrain::Shore:
            ++snapshot.shoreCells;
            break;
        case Terrain::Dirt:
            ++snapshot.dirtCells;
            ++snapshot.landCells;
            break;
        case Terrain::Rough:
            ++snapshot.roughCells;
            ++snapshot.landCells;
            break;
        case Terrain::Pave:
            ++snapshot.paveCells;
            ++snapshot.landCells;
            break;
        case Terrain::Green:
            ++snapshot.greenCells;
            ++snapshot.landCells;
            break;
        case Terrain::Cliff:
            ++snapshot.cliffCells;
            break;
        }

        switch (cell.overlay) {
        case Overlay::None:
            break;
        case Overlay::Ore:
            ++snapshot.oreCells;
            break;
        case Overlay::Gems:
            ++snapshot.gemCells;
            break;
        case Overlay::Tree:
            ++snapshot.treeOverlayCells;
            break;
        case Overlay::Tech:
            ++snapshot.techOverlayCells;
            break;
        case Overlay::Start:
            ++snapshot.startOverlayCells;
            break;
        case Overlay::NeutralBuilding:
            ++snapshot.neutralBuildingCells;
            break;
        case Overlay::NeutralUnit:
            ++snapshot.neutralUnitCells;
            break;
        case Overlay::TerrainChain:
            ++snapshot.terrainChainCells;
            break;
        case Overlay::Grass:
        case Overlay::Bridge:
            break;
        }
        if (nativeCell.overlayTypeIndex >= 168 && nativeCell.overlayTypeIndex <= 177) {
            ++snapshot.grassCells;
        }

        switch (cell.terrainObject) {
        case TerrainObject::None:
            break;
        case TerrainObject::TiberiumTree:
            ++snapshot.tiberiumTreeObjects;
            break;
        case TerrainObject::OrePatchLamp:
            ++snapshot.orePatchLampObjects;
            break;
        }

        if (isPassable(cell)) {
            ++snapshot.passableCells;
        }
        if (isBuildable(cell)) {
            ++snapshot.buildableCells;
        }
        if (isWet(cell)) {
            ++snapshot.wetCells;
        }
        if (isEncodedShorePiece(cell.slope)) {
            const ShorePiece piece = decodeShorePiece(cell.slope);
            ++snapshot.encodedShoreCells;
            if (cell.terrain == Terrain::Water) {
                ++snapshot.encodedShoreWaterCells;
                if (piece.subTile != 0) {
                    ++snapshot.encodedShoreWaterNonZeroSubTiles;
                }
            } else {
                ++snapshot.encodedShoreLandCells;
                if (piece.subTile != 0) {
                    ++snapshot.encodedShoreLandNonZeroSubTiles;
                }
            }
            if (piece.subTile != 0) {
                ++snapshot.encodedShoreNonZeroSubTiles;
            }
        }
        if (nativeCell.height != 0) {
            ++snapshot.heightCells;
        }
        if (nativeCell.level != 0) {
            ++snapshot.levelNonZeroCells;
        }
        if (nativeCell.slopeIndex >= 0) {
            ++snapshot.slopeCells;
        }
        if (cell.spawnExclusion) {
            ++snapshot.spawnExclusionCells;
        }
        if (cell.cliffShoreExclusion) {
            ++snapshot.cliffShoreExclusionCells;
        }
        if (cell.regionTag != 0) {
            ++snapshot.regionTaggedCells;
        }
        if (cell.tempTag != 0) {
            ++snapshot.tempTaggedCells;
        }
        if (cell.urbanRegionMark) {
            ++snapshot.urbanMarkedCells;
        }

        hashPointPosition(mapHash, nativeCell.cell);
        hashI32(mapHash, nativeCell.isoTileTypeIndex);
        hashI32(mapHash, nativeCell.overlayTypeIndex);
        hashI32(mapHash, nativeCell.smudgeTypeIndex);
        hashI32(mapHash, static_cast<unsigned char>(nativeCell.height));
        hashI32(mapHash, static_cast<unsigned char>(nativeCell.level));
        hashI32(mapHash, nativeCell.slopeIndex);
        hashI32(mapHash, nativeCell.overlayData);
        hashI32(mapHash, nativeCell.smudgeData);
        hashI32(mapHash, static_cast<unsigned char>(nativeCell.iceGrowthAllowed));
        hashI32(mapHash, nativeCell.landType);
        hashI32(mapHash, nativeCell.objectAbstractType);
        hashI32(mapHash, nativeCell.objectTypeIndex);
        hashI32(mapHash, nativeCell.altObjectAbstractType);
        hashI32(mapHash, nativeCell.altObjectTypeIndex);

        hashPointPosition(terrainHash, nativeCell.cell);
        hashI32(terrainHash, nativeCell.isoTileTypeIndex);
        hashI32(terrainHash, nativeCell.smudgeTypeIndex);
        hashI32(terrainHash, nativeCell.landType);

        hashPointPosition(overlayHash, nativeCell.cell);
        hashI32(overlayHash, nativeCell.overlayTypeIndex);
        hashI32(overlayHash, nativeCell.overlayData);

        hashPointPosition(objectHash, nativeCell.cell);
        hashI32(objectHash, nativeCell.objectAbstractType);
        hashI32(objectHash, nativeCell.objectTypeIndex);
        hashI32(objectHash, nativeCell.altObjectAbstractType);
        hashI32(objectHash, nativeCell.altObjectTypeIndex);

        hashPointPosition(heightHash, nativeCell.cell);
        hashI32(heightHash, static_cast<unsigned char>(nativeCell.height));
        hashI32(heightHash, static_cast<unsigned char>(nativeCell.level));
        hashI32(heightHash, nativeCell.slopeIndex);
        hashI32(heightHash, static_cast<unsigned char>(nativeCell.iceGrowthAllowed));

        hashPointPosition(flagsHash, nativeCell.cell);
        hashI32(flagsHash, cell.regionTag);
        hashI32(flagsHash, cell.tempTag);
        hashBool(flagsHash, cell.spawnExclusion);
        hashBool(flagsHash, cell.cliffShoreExclusion);
        hashBool(flagsHash, cell.urbanRegionMark);
    }

    snapshot.mapHash = hex64(mapHash);
    snapshot.terrainHash = hex64(terrainHash);
    snapshot.overlayHash = hex64(overlayHash);
    snapshot.objectHash = hex64(objectHash);
    snapshot.heightHash = hex64(heightHash);
    snapshot.flagsHash = hex64(flagsHash);

    snapshot.terrainType = report_.terrainType;
    snapshot.terrainPatches = report_.terrainPatches;
    snapshot.floodFillAttempts = report_.floodFillAttempts;
    snapshot.cliffLines = report_.cliffLines;
    snapshot.waterChains = report_.waterChains;
    snapshot.regions = report_.regions;
    snapshot.connectedRegions = report_.connectedRegions;
    snapshot.regionProcessCalls = report_.regionProcessCalls;
    snapshot.startsPlaced = report_.startsPlaced;
    snapshot.techBuildings = report_.techBuildings;
    snapshot.tiberiumPatches = report_.tiberiumPatches;
    snapshot.pond2x2Tiles = report_.pond2x2Tiles;
    snapshot.pondEdgeTiles = report_.pondEdgeTiles;
    snapshot.pondTemplateCells = report_.pondTemplateCells;
    snapshot.shoreTilesPlaced = report_.shoreTilesPlaced;
    snapshot.heightNoiseCells = report_.heightNoiseCells;
    snapshot.slopePatternMatches = report_.slopePatternMatches;
    snapshot.surfaceBlobs = report_.surfaceBlobs;
    snapshot.surfaceCells = report_.surfaceCells;
    snapshot.treeClusters = report_.treeClusters;
    snapshot.treeCells = report_.treeCells;
    snapshot.decorationEvents = report_.decorationEvents;
    snapshot.terrainChains = report_.terrainChains;
    snapshot.urbanRegions = report_.urbanRegions;
    snapshot.isoTileOverrides = static_cast<int>(report_.isoTileOverrides.size());

    report_.stageSnapshots.push_back(std::move(snapshot));
}

std::string describeNativeStageReport(const NativeGenerationReport& report)
{
    std::ostringstream out;
    out << "native stage report\n";
    out << "stages=" << report.stageSnapshots.size() << '\n';
    for (const NativeStageSnapshot& stage : report.stageSnapshots) {
        out << "stage=" << stage.index
            << " name=" << stage.name
            << " valid=" << (stage.valid ? 1 : 0)
            << " x=" << stage.x
            << " y=" << stage.y
            << " width=" << stage.width
            << " height=" << stage.height
            << " cells=" << stage.cells
            << " nonNullCells=" << stage.nonNullCells
            << " rngDraws=" << stage.rngDraws
            << " rngState=" << stage.rngState
            << " mapHash=" << stage.mapHash
            << " terrainHash=" << stage.terrainHash
            << " overlayHash=" << stage.overlayHash
            << " objectHash=" << stage.objectHash
            << " heightHash=" << stage.heightHash
            << " flagsHash=" << stage.flagsHash
            << " terrainType=" << stage.terrainType
            << " land=" << stage.landCells
            << " water=" << stage.waterCells
            << " shore=" << stage.shoreCells
            << " dirt=" << stage.dirtCells
            << " rough=" << stage.roughCells
            << " pave=" << stage.paveCells
            << " green=" << stage.greenCells
            << " cliff=" << stage.cliffCells
            << " passable=" << stage.passableCells
            << " buildable=" << stage.buildableCells
            << " wet=" << stage.wetCells
            << " heightCells=" << stage.heightCells
            << " encodedShore=" << stage.encodedShoreCells
            << " encodedShoreWater=" << stage.encodedShoreWaterCells
            << " encodedShoreLand=" << stage.encodedShoreLandCells
            << " encodedShoreSubTileNonZero=" << stage.encodedShoreNonZeroSubTiles
            << " encodedShoreWaterSubTileNonZero=" << stage.encodedShoreWaterNonZeroSubTiles
            << " encodedShoreLandSubTileNonZero=" << stage.encodedShoreLandNonZeroSubTiles
            << " levelNonZero=" << stage.levelNonZeroCells
            << " slopeCells=" << stage.slopeCells
            << " spawnExclusion=" << stage.spawnExclusionCells
            << " cliffShoreExclusion=" << stage.cliffShoreExclusionCells
            << " regionTagged=" << stage.regionTaggedCells
            << " tempTagged=" << stage.tempTaggedCells
            << " urbanMarked=" << stage.urbanMarkedCells
            << " ore=" << stage.oreCells
            << " gems=" << stage.gemCells
            << " treeOverlay=" << stage.treeOverlayCells
            << " techOverlay=" << stage.techOverlayCells
            << " startOverlay=" << stage.startOverlayCells
            << " neutralBuildings=" << stage.neutralBuildingCells
            << " neutralUnits=" << stage.neutralUnitCells
            << " terrainChainOverlay=" << stage.terrainChainCells
            << " grass=" << stage.grassCells
            << " tiberiumTreeObjects=" << stage.tiberiumTreeObjects
            << " orePatchLampObjects=" << stage.orePatchLampObjects
            << " terrainPatches=" << stage.terrainPatches
            << " floodFillAttempts=" << stage.floodFillAttempts
            << " cliffLines=" << stage.cliffLines
            << " waterChains=" << stage.waterChains
            << " regions=" << stage.regions
            << " connectedRegions=" << stage.connectedRegions
            << " regionProcessCalls=" << stage.regionProcessCalls
            << " startsPlaced=" << stage.startsPlaced
            << " techBuildings=" << stage.techBuildings
            << " tiberiumPatches=" << stage.tiberiumPatches
            << " pond2x2=" << stage.pond2x2Tiles
            << " pondEdge=" << stage.pondEdgeTiles
            << " pondTemplateCells=" << stage.pondTemplateCells
            << " shoreTilesPlaced=" << stage.shoreTilesPlaced
            << " heightNoiseCells=" << stage.heightNoiseCells
            << " slopePatternMatches=" << stage.slopePatternMatches
            << " surfaceBlobs=" << stage.surfaceBlobs
            << " surfaceCells=" << stage.surfaceCells
            << " treeClusters=" << stage.treeClusters
            << " treeCells=" << stage.treeCells
            << " decorationEvents=" << stage.decorationEvents
            << " terrainChains=" << stage.terrainChains
            << " urbanRegions=" << stage.urbanRegions
            << " isoTileOverrides=" << stage.isoTileOverrides
            << '\n';
    }
    return out.str();
}
