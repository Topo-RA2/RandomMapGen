void NativeMapGenerator::smoothHeightsAndAssignSlopes(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng)
{
    markCliffShoreExclusion(map);

    const std::vector<int> heightNoise = generateHeightNoise(map, options, rng);
    NativeHeightWorkspace workspace(
        map,
        report_.isoTileOverrides,
        report_.nativeOverlayOverrides,
        report_.nativeObjectOverrides,
        nativeMapgenSurfaceBlockedKeys_,
        nativeTheaterCatalog_);
    workspace.dumpIfRequested(".workspace_init.csv");
    const int workspaceSize = workspace.cellSide();
    const auto workspaceIndex = [workspaceSize](Point nativeCell) {
        return static_cast<std::size_t>(nativeCell.y * workspaceSize + nativeCell.x);
    };

    for (Point nativeCell : nativeMapgenWorkspaceScanOrder(map)) {
        const int target = workspace.cellLevel(nativeCell) + heightNoise[workspaceIndex(nativeCell)];
        const int current = workspace.groundHeight(nativeCell);
        const int delta = nativeDoubleToInt(static_cast<double>(target - current));
        const int direction = delta >= 0 ? 1 : -1;
        for (int step = 0; step < std::abs(delta); ++step) {
            const int mask = workspace.cliffMask(nativeCell, direction);
            workspace.raiseOrLowerCell(direction, false, nativeCell, mask);
        }
    }

    workspace.dumpIfRequested(".workspace_final.csv");
    workspace.assignSlopes(map, report_.isoTileOverrides);
    cleanNativeSlopePatterns(map);

    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(
        map,
        report_.isoTileOverrides);
    std::unordered_map<std::uint64_t, int> landTypeByIso;
    landTypeByIso.reserve(iteratorCells.size());
    for (const NativeIteratorCell& cell : iteratorCells) {
        landTypeByIso[nativeIsoKey(cell.cell)] = cell.landType;
    }
    for (NativeOverlayOverride& overlay : report_.nativeOverlayOverrides) {
        if (!isNativeTiberiumOverlayType(overlay.overlayTypeIndex)
            || overlay.landTypeOverride >= 0) {
            continue;
        }
        const auto landTypeIt = landTypeByIso.find(nativeIsoKey(overlay.iso));
        if (landTypeIt != landTypeByIso.end() && landTypeIt->second == 0) {
            overlay.landTypeOverride = 5;
        }
    }
}

void NativeMapGenerator::cleanNativeSlopePatterns(MapGrid& map)
{
    static constexpr std::array<Point, 4> kOffsets{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    static constexpr std::array<int, 4> kLowerPattern{{5, 6, 7, 8}};
    static constexpr std::array<int, 4> kRaisedPattern{{11, 12, 9, 10}};

    std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
    overrideIndexByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t i = 0; i < report_.isoTileOverrides.size(); ++i) {
        overrideIndexByIso[nativeIsoKey(report_.isoTileOverrides[i].iso)] = i;
    }

    auto slopeAt = [&](Point nativeCell) {
        const std::uint64_t key = nativeIsoKey(nativeCell);
        if (const auto overrideIt = overrideIndexByIso.find(key); overrideIt != overrideIndexByIso.end()) {
            return report_.isoTileOverrides[overrideIt->second].slopeIndex;
        }
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        return map.inBounds(source) ? nativeSlopeIndexForCell(map.at(source)) : 0;
    };

    auto matchesPattern = [&](Point anchor, const std::array<int, 4>& pattern) {
        for (std::size_t i = 0; i < kOffsets.size(); ++i) {
            const Point nativeCell{anchor.x + kOffsets[i].x, anchor.y + kOffsets[i].y};
            if (slopeAt(nativeCell) != pattern[i]) {
                return false;
            }
        }
        return true;
    };

    auto clearPattern = [&](Point anchor, bool raiseLevel) {
        ++report_.slopePatternMatches;
        for (Point offset : kOffsets) {
            const Point nativeCell{anchor.x + offset.x, anchor.y + offset.y};
            const std::uint64_t key = nativeIsoKey(nativeCell);
            const Point source = nativeCellToSourcePoint(map, nativeCell);
            int oldLevel = 0;
            int newLevel = 0;
            if (const auto overrideIt = overrideIndexByIso.find(key); overrideIt != overrideIndexByIso.end()) {
                IsoTileOverride& override = report_.isoTileOverrides[overrideIt->second];
                oldLevel = override.level;
                newLevel = std::min(oldLevel + (raiseLevel ? 1 : 0), 0xFF);
                override.terrain = Terrain::Land;
                override.variant = 0;
                override.subTile = 0;
                override.level = newLevel;
                override.slopeIndex = 0;
                override.nativeTileTypeIndex = 0xFFFF;
            } else {
                oldLevel = map.inBounds(source) ? map.at(source).level : kNativeBaseMapLevel;
                newLevel = std::min(oldLevel + (raiseLevel ? 1 : 0), 0xFF);
                const int landType = map.inBounds(source) ? map.at(source).landType : 0;
                report_.isoTileOverrides.push_back({
                    nativeCell,
                    Terrain::Land,
                    0,
                    0,
                    newLevel,
                    false,
                    landType,
                    0,
                    0xFFFF,
                });
                overrideIndexByIso[key] = report_.isoTileOverrides.size() - 1;
            }

            if (map.inBounds(source)) {
                Cell& cell = map.at(source);
                cell.terrain = Terrain::Land;
                cell.height = 0;
                cell.level = newLevel;
                cell.slope = 0;
            }
            ++report_.slopePatternClearedCells;
            if (raiseLevel && newLevel > oldLevel) {
                ++report_.slopePatternRaisedLevels;
            }
        }
    };

    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, report_.isoTileOverrides);
    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        const Point anchor = iteratorCell.cell;
        const int slope = slopeAt(anchor);
        if (slope != 5 && slope != 11) {
            continue;
        }

        ++report_.slopePatternChecks;
        if (slope == 5 && matchesPattern(anchor, kLowerPattern)) {
            clearPattern(anchor, false);
        } else if (slope == 11 && matchesPattern(anchor, kRaisedPattern)) {
            clearPattern(anchor, true);
        }
    }
}

void NativeMapGenerator::placeSurfaceBlobs(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng)
{
    nativeMapgenDataActive_ = true;
    nativeMapgenDefaultTempTag_ = 0;
    nativeMapgenDefaultZoneId_ = -1;
    nativeMapgenTempTags_.clear();
    nativeMapgenZoneIds_.clear();
    nativeMapgenScratchZoneLevels_.clear();
    nativeMapgenScratchZoneCellCounts_.clear();
    nativeMapgenScratchZoneWetFlags_.clear();
    resetNativeSurfaceTrace();
    nativeTreeCandidateKeys_.clear();
    nativeSurfacePreservedLandTypes_.clear();
    appendNativeSurfaceTrace(
        "simple_begin",
        0,
        {0, 0},
        0,
        0.0,
        0.0,
        0,
        0,
        0,
        -1,
        -2,
        -1,
        0,
        -1,
        nativeSeedWidthForTreeTarget(options),
        options.vegetation,
        "width_vegetation");
    appendNativeSurfaceTrace(
        "simple_config_max_trees",
        0,
        {0, 0},
        0,
        0.0,
        0.0,
        0,
        report_.maxTrees,
        0,
        -1,
        -2,
        -1,
        0,
        -1,
        options.theater,
        options.resources,
        "maxTrees_theater_resources");
    report_.surfaceMode = 1;
    constexpr double kSimpleGroundDensity = 0.005;
    constexpr double kSimpleSecondaryDensity = 0.001;
    (void)kSimpleSecondaryDensity;

    const int treeTargetWidth = nativeSeedWidthForTreeTarget(options);
    report_.surfaceSimpleTreeTarget = nativeTreeTarget(
        treeTargetWidth,
        options.vegetation,
        report_.maxTrees);

    std::vector<Point> centers;
    const Rect mapRect = nativeSavedMapRect(map);
    auto nativeSurfaceBlocked = [&](Point nativeCell) {
        const std::uint64_t key = nativeIsoKey(nativeCell);
        return nativeSpawnExclusionKeys_.find(key) != nativeSpawnExclusionKeys_.end()
            || nativeCliffShoreExclusionKeys_.find(key) != nativeCliffShoreExclusionKeys_.end();
    };
    auto nativeSimpleCandidate = [&](const NativeIteratorCell& cell) {
        if (!isInsideNativeCellDiamond(cell.cell, mapRect)) {
            return false;
        }
        if (!isNativeClearTileIndex(cell.isoTileTypeIndex) || nativeSurfaceBlocked(cell.cell)) {
            return false;
        }
        return true;
    };

    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, report_.isoTileOverrides);
    centers.reserve(iteratorCells.size());
    for (const NativeIteratorCell& cell : iteratorCells) {
        if (!nativeSimpleCandidate(cell)) {
            continue;
        }
        centers.push_back(cell.cell);
    }

    for (Point center : centers) {
        NativeIteratorCell projected;
        projected.cell = center;
        const std::uint64_t centerKey = nativeIsoKey(center);
        const auto overrideIt = std::find_if(report_.isoTileOverrides.begin(), report_.isoTileOverrides.end(), [&](const IsoTileOverride& override) {
            return nativeIsoKey(override.iso) == centerKey;
        });
        projected.isoTileTypeIndex = overrideIt == report_.isoTileOverrides.end()
            ? kNativeTileWaterBase
            : nativeTileTypeIndexForOverride(*overrideIt);
        if (!nativeSimpleCandidate(projected)) {
            continue;
        }

        ++report_.surfaceSimpleDensityCells;
        ++report_.surfaceSimpleRolls;
        std::uint32_t raw = 0;
        const double roll = tracedRandomUnit(rng, &raw);
        const bool hit = roll < kSimpleGroundDensity;
        appendNativeSurfaceTrace(
            "density_roll",
            ++g_nativeSurfaceDensityIndex,
            center,
            raw,
            roll,
            kSimpleGroundDensity,
            hit ? 1 : 0,
            0,
            0,
            projected.isoTileTypeIndex,
            -1,
            0,
            0,
            -1,
            0,
            0);
        if (!hit) {
            continue;
        }

        ++report_.surfaceSimpleHits;
        ++report_.surfaceSimpleSizeSamples;
        const int placed = placeGroundTileBlobNative(map, rng, center, Terrain::Rough, randomNativeSimpleSurfaceBlobSize(rng), false);
        if (placed > 0) {
            ++report_.surfaceBlobs;
            report_.surfaceCells += placed;
        }
    }
    appendNativeSurfaceTrace(
        "tree_target",
        0,
        {0, 0},
        0,
        static_cast<double>(report_.surfaceSimpleTreeTarget),
        0.0,
        report_.surfaceSimpleTreeTarget > 0 ? 1 : 0,
        report_.surfaceSimpleTreeTarget,
        0,
        -1,
        -2,
        -1,
        0,
        -1,
        0,
        0);
    refreshNativeSurfaceLatOverrides(map);
}

int NativeMapGenerator::placeGroundTileBlob(MapGrid& map, NativeRandomizer& rng, Point center, Terrain terrain, int targetCells, bool allowPave)
{
    if (!map.inBounds(center) || targetCells <= 0) {
        return 0;
    }
    ++g_nativeSurfaceGroundBlobIndex;
    appendNativeSurfaceTrace(
        "ground_blob_begin",
        g_nativeSurfaceGroundBlobIndex,
        sourcePointToNativeCell(map, center),
        0,
        0.0,
        0.0,
        1,
        targetCells,
        0,
        nativeTileTypeIndexForTerrainValue(terrain, 0),
        -1,
        0,
        0,
        -1,
        center.x + center.y * map.width(),
        allowPave ? 1 : 0);

    std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
    overrideIndexByIso.reserve(report_.isoTileOverrides.size() + static_cast<std::size_t>(targetCells));
    for (std::size_t i = 0; i < report_.isoTileOverrides.size(); ++i) {
        overrideIndexByIso[nativeIsoKey(report_.isoTileOverrides[i].iso)] = i;
    }

    const int tag = 500000 + report_.groundBlobHeapPushes + 1;
    const int maxTaggedCells = std::max(1, targetCells * 10);
    int taggedThisBlob = 0;
    auto isCandidate = [&](Point p, bool countMetrics) {
        if (countMetrics) {
            ++report_.groundBlobCandidateChecks;
        }
        const Cell& cell = map.at(p);
        if (cell.tempTag == tag || !isGroundTileBlobCandidateCell(cell, allowPave)) {
            if (countMetrics) {
                ++report_.groundBlobRejectedCells;
            }
            return false;
        }
        return true;
    };

    if (!isCandidate(center, false)) {
        return 0;
    }

    struct SurfaceNode {
        Point point;
        double priority = 0.0;

        bool operator>(const SurfaceNode& rhs) const noexcept
        {
            return priority > rhs.priority;
        }
    };

    std::priority_queue<SurfaceNode, std::vector<SurfaceNode>, std::greater<SurfaceNode>> heap;
    map.at(center).tempTag = tag;
    heap.push({center, 0.0});
    ++report_.groundBlobHeapPushes;
    ++report_.groundBlobTaggedCells;
    ++taggedThisBlob;

    int written = 0;
    int visited = 0;
    while (!heap.empty() && visited < targetCells) {
        const Point p = heap.top().point;
        heap.pop();
        ++visited;
        ++report_.groundBlobHeapPops;

        Cell& cell = map.at(p);
        if (cell.terrain != terrain) {
            cell.terrain = terrain;
        }
        const Point nativeCell = sourcePointToNativeCell(map, p);
        int nativeSubTile = std::clamp(cell.height, 0, 255);
        int nativeLevel = std::clamp(cell.level, 0, 255);
        bool nativeIceGrowthAllowed = cell.iceGrowthAllowed;
        if (const auto overrideIt = overrideIndexByIso.find(nativeIsoKey(nativeCell)); overrideIt != overrideIndexByIso.end()) {
            const IsoTileOverride& existing = report_.isoTileOverrides[overrideIt->second];
            nativeSubTile = existing.subTile;
            nativeLevel = existing.level;
            nativeIceGrowthAllowed = existing.iceGrowthAllowed;
        }
        upsertIsoTileOverride(
            report_.isoTileOverrides,
            overrideIndexByIso,
            nativeCell,
            terrain,
            0,
            nativeSubTile,
            nativeLevel,
            nativeIceGrowthAllowed);
        appendNativeSurfaceTrace(
            "ground_write",
            g_nativeSurfaceGroundBlobIndex,
            nativeCell,
            0,
            0.0,
            0.0,
            1,
            0,
            written + 1,
            nativeTileTypeIndexForTerrainValue(terrain, 0),
            nativeOverlayTypeIndexForCell(cell),
            cell.landType,
            nativeObjectAbstractTypeForCell(cell),
            -1,
            0,
            0);
        ++written;

        for (Point next : map.neighbors8(p)) {
            if (!isCandidate(next, true)) {
                continue;
            }
            if (taggedThisBlob >= maxTaggedCells) {
                ++report_.groundBlobRejectedCells;
                continue;
            }
            map.at(next).tempTag = tag;
            heap.push({next, euclidean(next, center) + rng.randomUnit() * 5.0});
            ++report_.groundBlobHeapPushes;
            ++report_.groundBlobTaggedCells;
            ++taggedThisBlob;
        }
    }

    clearTags(map, tag);
    if (written > 0) {
        refreshNativeLatOverridesForMap(
            map,
            report_.isoTileOverrides,
            false,
            nativeHighResourceLatMissingNeighborSame_,
            nativeInvalidCellTile_);
        ++report_.heapSurfaceBlobs;
        report_.heapSurfaceCells += written;
    }
    return written;
}

int NativeMapGenerator::placeGroundTileBlobNative(MapGrid& map, NativeRandomizer& rng, Point center, Terrain terrain, int targetCells, bool allowPave)
{
    const Rect mapRect = nativeSavedMapRect(map);
    if (!isInsideNativeCellDiamond(center, mapRect) || targetCells <= 0) {
        return 0;
    }
    ++g_nativeSurfaceGroundBlobIndex;
    appendNativeSurfaceTrace(
        "ground_blob_begin",
        g_nativeSurfaceGroundBlobIndex,
        center,
        0,
        0.0,
        0.0,
        1,
        targetCells,
        0,
        nativeTileTypeIndexForTerrainValue(terrain, 0),
        -1,
        0,
        0,
        -1,
        center.x + center.y * (mapRect.w + mapRect.h + 1),
        allowPave ? 1 : 0);

    std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
    overrideIndexByIso.reserve(report_.isoTileOverrides.size() + static_cast<std::size_t>(targetCells));
    for (std::size_t i = 0; i < report_.isoTileOverrides.size(); ++i) {
        overrideIndexByIso[nativeIsoKey(report_.isoTileOverrides[i].iso)] = i;
    }

    std::unordered_map<std::uint64_t, int> landTypeByIso;
    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, report_.isoTileOverrides);
    landTypeByIso.reserve(iteratorCells.size());
    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        const std::uint64_t key = nativeIsoKey(iteratorCell.cell);
        landTypeByIso[key] = iteratorCell.landType;
    }

    std::unordered_map<std::uint64_t, const NativeOverlayOverride*> overlayByIso;
    overlayByIso.reserve(report_.nativeOverlayOverrides.size());
    for (const NativeOverlayOverride& overlay : report_.nativeOverlayOverrides) {
        overlayByIso[nativeIsoKey(overlay.iso)] = &overlay;
    }
    std::unordered_map<std::uint64_t, const NativeObjectOverride*> objectByIso;
    objectByIso.reserve(report_.nativeObjectOverrides.size());
    for (const NativeObjectOverride& object : report_.nativeObjectOverrides) {
        objectByIso[nativeIsoKey(object.iso)] = &object;
    }

    auto nativeProjection = [&](Point nativeCell, int* tile, int* slope, int* overlay, int* object, int* subTile, int* level, bool* iceGrowthAllowed) {
        const std::uint64_t key = nativeIsoKey(nativeCell);
        const auto overrideIt = overrideIndexByIso.find(key);
        const IsoTileOverride* isoOverride = overrideIt == overrideIndexByIso.end() ? nullptr : &report_.isoTileOverrides[overrideIt->second];
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        const Cell* cell = map.inBounds(source) ? &map.at(source) : nullptr;

        *tile = isoOverride ? nativeTileTypeIndexForOverride(*isoOverride) : (cell ? nativeTileTypeIndexForTerrain(*cell) : kNativeTileWaterBase);
        *slope = isoOverride ? 0 : (cell ? nativeSlopeIndexForCell(*cell) : 0);
        *overlay = cell ? nativeOverlayTypeIndexForCell(*cell) : -1;
        if (cell && (cell->overlay == Overlay::Ore || cell->overlay == Overlay::Gems) && !report_.nativeOverlayOverrides.empty()) {
            *overlay = -1;
        }
        *object = cell ? nativeObjectAbstractTypeForCell(*cell) : -1;
        if (cell && (cell->overlay == Overlay::Tree || cell->overlay == Overlay::Tech) && !report_.nativeObjectOverrides.empty()) {
            *object = -1;
        }
        *subTile = isoOverride ? isoOverride->subTile : (cell ? std::clamp(cell->height, 0, 255) : 0);
        *level = isoOverride ? isoOverride->level : (cell ? std::clamp(cell->level, 0, 255) : 0);
        *iceGrowthAllowed = isoOverride ? isoOverride->iceGrowthAllowed : (cell && cell->iceGrowthAllowed);
        if (overlayByIso.find(key) != overlayByIso.end()) {
            *overlay = overlayByIso[key]->overlayTypeIndex;
        }
        if (objectByIso.find(key) != objectByIso.end()) {
            *object = objectByIso[key]->objectAbstractType;
        }
    };

    const int nativeScratchTag = nativeMapgenScratchTagForCell(center);
    const int maxTaggedCells = std::max(1, targetCells * 10);
    std::unordered_set<std::uint64_t> tagged;
    tagged.reserve(static_cast<std::size_t>(maxTaggedCells));

    auto canQueue = [&](Point nativeCell, bool countMetrics) {
        if (countMetrics) {
            ++report_.groundBlobCandidateChecks;
        }
        const std::uint64_t key = nativeIsoKey(nativeCell);
        if (!isInsideNativeCellDiamond(nativeCell, mapRect) || tagged.find(key) != tagged.end()) {
            if (countMetrics) {
                ++report_.groundBlobRejectedCells;
            }
            return false;
        }
        int tile = kNativeTileWaterBase;
        int slope = 0;
        int overlay = -1;
        int object = -1;
        int subTile = 0;
        int level = 0;
        bool iceGrowthAllowed = false;
        nativeProjection(nativeCell, &tile, &slope, &overlay, &object, &subTile, &level, &iceGrowthAllowed);
        const bool tileOk = isNativeClearTileIndex(tile) || (allowPave && isNativePaveTileIndex(tile));
        if (!tileOk || slope != 0 || overlay != -1 || object != -1) {
            if (countMetrics) {
                ++report_.groundBlobRejectedCells;
            }
            return false;
        }
        return true;
    };

    struct SurfaceNode {
        Point point;
        float priority = 0.0f;
    };
    struct SurfaceHeap {
        explicit SurfaceHeap(int capacity)
            : capacity(std::max(1, capacity)),
              nodes(static_cast<std::size_t>(std::max(1, capacity))),
              heap(static_cast<std::size_t>(std::max(1, capacity) + 1), -1)
        {
        }

        bool empty() const noexcept
        {
            return heapSize == 0;
        }

        int size() const noexcept
        {
            return heapSize;
        }

        int allocatedCount() const noexcept
        {
            return allocated;
        }

        int capacityValue() const noexcept
        {
            return capacity;
        }

        bool canAllocate() const noexcept
        {
            return allocated < capacity;
        }

        bool push(Point point, double priority)
        {
            if (!canAllocate()) {
                return false;
            }

            const int nodeIndex = allocated++;
            nodes[static_cast<std::size_t>(nodeIndex)] = {point, static_cast<float>(priority)};
            int slot = heapSize + 1;
            if (slot >= capacity) {
                return false;
            }

            while (slot > 1) {
                const int parent = slot >> 1;
                const int parentIndex = heap[static_cast<std::size_t>(parent)];
                if (nodes[static_cast<std::size_t>(parentIndex)].priority <= nodes[static_cast<std::size_t>(nodeIndex)].priority) {
                    break;
                }
                heap[static_cast<std::size_t>(slot)] = parentIndex;
                slot = parent;
            }

            heap[static_cast<std::size_t>(slot)] = nodeIndex;
            ++heapSize;
            return true;
        }

        Point pop()
        {
            if (heapSize <= 0) {
                return {-1, -1};
            }

            const int resultIndex = heap[1];
            const Point result = nodes[static_cast<std::size_t>(resultIndex)].point;
            lastPopPriority = nodes[static_cast<std::size_t>(resultIndex)].priority;
            heap[1] = heap[static_cast<std::size_t>(heapSize)];
            heap[static_cast<std::size_t>(heapSize)] = -1;
            --heapSize;
            siftDown(1);
            return result;
        }

        float nodePriority(int slot) const
        {
            const int nodeIndex = heap[static_cast<std::size_t>(slot)];
            return nodes[static_cast<std::size_t>(nodeIndex)].priority;
        }

        void siftDown(int slot)
        {
            int best = slot * 2;
            const int right = best + 1;
            if (best > heapSize || nodePriority(slot) <= nodePriority(best)) {
                best = slot;
            }
            if (right <= heapSize && nodePriority(best) > nodePriority(right)) {
                best = right;
            }

            while (best != slot) {
                std::swap(heap[static_cast<std::size_t>(slot)], heap[static_cast<std::size_t>(best)]);
                slot = best;
                best = slot * 2;
                const int nextRight = best + 1;
                if (best > heapSize || nodePriority(slot) <= nodePriority(best)) {
                    best = slot;
                }
                if (nextRight <= heapSize && nodePriority(best) > nodePriority(nextRight)) {
                    best = nextRight;
                }
            }
        }

        int capacity = 0;
        int allocated = 0;
        int heapSize = 0;
        float lastPopPriority = 0.0f;
        std::vector<SurfaceNode> nodes;
        std::vector<int> heap;
    };

    SurfaceHeap heap(std::max(1, targetCells * 10));
    const std::uint64_t centerKey = nativeIsoKey(center);
    tagged.insert(centerKey);
    nativeMapgenTempTags_[centerKey] = nativeScratchTag;
    heap.push(center, 0.0);
    ++report_.groundBlobHeapPushes;
    ++report_.groundBlobTaggedCells;

    int written = 0;
    while (!heap.empty() && written < targetCells) {
        const Point nativeCell = heap.pop();
        ++report_.groundBlobHeapPops;

        int tile = kNativeTileWaterBase;
        int slope = 0;
        int overlay = -1;
        int object = -1;
        int subTile = 0;
        int level = 0;
        bool iceGrowthAllowed = false;
        nativeProjection(nativeCell, &tile, &slope, &overlay, &object, &subTile, &level, &iceGrowthAllowed);
        const std::uint64_t key = nativeIsoKey(nativeCell);
        const auto landTypeIt = landTypeByIso.find(key);
        const int landTypeBeforeWrite = landTypeIt == landTypeByIso.end() ? 0 : landTypeIt->second;
        const auto [preservedLandTypeIt, inserted] = nativeSurfacePreservedLandTypes_.try_emplace(key, landTypeBeforeWrite);
        (void)inserted;
        upsertIsoTileOverride(
            report_.isoTileOverrides,
            overrideIndexByIso,
            nativeCell,
            terrain,
            0,
            subTile,
            level,
            iceGrowthAllowed);
        IsoTileOverride& placedOverride = report_.isoTileOverrides[overrideIndexByIso.at(key)];
        placedOverride.landType = preservedLandTypeIt->second;
        landTypeByIso[key] = placedOverride.landType;

        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (map.inBounds(source)) {
            map.at(source).terrain = terrain;
        }
        appendNativeSurfaceTrace(
            "ground_write",
            g_nativeSurfaceGroundBlobIndex,
            nativeCell,
            0,
            0.0,
            0.0,
            1,
            0,
            written + 1,
            nativeTileTypeIndexForTerrainValue(terrain, 0),
            overlay,
            0,
            object,
            -1,
            0,
            0);
        ++written;

        constexpr std::array<Point, 8> kNativeNeighbors{{
            {0, -1}, {1, -1}, {1, 0}, {1, 1},
            {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
        }};
        for (Point offset : kNativeNeighbors) {
            const Point next{nativeCell.x + offset.x, nativeCell.y + offset.y};
            if (!canQueue(next, true)) {
                continue;
            }
            if (static_cast<int>(tagged.size()) >= maxTaggedCells || !heap.canAllocate()) {
                ++report_.groundBlobRejectedCells;
                continue;
            }
            const std::uint64_t nextKey = nativeIsoKey(next);
            tagged.insert(nextKey);
            nativeMapgenTempTags_[nextKey] = nativeScratchTag;
            const double dx = static_cast<double>(center.x - next.x);
            const double dy = static_cast<double>(center.y - next.y);
            const std::uint32_t raw = rng.random();
            const float priorityFloat = nativeDoubleToFloatChop(
                nativeSurfaceHeapPriority(nativePatchedSqrt(dx * dx + dy * dy), raw));
            int priorityBits = 0;
            std::memcpy(&priorityBits, &priorityFloat, sizeof(priorityBits));
            const int allocatedIndex = heap.allocatedCount();
            const int heapSizeBefore = heap.size();
            appendNativeSurfaceTrace(
                "ground_enqueue",
                g_nativeSurfaceGroundBlobIndex,
                next,
                raw,
                static_cast<double>(priorityFloat),
                static_cast<double>(heap.lastPopPriority),
                1,
                heap.capacityValue(),
                allocatedIndex,
                -1,
                -1,
                0,
                0,
                priorityBits,
                (nativeCell.y << 16) | (nativeCell.x & 0xFFFF),
                heapSizeBefore);
            if (heap.push(next, static_cast<double>(priorityFloat))) {
                ++report_.groundBlobHeapPushes;
            }
            ++report_.groundBlobTaggedCells;
        }
    }

    if (written > 0) {
        ++report_.heapSurfaceBlobs;
        report_.heapSurfaceCells += written;
    }
    return written;
}

void NativeMapGenerator::refreshNativeSurfaceLatOverrides(MapGrid& map)
{
    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, report_.isoTileOverrides);
    std::unordered_map<std::uint64_t, int> tileByIso;
    std::unordered_map<std::uint64_t, IsoTileOverride*> overrideByIso;
    tileByIso.reserve(iteratorCells.size());
    overrideByIso.reserve(report_.isoTileOverrides.size());
    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        tileByIso[nativeIsoKey(iteratorCell.cell)] = iteratorCell.isoTileTypeIndex;
    }
    for (IsoTileOverride& override : report_.isoTileOverrides) {
        overrideByIso[nativeIsoKey(override.iso)] = &override;
    }

    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        const std::uint64_t key = nativeIsoKey(iteratorCell.cell);
        const auto overrideIt = overrideByIso.find(key);
        if (overrideIt == overrideByIso.end() || !isNativeLatTerrain(overrideIt->second->terrain)) {
            continue;
        }
        IsoTileOverride& override = *overrideIt->second;
        override.variant = nativeLatVariantForCell(
            tileByIso,
            override.iso,
            nativeLatGroupForTerrain(override.terrain),
            nativeHighResourceLatMissingNeighborSame_,
            nativeInvalidCellTile_);
        if (override.terrain == Terrain::Dirt || override.terrain == Terrain::Rough) {
            override.landType = 0;
        } else {
            override.landType = nativeLandTypeForTile(override.terrain, override.variant, override.subTile);
        }
        tileByIso[key] = nativeTileTypeIndexForOverride(override);
    }
    refreshNativeRampSmoothOverrides(iteratorCells, report_.isoTileOverrides);

    for (IsoTileOverride& override : report_.isoTileOverrides) {
        const auto it = nativeSurfacePreservedLandTypes_.find(nativeIsoKey(override.iso));
        if (it != nativeSurfacePreservedLandTypes_.end()) {
            override.landType = it->second;
        }
    }
}

void NativeMapGenerator::placeSurfaceAndVegetation(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng)
{
    nativeMapgenDataActive_ = true;
    nativeMapgenDefaultTempTag_ = 0;
    nativeMapgenDefaultZoneId_ = -1;
    nativeMapgenTempTags_.clear();
    nativeMapgenZoneIds_.clear();
    nativeMapgenScratchZoneLevels_.clear();
    nativeMapgenScratchZoneCellCounts_.clear();
    nativeMapgenScratchZoneWetFlags_.clear();
    resetNativeSurfaceTrace();
    nativeTreeCandidateKeys_.clear();
    nativeSurfacePreservedLandTypes_.clear();
    appendNativeSurfaceTrace(
        "rich_begin",
        0,
        {0, 0},
        0,
        0.0,
        0.0,
        0,
        0,
        0,
        -1,
        -2,
        -1,
        0,
        -1,
        nativeSeedWidthForTreeTarget(options),
        options.vegetation,
        "width_vegetation");
    appendNativeSurfaceTrace(
        "simple_config_max_trees",
        0,
        {0, 0},
        0,
        0.0,
        0.0,
        0,
        report_.maxTrees,
        0,
        -1,
        -2,
        -1,
        0,
        -1,
        options.theater,
        options.resources,
        "maxTrees_theater_resources");
    report_.surfaceMode = 2;
    const int treeTargetWidth = nativeSeedWidthForTreeTarget(options);
    report_.surfaceSimpleTreeTarget = nativeTreeTarget(
        treeTargetWidth,
        options.vegetation,
        report_.maxTrees);
    const double vegetationScale = static_cast<double>(std::max(0, options.vegetation)) * 0.01;
    const double groundABaseDensity = static_cast<double>(static_cast<float>(0.02f * static_cast<float>(vegetationScale)));
    const double groundABoostedDensity = groundABaseDensity * 10.0;
    const Rect mapRect = nativeSavedMapRect(map);
    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, report_.isoTileOverrides);
    std::unordered_map<std::uint64_t, SurfaceDensity> densityByIso;
    densityByIso.reserve(iteratorCells.size());

    std::unordered_map<std::uint64_t, const NativeOverlayOverride*> overlayByIso;
    overlayByIso.reserve(report_.nativeOverlayOverrides.size());
    for (const NativeOverlayOverride& overlay : report_.nativeOverlayOverrides) {
        overlayByIso[nativeIsoKey(overlay.iso)] = &overlay;
    }
    std::unordered_map<std::uint64_t, const NativeObjectOverride*> objectByIso;
    objectByIso.reserve(report_.nativeObjectOverrides.size());
    for (const NativeObjectOverride& object : report_.nativeObjectOverrides) {
        objectByIso[nativeIsoKey(object.iso)] = &object;
    }

    auto isoOverrideFor = [&](Point nativeCell) -> const IsoTileOverride* {
        const std::uint64_t key = nativeIsoKey(nativeCell);
        const auto it = std::find_if(report_.isoTileOverrides.begin(), report_.isoTileOverrides.end(), [&](const IsoTileOverride& override) {
            return nativeIsoKey(override.iso) == key;
        });
        return it == report_.isoTileOverrides.end() ? nullptr : &*it;
    };
    auto nativeProjection = [&](Point nativeCell, int* tile, int* slope, int* overlay, int* landType, int* firstObject, int* objectType) {
        const IsoTileOverride* isoOverride = isoOverrideFor(nativeCell);
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        const Cell* cell = map.inBounds(source) ? &map.at(source) : nullptr;

        *tile = isoOverride ? nativeTileTypeIndexForOverride(*isoOverride) : (cell ? nativeTileTypeIndexForTerrain(*cell) : kNativeTileWaterBase);
        *slope = isoOverride ? 0 : (cell ? nativeSlopeIndexForCell(*cell) : 0);
        *overlay = cell ? nativeOverlayTypeIndexForCell(*cell) : -1;
        if (cell && (cell->overlay == Overlay::Ore || cell->overlay == Overlay::Gems) && !report_.nativeOverlayOverrides.empty()) {
            *overlay = -1;
        }
        *landType = isoOverride ? isoOverride->landType : (cell ? nativeLandTypeForCell(*cell) : -1);
        *objectType = cell ? nativeObjectAbstractTypeForCell(*cell) : -1;
        if (cell && (cell->overlay == Overlay::Tree || cell->overlay == Overlay::Tech) && !report_.nativeObjectOverrides.empty()) {
            *objectType = -1;
        }
        const std::uint64_t key = nativeIsoKey(nativeCell);
        const auto overlayIt = overlayByIso.find(key);
        if (overlayIt != overlayByIso.end()) {
            *overlay = overlayIt->second->overlayTypeIndex;
            if (overlayIt->second->landTypeOverride >= 0) {
                *landType = overlayIt->second->landTypeOverride;
            }
        }
        const auto objectIt = objectByIso.find(key);
        if (objectIt != objectByIso.end()) {
            *objectType = objectIt->second->objectAbstractType;
        }
        *firstObject = *objectType == -1 ? 0 : 1;
    };
    auto nativeSurfaceBlocked = [&](Point nativeCell) {
        const std::uint64_t key = nativeIsoKey(nativeCell);
        return nativeSpawnExclusionKeys_.find(key) != nativeSpawnExclusionKeys_.end()
            || nativeCliffShoreExclusionKeys_.find(key) != nativeCliffShoreExclusionKeys_.end();
    };
    std::ofstream surfaceCandidateTrace;
    if (const char* dumpPath = std::getenv(kNativeSurfaceCandidateDumpPathEnv); dumpPath != nullptr && dumpPath[0] != '\0') {
        const std::filesystem::path path{dumpPath};
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        surfaceCandidateTrace.open(path, std::ios::out | std::ios::trunc);
        if (surfaceCandidateTrace.is_open()) {
            surfaceCandidateTrace
                << "event,x,y,tile,slope,overlay,landType,firstObject,objectType,"
                   "spawnBlocked,cliffShoreBlocked,blocked,insideDiamond,extra0,label\n";
        }
    }
    auto traceSurfaceCandidate = [&](const char* event,
                                     Point nativeCell,
                                     int tile,
                                     int slope,
                                     int overlay,
                                     int landType,
                                     int firstObject,
                                     int objectType,
                                     bool insideDiamond,
                                     int extra0,
                                     const char* label) {
        if (!surfaceCandidateTrace.is_open()) {
            return;
        }
        const std::uint64_t key = nativeIsoKey(nativeCell);
        const bool spawnBlocked = nativeSpawnExclusionKeys_.find(key) != nativeSpawnExclusionKeys_.end();
        const bool cliffShoreBlocked = nativeCliffShoreExclusionKeys_.find(key) != nativeCliffShoreExclusionKeys_.end();
        surfaceCandidateTrace
            << (event ? event : "") << ','
            << nativeCell.x << ','
            << nativeCell.y << ','
            << tile << ','
            << slope << ','
            << overlay << ','
            << landType << ','
            << firstObject << ','
            << objectType << ','
            << (spawnBlocked ? 1 : 0) << ','
            << (cliffShoreBlocked ? 1 : 0) << ','
            << ((spawnBlocked || cliffShoreBlocked) ? 1 : 0) << ','
            << (insideDiamond ? 1 : 0) << ','
            << extra0 << ','
            << (label ? label : "") << '\n';
    };

    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        int tile = kNativeTileWaterBase;
        int slope = 0;
        int overlay = -1;
        int landType = -1;
        int firstObject = 0;
        int objectType = -1;
        nativeProjection(iteratorCell.cell, &tile, &slope, &overlay, &landType, &firstObject, &objectType);
        SurfaceDensity& slot = densityByIso[nativeIsoKey(iteratorCell.cell)];
        const bool cliffAnchor = tile >= kNativeTileCliffBase && tile < kNativeTileShoreBase;
        if (cliffAnchor) {
            slot.groundA = groundABoostedDensity;
            slot.groundB = 0.005;
            slot.green = 0.0;
            ++report_.vegetationDensityBoosts;
        } else if (slot.groundB == 0.0) {
            slot.groundA = groundABaseDensity;
            slot.groundB = 0.005;
            slot.green = 0.005;
        }
    }

    for (const auto& entry : densityByIso) {
        const SurfaceDensity& slot = entry.second;
        if (slot.groundA > 0.0 || slot.groundB > 0.0 || slot.green > 0.0) {
            ++report_.vegetationDensityCells;
        }
    }

    auto randomSurfaceMean = [&](const char* label) {
        int value = 20;
        do {
            const std::uint32_t raw = rng.random();
            const double sampled = static_cast<double>(raw) * kNativeUInt32Unit * 21.0 + 20.0;
            value = nativeDoubleToInt(sampled);
            appendNativeSurfaceTrace(
                "base_size",
                ++g_nativeSurfaceBaseIndex,
                {0, 0},
                raw,
                sampled,
                40.0,
                value <= 40 ? 1 : 0,
                value,
                0,
                -1,
                -2,
                -1,
                0,
                -1,
                0,
                0,
                label ? label : "");
        } while (value > 40);
        return value;
    };

    const int groundAMean = randomSurfaceMean("groundA");
    const int groundBMean = randomSurfaceMean("groundB");
    const int greenMean = randomSurfaceMean("green");

    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        const Point nativeCenter = iteratorCell.cell;
        const bool insideDiamond = isInsideNativeCellDiamond(nativeCenter, mapRect);
        if (!insideDiamond) {
            traceSurfaceCandidate("skip_outside", nativeCenter, kNativeTileWaterBase, 0, -1, -1, 0, -1, false, 0, "");
            continue;
        }
        int tile = kNativeTileWaterBase;
        int slope = 0;
        int overlay = -1;
        int landType = -1;
        int firstObject = 0;
        int objectType = -1;
        nativeProjection(nativeCenter, &tile, &slope, &overlay, &landType, &firstObject, &objectType);
        const bool blocked = nativeSurfaceBlocked(nativeCenter);
        if (!isNativeClearTileIndex(tile) || slope != 0 || overlay != -1 || firstObject != 0 || blocked) {
            const char* reason = !isNativeClearTileIndex(tile) ? "tile"
                : slope != 0 ? "slope"
                : overlay != -1 ? "overlay"
                : firstObject != 0 ? "object"
                : "blocked";
            traceSurfaceCandidate("skip", nativeCenter, tile, slope, overlay, landType, firstObject, objectType, insideDiamond, 0, reason);
            continue;
        }

        const SurfaceDensity& slot = densityByIso[nativeIsoKey(nativeCenter)];
        const int originIndex = nativeCenter.x + (nativeCenter.y << 9);
        traceSurfaceCandidate("roll", nativeCenter, tile, slope, overlay, landType, firstObject, objectType, insideDiamond, originIndex, "candidate");
        ++report_.vegetationDensityRolls;
        std::uint32_t raw = 0;
        double roll = tracedRandomUnit(rng, &raw);
        bool hit = roll < slot.groundA;
        appendNativeSurfaceTrace(
            "density_roll",
            ++g_nativeSurfaceDensityIndex,
            nativeCenter,
            raw,
            roll,
            slot.groundA,
            hit ? 1 : 0,
            0,
            0,
            nativeTileTypeIndexForTerrainValue(Terrain::Rough, 0),
            overlay,
            landType,
            firstObject,
            objectType,
            originIndex,
            0,
            "groundA");
        if (hit) {
            const int placed = placeGroundTileBlobNative(map, rng, nativeCenter, Terrain::Rough, randomNativeSurfaceBlobSize(rng, groundAMean), false);
            if (placed > 0) {
                ++report_.vegetationDensityHits;
                ++report_.surfaceBlobs;
                report_.surfaceCells += placed;
            }
            continue;
        }

        ++report_.vegetationDensityRolls;
        roll = tracedRandomUnit(rng, &raw);
        hit = roll < slot.groundB;
        appendNativeSurfaceTrace(
            "density_roll",
            ++g_nativeSurfaceDensityIndex,
            nativeCenter,
            raw,
            roll,
            slot.groundB,
            hit ? 1 : 0,
            0,
            0,
            nativeTileTypeIndexForTerrainValue(Terrain::Dirt, 0),
            overlay,
            landType,
            firstObject,
            objectType,
            originIndex,
            1,
            "groundB");
        if (hit) {
            const int placed = placeGroundTileBlobNative(map, rng, nativeCenter, Terrain::Dirt, randomNativeSurfaceBlobSize(rng, groundBMean), false);
            if (placed > 0) {
                ++report_.vegetationDensityHits;
                ++report_.surfaceBlobs;
                report_.surfaceCells += placed;
            }
            continue;
        }

        ++report_.vegetationDensityRolls;
        roll = tracedRandomUnit(rng, &raw);
        hit = roll < slot.green;
        appendNativeSurfaceTrace(
            "density_roll",
            ++g_nativeSurfaceDensityIndex,
            nativeCenter,
            raw,
            roll,
            slot.green,
            hit ? 1 : 0,
            0,
            0,
            nativeTileTypeIndexForTerrainValue(Terrain::Green, 0),
            overlay,
            landType,
            firstObject,
            objectType,
            originIndex,
            2,
            "green");
        if (hit) {
            const int placed = placeGroundTileBlobNative(map, rng, nativeCenter, Terrain::Green, randomNativeSurfaceBlobSize(rng, greenMean), false);
            if (placed > 0) {
                ++report_.vegetationDensityHits;
                ++report_.surfaceBlobs;
                report_.surfaceCells += placed;
            }
        }
    }
    refreshNativeSurfaceLatOverrides(map);
}

void NativeMapGenerator::placeSurfaceGrass(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng)
{
    const Rect mapRect = nativeSavedMapRect(map);
    const int workspaceSize = mapRect.w + mapRect.h + 1;
    const int grassMax = nativeSurfaceDataCount(map) / 200;
    if (grassMax <= 0 || workspaceSize <= 0) {
        return;
    }

    std::ofstream grassDump;
    if (const char* dumpPath = std::getenv(kNativeGrassDumpPathEnv); dumpPath != nullptr && dumpPath[0] != '\0') {
        const std::filesystem::path path{dumpPath};
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        grassDump.open(path, std::ios::binary);
        grassDump << "event,attempt,processed,raw,value,max,index,nativeX,nativeY,sourceX,sourceY,"
                     "tile,overlay,placed,overlayValue,rejectedOccupied,rejectedTerrain\n";
    }
    auto randomScaledInclusiveTrace = [&](int minValue, int maxValue, const char* event, int attempt, int processed) {
        const int lo = std::min(minValue, maxValue);
        const int hi = std::max(minValue, maxValue);
        const double span = static_cast<double>(static_cast<std::int64_t>(hi) - static_cast<std::int64_t>(lo) + 1);
        int candidate = hi + 1;
        std::uint32_t raw = 0;
        double value = 0.0;
        while (candidate > hi) {
            raw = rng.random();
            value = static_cast<double>(raw) * span * kNativeUInt32Unit + static_cast<double>(lo);
            candidate = nativeDoubleToInt(value);
        }
        if (grassDump.is_open()) {
            grassDump << event << ','
                << attempt << ','
                << processed << ','
                << raw << ','
                << std::setprecision(17) << value << ','
                << hi << ','
                << candidate << ",-1,-1,-1,-1,-1,-1,0,-1,0,0\n";
        }
        return candidate;
    };

    const int grassBlocks = randomScaledInclusiveTrace(0, grassMax, "target", -1, 0);
    report_.grassTarget = grassBlocks;
    report_.grassAttemptBudget = grassBlocks * 5;
    if (report_.grassTarget <= 0) {
        return;
    }

    std::unordered_map<std::uint64_t, const IsoTileOverride*> overrideByIso;
    overrideByIso.reserve(report_.isoTileOverrides.size());
    for (const IsoTileOverride& override : report_.isoTileOverrides) {
        overrideByIso[nativeIsoKey(override.iso)] = &override;
    }
    report_.nativeOverlayOverrides.reserve(report_.nativeOverlayOverrides.size() + static_cast<std::size_t>(grassBlocks));
    std::unordered_map<std::uint64_t, const NativeOverlayOverride*> overlayByIso;
    overlayByIso.reserve(report_.nativeOverlayOverrides.size() + static_cast<std::size_t>(grassBlocks));
    for (const NativeOverlayOverride& overlay : report_.nativeOverlayOverrides) {
        overlayByIso[nativeIsoKey(overlay.iso)] = &overlay;
    }
    auto nativeGrassOverlayCell = [](Point nativeCell, int, int) {
        return nativeCell;
    };
    std::vector<std::pair<Point, int>> placedGrassSources;
    placedGrassSources.reserve(static_cast<std::size_t>(grassBlocks));

    auto isDirtLatForLandType = [](int tile) {
        return tile >= kNativeTileDirtBase && tile <= kNativeTileDirtBase + 16;
    };
    auto isClearToSandLatOverlay = [](int tile) {
        return tile > kNativeTileDirtBase && tile <= kNativeTileDirtBase + 16;
    };
    auto projectGrassCandidate = [&](Point nativeCell, Cell** cellOut, int* tileOut, int* overlayOut) {
        const std::uint64_t key = nativeIsoKey(nativeCell);
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        Cell* cell = map.inBounds(source) ? &map.at(source) : nullptr;
        const auto overrideIt = overrideByIso.find(key);
        const IsoTileOverride* isoOverride = overrideIt == overrideByIso.end() ? nullptr : overrideIt->second;
        int tile = isoOverride
            ? nativeTileTypeIndexForOverride(*isoOverride)
            : (cell ? nativeTileTypeIndexForTerrain(*cell) : kNativeTileWaterBase);
        int overlay = cell ? nativeOverlayTypeIndexForCell(*cell) : -1;
        if (cell && (cell->overlay == Overlay::Ore || cell->overlay == Overlay::Gems) && !report_.nativeOverlayOverrides.empty()) {
            overlay = -1;
        }
        if (const auto overlayIt = overlayByIso.find(key); overlayIt != overlayByIso.end()) {
            overlay = overlayIt->second->overlayTypeIndex;
        }
        *cellOut = cell;
        *tileOut = tile;
        *overlayOut = overlay;
    };

    const int maxAttempts = report_.grassAttemptBudget;
    const int maxWorkspaceIndex = workspaceSize * workspaceSize - 1;
    int processedBlocks = 0;
    for (int attempt = 0; attempt < maxAttempts && processedBlocks < grassBlocks; ++attempt) {
        ++report_.grassAttempts;
        ++report_.grassCandidateSamples;

        Point nativeCell{};
        int index = -1;
        do {
            index = randomScaledInclusiveTrace(0, maxWorkspaceIndex, "index", attempt, processedBlocks);
            nativeCell = {index % workspaceSize, index / workspaceSize};
        } while (!isInsideNativeCellDiamond(nativeCell, mapRect));

        Cell* cell = nullptr;
        int tile = kNativeTileWaterBase;
        int overlay = -1;
        projectGrassCandidate(nativeCell, &cell, &tile, &overlay);
        if (overlay != -1) {
            ++report_.grassRejectedOccupied;
            if (grassDump.is_open()) {
                const Point source = nativeCellToSourcePoint(map, nativeCell);
                grassDump << "candidate," << attempt << ',' << processedBlocks << ",0,0,"
                    << maxWorkspaceIndex << ','
                    << index << ','
                    << nativeCell.x << ','
                    << nativeCell.y << ','
                    << source.x << ','
                    << source.y << ','
                    << tile << ','
                    << overlay << ",0,-1,1,0\n";
            }
            continue;
        }

        int overlayValue = -1;
        if (isClearToSandLatOverlay(tile)) {
            overlayValue = 168 + randomScaledInclusiveTrace(0, 4, "overlay", attempt, processedBlocks);
            const std::uint64_t key = nativeIsoKey(nativeCell);
            const auto it = std::find_if(report_.isoTileOverrides.begin(), report_.isoTileOverrides.end(), [&](const IsoTileOverride& override) {
                return nativeIsoKey(override.iso) == key;
            });
            if (it != report_.isoTileOverrides.end()) {
                it->landType = nativeLandTypeForTile(Terrain::Dirt, tile - kNativeTileDirtBase, it->subTile);
            }
            const Point overlayCell = nativeGrassOverlayCell(nativeCell, overlayValue, tile);
            report_.nativeOverlayOverrides.push_back({overlayCell, overlayValue, 0, 3});
            overlayByIso[nativeIsoKey(nativeCell)] = &report_.nativeOverlayOverrides.back();
            overlayByIso[nativeIsoKey(overlayCell)] = &report_.nativeOverlayOverrides.back();
            placedGrassSources.push_back({nativeCellToSourcePoint(map, nativeCell), overlayValue});
            ++report_.grassPlaced;
        } else if (isNativeClearTileIndex(tile) || isNativeGreenTileIndex(tile)) {
            overlayValue = 173 + randomScaledInclusiveTrace(0, 4, "overlay", attempt, processedBlocks);
            const Point overlayCell = nativeGrassOverlayCell(nativeCell, overlayValue, tile);
            report_.nativeOverlayOverrides.push_back({overlayCell, overlayValue, 0, 3});
            overlayByIso[nativeIsoKey(nativeCell)] = &report_.nativeOverlayOverrides.back();
            overlayByIso[nativeIsoKey(overlayCell)] = &report_.nativeOverlayOverrides.back();
            placedGrassSources.push_back({nativeCellToSourcePoint(map, nativeCell), overlayValue});
            ++report_.grassPlaced;
        } else {
            if (isDirtLatForLandType(tile)) {
                const std::uint64_t key = nativeIsoKey(nativeCell);
                const auto it = std::find_if(report_.isoTileOverrides.begin(), report_.isoTileOverrides.end(), [&](const IsoTileOverride& override) {
                    return nativeIsoKey(override.iso) == key;
                });
                if (it != report_.isoTileOverrides.end()) {
                    it->landType = nativeLandTypeForTile(Terrain::Dirt, tile - kNativeTileDirtBase, it->subTile);
                }
            }
            ++report_.grassRejectedTerrain;
        }
        if (grassDump.is_open()) {
            const Point source = nativeCellToSourcePoint(map, nativeCell);
            grassDump << "candidate," << attempt << ',' << processedBlocks << ",0,0,"
                << maxWorkspaceIndex << ','
                << index << ','
                << nativeCell.x << ','
                << nativeCell.y << ','
                << source.x << ','
                << source.y << ','
                << tile << ','
                << overlay << ','
                << (overlayValue >= 0 ? 1 : 0) << ','
                << overlayValue << ','
                << 0 << ','
                << (overlayValue >= 0 ? 0 : 1) << '\n';
        }
        ++processedBlocks;
    }

    for (const auto& [source, overlayValue] : placedGrassSources) {
        if (!map.inBounds(source)) {
            continue;
        }
        Cell& cell = map.at(source);
        cell.overlay = Overlay::Grass;
        cell.overlayValue = overlayValue;
    }
}
