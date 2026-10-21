void NativeMapGenerator::postProcessGeneratedTerrain(MapGrid& map)
{
    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(
        map,
        report_.isoTileOverrides);
    std::unordered_set<std::uint64_t> nativeCellKeys;
    nativeCellKeys.reserve(iteratorCells.size());
    for (const NativeIteratorCell& nativeCell : iteratorCells) {
        nativeCellKeys.insert(nativeIsoKey(nativeCell.cell));
    }
    static constexpr std::array<Point, 4> kNativeOffsets{{
        Point{0, -1}, Point{1, 0}, Point{0, 1}, Point{-1, 0}}};
    for (const NativeIteratorCell& nativeCell : iteratorCells) {
        if (nativeCell.isoTileTypeIndex != kNativeTileWaterBase) {
            continue;
        }
        for (Point offset : kNativeOffsets) {
            const Point neighbor{nativeCell.cell.x + offset.x, nativeCell.cell.y + offset.y};
            if (!nativeCellKeys.contains(nativeIsoKey(neighbor))) {
                nativeInvalidCellPoint_ = neighbor;
            }
        }
    }

    std::vector<Point> toLand;
    for (const auto& cell : map.cells()) {
        if (cell.terrain != Terrain::Water) {
            continue;
        }

        ++report_.terrainPostWaterChecks;
        const auto neighbors = map.neighbors4({cell.x, cell.y});
        bool cardinalNeighborsAreBlank = neighbors.size() == 4;
        for (Point neighbor : neighbors) {
            if (!isSolidLand(map.at(neighbor))) {
                cardinalNeighborsAreBlank = false;
                break;
            }
        }
        if (cardinalNeighborsAreBlank) {
            toLand.push_back({cell.x, cell.y});
        }
    }

    for (Point point : toLand) {
        map.at(point).terrain = Terrain::Land;
        ++report_.terrainPostWaterCleared;
    }

    rebuildWaterShorePieces(map);
    seedGreenTilesAroundShorePieces(map, true);
}

void NativeMapGenerator::rebuildWaterShorePieces(MapGrid& map)
{
    for (auto& cell : map.cells()) {
        if (cell.terrain == Terrain::Shore) {
            cell.terrain = Terrain::Land;
            cell.slope = -1;
        }
    }

    std::vector<Point> toShore;
    for (const auto& cell : map.cells()) {
        if (cell.terrain != Terrain::Water) {
            continue;
        }

        for (Point neighbor : map.neighbors4({cell.x, cell.y})) {
            if (isSolidLand(map.at(neighbor))) {
                toShore.push_back(neighbor);
            }
        }
    }

    for (Point point : toShore) {
        Cell& cell = map.at(point);
        if (isSolidLand(cell)) {
            cell.terrain = Terrain::Shore;
            cell.height = 0;
            cell.slope = -1;
        }
    }
}

void NativeMapGenerator::seedGreenTilesAroundShorePieces(MapGrid& map, bool countTerrainPostMetrics)
{
    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(
        map,
        report_.isoTileOverrides);
    std::unordered_set<std::uint64_t> nativeCellKeys;
    nativeCellKeys.reserve(iteratorCells.size());
    for (const NativeIteratorCell& nativeCell : iteratorCells) {
        nativeCellKeys.insert(nativeIsoKey(nativeCell.cell));
    }
    static constexpr std::array<Point, 4> kNativeOffsets{{
        Point{0, -1}, Point{1, 0}, Point{0, 1}, Point{-1, 0}}};
    for (const NativeIteratorCell& nativeCell : iteratorCells) {
        if (!isNativeShoreTileIndex(nativeCell.isoTileTypeIndex)) {
            continue;
        }
        for (Point offset : kNativeOffsets) {
            const Point neighbor{nativeCell.cell.x + offset.x, nativeCell.cell.y + offset.y};
            if (!nativeCellKeys.contains(nativeIsoKey(neighbor))) {
                nativeInvalidCellPoint_ = neighbor;
            }
        }
    }

    std::vector<Point> toGreen;
    for (const auto& cell : map.cells()) {
        if (!isNativeShorePiece(cell)) {
            continue;
        }

        bool hasCandidate = false;
        for (Point neighbor : map.neighbors4({cell.x, cell.y})) {
            if (countTerrainPostMetrics) {
                ++report_.terrainPostShoreCandidates;
            }
            if (isNativeBlankTile(map.at(neighbor))) {
                toGreen.push_back(neighbor);
                hasCandidate = true;
            }
        }
        if (hasCandidate && countTerrainPostMetrics) {
            ++report_.terrainPostShoreAnchors;
        }
    }

    for (Point point : toGreen) {
        Cell& cell = map.at(point);
        if (isNativeBlankTile(cell)) {
            cell.terrain = Terrain::Green;
            if (countTerrainPostMetrics) {
                ++report_.terrainPostShoreTiles;
            }
        }
    }
}

void NativeMapGenerator::placeGreenTilesNearShore(MapGrid& map, NativeRandomizer& rng)
{
    const bool traceEnabled = native_trace::coreFunctionEnabled(native_trace::ExpandGreenTiles);
    native_trace::EventData traceData;
    if (traceEnabled) {
        const Rect visibleRect = nativeLocalRect(map);
        const Rect mapRect = nativeSavedMapRect(map);
        const int mapSize = mapRect.w + mapRect.h + 1;
        traceData.args[0] = static_cast<std::uint32_t>(visibleRect.w);
        traceData.args[1] = static_cast<std::uint32_t>(visibleRect.h);
        traceData.args[2] = static_cast<std::uint32_t>(mapRect.w);
        traceData.args[3] = static_cast<std::uint32_t>(mapRect.h);
        traceData.args[4] = static_cast<std::uint32_t>(mapSize);
        traceData.args[5] = packNativeCell(nativeInvalidCellPoint_);
        traceData.regionCount = mapSize;
        traceData.regionCellCount = mapSize * mapSize;
        traceData.resultKind = native_trace::ResultNone;
        native_trace::enterDetailed(0x59B740, native_trace::ExpandGreenTiles, traceData);
    }
    const char* greenDumpPath = std::getenv(kNativeGreenDumpPathEnv);
    std::ofstream greenDump;
    if (greenDumpPath != nullptr && greenDumpPath[0] != '\0') {
        const std::filesystem::path path{greenDumpPath};
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        greenDump.open(path, std::ios::binary);
        greenDump << "event,step,index,candidateCount,x,y,tileBefore,selectedWasBlank,placed,added,"
                     "rngDrawBefore,rngDrawAfter,rngRaw,rngFingerprintAfter,anchorX,anchorY,facing\n";
    }

    for (Cell& cell : map.cells()) {
        cell.landType = nativeLandTypeForCell(cell);
    }

    std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
    overrideIndexByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t i = 0; i < report_.isoTileOverrides.size(); ++i) {
        IsoTileOverride& override = report_.isoTileOverrides[i];
        override.landType = nativeLandTypeForTile(override.terrain, override.variant, override.subTile);
        overrideIndexByIso[nativeIsoKey(override.iso)] = i;
    }

    std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, report_.isoTileOverrides);
    std::unordered_map<std::uint64_t, NativeIteratorCell> cellByIso;
    std::unordered_map<std::uint64_t, int> tileByIso;
    cellByIso.reserve(iteratorCells.size());
    tileByIso.reserve(iteratorCells.size());
    for (const NativeIteratorCell& nativeCell : iteratorCells) {
        const std::uint64_t key = nativeIsoKey(nativeCell.cell);
        cellByIso[key] = nativeCell;
        tileByIso[key] = nativeCell.isoTileTypeIndex;
    }

    std::vector<NativeGreenCandidate> candidates;
    std::vector<NativeGreenProbe> probes;
    Point invalidCellPoint = nativeInvalidCellPoint_;
    int invalidCellTile = nativeInvalidCellTile_;
    auto lookupNeighbor = [&](Point neighbor) {
        const auto tileIt = tileByIso.find(nativeIsoKey(neighbor));
        if (tileIt != tileByIso.end()) {
            return std::pair{false, tileIt->second};
        }

        // MouseClass::operator[] returns one shared InvalidCell and overwrites
        // its MapCoords on every failed lookup. Candidate vectors retain that
        // pointer, so all invalid entries observe the latest coordinates/tile.
        invalidCellPoint = neighbor;
        return std::pair{true, invalidCellTile};
    };
    for (const NativeIteratorCell& nativeCell : iteratorCells) {
        if (!isNativeGreenTileIndex(nativeCell.isoTileTypeIndex)) {
            continue;
        }

        static constexpr std::array<Point, 4> kOffsets{{Point{0, -1}, Point{1, 0}, Point{0, 1}, Point{-1, 0}}};
        for (std::size_t facingIndex = 0; facingIndex < kOffsets.size(); ++facingIndex) {
            const Point offset = kOffsets[facingIndex];
            const Point neighbor{nativeCell.cell.x + offset.x, nativeCell.cell.y + offset.y};
            const auto [invalidCell, tile] = lookupNeighbor(neighbor);
            const bool blank = isNativeClearTileIndex(tile);
            if (greenDump) {
                probes.push_back({neighbor, nativeCell.cell, static_cast<int>(facingIndex) * 2, tile, blank, invalidCell});
            }
            if (blank) {
                candidates.push_back({neighbor, nativeCell.cell, static_cast<int>(facingIndex) * 2, invalidCell});
            }
        }
    }

    report_.shoreInitialCandidates = static_cast<int>(candidates.size());
    report_.shoreTargetTiles = std::min(1000, report_.shoreInitialCandidates / 3);
    if (greenDump) {
        greenDump << "summary,-1,-1," << candidates.size()
            << ",-1,-1,-1,0,0,0,"
            << rng.drawCount() << ',' << rng.drawCount() << ",0,"
            << rng.stateFingerprint() << '\n';
        for (std::size_t i = 0; i < probes.size(); ++i) {
            const Point probePoint = probes[i].invalidCell ? invalidCellPoint : probes[i].point;
            const int probeTile = probes[i].invalidCell ? invalidCellTile : probes[i].tile;
            greenDump << "probe,-1," << i << ',' << candidates.size() << ','
                << probePoint.x << ',' << probePoint.y << ','
                << probeTile << ','
                << static_cast<int>(isNativeClearTileIndex(probeTile)) << ",0,0,"
                << rng.drawCount() << ',' << rng.drawCount() << ",0,"
                << rng.stateFingerprint() << ','
                << probes[i].anchor.x << ','
                << probes[i].anchor.y << ','
                << probes[i].facing << '\n';
        }
        if (const char* dumpInitial = std::getenv("RMG_NATIVE_GREEN_DUMP_INITIAL_CANDIDATES");
            dumpInitial != nullptr && dumpInitial[0] == '1') {
            for (std::size_t i = 0; i < candidates.size(); ++i) {
                const Point candidatePoint = candidates[i].invalidCell ? invalidCellPoint : candidates[i].point;
                const auto tileIt = tileByIso.find(nativeIsoKey(candidatePoint));
                const int tile = candidates[i].invalidCell
                    ? invalidCellTile
                    : (tileIt == tileByIso.end() ? -1 : tileIt->second);
                greenDump << "initial,-1," << i << ',' << candidates.size() << ','
                    << candidatePoint.x << ',' << candidatePoint.y
                    << ',' << tile
                    << ",0,0,0,"
                    << rng.drawCount() << ',' << rng.drawCount() << ",0,"
                    << rng.stateFingerprint() << ','
                    << candidates[i].anchor.x << ','
                    << candidates[i].anchor.y << ','
                    << candidates[i].facing << '\n';
            }
        }
    }

    int count = report_.shoreTargetTiles;
    int step = 0;
    while (count-- > 0 && !candidates.empty()) {
        const std::uint64_t drawBefore = rng.drawCount();
        const int index = rng.randomScaledInclusive(0, static_cast<int>(candidates.size()) - 1);
        const std::uint64_t drawAfter = rng.drawCount();
        const std::size_t candidateCountBefore = candidates.size();
        const NativeGreenCandidate candidate = candidates[static_cast<std::size_t>(index)];
        const Point point = candidate.invalidCell ? invalidCellPoint : candidate.point;
        candidates.erase(candidates.begin() + index);

        const auto selectedIt = cellByIso.find(nativeIsoKey(point));
        const NativeIteratorCell selected = selectedIt == cellByIso.end() ? NativeIteratorCell{point} : selectedIt->second;
        const int selectedTile = candidate.invalidCell ? invalidCellTile : selected.isoTileTypeIndex;
        const bool selectedWasBlank = isNativeClearTileIndex(selectedTile);
        if (candidate.invalidCell) {
            invalidCellTile = nativeGreenTileBase();
        } else {
            upsertIsoTileOverride(
                report_.isoTileOverrides,
                overrideIndexByIso,
                point,
                Terrain::Green,
                0,
                std::clamp(selected.height, 0, 255),
                std::clamp(selected.level, 0, 255),
                selected.iceGrowthAllowed);
            tileByIso[nativeIsoKey(point)] = nativeGreenTileBase();
            NativeIteratorCell updatedSelected = selected;
            updatedSelected.cell = point;
            updatedSelected.isoTileTypeIndex = nativeGreenTileBase();
            cellByIso[nativeIsoKey(point)] = updatedSelected;
        }
        ++report_.shoreTilesPlaced;

        int added = 0;
        static constexpr std::array<Point, 4> kOffsets{{Point{0, -1}, Point{1, 0}, Point{0, 1}, Point{-1, 0}}};
        for (std::size_t facingIndex = 0; facingIndex < kOffsets.size(); ++facingIndex) {
            const Point offset = kOffsets[facingIndex];
            const Point base = candidate.invalidCell ? invalidCellPoint : point;
            const Point neighbor{base.x + offset.x, base.y + offset.y};
            const auto [invalidCell, tile] = lookupNeighbor(neighbor);
            if (isNativeClearTileIndex(tile)) {
                candidates.push_back({neighbor, point, static_cast<int>(facingIndex) * 2, invalidCell});
                ++report_.shoreExpansionCandidates;
                ++added;
            }
        }
        if (greenDump) {
            greenDump << "step," << step << ',' << index << ',' << candidateCountBefore << ','
                << point.x << ',' << point.y << ','
                << selectedTile << ','
                << static_cast<int>(selectedWasBlank) << ','
                << 1 << ','
                << added << ','
                << drawBefore << ',' << drawAfter << ','
                << rng.lastRandom() << ','
                << rng.stateFingerprint() << ','
                << candidate.anchor.x << ','
                << candidate.anchor.y << ','
                << candidate.facing << '\n';
        }
        ++step;
    }

    if (traceEnabled) {
        const Rect mapRect = nativeSavedMapRect(map);
        const int mapSize = mapRect.w + mapRect.h + 1;
        std::unordered_map<std::uint64_t, int> tileByCell;
        tileByCell.reserve(report_.isoTileOverrides.size());
        for (const IsoTileOverride& override : report_.isoTileOverrides) {
            tileByCell[nativeIsoKey(override.iso)] = nativeTileTypeIndexForOverride(override);
        }
        std::uint64_t hash = kFnvOffset;
        std::vector<std::uint32_t> detail;
        if (native_trace::detailRequested()) {
            detail.reserve((static_cast<std::size_t>(mapSize) * mapSize + 1) * 3);
        }
        const auto appendState = [&](Point point, int tile, std::uint32_t valid) {
            const std::array<std::uint32_t, 3> values{
                static_cast<std::uint32_t>(static_cast<std::uint16_t>(point.x))
                    | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(point.y)) << 16),
                static_cast<std::uint32_t>(tile),
                valid,
            };
            for (std::uint32_t value : values) {
                hashU32(hash, value);
                if (native_trace::detailRequested()) {
                    detail.push_back(value);
                }
            }
        };
        for (int y = 0; y < mapSize; ++y) {
            for (int x = 0; x < mapSize; ++x) {
                const Point point{x, y};
                const auto tileIt = tileByCell.find(nativeIsoKey(point));
                appendState(point, tileIt == tileByCell.end() ? -1 : tileIt->second, 1);
            }
        }
        appendState(invalidCellPoint, invalidCellTile, 0);
        traceData.result[0] = static_cast<std::uint32_t>(mapSize * mapSize + 1);
        traceData.result[1] = static_cast<std::uint32_t>(hash);
        traceData.result[2] = static_cast<std::uint32_t>(hash >> 32);
        traceData.result[3] = static_cast<std::uint32_t>(invalidCellTile);
        traceData.resultKind = native_trace::ResultLegacy;
        native_trace::exitDetailed(
            0x59B740,
            native_trace::ExpandGreenTiles,
            traceData,
            native_trace::DetailOrderedIsoTiles,
            detail);
    }
    nativeInvalidCellPoint_ = invalidCellPoint;
    nativeInvalidCellTile_ = invalidCellTile;

    const std::vector<NativeIteratorCell> latCells = buildNativeIteratorCells(map, report_.isoTileOverrides);
    tileByIso.clear();
    tileByIso.reserve(latCells.size());
    for (const NativeIteratorCell& nativeCell : latCells) {
        tileByIso[nativeIsoKey(nativeCell.cell)] = nativeCell.isoTileTypeIndex;
    }

    for (const NativeIteratorCell& nativeCell : latCells) {
        if (!isNativeGreenTileIndex(nativeCell.isoTileTypeIndex)) {
            continue;
        }

        const Point iso = nativeCell.cell;
        const int variant = nativeGreenLatVariantForCell(tileByIso, iso);
        const int subTile = std::clamp(nativeCell.height, 0, 255);
        upsertIsoTileOverride(
            report_.isoTileOverrides,
            overrideIndexByIso,
            iso,
            Terrain::Green,
            variant,
            subTile,
            nativeCell.level,
            nativeCell.iceGrowthAllowed);
    }
    refreshNativeLatOverridesForMap(
        map,
        report_.isoTileOverrides,
        false,
        nativeHighResourceLatMissingNeighborSame_,
        nativeInvalidCellTile_);
}

void NativeMapGenerator::recalculateNativeHighResourceLandTypes(MapGrid& map)
{
    std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, report_.isoTileOverrides);
    std::unordered_map<std::uint64_t, std::size_t> iteratorCellByIso;
    iteratorCellByIso.reserve(iteratorCells.size() * 2);
    for (std::size_t index = 0; index < iteratorCells.size(); ++index) {
        iteratorCellByIso[nativeIsoKey(iteratorCells[index].cell)] = index;
    }

    std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
    overrideIndexByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t index = 0; index < report_.isoTileOverrides.size(); ++index) {
        overrideIndexByIso[nativeIsoKey(report_.isoTileOverrides[index].iso)] = index;
    }

    if (nativeTheaterCatalog_ != nullptr) {
        for (NativeIteratorCell& current : iteratorCells) {
            const std::uint64_t key = nativeIsoKey(current.cell);
            if (!nativePlacedIsoTileKeys_.contains(key)) {
                continue;
            }
            const NativeIsoTileTemplate* tile = nativeTheaterCatalog_->tileType(
                current.isoTileTypeIndex);
            if (tile == nullptr
                || current.height < 0
                || static_cast<std::size_t>(current.height) >= tile->slots.size()) {
                continue;
            }
            const NativeIsoTileSlot& slot = tile->slots[static_cast<std::size_t>(current.height)];
            const int landType = slot.present
                ? nativeLandTypeForTmpTerrainType(slot.terrainType)
                : -1;
            if (landType < 0) {
                continue;
            }
            current.landType = landType;
            const auto overrideIt = overrideIndexByIso.find(key);
            if (overrideIt != overrideIndexByIso.end()) {
                report_.isoTileOverrides[overrideIt->second].landType = landType;
            }
            const Point source = nativeCellToSourcePoint(map, current.cell);
            if (map.inBounds(source) && sourcePointToNativeCell(map, source) == current.cell) {
                map.at(source).landType = landType;
            }
        }
    }

    constexpr std::array<Point, 6> kCliffBackNeighbors{{
        {0, -1}, {-1, 0}, {2, 2}, {1, 1}, {-1, 1}, {1, -1},
    }};
    std::vector<std::size_t> cliffBackCells;
    for (std::size_t index = 0; index < iteratorCells.size(); ++index) {
        const NativeIteratorCell& current = iteratorCells[index];
        if (current.landType != 0
            && current.landType != 2
            && current.landType != 6
            && current.landType != 8) {
            continue;
        }
        for (Point offset : kCliffBackNeighbors) {
            const auto neighborIt = iteratorCellByIso.find(nativeIsoKey({
                current.cell.x + offset.x,
                current.cell.y + offset.y,
            }));
            if (neighborIt != iteratorCellByIso.end()
                && iteratorCells[neighborIt->second].level >= current.level + 4) {
                cliffBackCells.push_back(index);
                break;
            }
        }
    }

    for (std::size_t index : cliffBackCells) {
        NativeIteratorCell& current = iteratorCells[index];
        current.landType = 3;
        const auto overrideIt = overrideIndexByIso.find(nativeIsoKey(current.cell));
        if (overrideIt != overrideIndexByIso.end()) {
            report_.isoTileOverrides[overrideIt->second].landType = 3;
        }
        const Point source = nativeCellToSourcePoint(map, current.cell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == current.cell) {
            map.at(source).landType = 3;
        }
    }
}

void NativeMapGenerator::decoratePonds(MapGrid& map, NativeRandomizer& rng)
{
    for (auto& cell : map.cells()) {
        if (cell.terrain != Terrain::Water || cell.height != 0) {
            continue;
        }

        bool openWater2x2 = true;
        for (Point p : {Point{cell.x + 1, cell.y}, Point{cell.x, cell.y + 1}, Point{cell.x + 1, cell.y + 1}}) {
            if (!map.inBounds(p) || map.at(p).terrain != Terrain::Water || map.at(p).height != 0) {
                openWater2x2 = false;
                break;
            }
        }

        if (!openWater2x2 || rng.randomScaledInclusive(1, 10) == 1) {
            const int raw = rng.randomScaledInclusive(0, 200);
            cell.slope = std::clamp(8 + raw / 40, 8, 13);
            ++report_.pondEdgeTiles;
            continue;
        }

        const int raw = rng.randomScaledInclusive(0, 241);
        const int variant = raw < 240 ? raw / 40 : 247 - raw;
        const Point origin{cell.x, cell.y};
        for (Point p : {origin, Point{cell.x + 1, cell.y}, Point{cell.x, cell.y + 1}, Point{cell.x + 1, cell.y + 1}}) {
            if (!map.inBounds(p)) {
                continue;
            }
            Cell& water = map.at(p);
            if (water.terrain == Terrain::Water && water.height == 0) {
                water.slope = std::clamp(variant, 0, 7);
                ++report_.pondTemplateCells;
            }
        }
        ++report_.pond2x2Tiles;
    }
}

void NativeMapGenerator::decorateNativeFramePonds(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng)
{
    struct DecorCell {
        Point iso;
        Point source;
        bool hasSource = false;
        Terrain terrain = Terrain::Water;
        int sourceHeight = 0;
        int variant = 0;
        int subTile = 0;
        int level = 4;
        int landType = 0;
        bool iceGrowthAllowed = false;
    };

    const Rect localRect = nativeLocalRect(map);
    const Rect mapRect = nativeSavedMapRect(map);
    const int displayWidth = mapRect.w * 2 - 1;
    const int workspaceSize = mapRect.w + mapRect.h + 1;
    const int diamondBoundsMin = mapRect.w;
    const int diamondBoundsMax = mapRect.w + 2 * mapRect.h;
    const std::vector<IsoTileOverride> inputIsoOverrides = report_.isoTileOverrides;
    std::unordered_map<std::uint64_t, const IsoTileOverride*> inputOverrideByIso;
    inputOverrideByIso.reserve(inputIsoOverrides.size());
    for (const IsoTileOverride& override : inputIsoOverrides) {
        inputOverrideByIso[nativeIsoKey(override.iso)] = &override;
    }
    report_.isoTileOverrides.clear();
    const char* pondDumpPath = std::getenv(kNativePondDumpPathEnv);
    std::ofstream pondDump;
    if (pondDumpPath != nullptr && pondDumpPath[0] != '\0') {
        const std::filesystem::path path{pondDumpPath};
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        pondDump.open(path, std::ios::binary);
        pondDump << "step,anchorX,anchorY,sourceX,sourceY,baseWater,eastWater,southWater,southEastWater,"
                    "open2x2,"
                    "anchorTerrain,anchorHeight,anchorVariant,anchorSubTile,anchorHasSource,anchorSourceX,anchorSourceY,"
                    "eastTerrain,eastHeight,eastVariant,eastSubTile,eastHasSource,eastSourceX,eastSourceY,"
                    "southTerrain,southHeight,southVariant,southSubTile,southHasSource,southSourceX,southSourceY,"
                    "southEastTerrain,southEastHeight,southEastVariant,southEastSubTile,southEastHasSource,southEastSourceX,southEastSourceY,"
                    "branch,drawBefore,drawAfter,rollRaw,rollValue,choiceRaw,choiceValue,variant,subTile,"
                    "writeCount,slot0X,slot0Y,slot0Variant,slot0SubTile,slot0Written,"
                    "slot1X,slot1Y,slot1Variant,slot1SubTile,slot1Written,"
                    "slot2X,slot2Y,slot2Variant,slot2SubTile,slot2Written,"
                    "slot3X,slot3Y,slot3Variant,slot3SubTile,slot3Written,"
                    "rngFingerprintAfter\n";
    }

    std::vector<DecorCell> cells;
    cells.reserve(static_cast<std::size_t>(displayWidth * mapRect.h));
    std::unordered_map<std::uint64_t, std::size_t> indexByIso;
    indexByIso.reserve(static_cast<std::size_t>(displayWidth * mapRect.h));

    for (int row = 0; row < mapRect.h; ++row) {
        for (int dx = 0; dx < displayWidth; ++dx) {
            DecorCell cell;
            cell.iso = nativeDisplayToIso(dx, row, mapRect.w);
            const Point mapPoint = nativeCellToMapPoint(cell.iso, mapRect.w);
            cell.level = kNativeBaseMapLevel;
            if (contains(localRect, mapPoint)) {
                const Point source{mapPoint.x - localRect.x, mapPoint.y - localRect.y};
                if (map.inBounds(source)) {
                    const Cell& sourceCell = map.at(source);
                    cell.source = source;
                    cell.hasSource = true;
                    cell.terrain = sourceCell.terrain;
                    cell.sourceHeight = sourceCell.height;
                    cell.level = sourceCell.level;
                    cell.landType = sourceCell.landType;
                    cell.iceGrowthAllowed = sourceCell.iceGrowthAllowed;
                    if (sourceCell.terrain == Terrain::Water) {
                        cell.variant = std::max(0, sourceCell.slope);
                    }
                }
            }
            if (const auto overrideIt = inputOverrideByIso.find(nativeIsoKey(cell.iso)); overrideIt != inputOverrideByIso.end()) {
                const IsoTileOverride& override = *overrideIt->second;
                cell.terrain = override.terrain;
                cell.sourceHeight = override.subTile;
                cell.variant = override.variant;
                cell.subTile = override.subTile;
                cell.level = override.level;
                cell.landType = override.landType;
                cell.iceGrowthAllowed = override.iceGrowthAllowed;
            }
            indexByIso[nativeIsoKey(cell.iso)] = cells.size();
            cells.push_back(cell);
        }
    }

    if (pondDumpPath != nullptr && pondDumpPath[0] != '\0') {
        std::filesystem::path inputPath{pondDumpPath};
        inputPath += ".input.csv";
        std::ofstream inputDump(inputPath, std::ios::binary);
        inputDump << "index,x,y,sourceX,sourceY,hasSource,terrain,sourceHeight,variant,subTile,level,landType\n";
        for (std::size_t index = 0; index < cells.size(); ++index) {
            const DecorCell& cell = cells[index];
            inputDump << index << ',' << cell.iso.x << ',' << cell.iso.y << ','
                << (cell.hasSource ? cell.source.x : -1) << ','
                << (cell.hasSource ? cell.source.y : -1) << ','
                << static_cast<int>(cell.hasSource) << ','
                << static_cast<int>(cell.terrain) << ',' << cell.sourceHeight << ','
                << cell.variant << ',' << cell.subTile << ',' << cell.level << ',' << cell.landType << '\n';
        }
    }

    auto lookup = [&](Point iso) -> DecorCell* {
        const auto it = indexByIso.find(nativeIsoKey(iso));
        if (it == indexByIso.end()) {
            return nullptr;
        }
        return &cells[it->second];
    };

    auto isBaseWater = [](const DecorCell* cell) {
        return cell
            && cell->terrain == Terrain::Water
            && cell->sourceHeight == 0
            && cell->variant == 0
            && cell->subTile == 0;
    };
    auto appendDecorCellDebug = [](std::ostream& out, const DecorCell* cell) {
        if (cell == nullptr) {
            out << "-1,-1,-1,-1,0,-1,-1,";
            return;
        }
        out << static_cast<int>(cell->terrain) << ','
            << cell->sourceHeight << ','
            << cell->variant << ','
            << cell->subTile << ','
            << static_cast<int>(cell->hasSource) << ','
            << (cell->hasSource ? cell->source.x : -1) << ','
            << (cell->hasSource ? cell->source.y : -1) << ',';
    };
    auto insideNativeDiamond = [&](Point cell) {
        if (cell.x < 0 || cell.y < 0 || cell.x >= workspaceSize || cell.y >= workspaceSize) {
            return false;
        }
        const int sum = cell.x + cell.y;
        return sum > diamondBoundsMin
            && cell.x - cell.y < diamondBoundsMin
            && cell.y - cell.x < diamondBoundsMin
            && sum <= diamondBoundsMax;
    };

    std::vector<Point> iteratorCells;
    iteratorCells.reserve(static_cast<std::size_t>(workspaceSize * workspaceSize / 2));
    int nextX = 1;
    int nextY = mapRect.w;
    int currentY = mapRect.w - 1;
    const int maxIterations = std::max(1, workspaceSize * workspaceSize * 2);
    for (int i = 0; i < maxIterations; ++i) {
        const Point current{nextX, nextY};
        if (currentY != 0) {
            ++nextX;
            --nextY;
            --currentY;
        } else {
            const int oldNextY = nextY;
            const int oldNextX = nextX;
            nextX = oldNextY;
            nextY = oldNextX;
            if (((oldNextY - mapRect.w + oldNextX - 1) & 1) != 0) {
                currentY = mapRect.w - 1;
                nextY = oldNextX + 1;
            } else {
                currentY = mapRect.w - 2;
                nextX = oldNextY + 1;
            }
        }
        if (!insideNativeDiamond(current)) {
            break;
        }
        iteratorCells.push_back(current);
    }

    int pondStep = 0;
    for (Point anchor : iteratorCells) {
        DecorCell* cellPtr = lookup(anchor);
        if (cellPtr == nullptr) {
            continue;
        }
        DecorCell& cell = *cellPtr;
        const bool baseWater = isBaseWater(&cell);
        if (!baseWater) {
            continue;
        }

        const auto lookupNeighbor = [&](Point point) {
            DecorCell* neighbor = lookup(point);
            if (neighbor == nullptr) {
                nativeInvalidCellPoint_ = point;
            }
            return neighbor;
        };
        DecorCell* east = lookupNeighbor({cell.iso.x + 1, cell.iso.y});
        DecorCell* south = lookupNeighbor({cell.iso.x, cell.iso.y + 1});
        DecorCell* southEast = lookupNeighbor({cell.iso.x + 1, cell.iso.y + 1});
        const bool eastWater = isBaseWater(east);
        const bool southWater = isBaseWater(south);
        const bool southEastWater = isBaseWater(southEast);
        const bool openWater2x2 = eastWater && southWater && southEastWater;

        const std::uint64_t drawBefore = rng.drawCount();
        std::uint32_t rollRaw = 0;
        int rollValue = -1;
        bool edgeBranch = !openWater2x2;
        if (openWater2x2) {
            rollValue = rng.randomScaledInclusive(1, 10);
            rollRaw = rng.lastRandom();
            edgeBranch = rollValue == 1;
        }

        if (edgeBranch) {
            const int raw = rng.randomScaledInclusive(0, 200);
            const std::uint32_t rawRandom = rng.lastRandom();
            cell.variant = std::clamp(8 + raw / 40, 8, 13);
            cell.subTile = 0;
            ++report_.pondEdgeTiles;
            if (pondDump) {
                pondDump << pondStep << ','
                    << cell.iso.x << ',' << cell.iso.y << ','
                    << (cell.hasSource ? cell.source.x : -1) << ',' << (cell.hasSource ? cell.source.y : -1) << ','
                    << static_cast<int>(baseWater) << ','
                    << static_cast<int>(eastWater) << ','
                    << static_cast<int>(southWater) << ','
                    << static_cast<int>(southEastWater) << ','
                    << static_cast<int>(openWater2x2) << ',';
                appendDecorCellDebug(pondDump, &cell);
                appendDecorCellDebug(pondDump, east);
                appendDecorCellDebug(pondDump, south);
                appendDecorCellDebug(pondDump, southEast);
                pondDump << "edge," << drawBefore << ',' << rng.drawCount() << ','
                    << rollRaw << ',' << rollValue << ','
                    << rawRandom << ',' << raw << ',' << cell.variant << ',' << cell.subTile << ','
                    << 1 << ','
                    << cell.iso.x << ',' << cell.iso.y << ',' << cell.variant << ',' << cell.subTile << ",1,"
                    << "-1,-1,-1,-1,0,"
                    << "-1,-1,-1,-1,0,"
                    << "-1,-1,-1,-1,0,"
                    << rng.stateFingerprint() << '\n';
            }
            ++pondStep;
            continue;
        }

        const int raw = rng.randomScaledInclusive(0, 241);
        const std::uint32_t rawRandom = rng.lastRandom();
        const int variant = raw < 240 ? raw / 40 : 247 - raw;
        const std::array<Point, 4> slots{
            cell.iso,
            Point{cell.iso.x + 1, cell.iso.y},
            Point{cell.iso.x, cell.iso.y + 1},
            Point{cell.iso.x + 1, cell.iso.y + 1},
        };
        std::array<int, 4> slotVariants{-1, -1, -1, -1};
        std::array<int, 4> slotSubTiles{-1, -1, -1, -1};
        std::array<int, 4> slotWritten{0, 0, 0, 0};
        int writeCount = 0;
        for (std::size_t slot = 0; slot < slots.size(); ++slot) {
            DecorCell* target = lookup(slots[slot]);
            if (!target || target->terrain != Terrain::Water) {
                continue;
            }
            target->variant = std::clamp(variant, 0, 7);
            target->subTile = static_cast<int>(slot);
            slotVariants[slot] = target->variant;
            slotSubTiles[slot] = target->subTile;
            slotWritten[slot] = 1;
            ++writeCount;
            ++report_.pondTemplateCells;
        }
        ++report_.pond2x2Tiles;
        if (pondDump) {
            pondDump << pondStep << ','
                << cell.iso.x << ',' << cell.iso.y << ','
                << (cell.hasSource ? cell.source.x : -1) << ',' << (cell.hasSource ? cell.source.y : -1) << ','
                << static_cast<int>(baseWater) << ','
                << static_cast<int>(eastWater) << ','
                << static_cast<int>(southWater) << ','
                << static_cast<int>(southEastWater) << ','
                << static_cast<int>(openWater2x2) << ',';
            appendDecorCellDebug(pondDump, &cell);
            appendDecorCellDebug(pondDump, east);
            appendDecorCellDebug(pondDump, south);
            appendDecorCellDebug(pondDump, southEast);
            pondDump << "template," << drawBefore << ',' << rng.drawCount() << ','
                << rollRaw << ',' << rollValue << ','
                << rawRandom << ',' << raw << ',' << variant << ",-1,"
                << writeCount << ',';
            for (std::size_t slot = 0; slot < slots.size(); ++slot) {
                pondDump << slots[slot].x << ',' << slots[slot].y << ','
                    << slotVariants[slot] << ',' << slotSubTiles[slot] << ','
                    << slotWritten[slot] << ',';
            }
            pondDump << rng.stateFingerprint() << '\n';
        }
        ++pondStep;
    }

    report_.isoTileOverrides.reserve(cells.size());
    for (const DecorCell& cell : cells) {
        report_.isoTileOverrides.push_back({
            cell.iso,
            cell.terrain,
            cell.variant,
            cell.subTile,
            cell.level,
            cell.iceGrowthAllowed,
            cell.landType,
        });
    }
}
