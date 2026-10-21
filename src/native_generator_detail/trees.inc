int NativeMapGenerator::placeTreePatch(MapGrid& map, NativeRandomizer& rng, Point center, int targetTrees, double placementChance)
{
    if (!map.inBounds(center) || targetTrees <= 0 || placementChance <= 0.0) {
        return 0;
    }

    const int tag = 600000 + report_.treePatchHeapPushes + 1;
    auto canTraverseCandidate = [&](Point p, bool countMetrics) {
        if (countMetrics) {
            ++report_.treePatchCandidateChecks;
        }
        const Cell& cell = map.at(p);
        if (cell.tempTag == tag || !canTraverseTreePatchCell(cell)) {
            if (countMetrics) {
                ++report_.treePatchRejectedCells;
            }
            return false;
        }
        return true;
    };

    if (!canTraverseCandidate(center, false)) {
        return 0;
    }

    struct TreeNode {
        Point point;
        double priority = 0.0;

        bool operator>(const TreeNode& rhs) const noexcept
        {
            return priority > rhs.priority;
        }
    };

    std::priority_queue<TreeNode, std::vector<TreeNode>, std::greater<TreeNode>> heap;
    map.at(center).tempTag = tag;
    heap.push({center, 0.0});
    ++report_.treePatchHeapPushes;
    ++report_.treePatchTaggedCells;

    const int maxCandidates = std::max(1, targetTrees * 25);
    int taggedThisPatch = 1;
    int placed = 0;
    while (!heap.empty() && placed < targetTrees && taggedThisPatch < maxCandidates) {
        const Point p = heap.top().point;
        heap.pop();
        ++report_.treePatchHeapPops;

        Cell& cell = map.at(p);
        ++report_.treePatchPlacementChecks;
        if (canPlaceTreePatchCell(cell) && rng.randomUnit() < placementChance) {
            const int treeValue = rng.randomRanged(1, 25);
            cell.overlay = Overlay::Tree;
            cell.overlayValue = treeValue;
            upsertNativeObjectOverride(report_.nativeObjectOverrides, sourcePointToNativeCell(map, p), 36, 13 + treeValue);
            ++placed;
        } else {
            ++report_.treePatchRejectedPlacements;
        }

        for (Point next : map.neighbors8(p)) {
            if (!canTraverseCandidate(next, true)) {
                continue;
            }
            if (taggedThisPatch >= maxCandidates) {
                ++report_.treePatchRejectedCells;
                continue;
            }
            map.at(next).tempTag = tag;
            heap.push({next, euclidean(next, center) + rng.randomUnit() * 5.0});
            ++report_.treePatchHeapPushes;
            ++report_.treePatchTaggedCells;
            ++taggedThisPatch;
        }
    }

    clearTags(map, tag);
    if (placed > 0) {
        ++report_.heapTreeClusters;
        report_.heapTreeCells += placed;
    }
    return placed;
}

int NativeMapGenerator::placeTreePatchNative(MapGrid& map, NativeRandomizer& rng, Point center, int targetTrees, double placementChance)
{
    const Rect mapRect = nativeSavedMapRect(map);
    if (!isInsideNativeCellDiamond(center, mapRect) || targetTrees <= 0 || placementChance <= 0.0) {
        return 0;
    }

    report_.nativeObjectOverrides.reserve(report_.nativeObjectOverrides.size() + static_cast<std::size_t>(targetTrees));
    std::unordered_map<std::uint64_t, const IsoTileOverride*> overrideByIso;
    overrideByIso.reserve(report_.isoTileOverrides.size());
    for (const IsoTileOverride& override : report_.isoTileOverrides) {
        overrideByIso[nativeIsoKey(override.iso)] = &override;
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

    auto nativeSurfaceBlocked = [&](Point nativeCell) {
        const std::uint64_t key = nativeIsoKey(nativeCell);
        return nativeSpawnExclusionKeys_.find(key) != nativeSpawnExclusionKeys_.end()
            || nativeCliffShoreExclusionKeys_.find(key) != nativeCliffShoreExclusionKeys_.end();
    };
    auto nativePlacementProjection = [&](Point nativeCell, int* tile, int* overlay, int* object, int* landType, int* slope) {
        const std::uint64_t key = nativeIsoKey(nativeCell);
        const auto overrideIt = overrideByIso.find(key);
        const IsoTileOverride* isoOverride = overrideIt == overrideByIso.end() ? nullptr : overrideIt->second;
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        const Cell* cell = map.inBounds(source) ? &map.at(source) : nullptr;
        *tile = isoOverride ? nativeTileTypeIndexForOverride(*isoOverride) : (cell ? nativeTileTypeIndexForTerrain(*cell) : kNativeTileWaterBase);
        *overlay = cell ? nativeOverlayTypeIndexForCell(*cell) : -1;
        if (cell && (cell->overlay == Overlay::Ore || cell->overlay == Overlay::Gems) && !report_.nativeOverlayOverrides.empty()) {
            *overlay = -1;
        }
        *object = cell ? nativeObjectAbstractTypeForCell(*cell) : -1;
        *slope = isoOverride ? isoOverride->slopeIndex : (cell ? cell->slope : 0);
        if (cell && (cell->overlay == Overlay::Tree || cell->overlay == Overlay::Tech) && !report_.nativeObjectOverrides.empty()) {
            *object = -1;
        }
        *landType = isoOverride ? isoOverride->landType : (cell ? cell->landType : 0);
        if (const auto overlayIt = overlayByIso.find(key); overlayIt != overlayByIso.end()) {
            *overlay = overlayIt->second->overlayTypeIndex;
            if (overlayIt->second->landTypeOverride >= 0) {
                *landType = overlayIt->second->landTypeOverride;
            }
        }
        if (const auto objectIt = objectByIso.find(key); objectIt != objectByIso.end()) {
            *object = objectIt->second->objectAbstractType;
        }
    };
    int centerTile = kNativeTileWaterBase;
    int centerOverlay = -1;
    int centerObject = -1;
    int centerLandType = 0;
    int centerSlope = 0;
    nativePlacementProjection(center, &centerTile, &centerOverlay, &centerObject, &centerLandType, &centerSlope);
    ++g_nativeSurfaceTreePatchIndex;
    appendNativeSurfaceTrace(
        "tree_patch_begin",
        g_nativeSurfaceTreePatchIndex,
        center,
        0,
        placementChance,
        0.0,
        1,
        targetTrees,
        0,
        centerTile,
        centerOverlay,
        centerLandType,
        centerObject,
        -1,
        0,
        0);

    struct TreeNode {
        Point point;
        float priority = 0.0f;
    };
    struct TreeHeap {
        explicit TreeHeap(int capacity)
            : capacity(std::max(1, capacity)),
              nodes(static_cast<std::size_t>(std::max(1, capacity))),
              heap(static_cast<std::size_t>(std::max(1, capacity) + 1), -1)
        {
        }

        bool empty() const noexcept
        {
            return heapSize == 0;
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
                return {-1, -1};
            }

            const int resultIndex = heap[1];
            const Point result = nodes[static_cast<std::size_t>(resultIndex)].point;
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
        std::vector<TreeNode> nodes;
        std::vector<int> heap;
    };

    const int maxCandidates = std::max(1, targetTrees * 25);
    nativeTreeCandidateKeys_.reserve(nativeTreeCandidateKeys_.size() + static_cast<std::size_t>(maxCandidates));
    TreeHeap heap(maxCandidates);
    const std::uint64_t centerKey = nativeIsoKey(center);
    nativeTreeCandidateKeys_.insert(centerKey);
    heap.push(center, 0.0);
    ++report_.treePatchHeapPushes;
    ++report_.treePatchTaggedCells;

    int placed = 0;
    while (!heap.empty() && placed < targetTrees && heap.allocated < maxCandidates) {
        const Point nativeCell = heap.pop();
        ++report_.treePatchHeapPops;

        int tile = kNativeTileWaterBase;
        int overlay = -1;
        int object = -1;
        int landType = 0;
        int slope = 0;
        nativePlacementProjection(nativeCell, &tile, &overlay, &object, &landType, &slope);
        ++report_.treePatchPlacementChecks;
        if (isNativeClearTileIndex(tile) && overlay == -1 && object == -1 && landType != 3) {
            const std::uint32_t rollRaw = rng.random();
            const double rollValue = static_cast<double>(rollRaw) * kNativeUInt32Unit;
            appendNativeSurfaceTrace(
                "tree_roll",
                g_nativeSurfaceTreePatchIndex,
                nativeCell,
                rollRaw,
                rollValue,
                placementChance,
                rollValue < placementChance ? 1 : 0,
                0,
                placed,
                tile,
                overlay,
                landType,
                object,
                -1,
                heap.allocated,
                heap.heapSize);
            if (rollValue >= placementChance) {
                ++report_.treePatchRejectedPlacements;
            } else {
            const int treeValue = rng.randomScaledInclusive(1, 25);
            upsertNativeObjectOverride(report_.nativeObjectOverrides, nativeCell, 36, 13 + treeValue);
            objectByIso[nativeIsoKey(nativeCell)] = &report_.nativeObjectOverrides.back();
            const Point source = nativeCellToSourcePoint(map, nativeCell);
            if (map.inBounds(source)) {
                Cell& cell = map.at(source);
                cell.overlay = Overlay::Tree;
                cell.overlayValue = treeValue;
            }
            appendNativeSurfaceTrace(
                "tree_place",
                g_nativeSurfaceTreePatchIndex,
                nativeCell,
                0,
                0.0,
                0.0,
                1,
                0,
                placed + 1,
                tile,
                overlay,
                landType,
                object,
                13 + treeValue,
                0,
                0,
                "TREE" + std::to_string(treeValue / 10) + std::to_string(treeValue % 10));
            ++placed;
            }
        } else {
            ++report_.treePatchRejectedPlacements;
        }

        constexpr std::array<Point, 8> kNativeNeighbors{{
            {0, -1}, {1, -1}, {1, 0}, {1, 1},
            {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
        }};
        for (Point offset : kNativeNeighbors) {
            const Point next{nativeCell.x + offset.x, nativeCell.y + offset.y};
            ++report_.treePatchCandidateChecks;
            const std::uint64_t key = nativeIsoKey(next);
            if (!isInsideNativeCellDiamond(next, mapRect)
                || nativeTreeCandidateKeys_.find(key) != nativeTreeCandidateKeys_.end()
                || nativeSurfaceBlocked(next)
                || heap.allocated >= maxCandidates) {
                ++report_.treePatchRejectedCells;
                continue;
            }

            nativeTreeCandidateKeys_.insert(key);
            const double dx = static_cast<double>(center.x - next.x);
            const double dy = static_cast<double>(center.y - next.y);
            const std::uint32_t raw = rng.random();
            const float priorityFloat = nativeDoubleToFloatChop(
                nativeSurfaceHeapPriority(nativePatchedSqrt(dx * dx + dy * dy), raw));
            int priorityBits = 0;
            std::memcpy(&priorityBits, &priorityFloat, sizeof(priorityBits));
            const int parentPacked = (nativeCell.y << 16) | (nativeCell.x & 0xFFFF);
            appendNativeSurfaceTrace(
                "tree_enqueue",
                g_nativeSurfaceTreePatchIndex,
                next,
                raw,
                static_cast<double>(priorityFloat),
                0.0,
                1,
                heap.capacity,
                heap.allocated,
                -1,
                -1,
                0,
                0,
                priorityBits,
                parentPacked,
                heap.heapSize);
            if (heap.push(next, static_cast<double>(priorityFloat))) {
                ++report_.treePatchHeapPushes;
            }
            ++report_.treePatchTaggedCells;
        }
    }

    if (placed > 0) {
        ++report_.heapTreeClusters;
        report_.heapTreeCells += placed;
    }
    return placed;
}

void NativeMapGenerator::placeTrees(MapGrid& map, const MapSeedOptions& options, const RmgConfig& config, NativeRandomizer& rng)
{
    if (config.maxTrees <= 0) {
        return;
    }

    int minimum = 0;
    int maximum = 0;
    if (!config.vegetationMaximums.empty()) {
        const int maxIndex = static_cast<int>(config.vegetationMaximums.size()) - 1;
        const int index = std::clamp(options.vegetation, 0, maxIndex);
        minimum = index < static_cast<int>(config.vegetationMinimums.size())
            ? config.vegetationMinimums[static_cast<std::size_t>(index)]
            : 0;
        maximum = config.vegetationMaximums[static_cast<std::size_t>(index)];
        minimum = std::clamp(minimum, 0, config.maxTrees);
        maximum = std::clamp(maximum, 0, config.maxTrees);
        if (maximum < minimum) {
            std::swap(minimum, maximum);
        }
    }

    report_.vegetationMinimum = minimum;
    report_.vegetationMaximum = maximum;
    const int treeTarget = std::max(0, report_.surfaceSimpleTreeTarget);
    report_.treeClusterTarget = treeTarget;
    int remainingTrees = treeTarget;
    const Rect mapRect = nativeSavedMapRect(map);
    const int mapSize = mapRect.w + mapRect.h + 1;
    std::unordered_map<std::uint64_t, const IsoTileOverride*> overrideByIso;
    overrideByIso.reserve(report_.isoTileOverrides.size());
    for (const IsoTileOverride& override : report_.isoTileOverrides) {
        overrideByIso[nativeIsoKey(override.iso)] = &override;
    }
    auto nativeTreeSeedTile = [&](Point nativeCell) {
        if (!isInsideNativeCellDiamond(nativeCell, mapRect)) {
            return kNativeTileWaterBase;
        }
        const auto overrideIt = overrideByIso.find(nativeIsoKey(nativeCell));
        if (overrideIt != overrideByIso.end()) {
            return nativeTileTypeIndexForOverride(*overrideIt->second);
        }
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        return map.inBounds(source) ? nativeTileTypeIndexForTerrain(map.at(source)) : kNativeTileWaterBase;
    };
    auto isNativeBlankTreeSeed = [&](Point nativeCell) {
        return isNativeClearTileIndex(nativeTreeSeedTile(nativeCell));
    };

    for (int i = 0; i < 100 && remainingTrees > 0; ++i) {
        Point center{0, 0};
        bool foundCenter = false;
        for (int attempts = 1; attempts <= 200; ++attempts) {
            std::uint32_t raw = 0;
            const int sampleIndex = randomNativeFloatScaledInclusive(rng, 0, mapSize * mapSize - 1, &raw);
            const Point candidate{sampleIndex % mapSize, sampleIndex / mapSize};
            const bool insideDiamond = isInsideNativeCellDiamond(candidate, mapRect);
            const bool hasCoords = insideDiamond;
            const int tile = nativeTreeSeedTile(candidate);
            const bool accepted = isNativeClearTileIndex(tile);
            appendNativeTreeCenterTrace(
                i + 1,
                attempts,
                candidate,
                raw,
                sampleIndex,
                insideDiamond,
                hasCoords,
                tile,
                -1,
                -1,
                -1,
                false,
                accepted);
            if (!accepted) {
                continue;
            }
            center = candidate;
            foundCenter = true;
            break;
        }
        if (!foundCenter) {
            center = {0, 0};
        }

        const double placementChance = randomNativeTreePatchChance(rng);
        const int patchSize = randomNativeTreePatchSize(rng);
        const int placed = placeTreePatchNative(map, rng, center, patchSize, placementChance);
        if (placed <= 0) {
            continue;
        }
        ++report_.treeClusters;
        remainingTrees -= placed;
        report_.treeCells += placed;
    }
    appendNativeSurfaceTrace(
        report_.surfaceMode == 2 ? "rich_end" : "simple_end",
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
}
