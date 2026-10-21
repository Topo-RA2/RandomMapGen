void NativeMapGenerator::generateTerrainType0(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng)
{
    generateTerrainNativeWorkspace(map, options, rng, 0);
}

void NativeMapGenerator::generateTerrainType1(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng)
{
    generateTerrainNativeWorkspace(map, options, rng, 1);
}

void NativeMapGenerator::generateTerrainType2(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng)
{
    generateTerrainNativeWorkspace(map, options, rng, 2);
}

void NativeMapGenerator::generateTerrainNativeWorkspace(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, int terrainType)
{
    ScopedNativeX87Chop scopedRounding;
    const std::uint16_t terrainFunctionId = terrainType == 0
        ? native_trace::GenerateTerrainType0
        : terrainType == 1
        ? native_trace::GenerateTerrainType1
        : native_trace::GenerateTerrainType2;
    const bool terrainTraceEnabled = native_trace::coreFunctionEnabled(terrainFunctionId);
    native_trace::EventData terrainTraceData;
    if (terrainTraceEnabled) {
        const Rect traceVisibleRect = nativeLocalRect(map);
        const Rect traceMapRect = nativeSavedMapRect(map);
        terrainTraceData.args[0] = static_cast<std::uint32_t>(options.mapType);
        terrainTraceData.args[1] = static_cast<std::uint32_t>(options.resources);
        terrainTraceData.args[2] = static_cast<std::uint32_t>(options.waterAmount);
        terrainTraceData.args[3] = static_cast<std::uint32_t>(options.numPlayers);
        terrainTraceData.args[4] = static_cast<std::uint32_t>(traceVisibleRect.w);
        terrainTraceData.args[5] = static_cast<std::uint32_t>(traceVisibleRect.h);
        terrainTraceData.regionCount = traceMapRect.w;
        terrainTraceData.regionCellCount = traceMapRect.h;
        terrainTraceData.resultKind = native_trace::ResultNone;
        native_trace::enterDetailed(
            terrainType == 0 ? 0x59AD10u : terrainType == 1 ? 0x59AFA0u : 0x59B200u,
            terrainFunctionId,
            terrainTraceData);
    }
    struct NativeTerrainCell {
        Point coords;
        int isoTileTypeIndex = kNativeTileWaterBase;
        int height = 0;
        int queuedTag = 0;
        int processedTag = 0;
        int cliffFlagsCache = -1;
        bool active = false;
    };
    struct NativePatchHeapNode {
        Point point;
        float priority = 0.0f;
    };
    struct NativePatchHeap {
        explicit NativePatchHeap(int capacity)
            : capacity(std::max(1, capacity)),
              nodes(static_cast<std::size_t>(std::max(1, capacity))),
              heap(static_cast<std::size_t>(std::max(1, capacity) + 1), -1)
        {
        }

        bool canAllocate() const noexcept
        {
            return allocated < capacity;
        }

        int allocatedCount() const noexcept
        {
            return allocated;
        }

        bool empty() const noexcept
        {
            return heapSize == 0;
        }

        void push(Point point, double priority)
        {
            if (!canAllocate()) {
                return;
            }

            const int nodeIndex = allocated++;
            nodes[static_cast<std::size_t>(nodeIndex)] = {point, static_cast<float>(priority)};
            int slot = heapSize + 1;
            if (slot >= capacity) {
                return;
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
        }

        Point pop()
        {
            if (heapSize <= 0) {
                return {-1, -1};
            }

            const int resultIndex = heap[1];
            lastPopPriority = nodes[static_cast<std::size_t>(resultIndex)].priority;
            heap[1] = heap[static_cast<std::size_t>(heapSize)];
            heap[static_cast<std::size_t>(heapSize)] = -1;
            --heapSize;
            siftDown(1);
            return nodes[static_cast<std::size_t>(resultIndex)].point;
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

        float nodePriority(int slot) const
        {
            const int nodeIndex = heap[static_cast<std::size_t>(slot)];
            return nodes[static_cast<std::size_t>(nodeIndex)].priority;
        }

        int capacity = 0;
        int allocated = 0;
        int heapSize = 0;
        float lastPopPriority = 0.0f;
        std::vector<NativePatchHeapNode> nodes;
        std::vector<int> heap;
    };
    struct NativePatchTrace {
        int index = 0;
        int tag = 0;
        Rect region;
        Point inputSeed;
        Point actualSeed;
        Point anchor;
        int requested = 0;
        int target = 0;
        int returned = 0;
        int committed = 0;
        int unknownBefore = 0;
        int unknownAfter = 0;
        int smoothCalls = 0;
        int restoredWater = 0;
        int clearedWater = 0;
        int shoreCandidates = 0;
        int smoothPlacements = 0;
        int failure = 0;
    };
    struct NativePatchCellTrace {
        int patch = 0;
        const char* event = "";
        Point cell;
        int tileBefore = 0;
        int zoneId = 0;
        int queuedTag = 0;
        int cacheFlags = 0;
        int active = 1;
    };
    struct NativeFloodEventTrace {
        int patch = 0;
        int step = 0;
        const char* event = "";
        Point cell;
        Point noisy;
        int processedCount = 0;
        int heapSize = 0;
        int allocated = 0;
        float priority = 0.0f;
        int tile = 0;
        int queuedTag = 0;
        int processedTag = 0;
    };
    struct NativeNoiseTrace {
        int patch = 0;
        int step = 0;
        const char* event = "";
        double value = 0.0;
        double accum = 0.0;
    };
    struct NativeRecalculateResult {
        int restoredWater = 0;
        int clearedWater = 0;
        int shoreCandidates = 0;
        int smoothCalls = 0;
        int smoothPlacements = 0;
        bool success = true;
    };

    const bool type0 = terrainType == 0;
    const bool type1 = terrainType == 1;
    const int surfaceCount = nativeSurfaceDataCount(map);
    const int nativeWaterAmountForTerrain = std::max(0, options.waterAmount);
    const double baseTargetRatio = type1
        ? (1.0 - static_cast<double>(nativeWaterAmountForTerrain) * 0.01) * (0.5 - 0.45) + 0.45
        : type0
        ? 0.0
        : (1.0 - static_cast<double>(nativeWaterAmountForTerrain) * 0.01) * (0.2 - 0.15) + 0.15;
    const double targetRatio = baseTargetRatio;
    const double patchScale = type1 ? 0.03 : (type0 ? 0.0 : 0.06);
    const int patchSize = std::max(1, nativeDoubleToInt(static_cast<double>(surfaceCount) * patchScale * targetRatio));
    const Rect mapRect = nativeSavedMapRect(map);
    const int nativeMapWidth = mapRect.w;
    const Rect visibleRect = nativeLocalRect(map);
    const int visibleWidth = visibleRect.w;
    const int visibleHeight = visibleRect.h;
    const int workspaceSize = mapRect.w + mapRect.h + 1;
    const int diamondBoundsMin = mapRect.w;
    const int diamondBoundsMax = mapRect.w + 2 * mapRect.h;
    std::vector<NativeTerrainCell> workspace(static_cast<std::size_t>(workspaceSize * workspaceSize));
    std::vector<NativePatchTrace> patchTraces;
    std::vector<NativePatchCellTrace> patchCellTraces;
    std::vector<NativeFloodEventTrace> floodEventTraces;
    std::vector<NativeNoiseTrace> noiseTraces;
    std::vector<Point> nativeCliffPassCells;
    std::vector<Point> nativeSmoothPassCells;
    int nativeIteratorCells = 0;

    auto workspaceInRange = [&](Point cell) {
        return cell.x >= 0 && cell.y >= 0 && cell.x < workspaceSize && cell.y < workspaceSize;
    };
    auto workspaceIndex = [&](Point cell) {
        return static_cast<std::size_t>(cell.y * workspaceSize + cell.x);
    };
    auto insideNativeDiamond = [&](Point cell) {
        if (!workspaceInRange(cell)) {
            return false;
        }
        const int sum = cell.x + cell.y;
        return sum > diamondBoundsMin
            && cell.x - cell.y < diamondBoundsMin
            && cell.y - cell.x < diamondBoundsMin
            && sum <= diamondBoundsMax;
    };
    for (int y = 0; y < workspaceSize; ++y) {
        for (int x = 0; x < workspaceSize; ++x) {
            const Point cell{x, y};
            NativeTerrainCell& native = workspace[workspaceIndex(cell)];
            if (!insideNativeDiamond(cell)) {
                continue;
            }
            native.coords = cell;
            native.active = true;
        }
    }
    auto nativeCellIsActive = [&](Point cell) {
        return workspaceInRange(cell) && workspace[workspaceIndex(cell)].active;
    };
    auto fillWorkspaceTraceResult = [&](
        native_trace::EventData& data,
        int primaryResult,
        std::vector<std::uint32_t>* detail) {
        std::uint64_t hash = kFnvOffset;
        int slotCount = 0;
        if (detail != nullptr) {
            detail->clear();
            detail->reserve(workspace.size() * 7);
        }
        for (int y = 0; y < workspaceSize; ++y) {
            for (int x = 0; x < workspaceSize; ++x) {
                const Point cell{x, y};
                const NativeTerrainCell& native = workspace[workspaceIndex(cell)];
                const std::array<std::uint32_t, 7> values{
                    static_cast<std::uint32_t>(static_cast<std::uint16_t>(x))
                        | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(y)) << 16),
                    static_cast<std::uint32_t>(native.active ? native.isoTileTypeIndex : -1),
                    static_cast<std::uint32_t>(native.active ? native.height : -1),
                    static_cast<std::uint32_t>(native.queuedTag),
                    static_cast<std::uint32_t>(native.processedTag),
                    static_cast<std::uint32_t>(native.cliffFlagsCache),
                    1u,
                };
                for (std::uint32_t value : values) {
                    hashU32(hash, value);
                    if (detail != nullptr) {
                        detail->push_back(value);
                    }
                }
                ++slotCount;
            }
        }
        data.result[0] = static_cast<std::uint32_t>(primaryResult);
        data.result[1] = static_cast<std::uint32_t>(hash);
        data.result[2] = static_cast<std::uint32_t>(hash >> 32);
        data.result[3] = static_cast<std::uint32_t>(slotCount);
        data.resultKind = native_trace::ResultLegacy;
    };
    const char* recalcProfileEnv = std::getenv(kNativeType2RecalcProfileEnv);
    const std::string recalcProfile = recalcProfileEnv != nullptr ? std::string{recalcProfileEnv} : std::string{};
    const bool useIdaRecalcProfile = recalcProfile == "native" || recalcProfile == "ida";
    const char* restoreOrderEnv = std::getenv(kNativeType2CliffRestoreOrderEnv);
    const std::string restoreOrder = restoreOrderEnv != nullptr
        ? std::string(restoreOrderEnv)
        : std::string{"celliterator"};
    const char* smoothFootprintEnv = std::getenv(kNativeType2SmoothFootprintEnv);
    const std::string smoothFootprintMode = smoothFootprintEnv != nullptr ? std::string{smoothFootprintEnv} : std::string{};
    const bool useSmoothFootprint = smoothFootprintMode.empty() || smoothFootprintMode != "0";
    const bool useFullSmoothFootprint = smoothFootprintMode.empty()
        || smoothFootprintMode == "full"
        || smoothFootprintMode == "all"
        || smoothFootprintMode == "native";
    const char* placeCliffCompatEnv = std::getenv(kNativeType2PlaceCliffCompatEnv);
    const std::string placeCliffCompatMode = placeCliffCompatEnv != nullptr ? std::string{placeCliffCompatEnv} : std::string{};
    const bool usePlaceCliffCompat = placeCliffCompatMode.empty() || placeCliffCompatMode != "0";
    const char* emptyNeighborModeEnv = std::getenv(kNativeType2EmptyNeighborModeEnv);
    const std::string emptyNeighborMode = emptyNeighborModeEnv != nullptr ? std::string{emptyNeighborModeEnv} : std::string{};
    auto appendCellIteratorSequence = [&](std::vector<Point>& cells, bool localOnly) {
        int nextX = 1;
        int nextY = mapRect.w;
        int currentY = mapRect.w - 1;
        const int maxIterations = std::max(1, workspaceSize * workspaceSize * 2);
        for (int i = 0; i < maxIterations; ++i) {
            Point current{nextX, nextY};
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

            if (localOnly) {
                if (contains(mapRect, current)) {
                    cells.push_back(current);
                }
                continue;
            }

            if (!insideNativeDiamond(current)) {
                break;
            }
            cells.push_back(current);
        }
    };
    if (restoreOrder == "display" || restoreOrder == "local") {
        for (int row = 0; row < mapRect.h; ++row) {
            for (int dx = 0; dx < mapRect.w * 2 - 1; ++dx) {
                const Point cellCoords = nativeDisplayToIso(dx, row, mapRect.w);
                if (insideNativeDiamond(cellCoords) && (restoreOrder != "local" || contains(mapRect, cellCoords))) {
                    nativeCliffPassCells.push_back(cellCoords);
                }
            }
        }
    } else if (restoreOrder == "celliterator") {
        appendCellIteratorSequence(nativeCliffPassCells, false);
    } else if (restoreOrder == "celliterator-local") {
        appendCellIteratorSequence(nativeCliffPassCells, true);
    } else {
        for (int y = 0; y < workspaceSize; ++y) {
            for (int x = 0; x < workspaceSize; ++x) {
                const Point cell{x, y};
                if (insideNativeDiamond(cell)) {
                    nativeCliffPassCells.push_back(cell);
                }
            }
        }
    }
    appendCellIteratorSequence(nativeSmoothPassCells, false);
    nativeIteratorCells = static_cast<int>(nativeSmoothPassCells.size());
    if (const char* smoothCells = std::getenv(kNativeType2SmoothRngCellsEnv);
        smoothCells != nullptr && smoothCells[0] != '\0') {
        nativeIteratorCells = std::max(0, std::atoi(smoothCells));
    }
    auto nativeCellIsWet = [&](Point cell) {
        if (!nativeCellIsActive(cell)) {
            return false;
        }
        const int tile = workspace[workspaceIndex(cell)].isoTileTypeIndex;
        return (tile >= kNativeTileWaterBase && tile < kNativeTileWaterBase + 14)
            || (tile >= kNativeTileShoreBase && tile < kNativeTileShoreBase + 42);
    };
    auto nativeCellIsWater = [&](Point cell) {
        if (!nativeCellIsActive(cell)) {
            return false;
        }
        const int tile = workspace[workspaceIndex(cell)].isoTileTypeIndex;
        return tile >= kNativeTileWaterBase && tile < kNativeTileWaterBase + 14;
    };
    auto nativeCellIsBlank = [&](Point cell) {
        if (!nativeCellIsActive(cell)) {
            return false;
        }
        const int tile = workspace[workspaceIndex(cell)].isoTileTypeIndex;
        return tile == kNativeTileClear || tile == 0xFFFF;
    };
    auto nativeCellIsShoreRange = [&](Point cell) {
        if (!nativeCellIsActive(cell)) {
            return false;
        }
        const int tile = workspace[workspaceIndex(cell)].isoTileTypeIndex;
        return tile >= kNativeTileShoreBase && tile < kNativeTileShoreBase + 42;
    };
    static constexpr std::array<Point, 4> kNativeDefaultCardinalOffsets{
        Point{0, -1},
        Point{1, 0},
        Point{0, 1},
        Point{-1, 0},
    };
    std::array<Point, 4> nativeCardinalOffsets = kNativeDefaultCardinalOffsets;
    if (const char* cardinalOffsetsEnv = std::getenv(kNativeType2CardinalOffsetsEnv);
        cardinalOffsetsEnv != nullptr && cardinalOffsetsEnv[0] != '\0') {
        const std::string offsets = cardinalOffsetsEnv;
        if (offsets == "diag") {
            nativeCardinalOffsets = {{
                Point{-1, -1},
                Point{1, -1},
                Point{1, 1},
                Point{-1, 1},
            }};
        } else {
            std::array<Point, 4> parsed{};
            const int parsedCount = std::sscanf(
                cardinalOffsetsEnv,
                "%d,%d;%d,%d;%d,%d;%d,%d",
                &parsed[0].x,
                &parsed[0].y,
                &parsed[1].x,
                &parsed[1].y,
                &parsed[2].x,
                &parsed[2].y,
                &parsed[3].x,
                &parsed[3].y);
            if (parsedCount == 8) {
                nativeCardinalOffsets = parsed;
            }
        }
    }
    constexpr int kNativeFlagNorthWest = 0x40;
    constexpr int kNativeFlagNorth = 0x80;
    constexpr int kNativeFlagNorthEast = 0x01;
    constexpr int kNativeFlagWest = 0x20;
    constexpr int kNativeFlagEast = 0x02;
    constexpr int kNativeFlagSouthWest = 0x10;
    constexpr int kNativeFlagSouth = 0x08;
    constexpr int kNativeFlagSouthEast = 0x04;
    static constexpr std::array<std::pair<Point, int>, 8> kNativeWaterFlagOffsets{{
        {Point{-1, -1}, kNativeFlagNorthWest},
        {Point{0, -1}, kNativeFlagNorth},
        {Point{1, -1}, kNativeFlagNorthEast},
        {Point{-1, 0}, kNativeFlagWest},
        {Point{1, 0}, kNativeFlagEast},
        {Point{-1, 1}, kNativeFlagSouthWest},
        {Point{0, 1}, kNativeFlagSouth},
        {Point{1, 1}, kNativeFlagSouthEast},
    }};
    auto nativeWaterFlagsForBlankCell = [&](Point cell) {
        if (!nativeCellIsBlank(cell)) {
            return 0;
        }

        int flags = 0;
        for (const auto& [offset, flag] : kNativeWaterFlagOffsets) {
            if (nativeCellIsWater({cell.x + offset.x, cell.y + offset.y})) {
                flags |= flag;
            }
        }
        return flags;
    };
    auto nativeWaterFlagsForAnyCell = [&](Point cell) {
        if (!nativeCellIsActive(cell)) {
            return 0;
        }

        const bool currentIsWater = nativeCellIsWater(cell);
        int flags = 0;
        for (const auto& [offset, flag] : kNativeWaterFlagOffsets) {
            const Point neighbor{cell.x + offset.x, cell.y + offset.y};
            bool emptyNeighborActsAsWater = false;
            if (currentIsWater && !nativeCellIsActive(neighbor)) {
                if (emptyNeighborMode == "zero" || emptyNeighborMode == "none") {
                    emptyNeighborActsAsWater = false;
                } else if (emptyNeighborMode == "bounded") {
                    emptyNeighborActsAsWater = workspaceInRange(neighbor);
                } else {
                    emptyNeighborActsAsWater = true;
                }
            }
            if (nativeCellIsWater(neighbor) || emptyNeighborActsAsWater) {
                flags |= flag;
            }
        }
        return flags;
    };
    auto nativeCliffSlopeFlags = [&](Point cell, int mode) {
        if (!nativeCellIsActive(cell)) {
            return 0;
        }
        if (mode == 0 && !nativeCellIsBlank(cell)) {
            return 0;
        }
        if (mode == 1) {
            const int tile = workspace[workspaceIndex(cell)].isoTileTypeIndex;
            if (tile >= kNativeTileWaterBase && tile < kNativeTileWaterBase + 14) {
                return 0;
            }
        }

        const int cached = workspace[workspaceIndex(cell)].cliffFlagsCache;
        if (cached >= 0) {
            return cached;
        }
        return nativeWaterFlagsForAnyCell(cell);
    };
    auto shouldRestoreNativeWaterAfterCliffLevel = [&](Point cell) {
        const int flags = nativeCliffSlopeFlags(cell, 0);
        if (flags == 0) {
            return false;
        }

        bool forceWater = flags == 11 || flags == 26;
        forceWater = forceWater
            || ((flags & (kNativeFlagNorth | kNativeFlagWest)) == (kNativeFlagNorth | kNativeFlagWest)
                && (flags & (kNativeFlagNorthEast | kNativeFlagSouthWest)) == 0)
            || ((flags & (kNativeFlagNorth | kNativeFlagEast)) == (kNativeFlagNorth | kNativeFlagEast)
                && (flags & (kNativeFlagNorthWest | kNativeFlagSouthEast)) == 0)
            || ((flags & (kNativeFlagSouth | kNativeFlagEast)) == (kNativeFlagSouth | kNativeFlagEast)
                && (flags & (kNativeFlagNorthEast | kNativeFlagSouthWest)) == 0)
            || ((flags & (kNativeFlagSouth | kNativeFlagWest)) == (kNativeFlagSouth | kNativeFlagWest)
                && (flags & (kNativeFlagNorthWest | kNativeFlagSouthEast)) == 0);

        if ((flags & kNativeFlagWest) != 0) {
            forceWater = forceWater
                || (nativeCliffSlopeFlags({cell.x + 1, cell.y}, 0) & kNativeFlagEast) != 0
                || (nativeCliffSlopeFlags({cell.x + 2, cell.y}, 0) & kNativeFlagEast) != 0;
        }
        if ((flags & kNativeFlagEast) != 0) {
            forceWater = forceWater
                || (nativeCliffSlopeFlags({cell.x - 1, cell.y}, 0) & kNativeFlagWest) != 0
                || (nativeCliffSlopeFlags({cell.x - 2, cell.y}, 0) & kNativeFlagWest) != 0;
        }
        if ((flags & kNativeFlagSouth) != 0) {
            forceWater = forceWater
                || (nativeCliffSlopeFlags({cell.x, cell.y - 1}, 0) & kNativeFlagNorth) != 0
                || (nativeCliffSlopeFlags({cell.x, cell.y - 2}, 0) & kNativeFlagNorth) != 0;
        }
        if ((flags & kNativeFlagNorth) != 0) {
            forceWater = forceWater
                || (nativeCliffSlopeFlags({cell.x, cell.y + 1}, 0) & kNativeFlagSouth) != 0
                || (nativeCliffSlopeFlags({cell.x, cell.y + 2}, 0) & kNativeFlagSouth) != 0;
        }

        return ((flags & (kNativeFlagNorth | kNativeFlagSouth)) == (kNativeFlagNorth | kNativeFlagSouth))
            || ((flags & (kNativeFlagEast | kNativeFlagWest)) == (kNativeFlagEast | kNativeFlagWest))
            || forceWater;
    };
    auto nativeSmoothMode1Flags = [&](Point cell) {
        return nativeCliffSlopeFlags(cell, 1);
    };
    auto nativeSmoothRunLength = [&](Point cell, int direction) {
        int run = 1;
        Point land;
        Point water;
        Point step;
        switch (direction) {
        case kNativeFlagSouth:
            land = {cell.x + 1, cell.y};
            water = {cell.x + 1, cell.y + 1};
            step = {1, 0};
            break;
        case kNativeFlagEast:
            land = {cell.x, cell.y + 1};
            water = {cell.x + 1, cell.y + 1};
            step = {0, 1};
            break;
        case kNativeFlagWest:
            land = {cell.x, cell.y + 1};
            water = {cell.x - 1, cell.y + 1};
            step = {0, 1};
            break;
        case kNativeFlagNorth:
            land = {cell.x + 1, cell.y};
            water = {cell.x + 1, cell.y - 1};
            step = {1, 0};
            break;
        default:
            return run;
        }

        while (!nativeCellIsWater(land) && nativeCellIsWater(water)) {
            ++run;
            land = {land.x + step.x, land.y + step.y};
            water = {water.x + step.x, water.y + step.y};
        }
        return run;
    };
    auto nativeSmoothPieceForFlags = [&](Point cell, int pass, int flags, int roll) {
        if (flags == 0) {
            return 0;
        }

        if (pass == 2) {
            if ((flags & (kNativeFlagNorth | kNativeFlagWest)) == (kNativeFlagNorth | kNativeFlagWest)) {
                if ((flags & (kNativeFlagNorthEast | kNativeFlagSouthWest)) == (kNativeFlagNorthEast | kNativeFlagSouthWest)) {
                    return (roll & 1) + 23;
                }
                return (flags & kNativeFlagNorthEast) != 0 ? 21 : 30;
            }
            if ((flags & (kNativeFlagNorth | kNativeFlagEast)) == (kNativeFlagNorth | kNativeFlagEast)) {
                if ((flags & (kNativeFlagNorthWest | kNativeFlagSouthEast)) == (kNativeFlagNorthWest | kNativeFlagSouthEast)) {
                    return (roll & 1) + 15;
                }
                return (flags & kNativeFlagSouthEast) != 0 ? 14 : 22;
            }
            if ((flags & (kNativeFlagSouth | kNativeFlagEast)) == (kNativeFlagSouth | kNativeFlagEast)) {
                if ((flags & (kNativeFlagNorthEast | kNativeFlagSouthWest)) == (kNativeFlagNorthEast | kNativeFlagSouthWest)) {
                    return (roll & 1) + 7;
                }
                return (flags & kNativeFlagNorthEast) != 0 ? 13 : 6;
            }
            if ((flags & (kNativeFlagSouth | kNativeFlagWest)) != (kNativeFlagSouth | kNativeFlagWest)) {
                return 0;
            }
            if ((flags & (kNativeFlagNorthWest | kNativeFlagSouthEast)) == (kNativeFlagNorthWest | kNativeFlagSouthEast)) {
                return (roll & 1) + 31;
            }
            return (flags & kNativeFlagSouthEast) != 0 ? 5 : 29;
        }

        if ((flags & kNativeFlagEast) != 0) {
            const int run = nativeSmoothRunLength(cell, kNativeFlagEast);
            if (((run & 1) != 0 && (flags & kNativeFlagNorth) == 0)
                || (flags & kNativeFlagSouthEast) == 0
                || (flags & (kNativeFlagSouth | kNativeFlagSouthWest)) != 0) {
                return 12;
            }
            return roll % 3 + 9;
        }
        if ((flags & kNativeFlagWest) != 0) {
            const int run = nativeSmoothRunLength(cell, kNativeFlagWest);
            if (((run & 1) != 0 && (flags & kNativeFlagNorth) == 0)
                || (flags & kNativeFlagSouthWest) == 0
                || (flags & (kNativeFlagSouth | kNativeFlagSouthEast)) != 0) {
                return 28;
            }
            return roll % 3 + 25;
        }
        if ((flags & kNativeFlagSouth) != 0) {
            const int run = nativeSmoothRunLength(cell, kNativeFlagSouth);
            if ((run & 1) != 0
                || (flags & kNativeFlagSouthEast) == 0
                || (flags & (kNativeFlagNorthEast | kNativeFlagEast)) != 0) {
                return 4;
            }
            return roll % 3 + 1;
        }
        if ((flags & kNativeFlagNorth) != 0) {
            const int run = nativeSmoothRunLength(cell, kNativeFlagNorth);
            if ((run & 1) != 0
                || (flags & kNativeFlagNorthEast) == 0
                || (flags & (kNativeFlagEast | kNativeFlagSouthEast)) != 0) {
                return 20;
            }
            return roll % 3 + 17;
        }
        if ((flags & kNativeFlagNorthEast) != 0) {
            return (roll & 1) + 35;
        }
        if ((flags & kNativeFlagSouthEast) != 0) {
            return (roll & 1) + 33;
        }
        if ((flags & kNativeFlagSouthWest) != 0) {
            return (roll & 1) + 39;
        }
        if ((flags & kNativeFlagNorthWest) != 0) {
            return (roll & 1) + 37;
        }
        return 0;
    };
    auto nativeTemplateAnchorForSmoothCell = [&](Point cell, int piece, int flags) -> Point {
        (void)flags;
        static constexpr std::array<Point, 43> kNativeSmoothTemplateOffsets{{
            Point{0, 0},
            Point{0, -1},
            Point{0, -1},
            Point{0, -1},
            Point{0, -1},
            Point{0, -2},
            Point{-1, -2},
            Point{-1, -1},
            Point{-1, -1},
            Point{-1, 0},
            Point{-1, 0},
            Point{-1, 0},
            Point{-1, 0},
            Point{-2, -1},
            Point{-2, 0},
            Point{-1, 0},
            Point{-1, 0},
            Point{0, 0},
            Point{0, 0},
            Point{0, 0},
            Point{0, 0},
            Point{0, 0},
            Point{-1, 0},
            Point{0, 0},
            Point{0, 0},
            Point{0, 0},
            Point{0, 0},
            Point{0, 0},
            Point{0, 0},
            Point{0, -1},
            Point{0, 0},
            Point{0, -1},
            Point{0, -1},
            Point{-1, -1},
            Point{-1, -1},
            Point{-1, 0},
            Point{-1, 0},
            Point{0, 0},
            Point{0, 0},
            Point{0, -1},
            Point{0, -1},
            Point{0, 0},
            Point{0, 0},
        }};
        const Point offset = kNativeSmoothTemplateOffsets[static_cast<std::size_t>(std::clamp(piece, 0, 42))];
        return {cell.x + offset.x, cell.y + offset.y};
    };
    auto nativeTemporaryShoreIndex = [](int tile) noexcept {
        const int index = tile - kNativeTileShoreBase;
        return index >= 0 && index < static_cast<int>(kNativePlaceCliffBandCompat.size()) ? index : -1;
    };
    auto nativeBandCompatWouldFail = [&](int currentTile, int newTile) {
        if (!usePlaceCliffCompat) {
            return false;
        }

        const int currentIndex = nativeTemporaryShoreIndex(currentTile);
        const int newIndex = nativeTemporaryShoreIndex(newTile);
        if (currentIndex < 0 || newIndex < 0) {
            return false;
        }

        const int diff = std::abs(kNativePlaceCliffBandCompat[static_cast<std::size_t>(currentIndex)]
            - kNativePlaceCliffBandCompat[static_cast<std::size_t>(newIndex)]);
        return diff >= 3 && diff <= 5;
    };
    auto traceNativeSmoothPlaceEvent = [&](int patchIndex, const char* event, Point target, const NativeTerrainCell& native, int flags) {
        patchCellTraces.push_back({
            patchIndex,
            event,
            target,
            native.isoTileTypeIndex,
            native.processedTag,
            native.queuedTag,
            flags,
            native.active ? 1 : 0,
        });
    };
    auto placeNativeSmoothTemplate = [&](Point cell, int piece, int pass, int flags, int patchIndex) {
        const int allowedMin = pass == 1 ? kNativeTileClear : kNativeTileShoreBase;
        const int allowedMax = pass == 1 ? kNativeTileClear : kNativeTileShoreBase + 41;
        const int tile = kNativeTileShoreBase + piece - 1;
        const Point anchor = nativeTemplateAnchorForSmoothCell(cell, piece, flags);
        int written = 0;
        const int width = shoreTemplateWidth(piece);
        const int height = shoreTemplateHeight(piece);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const char slot = shoreTemplateSlot(piece, x, y);
                if (slot == '.' || (!useFullSmoothFootprint && slot != 'L')) {
                    continue;
                }
                const Point target{anchor.x + x, anchor.y + y};
                if (!nativeCellIsActive(target)) {
                    continue;
                }
                NativeTerrainCell& native = workspace[workspaceIndex(target)];
                if (nativeBandCompatWouldFail(native.isoTileTypeIndex, tile)) {
                    traceNativeSmoothPlaceEvent(patchIndex, "smooth_fail_band_compat", target, native, flags);
                    return std::pair<bool, int>{false, written};
                }
                const bool canWrite = nativeCellIsBlank(target)
                    || (native.isoTileTypeIndex >= allowedMin && native.isoTileTypeIndex <= allowedMax);
                if (!canWrite) {
                    if (usePlaceCliffCompat) {
                        traceNativeSmoothPlaceEvent(patchIndex, "smooth_blocked_nonfatal", target, native, flags);
                    }
                    return std::pair<bool, int>{true, written};
                }
                if (native.isoTileTypeIndex != tile) {
                    native.isoTileTypeIndex = tile;
                    native.height = y * width + x;
                    ++written;
                }
            }
        }
        return std::pair<bool, int>{true, written};
    };
    auto clearNativeShoreRange = [&]() {
        int cleared = 0;
        for (Point cell : nativeSmoothPassCells) {
            if (!nativeCellIsShoreRange(cell)) {
                continue;
            }
            NativeTerrainCell& native = workspace[workspaceIndex(cell)];
            native.isoTileTypeIndex = kNativeTileClear;
            native.height = 0;
            ++cleared;
        }
        return cleared;
    };
    std::function<void(const NativePatchTrace&, const char*)> dumpPatchStateIfRequested =
        [](const NativePatchTrace&, const char*) {};
    auto recalculateNativeShoreRange = [&](bool clearShoreToBlank, int tag, int patchIndex, const NativePatchTrace* trace) {
        NativeRecalculateResult result;
        for (NativeTerrainCell& native : workspace) {
            native.cliffFlagsCache = -1;
        }

        int restorePasses = 1;
        if (const char* restorePassEnv = std::getenv(kNativeType2CliffRestorePassesEnv);
            restorePassEnv != nullptr && restorePassEnv[0] != '\0') {
            restorePasses = std::max(0, std::atoi(restorePassEnv));
        }
        static constexpr std::array<Point, 8> kNativeAdjacentOffsets{
            Point{0, -1},
            Point{1, -1},
            Point{1, 0},
            Point{1, 1},
            Point{0, 1},
            Point{-1, 1},
            Point{-1, 0},
            Point{-1, -1},
        };
        auto restoreCellAndNeighborsStack = [&](Point start) {
            bool restored = false;
            std::vector<Point> pending{start};
            while (!pending.empty()) {
                const Point cell = pending.back();
                pending.pop_back();
                workspace[workspaceIndex(cell)].cliffFlagsCache = nativeCliffSlopeFlags(cell, 2);
                if (!shouldRestoreNativeWaterAfterCliffLevel(cell)) {
                    continue;
                }

                NativeTerrainCell& native = workspace[workspaceIndex(cell)];
                if (native.isoTileTypeIndex == kNativeTileWaterBase) {
                    continue;
                }
                const int tileBefore = native.isoTileTypeIndex;
                patchCellTraces.push_back({
                    patchIndex,
                    "restore",
                    cell,
                    tileBefore,
                    native.processedTag,
                    native.queuedTag,
                    native.cliffFlagsCache,
                    1,
                });
                native.isoTileTypeIndex = kNativeTileWaterBase;
                native.height = 0;
                native.processedTag = 0;
                ++result.restoredWater;
                restored = true;

                for (Point offset : kNativeAdjacentOffsets) {
                    const Point next{cell.x + offset.x, cell.y + offset.y};
                    if (nativeCellIsActive(next)) {
                        workspace[workspaceIndex(next)].cliffFlagsCache = -1;
                        pending.push_back(next);
                    }
                }
            }
            return restored;
        };
        std::function<bool(Point)> restoreCellAndNeighborsRecursive = [&](Point cell) {
            workspace[workspaceIndex(cell)].cliffFlagsCache = nativeCliffSlopeFlags(cell, 2);
            if (!shouldRestoreNativeWaterAfterCliffLevel(cell)) {
                return false;
            }

            NativeTerrainCell& native = workspace[workspaceIndex(cell)];
            if (native.isoTileTypeIndex == kNativeTileWaterBase) {
                return false;
            }
            const int tileBefore = native.isoTileTypeIndex;
            patchCellTraces.push_back({
                patchIndex,
                "restore",
                cell,
                tileBefore,
                native.processedTag,
                native.queuedTag,
                native.cliffFlagsCache,
                1,
            });
            native.isoTileTypeIndex = kNativeTileWaterBase;
            native.height = 0;
            native.processedTag = 0;
            ++result.restoredWater;

            for (Point offset : kNativeAdjacentOffsets) {
                const Point next{cell.x + offset.x, cell.y + offset.y};
                if (!nativeCellIsActive(next)) {
                    continue;
                }
                workspace[workspaceIndex(next)].cliffFlagsCache = -1;
                (void)restoreCellAndNeighborsRecursive(next);
            }
            return true;
        };
        const char* restoreAlgorithmEnv = std::getenv(kNativeType2CliffRestoreAlgorithmEnv);
        const bool useRecursiveRestore = restoreAlgorithmEnv != nullptr
            ? std::string{restoreAlgorithmEnv} == "recursive"
            : true;
        for (int pass = 0; pass < restorePasses; ++pass) {
            bool changed = false;
            for (Point cell : nativeCliffPassCells) {
                changed = (useRecursiveRestore ? restoreCellAndNeighborsRecursive(cell) : restoreCellAndNeighborsStack(cell))
                    || changed;
            }
            if (!changed) {
                break;
            }
        }
        if (trace != nullptr) {
            dumpPatchStateIfRequested(*trace, "after_calculate_cliff_level");
        }

        const char* processCliffHeightEnv = std::getenv(kNativeType2ProcessCliffHeightEnv);
        const std::string processCliffHeightMode = processCliffHeightEnv != nullptr ? std::string{processCliffHeightEnv} : std::string{};
        const bool processCliffHeightEnabled = processCliffHeightMode.empty()
            || processCliffHeightMode == "1"
            || processCliffHeightMode == "fresh"
            || (useIdaRecalcProfile && processCliffHeightMode != "0");
        if (processCliffHeightEnabled) {
            static constexpr std::array<int, 8> kProcessCliffCellHeightWaterFlags{
                199, 124, 241, 31, 198, 108, 177, 27,
            };
            for (Point cell : nativeCliffPassCells) {
                NativeTerrainCell& native = workspace[workspaceIndex(cell)];
                if (native.isoTileTypeIndex < kNativeTileWaterBase || native.isoTileTypeIndex >= kNativeTileWaterBase + 12) {
                    continue;
                }

                const int flags = processCliffHeightMode == "fresh"
                    ? nativeWaterFlagsForAnyCell(cell)
                    : (native.cliffFlagsCache >= 0 ? native.cliffFlagsCache : nativeWaterFlagsForAnyCell(cell));
                if (std::find(kProcessCliffCellHeightWaterFlags.begin(), kProcessCliffCellHeightWaterFlags.end(), flags)
                    == kProcessCliffCellHeightWaterFlags.end()) {
                    continue;
                }

                const int tileBefore = native.isoTileTypeIndex;
                patchCellTraces.push_back({
                    patchIndex,
                    "clear",
                    cell,
                    tileBefore,
                    native.processedTag,
                    native.queuedTag,
                    native.cliffFlagsCache,
                    1,
                });
                native.isoTileTypeIndex = 0xFFFF;
                native.height = 0;
                if (tag != 0) {
                    native.processedTag = tag;
                }
                ++result.clearedWater;
                for (Point offset : kNativeAdjacentOffsets) {
                    const Point next{cell.x + offset.x, cell.y + offset.y};
                    if (nativeCellIsActive(next)) {
                        workspace[workspaceIndex(next)].cliffFlagsCache = -1;
                        workspace[workspaceIndex(next)].cliffFlagsCache = nativeCliffSlopeFlags(next, 2);
                    }
                }
            }
        }
        if (trace != nullptr) {
            dumpPatchStateIfRequested(*trace, "after_process_cliff_cell_height");
        }

        if (useSmoothFootprint) {
            for (int pass = 1; pass <= 2; ++pass) {
                if (result.success) {
                    for (Point cell : nativeSmoothPassCells) {
                        const int roll = rng.randomScaledInclusive(0, 5);
                        ++result.smoothCalls;
                        const int flags = pass == 1 ? nativeCliffSlopeFlags(cell, 0) : nativeSmoothMode1Flags(cell);
                        const int piece = nativeSmoothPieceForFlags(cell, pass, flags, roll);
                        if (piece <= 0) {
                            continue;
                        }

                        const auto [placed, written] = placeNativeSmoothTemplate(cell, piece, pass, flags, patchIndex);
                        result.smoothPlacements += written;
                        if (!placed) {
                            result.success = false;
                            break;
                        }
                    }
                }
                if (trace != nullptr) {
                    dumpPatchStateIfRequested(*trace, pass == 1 ? "after_smooth_pass1" : "after_smooth_pass2");
                }
            }
            if (clearShoreToBlank) {
                result.shoreCandidates = clearNativeShoreRange();
                if (trace != nullptr) {
                    dumpPatchStateIfRequested(*trace, "after_clear_temp_shore");
                }
            }
            return result;
        }

        std::vector<Point> shoreCells;
        shoreCells.reserve(workspace.size() / 16);
        for (int y = 0; y < workspaceSize; ++y) {
            for (int x = 0; x < workspaceSize; ++x) {
                const Point cell{x, y};
                if (nativeCliffSlopeFlags(cell, 0) == 0) {
                    continue;
                }
                shoreCells.push_back(cell);
            }
        }

        for (Point cell : shoreCells) {
            workspace[workspaceIndex(cell)].isoTileTypeIndex = kNativeTileShoreBase;
            workspace[workspaceIndex(cell)].height = 0;
        }
        if (trace != nullptr) {
            dumpPatchStateIfRequested(*trace, "after_shore_candidates");
        }
        if (!clearShoreToBlank) {
            result.shoreCandidates = static_cast<int>(shoreCells.size());
            return result;
        }
        for (Point cell : shoreCells) {
            if (nativeCellIsShoreRange(cell)) {
                NativeTerrainCell& native = workspace[workspaceIndex(cell)];
                native.isoTileTypeIndex = kNativeTileClear;
                native.height = 0;
            }
        }
        result.shoreCandidates = static_cast<int>(shoreCells.size());
        if (trace != nullptr) {
            dumpPatchStateIfRequested(*trace, "after_clear_temp_shore");
        }
        return result;
    };
    auto consumeNativeCliffSmoothRandoms = [&]() {
        int calls = 0;
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < nativeIteratorCells; ++i) {
                (void)rng.randomScaledInclusive(0, 5);
                ++calls;
            }
        }
        return calls;
    };
    auto nativeCellCenterForRect = [&](Rect rect) {
        return Point{
            rect.x + rect.h / 2 + rect.w / 2 + rect.y + 1,
            rect.y + nativeMapWidth + rect.h / 2 - rect.w / 2 - rect.x,
        };
    };
    auto seedDistance = [&](Point cell, Point center, Rect rect) {
        double xScale = 1.0;
        double yScale = 1.0;
        if (rect.h <= rect.w) {
            yScale = static_cast<double>(rect.w) / std::max(1.0, static_cast<double>(rect.h)) * 1.2;
        } else {
            xScale = static_cast<double>(rect.h) / std::max(1.0, static_cast<double>(rect.w)) * 1.2;
        }
        return nativeDoubleToInt(static_cast<double>(std::abs(cell.x - center.x)) * xScale
            + static_cast<double>(std::abs(cell.y - center.y)) * yScale);
    };
    auto projectWorkspaceToMap = [&](bool preserveNativePostpassTiles = false) {
        report_.isoTileOverrides.clear();
        report_.isoTileOverrides.reserve(workspace.size());
        for (const NativeTerrainCell& native : workspace) {
            if (!native.active) {
                continue;
            }
            Terrain projectedTerrain = Terrain::Land;
            int projectedVariant = 0;
            if (native.isoTileTypeIndex >= kNativeTileShoreBase && native.isoTileTypeIndex < kNativeTileShoreBase + 42) {
                projectedTerrain = Terrain::Shore;
                projectedVariant = native.isoTileTypeIndex - kNativeTileShoreBase;
            } else if (native.isoTileTypeIndex == nativeGreenTileBase()) {
                projectedTerrain = Terrain::Green;
            } else if (native.isoTileTypeIndex >= kNativeTileWaterBase && native.isoTileTypeIndex < kNativeTileWaterBase + 14) {
                projectedTerrain = Terrain::Water;
                projectedVariant = native.isoTileTypeIndex - kNativeTileWaterBase;
            } else {
                projectedTerrain = Terrain::Land;
            }
            report_.isoTileOverrides.push_back({
                native.coords,
                projectedTerrain,
                projectedVariant,
                native.height,
                4,
                false,
            });
        }
        for (auto& cell : map.cells()) {
            const Point mapPoint{cell.x + visibleRect.x, cell.y + visibleRect.y};
            const Point nativeCell = toNativeCellCoords(mapPoint, nativeMapWidth);
            if (!insideNativeDiamond(nativeCell)) {
                cell.terrain = Terrain::Water;
                cell.tempTag = 0;
                cell.height = 0;
                cell.level = 4;
                cell.slope = -1;
                continue;
            }

            const NativeTerrainCell& native = workspace[workspaceIndex(nativeCell)];
            Terrain projectedTerrain = Terrain::Land;
            int projectedVariant = 0;
            if (preserveNativePostpassTiles && native.isoTileTypeIndex >= kNativeTileShoreBase && native.isoTileTypeIndex < kNativeTileShoreBase + 42) {
                cell.terrain = Terrain::Shore;
                projectedTerrain = Terrain::Shore;
                projectedVariant = native.isoTileTypeIndex - kNativeTileShoreBase;
            } else if (preserveNativePostpassTiles && native.isoTileTypeIndex == nativeGreenTileBase()) {
                cell.terrain = Terrain::Green;
                projectedTerrain = Terrain::Green;
            } else if (nativeCellIsWet(nativeCell)) {
                cell.terrain = Terrain::Water;
                projectedTerrain = Terrain::Water;
                projectedVariant = native.isoTileTypeIndex >= kNativeTileWaterBase && native.isoTileTypeIndex < kNativeTileWaterBase + 14
                    ? native.isoTileTypeIndex - kNativeTileWaterBase
                    : 0;
            } else {
                cell.terrain = Terrain::Land;
                projectedTerrain = Terrain::Land;
            }
            cell.tempTag = native.processedTag != 0 ? native.processedTag : native.queuedTag;
            cell.height = std::clamp(native.height, 0, 255);
            cell.level = 4;
            cell.slope = -1;
        }
    };
    auto dumpWorkspaceIfRequested = [&]() {
        const char* dumpPath = std::getenv(kNativeType2WorkspaceDumpPathEnv);
        if (dumpPath == nullptr || dumpPath[0] == '\0') {
            return;
        }

        std::filesystem::path path{dumpPath};
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream out(path, std::ios::binary);
        out << "index,x,y,mapX,mapY,isoTileTypeIndex,queuedTag,processedTag,active\n";
        int index = 0;
        for (int y = 0; y < workspaceSize; ++y) {
            for (int x = 0; x < workspaceSize; ++x) {
                const Point cellCoords{x, y};
                if (!nativeCellIsActive(cellCoords)) {
                    continue;
                }
                const Point mapPoint = nativeCellToMapPoint(cellCoords, nativeMapWidth);
                const NativeTerrainCell& native = workspace[workspaceIndex(cellCoords)];
                out << index++ << ','
                    << cellCoords.x << ','
                    << cellCoords.y << ','
                    << mapPoint.x << ','
                    << mapPoint.y << ','
                    << native.isoTileTypeIndex << ','
                    << native.queuedTag << ','
                    << native.processedTag << ','
                    << (native.active ? 1 : 0) << '\n';
            }
        }
    };
    auto nativeType2PatchCellDumpPath = [](const std::filesystem::path& patchPath) {
        std::filesystem::path cellPath = patchPath;
        const std::filesystem::path extension = cellPath.extension();
        if (!extension.empty()) {
            cellPath.replace_extension(std::filesystem::path{".cells"}.concat(extension.native()));
        } else {
            cellPath += ".cells.csv";
        }
        return cellPath;
    };
    auto nativeType2PatchFloodDumpPath = [](const std::filesystem::path& patchPath) {
        std::filesystem::path floodPath = patchPath;
        const std::filesystem::path extension = floodPath.extension();
        if (!extension.empty()) {
            floodPath.replace_extension(std::filesystem::path{".flood"}.concat(extension.native()));
        } else {
            floodPath += ".flood.csv";
        }
        return floodPath;
    };
    auto nativeType2PatchNoiseDumpPath = [](const std::filesystem::path& patchPath) {
        std::filesystem::path noisePath = patchPath;
        const std::filesystem::path extension = noisePath.extension();
        if (!extension.empty()) {
            noisePath.replace_extension(std::filesystem::path{".noise"}.concat(extension.native()));
        } else {
            noisePath += ".noise.csv";
        }
        return noisePath;
    };
    auto dumpPatchTracesIfRequested = [&]() {
        const char* dumpPath = std::getenv(kNativeType2PatchDumpPathEnv);
        if (dumpPath == nullptr || dumpPath[0] == '\0') {
            return;
        }

        std::filesystem::path path{dumpPath};
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream out(path, std::ios::binary);
        out << "index,regionX,regionY,regionW,regionH,inputSeedX,inputSeedY,actualSeedX,actualSeedY,"
               "anchorX,anchorY,"
               "requested,target,returned,committed,unknownBefore,unknownAfter,smoothCalls,"
               "restoredWater,clearedWater,shoreCandidates,smoothPlacements,failure\n";
        for (const NativePatchTrace& trace : patchTraces) {
            out << trace.index << ','
                << trace.region.x << ','
                << trace.region.y << ','
                << trace.region.w << ','
                << trace.region.h << ','
                << trace.inputSeed.x << ','
                << trace.inputSeed.y << ','
                << trace.actualSeed.x << ','
                << trace.actualSeed.y << ','
                << trace.anchor.x << ','
                << trace.anchor.y << ','
                << trace.requested << ','
                << trace.target << ','
                << trace.returned << ','
                << trace.committed << ','
                << trace.unknownBefore << ','
                << trace.unknownAfter << ','
                << trace.smoothCalls << ','
                << trace.restoredWater << ','
                << trace.clearedWater << ','
                << trace.shoreCandidates << ','
                << trace.smoothPlacements << ','
                << trace.failure << '\n';
        }

        std::filesystem::path cellPath = nativeType2PatchCellDumpPath(path);
        if (!cellPath.parent_path().empty()) {
            std::filesystem::create_directories(cellPath.parent_path());
        }
        std::ofstream cellOut(cellPath, std::ios::binary);
        cellOut << "patch,event,x,y,tileBefore,zoneId,queuedTag,cacheFlags,active\n";
        for (const NativePatchCellTrace& trace : patchCellTraces) {
            cellOut << trace.patch << ','
                    << (trace.event != nullptr ? trace.event : "") << ','
                    << trace.cell.x << ','
                    << trace.cell.y << ','
                    << trace.tileBefore << ','
                    << trace.zoneId << ','
                    << trace.queuedTag << ','
                    << trace.cacheFlags << ','
                    << trace.active << '\n';
        }

        std::filesystem::path floodPath = nativeType2PatchFloodDumpPath(path);
        if (!floodPath.parent_path().empty()) {
            std::filesystem::create_directories(floodPath.parent_path());
        }
        std::ofstream floodOut(floodPath, std::ios::binary);
        floodOut << "patch,step,event,x,y,noisyX,noisyY,processedCount,heapSize,allocated,priority,priorityBits,"
                    "tile,queuedTag,processedTag\n";
        for (const NativeFloodEventTrace& trace : floodEventTraces) {
            std::uint32_t priorityBits = 0;
            static_assert(sizeof(priorityBits) == sizeof(trace.priority));
            std::memcpy(&priorityBits, &trace.priority, sizeof(priorityBits));
            floodOut << trace.patch << ','
                     << trace.step << ','
                     << (trace.event != nullptr ? trace.event : "") << ','
                     << trace.cell.x << ','
                     << trace.cell.y << ','
                     << trace.noisy.x << ','
                     << trace.noisy.y << ','
                     << trace.processedCount << ','
                     << trace.heapSize << ','
                     << trace.allocated << ','
                     << trace.priority << ','
                     << priorityBits << ','
                     << trace.tile << ','
                     << trace.queuedTag << ','
                     << trace.processedTag << '\n';
        }

        std::filesystem::path noisePath = nativeType2PatchNoiseDumpPath(path);
        if (!noisePath.parent_path().empty()) {
            std::filesystem::create_directories(noisePath.parent_path());
        }
        std::ofstream noiseOut(noisePath, std::ios::binary);
        noiseOut << "patch,step,event,value,valueBits,accum,accumBits\n";
        for (const NativeNoiseTrace& trace : noiseTraces) {
            std::uint64_t valueBits = 0;
            std::uint64_t accumBits = 0;
            static_assert(sizeof(valueBits) == sizeof(trace.value));
            static_assert(sizeof(accumBits) == sizeof(trace.accum));
            std::memcpy(&valueBits, &trace.value, sizeof(valueBits));
            std::memcpy(&accumBits, &trace.accum, sizeof(accumBits));
            noiseOut << trace.patch << ','
                     << trace.step << ','
                     << (trace.event != nullptr ? trace.event : "") << ','
                     << trace.value << ','
                     << valueBits << ','
                     << trace.accum << ','
                     << accumBits << '\n';
        }
    };
    auto nativeTileCategory = [](int tile) -> const char* {
        if (tile == kNativeTileClear || tile == 0xFFFF || tile == -1) {
            return "blank";
        }
        if (tile >= kNativeTileCliffBase && tile < kNativeTileShoreBase) {
            return "cliff";
        }
        if (tile >= kNativeTileShoreBase && tile < kNativeTileRoughBase) {
            return "shore";
        }
        if (tile >= kNativeTileRoughBase && tile < kNativeTileWaterBase) {
            return "rough";
        }
        if (tile >= kNativeTileWaterBase && tile < kNativeTileWaterBase + 14) {
            return "water";
        }
        if (tile >= kNativeTileDirtBase && tile < kNativeTileDirtBase + 17) {
            return "dirtlat";
        }
        if (isNativeGreenTileIndex(tile)) {
            return "greenlat";
        }
        if (tile >= kNativeTilePaveBase && tile < kNativeTilePaveBase + 17) {
            return "pavelat";
        }
        return "other";
    };
    std::ofstream patchStateOut;
    if (const char* dumpPath = std::getenv(kNativeType2PatchStateDumpPathEnv);
        dumpPath != nullptr && dumpPath[0] != '\0') {
        std::filesystem::path path{dumpPath};
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        patchStateOut.open(path, std::ios::binary);
        patchStateOut << "patch,stage,regionX,regionY,regionW,regionH,inputSeedX,inputSeedY,"
                         "actualSeedX,actualSeedY,index,x,y,mapX,mapY,isoTileTypeIndex,category,"
                         "queuedTag,processedTag,cacheFlags,wet,water,blank,active\n";
    }
    dumpPatchStateIfRequested = [&](const NativePatchTrace& trace, const char* stage) {
        if (!patchStateOut.is_open()) {
            return;
        }

        int index = 0;
        for (int y = 0; y < workspaceSize; ++y) {
            for (int x = 0; x < workspaceSize; ++x) {
                const Point cellCoords{x, y};
                if (!nativeCellIsActive(cellCoords)) {
                    continue;
                }
                const Point mapPoint = nativeCellToMapPoint(cellCoords, nativeMapWidth);
                const NativeTerrainCell& native = workspace[workspaceIndex(cellCoords)];
                patchStateOut << trace.index << ','
                    << (stage != nullptr ? stage : "") << ','
                    << trace.region.x << ','
                    << trace.region.y << ','
                    << trace.region.w << ','
                    << trace.region.h << ','
                    << trace.inputSeed.x << ','
                    << trace.inputSeed.y << ','
                    << trace.actualSeed.x << ','
                    << trace.actualSeed.y << ','
                    << index++ << ','
                    << cellCoords.x << ','
                    << cellCoords.y << ','
                    << mapPoint.x << ','
                    << mapPoint.y << ','
                    << native.isoTileTypeIndex << ','
                    << nativeTileCategory(native.isoTileTypeIndex) << ','
                    << native.queuedTag << ','
                    << native.processedTag << ','
                    << native.cliffFlagsCache << ','
                    << (nativeCellIsWet(cellCoords) ? 1 : 0) << ','
                    << (nativeCellIsWater(cellCoords) ? 1 : 0) << ','
                    << (nativeCellIsBlank(cellCoords) ? 1 : 0) << ','
                    << (native.active ? 1 : 0) << '\n';
            }
        }
    };
    auto floodNativePatch = [&](
        Point seedNative,
        Point anchorNative,
        Rect region,
        int requestedCells,
        int tag,
        double noiseScale,
        bool distanceToRegion) {
        ++report_.floodFillAttempts;

        const bool floodTraceEnabled = native_trace::coreFunctionEnabled(native_trace::FloodFillTerrainPatch);
        native_trace::EventData floodTraceData;
        if (floodTraceEnabled) {
            const std::uint64_t noiseScaleBits = std::bit_cast<std::uint64_t>(noiseScale);
            floodTraceData.args[0] = static_cast<std::uint32_t>(requestedCells);
            floodTraceData.args[1] = static_cast<std::uint32_t>(static_cast<std::uint16_t>(seedNative.x))
                | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(seedNative.y)) << 16);
            floodTraceData.args[2] = static_cast<std::uint32_t>(static_cast<std::uint16_t>(anchorNative.x))
                | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(anchorNative.y)) << 16);
            floodTraceData.args[3] = static_cast<std::uint32_t>(static_cast<std::uint16_t>(region.x))
                | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(region.y)) << 16);
            floodTraceData.args[4] = static_cast<std::uint32_t>(static_cast<std::uint16_t>(region.w))
                | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(region.h)) << 16);
            floodTraceData.args[5] = 1;
            floodTraceData.regionIndex = distanceToRegion ? 1 : 0;
            floodTraceData.regionId = static_cast<std::int32_t>(noiseScaleBits);
            floodTraceData.regionCount = static_cast<std::int32_t>(noiseScaleBits >> 32);
            floodTraceData.regionCellCount = tag;
            floodTraceData.cellX = static_cast<std::int16_t>(seedNative.x);
            floodTraceData.cellY = static_cast<std::int16_t>(seedNative.y);
            floodTraceData.resultKind = native_trace::ResultNone;
            native_trace::enterDetailed(0x59BBC0, native_trace::FloodFillTerrainPatch, floodTraceData);
        }
        auto traceFloodReturn = [&](int result) {
            if (!floodTraceEnabled) {
                return;
            }
            std::vector<std::uint32_t> detail;
            std::vector<std::uint32_t>* detailPtr = native_trace::detailRequested() ? &detail : nullptr;
            fillWorkspaceTraceResult(floodTraceData, result, detailPtr);
            native_trace::exitDetailed(
                0x59BBC0,
                native_trace::FloodFillTerrainPatch,
                floodTraceData,
                native_trace::DetailOrderedTerrainWorkspace,
                detail);
        };

        int target = std::max(1, requestedCells);
        if (target <= kMinimumNativePatchSize) {
            target = kMinimumNativePatchSize;
            ++report_.floodFillMinimumClamps;
        }
        NativePatchTrace trace;
        trace.index = static_cast<int>(patchTraces.size()) + 1;
        trace.tag = tag;
        trace.region = region;
        trace.inputSeed = seedNative;
        trace.actualSeed = seedNative;
        trace.anchor = anchorNative;
        trace.requested = requestedCells;
        trace.target = target;
        trace.unknownBefore = tag;
        trace.unknownAfter = tag;
        const int patchIndex = trace.index;

        if (seedNative.x == 0 && seedNative.y == 0) {
            bool foundRandomSeed = false;
            for (int attempts = 1; attempts < 200; ++attempts) {
                const int x = nativeDoubleToInt(rng.randomUnit() * static_cast<double>(mapRect.w));
                const int y = nativeDoubleToInt(rng.randomUnit() * static_cast<double>(mapRect.h));
                const Point candidate = toNativeCellCoords({x, y}, nativeMapWidth);
                if (!insideNativeDiamond(candidate)) {
                    continue;
                }

                const NativeTerrainCell& native = workspace[workspaceIndex(candidate)];
                if (native.processedTag == 0 && nativeCellIsWet(candidate)) {
                    seedNative = candidate;
                    foundRandomSeed = true;
                    break;
                }
            }
            trace.actualSeed = seedNative;

            if (!foundRandomSeed) {
                ++report_.floodFillFailures;
                trace.failure = 1;
                patchTraces.push_back(trace);
                traceFloodReturn(0);
                return 0;
            }
        } else if (!insideNativeDiamond(seedNative)) {
            ++report_.floodFillFailures;
            trace.failure = 2;
            patchTraces.push_back(trace);
            traceFloodReturn(0);
            return 0;
        }
        dumpPatchStateIfRequested(trace, "before_patch");

        const int maxQueued = std::max(100, target * 8 + 2);
        NativePatchHeap heap(maxQueued);
        int processed = 0;
        int committed = 0;
        double noisyX = static_cast<double>(seedNative.x);
        double noisyY = static_cast<double>(seedNative.y);
        int noiseRawCalls = 0;
        int floodStep = 0;
        auto traceFloodEvent = [&](const char* event, Point cell, Point noisy, int processedCount, float priority) {
            if (!nativeCellIsActive(cell)) {
                return;
            }
            const NativeTerrainCell& native = workspace[workspaceIndex(cell)];
            floodEventTraces.push_back({
                patchIndex,
                ++floodStep,
                event,
                cell,
                noisy,
                processedCount,
                heap.heapSize,
                heap.allocatedCount(),
                priority,
                native.isoTileTypeIndex,
                native.queuedTag,
                native.processedTag,
            });
        };

        workspace[workspaceIndex(seedNative)].queuedTag = tag;
        heap.push(seedNative, 0.0);
        Point currentPoint = heap.pop();

        while (currentPoint.x != -1 && processed < target) {
            const Point point = currentPoint;
            ++processed;

            NativeTerrainCell& native = workspace[workspaceIndex(point)];
            native.processedTag = tag;
            traceFloodEvent("commit", point, {}, processed - 1, heap.lastPopPriority);
            native.isoTileTypeIndex = kNativeTileClear;
            native.height = 0;
            ++committed;

            const Point noisyNative{
                nativeDoubleToInt(noisyX + 0.5),
                nativeDoubleToInt(noisyY + 0.5),
            };
            for (Point offset : nativeCardinalOffsets) {
                if (!heap.canAllocate()) {
                    continue;
                }

                const Point next{point.x + offset.x, point.y + offset.y};
                if (!nativeCellIsWet(next)) {
                    continue;
                }

                NativeTerrainCell& candidate = workspace[workspaceIndex(next)];
                if (candidate.processedTag != 0 || candidate.queuedTag == tag || !isInsideNativeEllipseCell(next, region, nativeMapWidth)) {
                    continue;
                }

                double priority = 0.0;
                if (distanceToRegion) {
                    priority = nativeRegionDistanceCells(noisyNative, next, anchorNative, region);
                } else {
                    const int dx = next.x - noisyNative.x;
                    const int dy = next.y - noisyNative.y;
                    const std::uint32_t priorityRaw = rng.random();
                    priority = nativePatchedSqrt(static_cast<double>(dx * dx + dy * dy))
                        + nativeHeapPriorityNoise(priorityRaw);
                }
                const float storedPriority = nativeDoubleToFloatChop(priority);
                traceFloodEvent("enqueue", next, noisyNative, processed - 1, storedPriority);
                candidate.queuedTag = tag;
                heap.push(next, storedPriority);
            }

            const int noiseStep = processed;
            noiseTraces.push_back({patchIndex, noiseStep, "x_before", 0.0, static_cast<double>(noisyX)});
            noiseTraces.push_back({patchIndex, noiseStep, "y_before", 0.0, static_cast<double>(noisyY)});
            std::vector<std::uint32_t> rawGaussianValues;
            const double gaussianX = rng.gaussianWithRawTrace(&rawGaussianValues);
            const double gaussianY = rng.gaussianWithRawTrace(&rawGaussianValues);
            for (std::uint32_t raw : rawGaussianValues) {
                noiseTraces.push_back({patchIndex, ++noiseRawCalls, "rng", static_cast<double>(raw), 0.0});
            }
            noisyX = nativeX87Chop53Add(
                noisyX,
                nativeX87Chop53Multiply(gaussianX, noiseScale));
            noisyY = nativeX87Chop53Add(
                noisyY,
                nativeX87Chop53Multiply(gaussianY, noiseScale));

            currentPoint = heap.pop();
        }

        while (!heap.empty()) {
            const Point point = heap.pop();
            ++processed;

            NativeTerrainCell& native = workspace[workspaceIndex(point)];
            if (nativeCellIsWet(point)) {
                traceFloodEvent("fringe_commit", point, {}, processed - 1, 0.0f);
                native.processedTag = tag;
                native.isoTileTypeIndex = kNativeTileClear;
                native.height = 0;
                ++committed;
                ++report_.floodFillFringeCells;
            }
        }

        trace.returned = processed;
        trace.committed = committed;
        dumpPatchStateIfRequested(trace, "after_flood");
        const NativeRecalculateResult recalculate = recalculateNativeShoreRange(true, tag, patchIndex, &trace);
        if (!useSmoothFootprint) {
            trace.smoothCalls = consumeNativeCliffSmoothRandoms();
        } else {
            trace.smoothCalls = recalculate.smoothCalls;
        }
        trace.restoredWater = recalculate.restoredWater;
        trace.clearedWater = recalculate.clearedWater;
        trace.shoreCandidates = recalculate.shoreCandidates;
        trace.smoothPlacements = recalculate.smoothPlacements;
        if (!recalculate.success) {
            for (NativeTerrainCell& native : workspace) {
                if (native.processedTag != tag) {
                    continue;
                }
                native.processedTag = 0;
                native.isoTileTypeIndex = kNativeTileWaterBase;
                native.height = 0;
            }
            ++report_.floodFillFailures;
            trace.failure = 3;
            trace.returned = 0;
            dumpPatchStateIfRequested(trace, "after_recalc");
            patchTraces.push_back(trace);
            traceFloodReturn(0);
            return 0;
        }
        dumpPatchStateIfRequested(trace, "after_recalc");
        ++report_.terrainPatches;
        trace.unknownAfter = tag + 1;
        patchTraces.push_back(trace);
        traceFloodReturn(processed);
        return processed;
    };

    int tag = 1;
    if (type0) {
        const int minimumPatches = std::max(2, options.numPlayers / 2);
        const int extraPatches = rng.randomScaledInclusive(1, minimumPatches);
        const int requestedRegions = extraPatches + options.numPlayers;
        const std::vector<Rect> regions = subdivideRegion(visibleRect, requestedRegions, rng);
        for (Rect region : regions) {
            if (region.w <= 0 || region.h <= 0) {
                continue;
            }

            const int area = 2 * region.w * region.h;
            const std::uint32_t targetRaw = rng.random();
            const int requested = nativeDoubleToInt(
                ((static_cast<double>(targetRaw) * kNativeUInt32Unit * 0.05) + 0.45)
                * static_cast<double>(area));
            const Point seed = nativeCellCenterForRect(region);
            for (int tries = 0; tries < 10; ++tries) {
                const int patchReturned = floodNativePatch(seed, seed, region, requested, tag, 0.25, false);
                ++tag;
                if (patchReturned > 0) {
                    break;
                }
            }
        }
    } else {
        std::vector<Rect> regions;
        std::vector<Point> regionCenters;
        if (type1) {
            regions.push_back({
                visibleRect.x + 1,
                visibleRect.y + 1,
                std::max(1, visibleWidth - 2),
                std::max(1, visibleHeight - 2),
            });
            const int centerAxis = mapRect.h / 2 + mapRect.w / 2;
            regionCenters.push_back({centerAxis + 1, centerAxis});
        } else if (rng.randomUnit() >= 0.5) {
            const int halfHeight = visibleHeight / 2 - 1;
            regions.push_back({visibleRect.x, visibleRect.y, visibleWidth, halfHeight});
            regions.push_back({visibleRect.x, visibleRect.y + visibleHeight / 2 + 1, visibleWidth, halfHeight});
        } else {
            const int halfWidth = visibleWidth / 2 - 1;
            regions.push_back({visibleRect.x, visibleRect.y, halfWidth, visibleHeight});
            regions.push_back({visibleRect.x + visibleWidth / 2 + 1, visibleRect.y, halfWidth, visibleHeight});
        }
        if (!type1) {
            for (Rect region : regions) {
                regionCenters.push_back(nativeCellCenterForRect(region));
            }
        }

        int globalAttempts = 0;
        for (std::size_t regionIndex = 0; regionIndex < regions.size(); ++regionIndex) {
            const Rect region = regions[regionIndex];
            if (region.w <= 0 || region.h <= 0) {
                continue;
            }

            int filled = 0;
            double filledRatio = 0.0;
            const Point regionCenter = regionCenters[regionIndex];
            Point seed = regionCenter;
            while (filledRatio < targetRatio) {
                if (globalAttempts >= 100) {
                    break;
                }

                int requested = nativeDoubleToInt((targetRatio - filledRatio) * static_cast<double>(surfaceCount));
                if (requested >= patchSize) {
                    requested = patchSize;
                }
                const int patchReturned = floodNativePatch(seed, regionCenter, region, requested, tag, 0.75, true);
                filled += patchReturned;
                if (patchReturned > 0) {
                    ++tag;
                }
                ++globalAttempts;

                double best = 1.0e9;
                Point nextSeed{0, 0};
                if (type1) {
                    for (Point cell : nativeCliffPassCells) {
                        if (!nativeCellIsActive(cell)) {
                            continue;
                        }
                        const NativeTerrainCell& record = workspace[workspaceIndex(cell)];
                        if (record.queuedTag != 0) {
                            continue;
                        }
                        const double dist = static_cast<double>(
                            std::abs(cell.x - regionCenter.x) + std::abs(cell.y - regionCenter.y));
                        if (dist < best) {
                            best = dist;
                            nextSeed = cell;
                        }
                    }
                } else {
                    const int sumMin = nativeMapWidth + 2 * region.y + 1;
                    const int sumMax = sumMin + 2 * region.h + 2;
                    const int diffMin = 2 * region.x - nativeMapWidth + 1;
                    const int diffMax = diffMin + 2 * region.w + 2;
                    for (const NativeTerrainCell& record : workspace) {
                        const Point cell = record.coords;
                        if (cell.x == 0 && cell.y == 0) {
                            continue;
                        }
                        const int sum = cell.x + cell.y;
                        const int diff = cell.x - cell.y;
                        if (sum < sumMin || sum > sumMax || diff < diffMin || diff > diffMax) {
                            continue;
                        }
                        if (record.queuedTag != 0) {
                            continue;
                        }
                        if (!isInsideNativeEllipseCell(cell, region, nativeMapWidth)) {
                            continue;
                        }
                        const double dist = seedDistance(cell, regionCenter, region);
                        if (dist < best) {
                            best = dist;
                            nextSeed = cell;
                        }
                    }
                }
                filledRatio = static_cast<double>(filled) / static_cast<double>(surfaceCount);
                seed = nextSeed;
            }
        }
    }

    if (terrainTraceEnabled) {
        std::vector<std::uint32_t> detail;
        std::vector<std::uint32_t>* detailPtr = native_trace::detailRequested() ? &detail : nullptr;
        fillWorkspaceTraceResult(terrainTraceData, 0, detailPtr);
        native_trace::exitDetailed(
            terrainType == 0 ? 0x59AD10u : terrainType == 1 ? 0x59AFA0u : 0x59B200u,
            terrainFunctionId,
            terrainTraceData,
            native_trace::DetailOrderedTerrainWorkspace,
            detail);
    }

    std::vector<Point> isolatedWaterCells;
    for (Point cell : nativeCliffPassCells) {
        if (!insideNativeDiamond(cell) || workspace[workspaceIndex(cell)].isoTileTypeIndex != kNativeTileWaterBase) {
            continue;
        }

        ++report_.terrainPostWaterChecks;
        bool cardinalNeighborsAreBlank = true;
        for (Point offset : nativeCardinalOffsets) {
            const Point neighbor{cell.x + offset.x, cell.y + offset.y};
            if (!nativeCellIsBlank(neighbor)) {
                cardinalNeighborsAreBlank = false;
                break;
            }
        }
        if (cardinalNeighborsAreBlank) {
            isolatedWaterCells.push_back(cell);
        }
    }
    for (Point cell : isolatedWaterCells) {
        workspace[workspaceIndex(cell)].isoTileTypeIndex = kNativeTileClear;
        workspace[workspaceIndex(cell)].height = 0;
        ++report_.terrainPostWaterCleared;
    }

    const NativeRecalculateResult finalRecalculate = recalculateNativeShoreRange(false, 0, 0, nullptr);
    if (!useSmoothFootprint) {
        (void)consumeNativeCliffSmoothRandoms();
    }
    (void)finalRecalculate;

    std::vector<Point> greenCells;
    for (Point cell : nativeCliffPassCells) {
        if (!nativeCellIsShoreRange(cell)) {
            continue;
        }

        bool hasCandidate = false;
        for (Point offset : nativeCardinalOffsets) {
            const Point neighbor{cell.x + offset.x, cell.y + offset.y};
            ++report_.terrainPostShoreCandidates;
            if (nativeCellIsBlank(neighbor)) {
                greenCells.push_back(neighbor);
                hasCandidate = true;
            }
        }
        if (hasCandidate) {
            ++report_.terrainPostShoreAnchors;
        }
    }
    for (Point cell : greenCells) {
        if (nativeCellIsBlank(cell)) {
            workspace[workspaceIndex(cell)].isoTileTypeIndex = nativeGreenTileBase();
            workspace[workspaceIndex(cell)].height = 0;
            ++report_.terrainPostShoreTiles;
        }
    }

    dumpWorkspaceIfRequested();
    dumpPatchTracesIfRequested();
    projectWorkspaceToMap(true);

    for (NativeTerrainCell& native : workspace) {
        native.queuedTag = -1;
        native.processedTag = -1;
    }

    nativeMapgenCacheFlags_.clear();
    nativeMapgenCacheFlags_.reserve(nativeCliffPassCells.size());
    for (Point cell : nativeCliffPassCells) {
        nativeMapgenCacheFlags_[nativeIsoKey(cell)] = workspace[workspaceIndex(cell)].cliffFlagsCache;
    }

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
        std::vector<std::uint32_t> detail;
        std::vector<std::uint32_t>* detailPtr = native_trace::detailRequested() ? &detail : nullptr;
        fillWorkspaceTraceResult(traceData, 0, detailPtr);
        native_trace::exitDetailed(
            0x59A6C0,
            native_trace::GenerateTerrain,
            traceData,
            native_trace::DetailOrderedTerrainWorkspace,
            detail);
    }
}
