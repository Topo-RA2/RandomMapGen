void NativeMapGenerator::spawnNeutralTechBuildings(
    MapGrid& map,
    const MapSeedOptions& options,
    NativeRandomizer& rng,
    const std::vector<NativeRegion>& regions,
    bool initialZeroQuotaRegions)
{
    std::ofstream techTrace;
    if (!initialZeroQuotaRegions) {
        if (const char* dumpPath = std::getenv(kNativeTechDumpPathEnv); dumpPath != nullptr && dumpPath[0] != '\0') {
            techTrace.open(dumpPath, std::ios::out | std::ios::trunc);
            if (techTrace.is_open()) {
                techTrace << "event,call,round,regionIndex,quota,regionCells,attempt,raw,index,candidateCount,x,y,typeIndex,placed\n";
            }
        }
    }

    auto traceTech = [&techTrace](
                         const char* event,
                         int call,
                         int round,
                         int regionIndex,
                         int quota,
                         int regionCells,
                         int attempt,
                         std::uint32_t raw,
                         int index,
                         int candidateCount,
                         Point point,
                         int typeIndex,
                         bool placed) {
        if (!techTrace.is_open()) {
            return;
        }
        techTrace << event << ','
                  << call << ','
                  << round << ','
                  << regionIndex << ','
                  << quota << ','
                  << regionCells << ','
                  << attempt << ','
                  << raw << ','
                  << index << ','
                  << candidateCount << ','
                  << point.x << ','
                  << point.y << ','
                  << typeIndex << ','
                  << (placed ? 1 : 0) << '\n';
        techTrace.flush();
    };

    if (!initialZeroQuotaRegions) {
        traceTech("begin", 0, -1, -1, 0, static_cast<int>(regions.size()), -1, 0, options.mapType, 0, {-1, -1}, -1, false);
    }
    int traceCall = 0;

    auto rectangularFoundation = [](int width, int height) {
        std::vector<Point> footprint;
        footprint.reserve(static_cast<std::size_t>(std::max(0, width) * std::max(0, height)));
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                footprint.push_back({x, y});
            }
        }
        return footprint;
    };
    struct NativeTechBuildingSpec {
        int objectTypeIndex = -1;
        std::vector<Point> foundation;
    };
    auto techBuildingSpecForType = [&rectangularFoundation](int typeValue) {
        switch (std::clamp(typeValue, 1, 7)) {
        case 1: // CAAIRP, BuildingTypes array index 70.
            return NativeTechBuildingSpec{70, rectangularFoundation(3, 3)};
        case 2: // CATHOSP, BuildingTypes array index 69.
            return NativeTechBuildingSpec{69, rectangularFoundation(6, 4)};
        case 3: // CAOILD, BuildingTypes array index 71.
            return NativeTechBuildingSpec{71, rectangularFoundation(2, 2)};
        case 4: // CAOUTP, BuildingTypes array index 68.
            return NativeTechBuildingSpec{68, rectangularFoundation(4, 3)};
        case 5: // CAMACH, BuildingTypes array index 56.
            return NativeTechBuildingSpec{56, rectangularFoundation(3, 3)};
        case 6: // CAPOWR, BuildingTypes array index 369.
            return NativeTechBuildingSpec{369, rectangularFoundation(2, 2)};
        default: // CASLAB, BuildingTypes array index 313, runtime NeutralTechBuildings index 6.
            return NativeTechBuildingSpec{313, rectangularFoundation(3, 3)};
        }
    };

    const Rect mapRect = nativeSavedMapRect(map);
    const Rect visibleRect = nativeLocalRect(map);
    const int nativeMapSize = mapRect.w + mapRect.h + 1;
    std::vector<Point> nativeMapgenCoords(static_cast<std::size_t>(std::max(1, nativeMapSize * nativeMapSize)), {0, 0});
    std::unordered_map<std::uint64_t, const NativeIteratorCell*> nativeCellByIso;
    const std::vector<NativeIteratorCell> nativeIteratorCells = buildNativeIteratorCells(map, report_.isoTileOverrides);
    {
        nativeCellByIso.reserve(nativeIteratorCells.size());
        for (const NativeIteratorCell& iteratorCell : nativeIteratorCells) {
            if (iteratorCell.cell.x < 0 || iteratorCell.cell.y < 0 || iteratorCell.cell.x >= nativeMapSize || iteratorCell.cell.y >= nativeMapSize) {
                continue;
            }
            nativeMapgenCoords[static_cast<std::size_t>(iteratorCell.cell.x + nativeMapSize * iteratorCell.cell.y)] = iteratorCell.cell;
            nativeCellByIso[nativeIsoKey(iteratorCell.cell)] = &iteratorCell;
        }
    }

    auto isWithinNativeUsableArea = [mapRect, visibleRect](const NativeIteratorCell& cell) {
        int boundaryLevel = cell.level;
        const int sum = cell.cell.x + cell.cell.y;
        if (cell.slopeIndex != 0
            && sum < boundaryLevel + mapRect.w + 2 * visibleRect.y + 4) {
            ++boundaryLevel;
        }
        return sum > boundaryLevel + mapRect.w + 2 * visibleRect.y
            && sum <= boundaryLevel + mapRect.w + 2 * (visibleRect.y + visibleRect.h) + 2
            && cell.cell.x - cell.cell.y < 2 * (visibleRect.x + visibleRect.w) - mapRect.w
            && cell.cell.y - cell.cell.x < mapRect.w - 2 * visibleRect.x;
    };

    auto canPlaceTechFoundation = [
                                      this,
                                      &nativeCellByIso,
                                      &isWithinNativeUsableArea](Point nativeAnchor, const std::vector<Point>& foundation) {
        ++report_.techBuildingFootprintChecks;
        const auto anchorIt = nativeCellByIso.find(nativeIsoKey(nativeAnchor));
        if (anchorIt == nativeCellByIso.end() || foundation.empty()) {
            ++report_.techBuildingFootprintFailures;
            ++report_.techBuildingRejectedUsableArea;
            return false;
        }

        const int level = anchorIt->second->level;
        for (Point offset : foundation) {
            const Point nativeCell{nativeAnchor.x + offset.x, nativeAnchor.y + offset.y};
            const std::uint64_t nativeKey = nativeIsoKey(nativeCell);
            const auto cellIt = nativeCellByIso.find(nativeKey);
            if (cellIt == nativeCellByIso.end()
                || !isWithinNativeUsableArea(*cellIt->second)) {
                ++report_.techBuildingFootprintFailures;
                ++report_.techBuildingRejectedUsableArea;
                return false;
            }

            const bool hasObject = std::any_of(
                report_.nativeObjectOverrides.begin(),
                report_.nativeObjectOverrides.end(),
                [nativeKey](const NativeObjectOverride& object) {
                    return nativeIsoKey(object.iso) == nativeKey
                        && object.objectAbstractType >= 0;
                });
            if (hasObject) {
                ++report_.techBuildingFootprintFailures;
                ++report_.techBuildingRejectedObjects;
                return false;
            }
            if (!isNativeClearTileIndex(cellIt->second->isoTileTypeIndex)) {
                ++report_.techBuildingFootprintFailures;
                ++report_.techBuildingRejectedNonBlank;
                return false;
            }
            if (cellIt->second->level != level) {
                ++report_.techBuildingFootprintFailures;
                ++report_.techBuildingRejectedLevel;
                return false;
            }
            if (cellIt->second->landType == 3) {
                ++report_.techBuildingFootprintFailures;
                ++report_.techBuildingRejectedRock;
                return false;
            }
            if (nativeMapgenSurfaceBlockedKeys_.contains(nativeKey)) {
                ++report_.techBuildingFootprintFailures;
                ++report_.techBuildingRejectedExclusion;
                return false;
            }
        }

        return true;
    };

    auto placeTechAt = [this, &map, &techBuildingSpecForType](Point anchor, Point nativeAnchor, int typeValue) {
        const NativeTechBuildingSpec spec = techBuildingSpecForType(typeValue);
        NativeStructureOverride structure{nativeAnchor, typeValue - 1};
        if (nativeScenarioUniqueId_ >= 0) {
            const int idSpan = nativeTechUniqueIdSpan(structure.nativeTechIndex);
            if (nativeScenarioUniqueId_ > std::numeric_limits<int>::max() - idSpan) {
                throw std::runtime_error("native Scenario UniqueID overflow while placing neutral tech building");
            }
            structure.nativeUniqueId = ++nativeScenarioUniqueId_;
            nativeScenarioUniqueId_ += idSpan - 1;
        }
        report_.nativeStructureOverrides.push_back(std::move(structure));
        for (Point offset : spec.foundation) {
            const Point nativeCell{nativeAnchor.x + offset.x, nativeAnchor.y + offset.y};
            report_.nativeObjectOverrides.push_back({nativeCell, 6, spec.objectTypeIndex, -1, -1});

            const Point source = nativeCellToSourcePoint(map, nativeCell);
            if (map.inBounds(source)) {
                Cell& cell = map.at(source);
                cell.overlay = Overlay::Tech;
                cell.overlayValue = typeValue;
            }
        }
        if (spec.foundation.empty() && map.inBounds(anchor)) {
            Cell& cell = map.at(anchor);
            cell.overlay = Overlay::Tech;
            cell.overlayValue = typeValue;
        }
        ++report_.techBuildingUnlimboSuccesses;
    };

    constexpr int kNativeTechTypeCount = 7;
    auto randomTechType = [&rng] {
        return rng.randomScaledInclusive(1, kNativeTechTypeCount);
    };

    auto tryPlaceInRegion = [
                                this,
                                &map,
                                &rng,
                                &techBuildingSpecForType,
                                &canPlaceTechFoundation,
                                &placeTechAt,
                                &randomTechType,
                                &traceTech,
                                &traceCall](const NativeRegion& region) {
        ++report_.techBuildingRegionCalls;
        ++traceCall;
        const bool useNativePool = !region.nativeCells.empty();
        const std::vector<Point>& pool = useNativePool ? region.nativeCells : region.cells;
        traceTech("call", traceCall, -1, -1, region.nativeWaypointQuota, static_cast<int>(pool.size()), -1, 0, -1, static_cast<int>(pool.size()), {-1, -1}, -1, false);
        if (pool.empty()) {
            return false;
        }

        const int typeValue = randomTechType();
        traceTech("type", traceCall, -1, -1, region.nativeWaypointQuota, static_cast<int>(pool.size()), -1, rng.lastRandom(), typeValue - 1, kNativeTechTypeCount, {-1, -1}, typeValue - 1, false);
        const NativeTechBuildingSpec spec = techBuildingSpecForType(typeValue);
        for (int attempt = 0; attempt < 100; ++attempt) {
            ++report_.techBuildingCandidateSamples;
            const int candidateIndex = rng.randomScaledInclusive(0, static_cast<int>(pool.size()) - 1);
            const Point tracePoint = pool[static_cast<std::size_t>(candidateIndex)];
            const Point nativePoint = useNativePool ? tracePoint : sourcePointToNativeCell(map, tracePoint);
            const Point p = nativeCellToSourcePoint(map, nativePoint);
            traceTech("candidate", traceCall, -1, -1, region.nativeWaypointQuota, static_cast<int>(pool.size()), -1, rng.lastRandom(), candidateIndex, static_cast<int>(pool.size()), tracePoint, -1, false);
            if (!map.inBounds(p) || !canPlaceTechFoundation(nativePoint, spec.foundation)) {
                traceTech("attempt", traceCall, -1, -1, region.nativeWaypointQuota, static_cast<int>(pool.size()), attempt, 0, -1, static_cast<int>(pool.size()), tracePoint, -1, false);
                continue;
            }

            placeTechAt(p, nativePoint, typeValue);
            traceTech("attempt", traceCall, -1, -1, region.nativeWaypointQuota, static_cast<int>(pool.size()), attempt, 0, -1, static_cast<int>(pool.size()), tracePoint, -1, true);
            return true;
        }

        return false;
    };

    auto isNativeGlobalBlank = [&nativeCellByIso](Point nativeCell) {
        const auto cellIt = nativeCellByIso.find(nativeIsoKey(nativeCell));
        return cellIt != nativeCellByIso.end()
            && isNativeClearTileIndex(cellIt->second->isoTileTypeIndex);
    };

    auto selectGlobalBlankCell = [
                                     this,
                                     &rng,
                                     &nativeMapgenCoords,
                                     nativeMapSize,
                                     &isNativeGlobalBlank,
                                     &traceTech](int call, int attempt, Point* outNative) {
        constexpr int maxBlankSearch = 200;
        int blankSearches = 0;
        const int candidateCount = nativeMapSize * nativeMapSize;
        while (true) {
            int index = rng.randomScaledInclusive(0, candidateCount - 1);
            Point nativeCell = nativeMapgenCoords[static_cast<std::size_t>(index)];
            while (nativeCell.x == 0 && nativeCell.y == 0) {
                ++report_.techBuildingBlankSearchRejects;
                const int retryIndex = rng.randomScaledInclusive(0, candidateCount - 1);
                index = retryIndex;
                nativeCell = nativeMapgenCoords[static_cast<std::size_t>(retryIndex)];
            }
            traceTech("global_candidate", call, -1, -1, 0, candidateCount, -1, 0, index, candidateCount, nativeCell, -1, false);

            ++report_.techBuildingCandidateSamples;
            ++blankSearches;
            if (blankSearches > maxBlankSearch) {
                ++report_.techBuildingGlobalFallbacks;
                *outNative = {0, 0};
                traceTech("global_fallback", call, -1, -1, 0, candidateCount, attempt, 0, -1, candidateCount, *outNative, -1, false);
                return false;
            }
            if (isNativeGlobalBlank(nativeCell)) {
                *outNative = nativeCell;
                traceTech("global_blank", call, -1, -1, 0, candidateCount, -1, 0, -1, candidateCount, nativeCell, -1, true);
                return true;
            }
            ++report_.techBuildingBlankSearchRejects;
            traceTech("global_nonblank", call, -1, -1, 0, candidateCount, -1, 0, -1, candidateCount, nativeCell, -1, false);
        }
    };

    auto tryPlaceGlobal = [
                              &map,
                              &rng,
                              &techBuildingSpecForType,
                              &canPlaceTechFoundation,
                              &placeTechAt,
                              &randomTechType,
                              &selectGlobalBlankCell,
                              &traceTech,
                              &traceCall]() {
        ++traceCall;
        const Rect mapRect = nativeSavedMapRect(map);
        const int nativeMapSize = mapRect.w + mapRect.h + 1;
        const int candidateCount = nativeMapSize * nativeMapSize;
        traceTech("global_call", traceCall, -1, -1, 0, candidateCount, -1, 0, -1, candidateCount, {-1, -1}, -1, false);
        const int typeValue = randomTechType();
        traceTech("global_type", traceCall, -1, -1, 0, candidateCount, -1, rng.lastRandom(), typeValue - 1, kNativeTechTypeCount, {-1, -1}, typeValue - 1, false);
        const NativeTechBuildingSpec spec = techBuildingSpecForType(typeValue);
        for (int attempt = 0; attempt < 100; ++attempt) {
            Point nativePoint;
            selectGlobalBlankCell(traceCall, attempt, &nativePoint);
            const Point p = nativeCellToSourcePoint(map, nativePoint);
            if (!canPlaceTechFoundation(nativePoint, spec.foundation)) {
                traceTech("global_attempt", traceCall, -1, -1, 0, candidateCount, attempt, 0, -1, candidateCount, nativePoint, -1, false);
                continue;
            }

            placeTechAt(p, nativePoint, typeValue);
            traceTech("global_attempt", traceCall, -1, -1, 0, candidateCount, attempt, 0, -1, candidateCount, nativePoint, -1, true);
            return true;
        }
        return false;
    };

    int placed = 0;
    if (initialZeroQuotaRegions) {
        report_.techBuildingPlacementMode = 3;
        report_.techBuildingEligibleRegions = static_cast<int>(std::count_if(regions.begin(), regions.end(), [](const NativeRegion& region) {
            return region.nativeWaypointQuota == 0;
        }));
        for (const NativeRegion& region : regions) {
            if (region.nativeWaypointQuota != 0) {
                continue;
            }
            if (rng.randomUnit() < 0.5 && tryPlaceInRegion(region)) {
                ++placed;
            }
        }
    } else if (options.mapType == 2) {
        report_.techBuildingPlacementMode = 2;
        const int rounds = rng.randomScaledInclusive(0, 2);
        traceTech("rounds", 0, rounds, -1, 0, static_cast<int>(regions.size()), -1, rng.lastRandom(), rounds, 3, {-1, -1}, -1, false);
        report_.techBuildingRounds = rounds;
        report_.techBuildingEligibleRegions = static_cast<int>(std::count_if(regions.begin(), regions.end(), [](const NativeRegion& region) {
            return region.nativeWaypointQuota > 0;
        }));
        report_.techBuildingTargets = rounds * report_.techBuildingEligibleRegions;
        std::vector<const NativeRegion*> techRegions;
        techRegions.reserve(regions.size());
        for (const NativeRegion& region : regions) {
            techRegions.push_back(&region);
        }
        std::sort(techRegions.begin(), techRegions.end(), [](const NativeRegion* lhs, const NativeRegion* rhs) {
            return lhs->id < rhs->id;
        });
        for (int round = 0; round < rounds; ++round) {
            for (std::size_t regionIndex = 0; regionIndex < techRegions.size(); ++regionIndex) {
                const NativeRegion& region = *techRegions[regionIndex];
                if (region.nativeWaypointQuota <= 0) {
                    continue;
                }
                const int regionCells = region.nativeCells.empty() ? static_cast<int>(region.cells.size()) : static_cast<int>(region.nativeCells.size());
                traceTech("region", traceCall + 1, rounds - round, static_cast<int>(regionIndex), region.nativeWaypointQuota, regionCells, -1, 0, static_cast<int>(regionIndex), static_cast<int>(techRegions.size()), {-1, -1}, -1, false);
                if (tryPlaceInRegion(region)) {
                    ++placed;
                }
            }
        }
    } else {
        report_.techBuildingPlacementMode = 1;
        const int target = rng.randomScaledInclusive(0, 4);
        traceTech("global_target", 0, target, -1, 0, static_cast<int>(regions.size()), -1, rng.lastRandom(), target, 5, {-1, -1}, -1, false);
        report_.techBuildingRounds = target;
        report_.techBuildingTargets = target;
        report_.techBuildingEligibleRegions = static_cast<int>(regions.size());
        for (int building = 0; building < target; ++building) {
            if (tryPlaceGlobal()) {
                ++placed;
            }
        }
    }

    report_.techBuildings = placed;
    if (!initialZeroQuotaRegions) {
        traceTech("end", traceCall, -1, -1, 0, static_cast<int>(regions.size()), -1, 0, -1, 0, {-1, -1}, -1, false);
    }
}

void NativeMapGenerator::placeTiberiumPatch(
    MapGrid& map,
    const MapSeedOptions& options,
    const RmgConfig& config,
    NativeRandomizer& rng,
    Point seed,
    int amount,
    bool rich,
    const char* traceBranch,
    bool seedIsNative,
    double amountJitter,
    int amountRawCount,
    std::uint32_t amountRaw0,
    std::uint32_t amountRaw1,
    int nativeScratchTag)
{
    if (amount <= 0) {
        return;
    }
    (void) options;
    (void) config;

    struct ResourcePatchHeapNode {
        Point point;
        float priority = 0.0f;
    };
    struct ResourcePatchHeap {
        explicit ResourcePatchHeap(int capacity)
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

        bool push(Point point, double priority)
        {
            if (allocated >= capacity) {
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
                lastPoppedIndex = -1;
                return {-1, -1};
            }

            const int resultIndex = heap[1];
            lastPoppedIndex = resultIndex;
            const Point result = nodes[static_cast<std::size_t>(resultIndex)].point;
            heap[1] = heap[static_cast<std::size_t>(heapSize)];
            heap[static_cast<std::size_t>(heapSize)] = -1;
            --heapSize;
            siftDown(1);
            return result;
        }

        void clear()
        {
            std::fill(heap.begin(), heap.end(), -1);
            allocated = 0;
            heapSize = 0;
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
        int lastPoppedIndex = -1;
        std::vector<ResourcePatchHeapNode> nodes;
        std::vector<int> heap;
    };

    ResourcePatchHeap heap(std::max(1, amount * 10));
    const Rect mapRect = nativeSavedMapRect(map);
    auto findNativeOverlay = [this](Point nativeCell) -> NativeOverlayOverride* {
        const std::uint64_t key = nativeIsoKey(nativeCell);
        for (NativeOverlayOverride& overlay : report_.nativeOverlayOverrides) {
            if (nativeIsoKey(overlay.iso) == key) {
                return &overlay;
            }
        }
        return nullptr;
    };
    auto findNativeObject = [this](Point nativeCell, int objectAbstractType) -> NativeObjectOverride* {
        const std::uint64_t key = nativeIsoKey(nativeCell);
        for (NativeObjectOverride& object : report_.nativeObjectOverrides) {
            if (nativeIsoKey(object.iso) == key && object.objectAbstractType == objectAbstractType) {
                return &object;
            }
        }
        return nullptr;
    };
    std::unordered_map<std::uint64_t, const IsoTileOverride*> overrideByIso;
    overrideByIso.reserve(report_.isoTileOverrides.size());
    for (const IsoTileOverride& override : report_.isoTileOverrides) {
        overrideByIso[nativeIsoKey(override.iso)] = &override;
    }
    const Rect visibleRect = nativeLocalRect(map);
    auto nativeResourceLevel = [&](Point nativeCell) {
        const auto overrideIt = overrideByIso.find(nativeIsoKey(nativeCell));
        if (overrideIt != overrideByIso.end()) {
            return std::clamp(overrideIt->second->level, 0, 255);
        }
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        return map.inBounds(source) ? std::clamp(map.at(source).level, 0, 255) : 0;
    };
    auto nativeResourceSlope = [&](Point nativeCell) {
        const auto overrideIt = overrideByIso.find(nativeIsoKey(nativeCell));
        if (overrideIt != overrideByIso.end()) {
            return 0;
        }
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        return map.inBounds(source) ? nativeSlopeIndexForCell(map.at(source)) : 0;
    };
    auto isNativeUsableForResource = [&](Point nativeCell) {
        const int workspaceIndex = nativeCell.x + (nativeCell.y << 9);
        if (nativeCell.x < 0 || nativeCell.y < 0 || workspaceIndex > 0x3FFFF) {
            return false;
        }

        int level = nativeResourceLevel(nativeCell);
        const int sum = nativeCell.x + nativeCell.y;
        if (nativeResourceSlope(nativeCell) != 0 && sum < level + mapRect.w + 2 * visibleRect.y + 4) {
            ++level;
        }

        if (sum <= level + mapRect.w + 2 * visibleRect.y
            || sum > level + mapRect.w + 2 * (visibleRect.y + visibleRect.h) + 2) {
            return false;
        }
        return nativeCell.x - nativeCell.y < 2 * (visibleRect.x + visibleRect.w) - mapRect.w
            && nativeCell.y - nativeCell.x < mapRect.w - 2 * visibleRect.x;
    };
    auto nativeResourceTile = [&](Point nativeCell) {
        const auto overrideIt = overrideByIso.find(nativeIsoKey(nativeCell));
        if (overrideIt != overrideByIso.end()) {
            return nativeTileTypeIndexForOverride(*overrideIt->second);
        }
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        return map.inBounds(source) ? nativeTileTypeIndexForTerrain(map.at(source)) : 0xFFFF;
    };
    auto isNativeBlankForResource = [&](Point nativeCell) {
        return isNativeClearTileIndex(nativeResourceTile(nativeCell));
    };

    if (!seedIsNative) {
        if (!map.inBounds(seed)) {
            return;
        }
        seed = clampPoint(seed, map.bounds());
    } else if (!isNativeUsableForResource(seed)) {
        return;
    }
    const Point sourceSeed = seedIsNative ? nativeCellToSourcePoint(map, seed) : seed;
    Point nativeSeed = seedIsNative ? seed : sourcePointToNativeCell(map, seed);
    const Point initialNativeSeed = nativeSeed;
    const int patchIndex = report_.tiberiumPatches + 1;
    const std::size_t overlayStartCount = report_.nativeOverlayOverrides.size();
    Point firstNativePlacement{-1, -1};
    std::unordered_set<std::uint64_t> taggedBlankCells;
    taggedBlankCells.reserve(static_cast<std::size_t>(std::max(16, amount * 2)));
    auto resetNativeTiberiumPatchScratch = [&] {
        if (nativeScratchTag <= 0) {
            return;
        }
        nativeMapgenDataActive_ = true;
        nativeMapgenDefaultTempTag_ = 0;
        nativeMapgenTempTags_.clear();
        nativeMapgenTempTags_[nativeIsoKey(nativeSeed)] = nativeScratchTag;
    };
    resetNativeTiberiumPatchScratch();
    taggedBlankCells.insert(nativeIsoKey(nativeSeed));
    heap.push(nativeSeed, 0.0);
    ++report_.tiberiumPatchHeapPushes;

    auto canTraverseNativeResourcePatch = [&](Point nativeCell) {
        if (!isNativeUsableForResource(nativeCell)) {
            return false;
        }

        if (!isNativeBlankForResource(nativeCell)) {
            return false;
        }
        if (NativeOverlayOverride* overlay = findNativeOverlay(nativeCell)) {
            return overlay->overlayData < 11 && isNativeTiberiumOverlayType(overlay->overlayTypeIndex);
        }
        return true;
    };
    auto canPlaceNativeResource = [&](Point nativeCell) {
        if (!isNativeUsableForResource(nativeCell)) {
            return false;
        }

        if (nativeSpawnExclusionKeys_.find(nativeIsoKey(nativeCell)) != nativeSpawnExclusionKeys_.end()) {
            return false;
        }
        if (NativeOverlayOverride* overlay = findNativeOverlay(nativeCell)) {
            return overlay->overlayData < 11;
        }
        return true;
    };
    auto hasOpenNativeResourceOverlay = [&](Point nativeCell) {
        if (NativeOverlayOverride* overlay = findNativeOverlay(nativeCell)) {
            return overlay->overlayData < 11 && isNativeTiberiumOverlayType(overlay->overlayTypeIndex);
        }
        return false;
    };
    auto canUseFullOverlayAsFirstResource = [&](Point nativeCell) {
        if (!isNativeUsableForResource(nativeCell)) {
            return false;
        }

        if (nativeSpawnExclusionKeys_.find(nativeIsoKey(nativeCell)) != nativeSpawnExclusionKeys_.end()) {
            return false;
        }
        if (NativeOverlayOverride* overlay = findNativeOverlay(nativeCell)) {
            return overlay->overlayData <= 11 && isNativeTiberiumOverlayType(overlay->overlayTypeIndex);
        }
        return false;
    };

    int placed = 0;
    int initialRetries = 1;

    auto packNativeCell = [](Point nativeCell) {
        const auto x = static_cast<std::uint32_t>(static_cast<std::uint16_t>(nativeCell.x));
        const auto y = static_cast<std::uint32_t>(static_cast<std::uint16_t>(nativeCell.y));
        return static_cast<int>(x | (y << 16));
    };
    auto floatBits = [](float value) {
        int bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    };
    std::ofstream tiberiumEventTrace;
    if (const char* dumpPath = std::getenv(kNativeTiberiumEventDumpPathEnv); dumpPath != nullptr && dumpPath[0] != '\0') {
        const std::filesystem::path path{dumpPath};
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::error_code ec;
        const bool writeHeader = !std::filesystem::exists(path, ec) || std::filesystem::file_size(path, ec) == 0;
        tiberiumEventTrace.open(path, std::ios::out | std::ios::app);
        if (tiberiumEventTrace.is_open() && writeHeader) {
            tiberiumEventTrace
                << "event,call,resources,regionCount,eax,ebx,ecx,edx,esi,edi,"
                   "stack0,stack4,stack8,stackC,stack10,stack14,stack18,stack1C,stack20,stack24,stack28,stack2C\n";
        }
    }
    auto traceTiberiumEvent = [&](const char* event, Point nativeCell, int extra0, int extra1, int priorityBitsValue) {
        if (!tiberiumEventTrace.is_open()) {
            return;
        }

        int overlayType = -1;
        int overlayData = 0;
        if (NativeOverlayOverride* overlay = findNativeOverlay(nativeCell)) {
            overlayType = overlay->overlayTypeIndex;
            overlayData = overlay->overlayData;
        }

        int blocked = 1;
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (isNativeUsableForResource(nativeCell) && map.inBounds(source)) {
            const Cell& cell = map.at(source);
            blocked = (nativeSpawnExclusionKeys_.find(nativeIsoKey(nativeCell)) != nativeSpawnExclusionKeys_.end())
                    || cell.cliffShoreExclusion
                    || cell.terrainObject == TerrainObject::OrePatchLamp
                ? 1
                : 0;
        }

        tiberiumEventTrace
            << (event ? event : "") << ','
            << patchIndex << ','
            << options.resources << ','
            << 0 << ','
            << packNativeCell(nativeCell) << ','
            << 0 << ','
            << 0 << ','
            << extra0 << ','
            << extra1 << ','
            << priorityBitsValue << ','
            << nativeCell.x << ','
            << nativeCell.y << ','
            << overlayType << ','
            << overlayData << ','
            << (taggedBlankCells.find(nativeIsoKey(nativeCell)) != taggedBlankCells.end() ? 1 : 0) << ','
            << blocked << ','
            << placed << ','
            << heap.size() << ','
            << initialRetries << ','
            << nativeSeed.x << ','
            << nativeSeed.y << ','
            << 0 << '\n';
    };

    bool foundFirstPlacement = false;
    while (placed < amount && initialRetries <= 10) {
        if (heap.empty()) {
            if (initialRetries >= 10) {
                break;
            }
            ++initialRetries;
            ++report_.tiberiumPatchInitialRetries;
            foundFirstPlacement = false;
            nativeSeed = initialNativeSeed;
            taggedBlankCells.clear();
            resetNativeTiberiumPatchScratch();
            taggedBlankCells.insert(nativeIsoKey(initialNativeSeed));
            heap.push(initialNativeSeed, 0.0);
            ++report_.tiberiumPatchHeapPushes;
            continue;
        }

        const Point nativeCell = heap.pop();
        const int poppedNodeIndex = heap.lastPoppedIndex;
        ++report_.tiberiumPatchHeapPops;
        traceTiberiumEvent("patch_pop", nativeCell, heap.size(), 0, 0);

        bool firstPlacementThisPop = false;
        const bool canPlaceResource = canPlaceNativeResource(nativeCell)
            || (!foundFirstPlacement && canUseFullOverlayAsFirstResource(nativeCell));
        if (canPlaceResource) {
            if (!foundFirstPlacement) {
                foundFirstPlacement = true;
                firstPlacementThisPop = true;
                nativeSeed = nativeCell;
                firstNativePlacement = nativeCell;
                traceTiberiumEvent("patch_first", nativeCell, 0, 0, 0);
                heap.clear();
                ++report_.tiberiumPatchFirstCellResets;

                if (!rich) {
                    const int treeVariant = rng.randomScaledInclusive(1, 3);
                    if (NativeObjectOverride* object = findNativeObject(nativeCell, 36)) {
                        object->objectTypeIndex = 43 + treeVariant;
                        object->altObjectAbstractType = -1;
                        object->altObjectTypeIndex = -1;
                    } else {
                        report_.nativeObjectOverrides.push_back({nativeCell, 36, 43 + treeVariant, -1, -1});
                    }

                    if (NativeOverlayOverride* existingOverlay = findNativeOverlay(nativeCell)) {
                        existingOverlay->overlayTypeIndex = -1;
                        existingOverlay->overlayData = 0;
                        existingOverlay->landTypeOverride = -1;
                    }
                    const Point source = nativeCellToSourcePoint(map, nativeCell);
                    if (map.inBounds(source)) {
                        map.at(source).overlay = Overlay::None;
                        map.at(source).overlayValue = 0;
                    }
                }
            }

            NativeOverlayOverride* overlay = findNativeOverlay(nativeCell);
            if (overlay != nullptr && overlay->overlayTypeIndex >= 0) {
                overlay->overlayData = std::min(11, overlay->overlayData + 1);
                ++report_.tiberiumPatchExistingOverlayIncrements;
                traceTiberiumEvent("patch_place_inc", nativeCell, overlay->overlayData, 0, 0);
            } else {
                const int overlayType = rng.randomScaledInclusive(rich ? 27 : 102, rich ? 38 : 113);
                if (overlay != nullptr) {
                    overlay->overlayTypeIndex = overlayType;
                    overlay->overlayData = 0;
                    overlay->landTypeOverride = firstPlacementThisPop ? 5 : -1;
                } else {
                    report_.nativeOverlayOverrides.push_back({
                        nativeCell,
                        overlayType,
                        0,
                    });
                }
                const Point source = nativeCellToSourcePoint(map, nativeCell);
                if (map.inBounds(source)) {
                    Cell& cell = map.at(source);
                    cell.overlay = rich ? Overlay::Gems : Overlay::Ore;
                    cell.overlayValue = 1;
                }
                traceTiberiumEvent("patch_place_new", nativeCell, overlayType, 0, 0);
            }
            ++placed;
        } else {
            ++report_.tiberiumPatchRejectedCells;
        }

        constexpr std::array<Point, 8> kNativePatchNeighbors{{
            {0, -1}, {1, -1}, {1, 0}, {1, 1},
            {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
        }};
        Point neighborBase = nativeCell;
        bool firstPushAliasesCurrentNode = firstPlacementThisPop && poppedNodeIndex >= 0;
        for (std::size_t offsetIndex = 0; offsetIndex < kNativePatchNeighbors.size(); ++offsetIndex) {
            const Point offset = kNativePatchNeighbors[offsetIndex];
            const Point next{neighborBase.x + offset.x, neighborBase.y + offset.y};
            if (!isNativeUsableForResource(next)) {
                continue;
            }
            const std::uint64_t key = nativeIsoKey(next);
            const bool existingOverlay = hasOpenNativeResourceOverlay(next);
            if (!existingOverlay && taggedBlankCells.find(key) != taggedBlankCells.end()) {
                continue;
            }
            ++report_.tiberiumPatchCandidateChecks;
            if (!canTraverseNativeResourcePatch(next)) {
                ++report_.tiberiumPatchRejectedCells;
                continue;
            }
            if (firstPushAliasesCurrentNode && heap.allocated == poppedNodeIndex) {
                neighborBase = next;
                firstPushAliasesCurrentNode = false;
            }
            taggedBlankCells.insert(key);
            if (nativeScratchTag > 0) {
                nativeMapgenTempTags_[key] = nativeScratchTag;
            }
            const int dx = nativeSeed.x - next.x;
            const int dy = nativeSeed.y - next.y;
            const double distance = nativePatchedSqrt(static_cast<double>(dx * dx + dy * dy));
            const std::uint32_t priorityRaw = rng.random();
            const float priority = nativeDoubleToFloatChop(
                nativeSurfaceHeapPriority(distance, priorityRaw));
            if (heap.push(next, priority)) {
                ++report_.tiberiumPatchHeapPushes;
                traceTiberiumEvent("patch_enqueue", next, 1, static_cast<int>(offsetIndex), floatBits(priority));
            }
        }
        if (initialRetries >= 10) {
            break;
        }
    }

    if (placed > 0) {
        ++report_.tiberiumPatches;
    }
    if (placed < amount) {
        ++report_.tiberiumPatchUnfilled;
    }

    if (const char* dumpPath = std::getenv(kNativeTiberiumDumpPathEnv); dumpPath != nullptr && dumpPath[0] != '\0') {
        const std::filesystem::path path{dumpPath};
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::error_code ec;
        const bool writeHeader = !std::filesystem::exists(path, ec) || std::filesystem::file_size(path, ec) == 0;
        std::ofstream out(path, std::ios::out | std::ios::app);
        if (out.is_open()) {
            if (writeHeader) {
                out << "patch,branch,seedX,seedY,nativeSeedX,nativeSeedY,amount,rich,amountJitter,amountRawCount,amountRaw0,amountRaw1,placed,uniqueOverlays,firstX,firstY,finalCenterX,finalCenterY,rngDraws\n";
            }
            out << patchIndex << ','
                << (traceBranch ? traceBranch : "") << ','
                << sourceSeed.x << ','
                << sourceSeed.y << ','
                << initialNativeSeed.x << ','
                << initialNativeSeed.y << ','
                << amount << ','
                << (rich ? 1 : 0) << ','
                << amountJitter << ','
                << amountRawCount << ','
                << amountRaw0 << ','
                << amountRaw1 << ','
                << placed << ','
                << (report_.nativeOverlayOverrides.size() - overlayStartCount) << ','
                << firstNativePlacement.x << ','
                << firstNativePlacement.y << ','
                << nativeSeed.x << ','
                << nativeSeed.y << ','
                << rng.drawCount() << '\n';
        }
    }
}

void NativeMapGenerator::distributeTiberium(
    MapGrid& map,
    const MapSeedOptions& options,
    const RmgConfig& config,
    NativeRandomizer& rng,
    const std::vector<NativeRegion>& regions,
    const std::vector<Point>& starts)
{
    if (regions.empty()) {
        return;
    }

    const int baseBudget = nativeTiberiumBudget(
        options.tiberium,
        config.minimumTiberium,
        config.maximumTiberium);
    const int totalCells = std::max(1, baseBudget / 90);
    report_.tiberiumBudget = baseBudget;
    report_.tiberiumBudgetCells = totalCells;

    bool specialRegionPatch = false;
    switch (options.mapType) {
    case 0:
        specialRegionPatch = options.resources == 3;
        break;
    case 1:
    case 3:
    case 4:
        specialRegionPatch = options.resources != 3;
        break;
    case 2:
        specialRegionPatch = true;
        break;
    default:
        specialRegionPatch = false;
        break;
    }

    const bool richWaypointPatch = options.resources == 3
        && (options.mapType == 1 || options.mapType == 3 || options.mapType == 4);
    std::vector<Point> globalWaypoints;
    for (const NativeRegion& region : regions) {
        const int quota = std::clamp(region.nativeWaypointQuota, 0, static_cast<int>(region.nativeWaypoints.size()));
        globalWaypoints.insert(globalWaypoints.end(), region.nativeWaypoints.begin(), region.nativeWaypoints.begin() + quota);
    }

    std::vector<const NativeRegion*> regionOrder;
    regionOrder.reserve(regions.size());
    for (const NativeRegion& region : regions) {
        regionOrder.push_back(&region);
    }
    std::stable_sort(regionOrder.begin(), regionOrder.end(), [](const NativeRegion* lhs, const NativeRegion* rhs) {
        if (lhs->nativeBaseZoneId >= 0 || rhs->nativeBaseZoneId >= 0) {
            const int lhsZone = lhs->nativeBaseZoneId >= 0
                ? lhs->nativeBaseZoneId
                : std::numeric_limits<int>::max();
            const int rhsZone = rhs->nativeBaseZoneId >= 0
                ? rhs->nativeBaseZoneId
                : std::numeric_limits<int>::max();
            return lhsZone < rhsZone;
        }
        return lhs->id < rhs->id;
    });

    int waypointOffset = 0;
    for (const NativeRegion* regionPtr : regionOrder) {
        const NativeRegion& region = *regionPtr;
        const int quota = std::max(0, region.nativeWaypointQuota);

        const int nativeQuota = std::clamp(quota, 0, static_cast<int>(region.nativeResourceSeeds.size()));

        std::vector<Point> patchCells;
        if (static_cast<int>(region.nativeResourceSeeds.size()) > nativeQuota) {
            patchCells.assign(region.nativeResourceSeeds.begin() + nativeQuota, region.nativeResourceSeeds.end());
        }
        const int patchCellCount = static_cast<int>(patchCells.size());
        const int regionPatchBase = patchCellCount > 0
            ? nativeDoubleToInt(static_cast<double>(baseBudget) * std::max(static_cast<double>(quota), 0.5)) / patchCellCount
            : 0;

        int specialIndex = -1;
        if (specialRegionPatch && !patchCells.empty()) {
            Point anchor{-1, -1};
            if (quota > 0 && waypointOffset < static_cast<int>(globalWaypoints.size())) {
                int sumX = 0;
                int sumY = 0;
                int count = 0;
                for (int i = 0; i < quota && waypointOffset + i < static_cast<int>(globalWaypoints.size()); ++i) {
                    sumX += globalWaypoints[static_cast<std::size_t>(waypointOffset + i)].x;
                    sumY += globalWaypoints[static_cast<std::size_t>(waypointOffset + i)].y;
                    ++count;
                }
                if (count > 0) {
                    anchor = {sumX / count, sumY / count};
                }
            } else {
                anchor = patchCells[static_cast<std::size_t>(rng.randomScaledInclusive(0, patchCellCount - 1))];
            }
            if (anchor.x >= 0) {
                int bestDistance = 500000;
                for (std::size_t i = 0; i < patchCells.size(); ++i) {
                    const double distance = euclidean(patchCells[i], anchor);
                    if (distance < static_cast<double>(bestDistance)) {
                        bestDistance = nativeDoubleToInt(distance);
                        specialIndex = static_cast<int>(i);
                    }
                }
            }
        }

        if (!patchCells.empty()) {
            for (std::size_t i = 0; i < patchCells.size(); ++i) {
                std::vector<std::uint32_t> amountRawValues;
                double amountJitter = 0.0;
                do {
                    amountRawValues.clear();
                    amountJitter = nativeX87Chop53Multiply(
                        rng.gaussianWithRawTrace(&amountRawValues),
                        50.0);
                } while (amountJitter < -100.0 || amountJitter > 100.0);
                const int nativeAmount = nativeDoubleToInt(nativeX87Chop53Add(
                    static_cast<double>(regionPatchBase),
                    amountJitter));
                if (nativeAmount < 0) {
                    continue;
                }
                const bool rich = specialRegionPatch && static_cast<int>(i) == specialIndex;
                ++report_.tiberiumRegionSeeds;
                if (rich) {
                    ++report_.tiberiumSpecialPatchSeeds;
                    ++report_.richTiberiumPatches;
                }
                placeTiberiumPatch(
                    map,
                    options,
                    config,
                    rng,
                    patchCells[i],
                    nativeAmount,
                    rich,
                    "region",
                    true,
                    amountJitter,
                    static_cast<int>(amountRawValues.size()),
                    amountRawValues.size() > 0 ? amountRawValues[0] : 0u,
                    amountRawValues.size() > 1 ? amountRawValues[1] : 0u,
                    static_cast<int>(i) + waypointOffset + 1);
            }
        }

        std::vector<Point> waypoints;
        for (int i = 0; i < quota && waypointOffset + i < static_cast<int>(globalWaypoints.size()); ++i) {
            waypoints.push_back(globalWaypoints[static_cast<std::size_t>(waypointOffset + i)]);
        }
        if (!waypoints.empty() && !patchCells.empty()) {
            std::vector<double> averageDistances;
            averageDistances.reserve(waypoints.size());
            double minAverageDistance = std::numeric_limits<double>::max();
            for (Point waypoint : waypoints) {
                double totalDistance = 0.0;
                for (Point seed : patchCells) {
                    totalDistance += euclidean(waypoint, seed);
                }
                const double averageDistance = totalDistance / static_cast<double>(patchCells.size());
                averageDistances.push_back(averageDistance);
                minAverageDistance = std::min(minAverageDistance, averageDistance);
            }

            for (std::size_t i = 0; i < waypoints.size(); ++i) {
                const int nativeAmount = static_cast<int>((averageDistances[i] - minAverageDistance) * 15.0) + 500;
                ++report_.tiberiumWaypointSeeds;
                if (richWaypointPatch) {
                    ++report_.richTiberiumPatches;
                }
                placeTiberiumPatch(
                    map,
                    options,
                    config,
                    rng,
                    waypoints[i],
                    nativeAmount,
                    richWaypointPatch,
                    "waypoint",
                    true,
                    0.0,
                    0,
                    0,
                    0,
                    static_cast<int>(i) + waypointOffset + 1);
            }
        }

        waypointOffset += quota;
    }
}
