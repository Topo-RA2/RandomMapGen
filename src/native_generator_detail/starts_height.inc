bool NativeMapGenerator::isAreaSuitableForPlacement(const MapGrid& map, Point center, int radius) const
{
    for (int y = center.y - radius; y <= center.y + radius; ++y) {
        for (int x = center.x - radius; x <= center.x + radius; ++x) {
            Point p{x, y};
            if (!map.inBounds(p) || map.at(p).cliffShoreExclusion || !isBuildable(map.at(p))) {
                return false;
            }
        }
    }
    return true;
}

NativeMapGenerator::SpawnSelection NativeMapGenerator::selectSpawnCells(
    const MapGrid& map,
    const NativeRegion& region,
    int desiredCount,
    const MapSeedOptions& options,
    NativeRandomizer& rng,
    bool consumeRandomOffset) const
{
    SpawnSelection result;
    if (desiredCount < 0) {
        return result;
    }

    std::vector<Point> candidates;
    candidates.reserve(region.nativeCells.empty() ? region.cells.size() : region.nativeCells.size());
    const Rect mapRect = nativeSavedMapRect(map);
    const Rect localRect = nativeLocalRect(map);
    const Rect view{
        localRect.x + 4,
        localRect.y + 4,
        std::max(1, localRect.w - 8),
        std::max(1, localRect.h - 8),
    };

    const int quota = std::max(0, desiredCount);
    const int randomOffset = consumeRandomOffset ? rng.randomScaledInclusive(0, 2) : 0;
    const int targetCandidates = randomOffset + 15 * quota;
    const bool useNativePool = !region.nativeCells.empty();
    const int sampleCount = useNativePool ? static_cast<int>(region.nativeCells.size()) : static_cast<int>(region.cells.size());
    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, report_.isoTileOverrides);
    std::unordered_map<std::uint64_t, const NativeIteratorCell*> iteratorCellByIso;
    iteratorCellByIso.reserve(iteratorCells.size());
    for (const NativeIteratorCell& cell : iteratorCells) {
        iteratorCellByIso[nativeIsoKey(cell.cell)] = &cell;
    }
    auto nativeAreaSuitable = [&iteratorCellByIso](Point nativeCenter, std::string* rejectionReason, Point* rejectionCell) {
        for (int y = nativeCenter.y - 3; y < nativeCenter.y + 3; ++y) {
            for (int x = nativeCenter.x - 3; x < nativeCenter.x + 3; ++x) {
                const Point native{x, y};
                const auto cellIt = iteratorCellByIso.find(nativeIsoKey(native));
                if (cellIt == iteratorCellByIso.end()) {
                    if (rejectionReason != nullptr) {
                        *rejectionReason = "bounds";
                    }
                    if (rejectionCell != nullptr) {
                        *rejectionCell = native;
                    }
                    return false;
                }
                const int tile = cellIt->second->isoTileTypeIndex;
                if (!isNativeClearTileIndex(tile) && !isNativePaveTileIndex(tile)) {
                    if (rejectionReason != nullptr) {
                        *rejectionReason = "not_blank";
                    }
                    if (rejectionCell != nullptr) {
                        *rejectionCell = native;
                    }
                    return false;
                }
            }
        }
        return true;
    };
    auto nativeCellInView = [&iteratorCellByIso, mapRect, view](Point nativeCell) {
        const auto cellIt = iteratorCellByIso.find(nativeIsoKey(nativeCell));
        if (cellIt == iteratorCellByIso.end()) {
            return false;
        }

        int level = cellIt->second->level;
        const int sum = nativeCell.x + nativeCell.y;
        if (cellIt->second->slopeIndex != 0
            && sum < level + mapRect.w + 2 * view.y + 4) {
            ++level;
        }
        return sum > level + mapRect.w + 2 * view.y
            && sum <= level + mapRect.w + 2 * (view.y + view.h) + 2
            && nativeCell.x - nativeCell.y < 2 * (view.x + view.w) - mapRect.w
            && nativeCell.y - nativeCell.x < mapRect.w - 2 * view.x;
    };
    std::ofstream spawnAttemptTrace;
    if (const char* dumpPath = std::getenv(kNativeSpawnCandidateDumpPathEnv); dumpPath != nullptr && dumpPath[0] != '\0') {
        std::filesystem::path path{dumpPath};
        path += ".attempts.csv";
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        spawnAttemptTrace.open(path, std::ios::out | std::ios::app);
        if (spawnAttemptTrace.is_open()) {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec) || std::filesystem::file_size(path, ec) == 0) {
                spawnAttemptTrace << "regionId,quota,attempt,sampleIndex,nativeX,nativeY,sourceX,sourceY,sourceInBounds,inView,accepted,rejection,failNativeX,failNativeY\n";
            }
        }
    }
    for (int attempts = 0; attempts < 300 && static_cast<int>(candidates.size()) < targetCandidates && sampleCount > 0; ++attempts) {
        const int sampleIndex = rng.randomScaledInclusive(0, sampleCount - 1);
        const Point candidate = useNativePool
            ? region.nativeCells[static_cast<std::size_t>(sampleIndex)]
            : sourcePointToNativeCell(map, region.cells[static_cast<std::size_t>(sampleIndex)]);
        const Point source = nativeCellToSourcePoint(map, candidate);
        const bool sourceInBounds = map.inBounds(source);
        const bool sourceEligible = useNativePool || sourceInBounds;
        const bool inView = nativeCellInView(candidate);
        std::string rejectionReason;
        Point rejectionCell{-1, -1};
        const bool areaSuitable = sourceEligible && inView && nativeAreaSuitable(candidate, &rejectionReason, &rejectionCell);
        const bool accepted = sourceEligible && inView && areaSuitable;
        if (spawnAttemptTrace.is_open()) {
            if (!sourceEligible) {
                rejectionReason = "source_bounds";
            } else if (!inView) {
                rejectionReason = "view";
            }
            spawnAttemptTrace << region.id << ','
                              << quota << ','
                              << attempts << ','
                              << sampleIndex << ','
                              << candidate.x << ','
                              << candidate.y << ','
                              << source.x << ','
                              << source.y << ','
                              << (sourceInBounds ? 1 : 0) << ','
                              << (inView ? 1 : 0) << ','
                              << (accepted ? 1 : 0) << ','
                              << rejectionReason << ','
                              << rejectionCell.x << ','
                              << rejectionCell.y << '\n';
        }
        if (accepted) {
            candidates.push_back(candidate);
        }
    }
    if (const char* dumpPath = std::getenv(kNativeSpawnCandidateDumpPathEnv); dumpPath != nullptr && dumpPath[0] != '\0') {
        const std::filesystem::path path{dumpPath};
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::error_code ec;
        const bool writeHeader = !std::filesystem::exists(path, ec) || std::filesystem::file_size(path, ec) == 0;
        std::ofstream out(path, std::ios::out | std::ios::app);
        if (out.is_open()) {
            if (writeHeader) {
                out << "regionId,quota,candidateIndex,x,y,nativeX,nativeY\n";
            }
            for (std::size_t i = 0; i < candidates.size(); ++i) {
                const Point source = nativeCellToSourcePoint(map, candidates[i]);
                out << region.id << ','
                    << quota << ','
                    << i << ','
                    << source.x << ','
                    << source.y << ','
                    << candidates[i].x << ','
                    << candidates[i].y << '\n';
            }
        }
    }

    int expandedTarget = nativeDoubleToInt(
        ((static_cast<double>(std::max(0, options.tiberiumLayout)) * 0.01 * 12.0 / static_cast<double>(std::max(1, options.numPlayers))) + 2.0)
        * static_cast<double>(quota));
    if (quota == 0 && !candidates.empty()) {
        expandedTarget = static_cast<int>(candidates.size());
    }
    if (expandedTarget > static_cast<int>(candidates.size()) || expandedTarget == 0) {
        if (quota == 0) {
            result.candidateCount = static_cast<int>(candidates.size());
            return result;
        }
        expandedTarget = static_cast<int>(candidates.size());
    }
    result.candidateCount = static_cast<int>(candidates.size());
    result.targetCount = std::min(expandedTarget, result.candidateCount);
    if (candidates.empty()) {
        return result;
    }

    const int selectedCount = result.targetCount;
    auto spawnDistance = [&map](Point lhs, Point rhs) {
        double distance = euclidean(lhs, rhs);
        const Point lhsSource = nativeCellToSourcePoint(map, lhs);
        const Point rhsSource = nativeCellToSourcePoint(map, rhs);
        if (map.inBounds(lhsSource)
            && map.inBounds(rhsSource)
            && map.at(lhsSource).regionTag != map.at(rhsSource).regionTag) {
            distance += 20.0;
        }
        return distance;
    };

    if (candidates.size() == 1 || selectedCount == 1) {
        result.selected.push_back(candidates.front());
        return result;
    }

    double bestDistance = -1.0;
    std::size_t firstIndex = 0;
    for (std::size_t i = 0; i + 1 < candidates.size(); ++i) {
        for (std::size_t j = i + 1; j < candidates.size(); ++j) {
            const double distance = spawnDistance(candidates[i], candidates[j]);
            if (distance > bestDistance) {
                bestDistance = distance;
                firstIndex = i;
            }
        }
    }
    result.selected.push_back(candidates[firstIndex]);

    while (static_cast<int>(result.selected.size()) < selectedCount) {
        double bestNearestDistance = -1.0;
        std::size_t bestIndex = 0;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (std::find(result.selected.begin(), result.selected.end(), candidates[i]) != result.selected.end()) {
                continue;
            }

            double nearest = 1.0e9;
            for (Point existing : result.selected) {
                nearest = std::min(nearest, spawnDistance(candidates[i], existing));
            }
            if (nearest > bestNearestDistance) {
                bestNearestDistance = nearest;
                bestIndex = i;
            }
        }
        result.selected.push_back(candidates[bestIndex]);
    }

    return result;
}

std::vector<Point> NativeMapGenerator::placeStartPoints(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, std::vector<NativeRegion>& regions)
{
    std::vector<Point> starts;
    auto dumpSpawnSeed = [&map](int regionId, int quota, const char* role, int index, Point nativeSeed) {
        const char* dumpPath = std::getenv(kNativeSpawnDumpPathEnv);
        if (dumpPath == nullptr || dumpPath[0] == '\0') {
            return;
        }
        const std::filesystem::path path{dumpPath};
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::error_code ec;
        const bool writeHeader = !std::filesystem::exists(path, ec) || std::filesystem::file_size(path, ec) == 0;
        std::ofstream out(path, std::ios::out | std::ios::app);
        if (!out.is_open()) {
            return;
        }
        if (writeHeader) {
            out << "regionId,quota,role,index,x,y,nativeX,nativeY\n";
        }
        const Point source = nativeCellToSourcePoint(map, nativeSeed);
        out << regionId << ','
            << quota << ','
            << (role ? role : "") << ','
            << index << ','
            << source.x << ','
            << source.y << ','
            << nativeSeed.x << ','
            << nativeSeed.y << '\n';
    };
    for (auto& region : regions) {
        SpawnSelection selection = selectSpawnCells(map, region, region.nativeWaypointQuota, options, rng);
        report_.nativeSpawnCandidateCells += selection.candidateCount;
        report_.nativeSpawnPoolCells += static_cast<int>(selection.selected.size());
        const int waypointCount = std::min(region.nativeWaypointQuota, static_cast<int>(selection.selected.size()));
        region.nativeWaypoints.assign(selection.selected.begin(), selection.selected.begin() + waypointCount);
        region.nativeResourceSeeds = std::move(selection.selected);
        report_.nativeWaypoints.insert(report_.nativeWaypoints.end(), region.nativeWaypoints.begin(), region.nativeWaypoints.end());
        for (std::size_t i = 0; i < region.nativeWaypoints.size(); ++i) {
            dumpSpawnSeed(region.id, region.nativeWaypointQuota, "waypoint", static_cast<int>(i), region.nativeWaypoints[i]);
        }
        for (std::size_t i = 0; i < region.nativeResourceSeeds.size(); ++i) {
            dumpSpawnSeed(region.id, region.nativeWaypointQuota, "resource", static_cast<int>(i), region.nativeResourceSeeds[i]);
        }
        report_.nativeSpawnExtraCells += std::max(0, static_cast<int>(region.nativeResourceSeeds.size()) - waypointCount);
        report_.nativeWaypointsSelected += static_cast<int>(region.nativeWaypoints.size());

        if (region.startQuota <= 0) {
            continue;
        }

        std::vector<Point> selected;
        selected.reserve(static_cast<std::size_t>(region.startQuota));
        std::vector<Point> selectedNative;
        selectedNative.reserve(static_cast<std::size_t>(region.startQuota));
        auto pushUnique = [&selected, &selectedNative, &region](Point p, Point native) {
            if (static_cast<int>(selected.size()) >= region.startQuota
                || std::find(selectedNative.begin(), selectedNative.end(), native) != selectedNative.end()) {
                return;
            }
            selected.push_back(p);
            selectedNative.push_back(native);
        };

        for (Point waypoint : region.nativeWaypoints) {
            const Point source = nativeCellToSourcePoint(map, waypoint);
            pushUnique(source, waypoint);
        }
        for (std::size_t startIndex = 0; startIndex < selected.size(); ++startIndex) {
            const Point start = selected[startIndex];
            const Point nativeStart = startIndex < selectedNative.size()
                ? selectedNative[startIndex]
                : sourcePointToNativeCell(map, start);
            starts.push_back(start);
            nativeStartsForExclusion_.push_back(nativeStart);
            if (!map.inBounds(start)) {
                continue;
            }

            Cell& cell = map.at(start);
            cell.overlay = Overlay::Start;
            cell.overlayValue = static_cast<int>(starts.size());

            for (auto& candidate : map.cells()) {
                if (manhattan({candidate.x, candidate.y}, start) <= 4 && isPassable(candidate)) {
                    candidate.terrain = Terrain::Land;
                    if (candidate.overlay == Overlay::Tree) {
                        candidate.overlay = Overlay::None;
                    }
                }
            }
        }
    }

    report_.startsPlaced = static_cast<int>(starts.size());
    return starts;
}

void NativeMapGenerator::evaluateStartFairness(const MapGrid& map, const std::vector<Point>& starts)
{
    report_.startResourceValues.assign(starts.size(), 0);
    report_.startResourceDistances.assign(starts.size(), -1);
    report_.buildableNearStarts.assign(starts.size(), 0);
    if (starts.empty()) {
        return;
    }

    auto index = [&map](Point p) {
        return static_cast<std::size_t>(p.y * map.width() + p.x);
    };

    int minResourceValue = std::numeric_limits<int>::max();
    int maxResourceValue = 0;
    int minResourceDistance = std::numeric_limits<int>::max();
    int maxResourceDistance = 0;

    for (std::size_t i = 0; i < starts.size(); ++i) {
        std::vector<int> distance(static_cast<std::size_t>(map.width() * map.height()), -1);
        if (map.inBounds(starts[i]) && isPassable(map.at(starts[i]))) {
            std::queue<Point> queue;
            distance[index(starts[i])] = 0;
            queue.push(starts[i]);
            while (!queue.empty()) {
                const Point current = queue.front();
                queue.pop();
                const int currentDistance = distance[index(current)];
                for (Point next : map.neighbors4(current)) {
                    auto nextIndex = index(next);
                    if (distance[nextIndex] == -1 && isPassable(map.at(next))) {
                        distance[nextIndex] = currentDistance + 1;
                        queue.push(next);
                    }
                }
            }
        }

        int resources = 0;
        int weightedResourceDistance = 0;
        int buildable = 0;
        for (const auto& cell : map.cells()) {
            const Point p{cell.x, cell.y};
            const int pathDistance = distance[index(p)];
            if ((cell.overlay == Overlay::Ore || cell.overlay == Overlay::Gems) && pathDistance >= 0 && pathDistance <= 34) {
                const int value = overlayValue(cell);
                resources += value;
                weightedResourceDistance += value * pathDistance;
            }
            if (manhattan(p, starts[i]) <= 7 && isBuildable(cell)) {
                ++buildable;
            }
        }

        report_.startResourceValues[i] = resources;
        report_.buildableNearStarts[i] = buildable;
        minResourceValue = std::min(minResourceValue, resources);
        maxResourceValue = std::max(maxResourceValue, resources);
        if (resources > 0) {
            const int resourceDistance = static_cast<int>(std::lround(static_cast<double>(weightedResourceDistance) / static_cast<double>(resources)));
            report_.startResourceDistances[i] = resourceDistance;
            minResourceDistance = std::min(minResourceDistance, resourceDistance);
            maxResourceDistance = std::max(maxResourceDistance, resourceDistance);
        }
    }

    if (minResourceValue != std::numeric_limits<int>::max()) {
        report_.minStartResourceValue = minResourceValue;
        report_.maxStartResourceValue = maxResourceValue;
        report_.startResourceValueSpread = maxResourceValue - minResourceValue;
    }
    if (minResourceDistance != std::numeric_limits<int>::max()) {
        report_.minStartResourceDistance = minResourceDistance;
        report_.maxStartResourceDistance = maxResourceDistance;
        report_.startResourceDistanceSpread = maxResourceDistance - minResourceDistance;
    }
    if (!report_.buildableNearStarts.empty()) {
        const auto [minBuildable, maxBuildable] = std::minmax_element(report_.buildableNearStarts.begin(), report_.buildableNearStarts.end());
        report_.minBuildableNearStart = *minBuildable;
        report_.maxBuildableNearStart = *maxBuildable;
        report_.buildableNearStartSpread = *maxBuildable - *minBuildable;
    }
}

void NativeMapGenerator::markSpawnExclusionZones(MapGrid& map, const std::vector<Point>& starts)
{
    struct SpawnExclusionHeapNode {
        Point point;
        float priority = 0.0f;
    };
    struct SpawnExclusionHeap {
        explicit SpawnExclusionHeap(int capacity)
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
        std::vector<SpawnExclusionHeapNode> nodes;
        std::vector<int> heap;
    };

    int marked = 0;
    nativeSpawnExclusionKeys_.clear();
    nativeMapgenDefaultTempTag_ = 0;
    nativeMapgenTempTags_.clear();

    std::unordered_map<std::uint64_t, const IsoTileOverride*> overrideByIso;
    overrideByIso.reserve(report_.isoTileOverrides.size());
    for (const IsoTileOverride& override : report_.isoTileOverrides) {
        overrideByIso[nativeIsoKey(override.iso)] = &override;
    }
    auto nativeBlank = [&map, &overrideByIso](Point native, Point startNative) {
        const auto overrideIt = overrideByIso.find(nativeIsoKey(native));
        if (overrideIt != overrideByIso.end()) {
            return isNativeClearTileIndex(nativeTileTypeIndexForOverride(*overrideIt->second));
        }
        const Point source = nativeCellToSourcePoint(map, native);
        if (!map.inBounds(source)) {
            return false;
        }
        const Cell& cell = map.at(source);
        return isNativeClearTileIndex(nativeTileTypeIndexForTerrain(cell));
    };

    auto packedNative = [](Point p) {
        const auto x = static_cast<std::uint32_t>(static_cast<std::uint16_t>(p.x));
        const auto y = static_cast<std::uint32_t>(static_cast<std::uint16_t>(p.y));
        return static_cast<int>(x | (y << 16));
    };
    auto floatBits = [](float value) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return static_cast<int>(bits);
    };
    auto traceSpawnExclusionEvent = [&](const char* event,
                                        int waypoint,
                                        int localIndex,
                                        Point native,
                                        float priority,
                                        int heapCount,
                                        int allocated,
                                        int tag,
                                        Point sourceNative) {
        const char* dumpPath = std::getenv(kNativeSpawnExclusionEventDumpPathEnv);
        if (dumpPath == nullptr || dumpPath[0] == '\0') {
            return;
        }
        const std::filesystem::path path{dumpPath};
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::error_code ec;
        const bool writeHeader = !std::filesystem::exists(path, ec) || std::filesystem::file_size(path, ec) == 0;
        std::ofstream out(path, std::ios::out | std::ios::app);
        if (!out.is_open()) {
            return;
        }
        if (writeHeader) {
            out << "event,waypoint,localIndex,x,y,packed,priorityBits,heapCount,allocated,tag,scratch,surfaceBlocked,mapgenPtr,nodePtr,sourceX,sourceY\n";
        }

        const bool surfaceBlocked = nativeSpawnExclusionKeys_.find(nativeIsoKey(native)) != nativeSpawnExclusionKeys_.end();
        out << (event ? event : "") << ','
            << waypoint << ','
            << localIndex << ','
            << native.x << ','
            << native.y << ','
            << packedNative(native) << ','
            << floatBits(priority) << ','
            << heapCount << ','
            << allocated << ','
            << tag << ','
            << tag << ','
            << (surfaceBlocked ? 1 : 0) << ','
            << 0 << ','
            << 0 << ','
            << sourceNative.x << ','
            << sourceNative.y << '\n';
    };

    const Rect mapRect = nativeSavedMapRect(map);
    for (std::size_t i = 0; i < starts.size(); ++i) {
        const int tag = static_cast<int>(i) + 1;
        nativeMapgenDefaultTempTag_ = 0;
        nativeMapgenTempTags_.clear();
        SpawnExclusionHeap heap(std::max(1, map.width() * map.height() * 2));
        const Point start = starts[i];
        const Point nativeStart = i < nativeStartsForExclusion_.size()
            ? nativeStartsForExclusion_[i]
            : sourcePointToNativeCell(map, start);
        if (!isInsideNativeCellDiamond(nativeStart, mapRect)) {
            continue;
        }

        ++report_.spawnExclusionStarts;
        std::unordered_set<std::uint64_t> visited;
        visited.reserve(512);
        visited.insert(nativeIsoKey(nativeStart));
        if (heap.push(nativeStart, 0.0)) {
            ++report_.spawnExclusionHeapPushes;
        }
        int localMarked = 0;
        while (!heap.empty()) {
            if (localMarked >= 400) {
                ++report_.spawnExclusionLimitHits;
                break;
            }
            const Point point = heap.pop();
            ++report_.spawnExclusionHeapPops;
            if (point != nativeStart && !nativeBlank(point, nativeStart)) {
                continue;
            }
            const std::uint64_t pointKey = nativeIsoKey(point);
            nativeMapgenTempTags_[pointKey] = tag;
            if (nativeSpawnExclusionKeys_.insert(pointKey).second) {
                nativeMapgenSurfaceBlockedKeys_.insert(pointKey);
                ++marked;
            }
            const Point source = nativeCellToSourcePoint(map, point);
            Cell* cell = map.inBounds(source) ? &map.at(source) : nullptr;
            if (cell != nullptr) {
                cell->spawnExclusion = true;
            }
            if (cell != nullptr && cell->overlay != Overlay::Start) {
                cell->overlay = Overlay::None;
                cell->overlayValue = 0;
                cell->terrainObject = TerrainObject::None;
                cell->terrainObjectValue = 0;
            }
            traceSpawnExclusionEvent(
                "mark",
                static_cast<int>(i),
                localMarked,
                point,
                static_cast<float>(euclidean(point, nativeStart)),
                heap.heapSize,
                heap.allocated,
                tag,
                nativeStart);
            ++localMarked;

            constexpr std::array<Point, 8> kNativeNeighbors{{
                {0, -1}, {1, -1}, {1, 0}, {1, 1},
                {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
            }};
            for (Point offset : kNativeNeighbors) {
                const Point next{point.x + offset.x, point.y + offset.y};
                if (!isInsideNativeCellDiamond(next, mapRect)) {
                    continue;
                }
                const std::uint64_t key = nativeIsoKey(next);
                if (visited.find(key) != visited.end()) {
                    continue;
                }
                visited.insert(key);
                ++report_.spawnExclusionCandidateChecks;
                if (!nativeBlank(next, nativeStart)) {
                    ++report_.spawnExclusionRejectedNonBlank;
                    continue;
                }
                const float priority = static_cast<float>(euclidean(next, nativeStart));
                traceSpawnExclusionEvent(
                    "enqueue",
                    static_cast<int>(i),
                    heap.allocated,
                    next,
                    priority,
                    heap.heapSize,
                    heap.allocated,
                    tag,
                    nativeStart);
                nativeMapgenTempTags_[key] = tag;
                if (heap.push(next, priority)) {
                    ++report_.spawnExclusionHeapPushes;
                }
            }
        }
        (void) tag;
    }

    report_.spawnExclusionCells = marked;
}

void NativeMapGenerator::markCliffShoreExclusion(MapGrid& map)
{
    nativeCliffShoreExclusionKeys_.clear();
    nativeMapgenHalfHeightKeys_.clear();
    const Rect mapRect = nativeSavedMapRect(map);
    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, report_.isoTileOverrides);
    std::unordered_map<std::uint64_t, NativeIteratorCell> cellByIso;
    cellByIso.reserve(iteratorCells.size());
    for (const NativeIteratorCell& cell : iteratorCells) {
        cellByIso[nativeIsoKey(cell.cell)] = cell;
    }

    constexpr std::array<Point, 8> kAdjacentCellOrder{{
        {0, -1}, {1, -1}, {1, 0}, {1, 1},
        {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
    }};

    std::ofstream exclusionTrace;
    if (const char* dumpPath = std::getenv(kNativeHeightNoiseDumpPathEnv); dumpPath != nullptr && dumpPath[0] != '\0') {
        std::filesystem::path path{dumpPath};
        path += ".exclusion.csv";
        exclusionTrace.open(path, std::ios::out | std::ios::trunc);
        if (exclusionTrace.is_open()) {
            exclusionTrace << "event,index,x,y,tile,clear,cliffRange,shoreRange,hasSource,neighborX,neighborY\n";
        }

        path = std::filesystem::path{dumpPath};
        path += ".occupancy.csv";
        std::ofstream occupancyTrace(path, std::ios::out | std::ios::trunc);
        if (occupancyTrace.is_open()) {
            occupancyTrace << "event,x,y,typeIndex,firstAbstract,firstType,altAbstract,altType\n";
            for (const NativeOverlayOverride& overlay : report_.nativeOverlayOverrides) {
                if (overlay.overlayTypeIndex >= 0) {
                    occupancyTrace << "overlay," << overlay.iso.x << ',' << overlay.iso.y << ','
                        << overlay.overlayTypeIndex << ",-1,-1,-1,-1\n";
                }
            }
            for (const NativeObjectOverride& object : report_.nativeObjectOverrides) {
                if (object.objectAbstractType >= 0 || object.altObjectAbstractType >= 0) {
                    occupancyTrace << "object," << object.iso.x << ',' << object.iso.y << ",-1,"
                        << object.objectAbstractType << ',' << object.objectTypeIndex << ','
                        << object.altObjectAbstractType << ',' << object.altObjectTypeIndex << '\n';
                }
            }
        }
    }

    auto markNativeCell = [&](Point nativeCell, bool seedHalfHeight) {
        if (!isInsideNativeCellDiamond(nativeCell, mapRect)) {
            return;
        }
        const std::uint64_t key = nativeIsoKey(nativeCell);
        if (seedHalfHeight) {
            nativeMapgenHalfHeightKeys_.insert(key);
        }
        nativeCliffShoreExclusionKeys_.insert(key);
        nativeMapgenSurfaceBlockedKeys_.insert(key);
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (!map.inBounds(source)) {
            return;
        }
        Cell& cell = map.at(source);
        if (!cell.cliffShoreExclusion) {
            cell.cliffShoreExclusion = true;
            ++report_.cliffShoreExclusionCells;
        }
    };

    int anchorIndex = 0;
    for (const NativeIteratorCell& anchor : iteratorCells) {
        const bool shoreAnchor = nativeTheaterCatalog_ != nullptr
            && nativeTheaterCatalog_->shorePiecesBase >= 0
            ? anchor.isoTileTypeIndex >= nativeTheaterCatalog_->shorePiecesBase
                && anchor.isoTileTypeIndex < nativeTheaterCatalog_->shorePiecesBase + 42
            : isNativeShoreTileIndex(anchor.isoTileTypeIndex);
        const bool cliffAnchor = isNativeFlatSlopeBlockingTile(
            nativeTheaterCatalog_,
            anchor.isoTileTypeIndex,
            anchor.height);
        if (exclusionTrace.is_open()) {
            exclusionTrace << "cell," << anchorIndex << ','
                << anchor.cell.x << ',' << anchor.cell.y << ','
                << anchor.isoTileTypeIndex << ','
                << (isNativeClearTileIndex(anchor.isoTileTypeIndex) ? 1 : 0) << ','
                << (cliffAnchor ? 1 : 0) << ','
                << (shoreAnchor ? 1 : 0) << ','
                << (anchor.source != nullptr ? 1 : 0) << ",-1,-1\n";
        }
        ++anchorIndex;
        if (!shoreAnchor && !cliffAnchor) {
            continue;
        }

        for (Point offset : kAdjacentCellOrder) {
            const Point neighbor{anchor.cell.x + offset.x, anchor.cell.y + offset.y};
            if (!isInsideNativeCellDiamond(neighbor, mapRect)) {
                continue;
            }
            const auto neighborIt = cellByIso.find(nativeIsoKey(neighbor));
            if (neighborIt == cellByIso.end() || !isNativeClearTileIndex(neighborIt->second.isoTileTypeIndex)) {
                continue;
            }
            if (exclusionTrace.is_open()) {
                exclusionTrace << "mark," << (anchorIndex - 1) << ','
                    << anchor.cell.x << ',' << anchor.cell.y << ','
                    << anchor.isoTileTypeIndex << ','
                    << 0 << ',' << (cliffAnchor ? 1 : 0) << ',' << (shoreAnchor ? 1 : 0) << ','
                    << (anchor.source != nullptr ? 1 : 0) << ',' << neighbor.x << ',' << neighbor.y << '\n';
            }
            markNativeCell(neighbor, shoreAnchor);
            break;
        }
    }
}

constexpr std::array<std::array<int, 4>, kNativeHeightSlopeCount> kNativeHeightSlopePatterns{{
    {{0, 0, 0, 0}},
    {{0, 15, 15, 0}}, {{0, 0, 15, 15}},
    {{15, 0, 0, 15}}, {{15, 15, 0, 0}},
    {{0, 0, 15, 0}}, {{0, 0, 0, 15}},
    {{15, 0, 0, 0}}, {{0, 15, 0, 0}},
    {{0, 15, 15, 15}}, {{15, 0, 15, 15}},
    {{15, 15, 0, 15}}, {{15, 15, 15, 0}},
    {{0, 15, 30, 15}}, {{15, 0, 15, 30}},
    {{30, 15, 0, 15}}, {{15, 30, 15, 0}},
    {{0, 15, 0, 15}}, {{15, 0, 15, 0}},
}};

class NativeHeightWorkspace {
public:
    NativeHeightWorkspace(
        const MapGrid& map,
        const std::vector<IsoTileOverride>& overrides,
        const std::vector<NativeOverlayOverride>& overlayOverrides,
        const std::vector<NativeObjectOverride>& objectOverrides,
        const std::unordered_set<std::uint64_t>& surfaceBlockedKeys,
        const NativeTheaterCatalog* theaterCatalog)
        : mapRect_(nativeSavedMapRect(map))
        , cellSide_(mapRect_.w + mapRect_.h + 1)
        , vertexSide_(cellSide_)
        , vertices_(static_cast<std::size_t>(vertexSide_ * vertexSide_))
    {
        std::unordered_map<std::uint64_t, const IsoTileOverride*> overrideByIso;
        overrideByIso.reserve(overrides.size());
        for (const IsoTileOverride& override : overrides) {
            overrideByIso[nativeIsoKey(override.iso)] = &override;
        }

        std::unordered_set<std::uint64_t> occupiedKeys;
        occupiedKeys.reserve(overlayOverrides.size() + objectOverrides.size());
        for (const NativeOverlayOverride& overlay : overlayOverrides) {
            if (overlay.overlayTypeIndex >= 0) {
                occupiedKeys.insert(nativeIsoKey(overlay.iso));
            }
        }
        for (const NativeObjectOverride& object : objectOverrides) {
            if (object.objectAbstractType >= 0 || object.altObjectAbstractType >= 0) {
                occupiedKeys.insert(nativeIsoKey(object.iso));
            }
        }

        cells_.reserve(static_cast<std::size_t>(cellSide_ * cellSide_ / 2));
        for (Point nativeCell : nativeMapgenWorkspaceScanOrder(map)) {
            const std::uint64_t key = nativeIsoKey(nativeCell);
            CellInfo info;
            info.level = kNativeBaseMapLevel;
            info.terrain = Terrain::Water;
            info.tileTypeIndex = kNativeTileWaterBase;
            info.landType = 0;
            bool occupied = occupiedKeys.contains(key);

            const Point source = nativeCellToSourcePoint(map, nativeCell);
            if (map.inBounds(source)) {
                const Cell& cell = map.at(source);
                info.level = std::clamp(cell.level, 0, 12);
                info.terrain = cell.terrain;
                info.tileTypeIndex = nativeTileTypeIndexForTerrain(cell);
                info.slopeIndex = isNativeHeightSlopeCell(cell) ? cell.slope : 0;
                info.landType = cell.landType;
            }

            if (const auto overrideIt = overrideByIso.find(key); overrideIt != overrideByIso.end()) {
                const IsoTileOverride& override = *overrideIt->second;
                info.level = std::clamp(override.level, 0, 12);
                info.terrain = override.terrain;
                info.tileTypeIndex = nativeTileTypeIndexForOverride(override);
                info.slopeIndex = std::clamp(override.slopeIndex, 0, kNativeHeightSlopeCount - 1);
                info.landType = override.landType;
            }

            const NativeIsoTileTemplate* tileType = theaterCatalog == nullptr
                ? nullptr
                : theaterCatalog->tileType(info.tileTypeIndex);
            const bool morphable = info.tileTypeIndex == 0xFFFF
                || (tileType != nullptr && tileType->morphable)
                || (theaterCatalog == nullptr
                    && (isNativeClearTileIndex(info.tileTypeIndex)
                        || (info.tileTypeIndex >= kNativeTileRampBase
                            && info.tileTypeIndex < kNativeTileRampBase + kNativeHeightSlopeCount)));
            info.slopeable = morphable
                && !occupied
                && !surfaceBlockedKeys.contains(key);
            info.occupied = occupied;
            info.surfaceBlocked = surfaceBlockedKeys.contains(key);
            cells_[key] = info;
        }

        for (int y = 0; y < vertexSide_; ++y) {
            for (int x = 0; x < vertexSide_; ++x) {
                const Point vertex{x, y};
                Vertex& slot = vertexAt(vertex);
                const CellInfo* base = baseCellForVertex(vertex);
                if (base == nullptr) {
                    slot.blocked = true;
                    continue;
                }
                slot.height = base->level * 15
                    + kNativeHeightSlopePatterns[static_cast<std::size_t>(base->slopeIndex)][0];
                slot.blocked = vertexIsBlocked(vertex);
            }
        }
    }

    int cellSide() const noexcept
    {
        return cellSide_;
    }

    void dumpIfRequested(const char* suffix) const
    {
        const char* dumpPath = std::getenv(kNativeHeightNoiseDumpPathEnv);
        if (dumpPath == nullptr || dumpPath[0] == '\0') {
            return;
        }

        std::filesystem::path path{dumpPath};
        path += suffix;
        std::ofstream out(path, std::ios::out | std::ios::trunc);
        if (!out.is_open()) {
            return;
        }

        out << "index,x,y,nativeX,nativeY,height,blocked,dirty,force\n";
        const int side = std::max(0, vertexSide_ - 1);
        int index = 0;
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                const Point nativeVertex{x + 1, y + 1};
                const Vertex& vertex = vertexAt(nativeVertex);
                out << index++ << ','
                    << x << ',' << y << ','
                    << nativeVertex.x << ',' << nativeVertex.y << ','
                    << vertex.height << ','
                    << (vertex.blocked ? 1 : 0) << ','
                    << (vertex.dirty ? 1 : 0) << ','
                    << 0 << '\n';
            }
        }

        path = std::filesystem::path{dumpPath};
        path += suffix;
        path += ".cells.csv";
        std::ofstream cellOut(path, std::ios::out | std::ios::trunc);
        if (!cellOut.is_open()) {
            return;
        }
        cellOut << "x,y,tile,terrain,level,slopeIndex,occupied,surfaceBlocked,slopeable\n";
        for (int y = 0; y < cellSide_; ++y) {
            for (int x = 0; x < cellSide_; ++x) {
                const Point cell{x, y};
                const CellInfo* info = cellInfo(cell);
                if (info == nullptr) {
                    continue;
                }
                cellOut << x << ',' << y << ','
                    << info->tileTypeIndex << ',' << static_cast<int>(info->terrain) << ','
                    << info->level << ',' << info->slopeIndex << ','
                    << (info->occupied ? 1 : 0) << ',' << (info->surfaceBlocked ? 1 : 0) << ','
                    << (info->slopeable ? 1 : 0) << '\n';
            }
        }
    }

    int cellLevel(Point cell) const noexcept
    {
        const CellInfo* info = cellInfo(cell);
        return info == nullptr ? kNativeBaseMapLevel : info->level;
    }

    int groundHeight(Point cell) const
    {
        const CellInfo* info = cellInfo(cell);
        if (info == nullptr || !info->slopeable) {
            return cellLevel(cell);
        }

        const auto corners = cellCorners(cell);
        int minimum = std::numeric_limits<int>::max();
        for (Point corner : corners) {
            minimum = std::min(minimum, vertexAt(corner).height);
        }
        return minimum / 15;
    }

    int cliffMask(Point cell, int direction) const
    {
        const auto corners = cellCorners(cell);
        int minimum = std::numeric_limits<int>::max();
        int maximum = std::numeric_limits<int>::min();
        for (Point corner : corners) {
            const int height = vertexAt(corner).height;
            minimum = std::min(minimum, height);
            maximum = std::max(maximum, height);
        }

        int mask = 0;
        for (std::size_t i = 0; i < corners.size(); ++i) {
            const Vertex& corner = vertexAt(corners[i]);
            if (corner.blocked) {
                continue;
            }
            if (maximum == minimum
                || (direction <= 0 && corner.height > minimum)
                || (direction > 0 && corner.height < maximum)) {
                mask |= 1 << static_cast<int>(i);
            }
        }
        return mask;
    }

    bool raiseOrLowerCell(int direction, bool force, Point cell, int mask)
    {
        std::vector<UndoEntry> undo;
        undo.reserve(16);
        const auto corners = cellCorners(cell);
        constexpr std::array<std::size_t, 4> kRaiseCornerOrder{{0, 1, 3, 2}};
        for (std::size_t cornerOffset : kRaiseCornerOrder) {
            if ((mask & (1 << static_cast<int>(cornerOffset))) == 0) {
                continue;
            }

            const Point cornerPoint = corners[cornerOffset];
            Vertex& corner = vertexAt(cornerPoint);
            if (corner.blocked) {
                if (!force) {
                    rollback(undo);
                    return false;
                }
                continue;
            }

            const int oldHeight = corner.height;
            const int newHeight = oldHeight + direction * 15;
            if (newHeight >= 0 && newHeight <= 180) {
                undo.push_back({cornerIndex(cornerPoint), oldHeight});
                corner.height = newHeight;
            }
            corner.dirty = true;
            if (!propagateHeight(direction, force, cornerPoint, undo) && !force) {
                rollback(undo);
                return false;
            }
        }
        return true;
    }

    void assignSlopes(MapGrid& map, std::vector<IsoTileOverride>& overrides) const
    {
        for (Cell& cell : map.cells()) {
            if (isSolidLand(cell)
                && !isEncodedCliffPiece(cell.slope)
                && !isEncodedShorePiece(cell.slope)) {
                cell.height = 0;
                if (cell.slope < 0) {
                    cell.slope = 0;
                }
            }
        }

        std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
        overrideIndexByIso.reserve(overrides.size());
        for (std::size_t i = 0; i < overrides.size(); ++i) {
            overrideIndexByIso[nativeIsoKey(overrides[i].iso)] = i;
        }

        for (Point nativeCell : nativeMapgenWorkspaceScanOrder(map)) {
            const CellInfo* info = cellInfo(nativeCell);
            if (info == nullptr || !info->slopeable) {
                continue;
            }

            const auto corners = cellCorners(nativeCell);
            bool dirty = false;
            int minimum = std::numeric_limits<int>::max();
            int maximum = std::numeric_limits<int>::min();
            std::array<int, 4> heights{};
            for (std::size_t i = 0; i < corners.size(); ++i) {
                const Vertex& corner = vertexAt(corners[i]);
                dirty = dirty || corner.dirty;
                heights[i] = corner.height;
                minimum = std::min(minimum, corner.height);
                maximum = std::max(maximum, corner.height);
            }
            if (!dirty || maximum - minimum > 15) {
                continue;
            }

            for (int& height : heights) {
                height -= minimum;
            }
            const auto patternIt = std::find(kNativeHeightSlopePatterns.begin(), kNativeHeightSlopePatterns.end(), heights);
            if (patternIt == kNativeHeightSlopePatterns.end()) {
                continue;
            }

            const int slopeIndex = static_cast<int>(std::distance(kNativeHeightSlopePatterns.begin(), patternIt));
            const int level = minimum / 15;
            const int landType = info->landType;
            const Point source = nativeCellToSourcePoint(map, nativeCell);
            if (map.inBounds(source)) {
                Cell& cell = map.at(source);
                cell.terrain = Terrain::Land;
                cell.height = 0;
                cell.level = level;
                cell.slope = slopeIndex;
                cell.landType = landType;
            }

            const std::uint64_t key = nativeIsoKey(nativeCell);
            if (const auto overrideIt = overrideIndexByIso.find(key); overrideIt != overrideIndexByIso.end()) {
                IsoTileOverride& override = overrides[overrideIt->second];
                override.terrain = Terrain::Land;
                override.variant = 0;
                override.subTile = 0;
                override.level = level;
                override.landType = landType;
                override.slopeIndex = slopeIndex;
                override.nativeTileTypeIndex = -1;
            } else if (!map.inBounds(source)) {
                overrides.push_back({nativeCell, Terrain::Land, 0, 0, level, false, landType, slopeIndex});
                overrideIndexByIso[key] = overrides.size() - 1;
            }
        }
    }

private:
    struct Vertex {
        int height = 0;
        bool blocked = false;
        bool dirty = false;
    };

    struct CellInfo {
        bool slopeable = false;
        bool occupied = false;
        bool surfaceBlocked = false;
        int level = kNativeBaseMapLevel;
        int slopeIndex = 0;
        int tileTypeIndex = kNativeTileWaterBase;
        int landType = 0;
        Terrain terrain = Terrain::Water;
    };

    struct UndoEntry {
        std::size_t index = 0;
        int height = 0;
    };

    bool isActiveCell(Point cell) const noexcept
    {
        return cell.x >= 0
            && cell.y >= 0
            && cell.x < cellSide_
            && cell.y < cellSide_
            && isInsideNativeCellDiamond(cell, mapRect_);
    }

    std::size_t cornerIndex(Point point) const noexcept
    {
        return static_cast<std::size_t>(point.y * vertexSide_ + point.x);
    }

    Vertex& vertexAt(Point point)
    {
        return vertices_[cornerIndex(point)];
    }

    const Vertex& vertexAt(Point point) const
    {
        return vertices_[cornerIndex(point)];
    }

    std::array<Point, 4> cellCorners(Point cell) const noexcept
    {
        return {{{cell.x, cell.y}, {cell.x + 1, cell.y}, {cell.x + 1, cell.y + 1}, {cell.x, cell.y + 1}}};
    }

    const CellInfo* cellInfo(Point cell) const noexcept
    {
        const auto it = cells_.find(nativeIsoKey(cell));
        return it == cells_.end() ? nullptr : &it->second;
    }

    const CellInfo* baseCellForVertex(Point vertex) const noexcept
    {
        constexpr std::array<Point, 4> kCandidateOffsets{{{0, 0}, {0, -1}, {-1, 0}, {-1, -1}}};
        for (Point offset : kCandidateOffsets) {
            const Point candidate{vertex.x + offset.x, vertex.y + offset.y};
            if (!isActiveCell(candidate)) {
                continue;
            }
            if (const CellInfo* info = cellInfo(candidate)) {
                return info;
            }
        }
        return nullptr;
    }

    bool vertexIsBlocked(Point vertex) const noexcept
    {
        constexpr std::array<Point, 4> kAdjacentCells{{{0, 0}, {0, -1}, {-1, -1}, {-1, 0}}};
        for (Point offset : kAdjacentCells) {
            const Point candidate{vertex.x + offset.x, vertex.y + offset.y};
            if (!isActiveCell(candidate)) {
                continue;
            }
            const CellInfo* info = cellInfo(candidate);
            if (info == nullptr || !info->slopeable) {
                return true;
            }
        }
        return false;
    }

    bool propagateHeight(int direction, bool force, Point current, std::vector<UndoEntry>& undo)
    {
        const int currentHeight = vertexAt(current).height;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }

                const Point neighbor{current.x + dx, current.y + dy};
                if (neighbor.x < 0 || neighbor.y < 0 || neighbor.x >= vertexSide_ || neighbor.y >= vertexSide_) {
                    if (!force) {
                        return false;
                    }
                    continue;
                }

                Vertex& adjacent = vertexAt(neighbor);
                if (adjacent.blocked) {
                    if (std::abs(adjacent.height - currentHeight) > 15) {
                        return false;
                    }
                    continue;
                }
                if (std::abs(adjacent.height - currentHeight) <= 15) {
                    continue;
                }

                const int oldHeight = adjacent.height;
                if (direction == 1 && adjacent.height < currentHeight) {
                    adjacent.height = currentHeight - 15;
                    undo.push_back({cornerIndex(neighbor), oldHeight});
                    adjacent.dirty = true;
                } else if (direction != 1 && adjacent.height > currentHeight) {
                    adjacent.height = currentHeight + 15;
                    undo.push_back({cornerIndex(neighbor), oldHeight});
                    adjacent.dirty = true;
                }

                if (propagateHeight(direction, force, neighbor, undo) || force) {
                    continue;
                }
                return false;
            }
        }
        return true;
    }

    void rollback(const std::vector<UndoEntry>& undo)
    {
        for (auto it = undo.rbegin(); it != undo.rend(); ++it) {
            vertices_[it->index].height = it->height;
        }
    }

    Rect mapRect_;
    int cellSide_ = 0;
    int vertexSide_ = 0;
    std::vector<Vertex> vertices_;
    std::unordered_map<std::uint64_t, CellInfo> cells_;
};

std::vector<int> NativeMapGenerator::generateHeightNoise(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng)
{
    const Rect mapRect = nativeSavedMapRect(map);
    const int workspaceSize = mapRect.w + mapRect.h + 1;
    std::vector<int> offsets(static_cast<std::size_t>(workspaceSize * workspaceSize), 0);
    const char* heightNoiseDumpPath = std::getenv(kNativeHeightNoiseDumpPathEnv);
    const bool captureHeightNoiseDetails = heightNoiseDumpPath != nullptr && heightNoiseDumpPath[0] != '\0';
    const char* heightNoiseTraceIndexText = std::getenv(kNativeHeightNoiseTraceIndexEnv);
    const long heightNoiseTraceIndex = heightNoiseTraceIndexText != nullptr
        ? std::strtol(heightNoiseTraceIndexText, nullptr, 10)
        : -1;
    std::vector<double> derivativeGaussians;
    std::vector<double> heightGaussians;
    std::vector<int> derivativeGaussianRawDraws;
    std::vector<int> heightGaussianRawDraws;
    std::vector<int> derivativeGaussianAttempts;
    std::vector<int> heightGaussianAttempts;
    std::vector<std::uint32_t> derivativeGaussianRaw0;
    std::vector<std::uint32_t> derivativeGaussianRaw1;
    std::vector<std::uint32_t> heightGaussianRaw0;
    std::vector<std::uint32_t> heightGaussianRaw1;
    if (captureHeightNoiseDetails) {
        derivativeGaussians.resize(offsets.size());
        heightGaussians.resize(offsets.size());
        derivativeGaussianRawDraws.resize(offsets.size());
        heightGaussianRawDraws.resize(offsets.size());
        derivativeGaussianAttempts.resize(offsets.size());
        heightGaussianAttempts.resize(offsets.size());
        derivativeGaussianRaw0.resize(offsets.size());
        derivativeGaussianRaw1.resize(offsets.size());
        heightGaussianRaw0.resize(offsets.size());
        heightGaussianRaw1.resize(offsets.size());
    }
    const int nativeRuggedness = std::max(0, options.ruggedness);
    const double driftStep = nativeX87Chop53MultiplyAdd(static_cast<double>(nativeRuggedness), 0.0001, 0.1);
    const double derivativeSeed = nativeX87Chop53Multiply(static_cast<double>(nativeRuggedness), 0.0025);
    const double derivativeLimit = nativeX87Chop53Multiply(static_cast<double>(nativeRuggedness), 0.005);
    if (derivativeSeed < 0.025) {
        ++report_.heightNoiseSkippedLowRuggedness;
        return offsets;
    }

    auto index = [workspaceSize](Point p) {
        return static_cast<std::size_t>(p.y * workspaceSize + p.x);
    };

    auto isHeightNoiseCell = [&mapRect, workspaceSize](Point p) {
        return p.x >= 0
            && p.y >= 0
            && p.x < workspaceSize
            && p.y < workspaceSize
            && isInsideNativeCellDiamond(p, mapRect);
    };

    struct HeightGaussianSample {
        double value = 0.0;
        double gaussian = 0.0;
        int rawDraws = 0;
        int attempts = 0;
        std::uint32_t raw0 = 0;
        std::uint32_t raw1 = 0;
    };
    auto sampleTruncatedGaussian = [this, &rng, captureHeightNoiseDetails](
                                       double mean,
                                       double deviation,
                                       double minimum,
                                       double maximum) {
        HeightGaussianSample result;
        result.value = mean;
        do {
            std::vector<std::uint32_t> rawValues;
            result.gaussian = captureHeightNoiseDetails
                ? rng.gaussianWithRawTrace(&rawValues)
                : rng.gaussian();
            result.rawDraws = static_cast<int>(rawValues.size());
            if (rawValues.size() >= 2) {
                result.raw0 = rawValues[rawValues.size() - 2];
                result.raw1 = rawValues[rawValues.size() - 1];
            } else {
                result.raw0 = 0;
                result.raw1 = 0;
            }
            ++result.attempts;
            result.value = nativeX87Chop53MultiplyAdd(result.gaussian, deviation, mean);
            ++report_.heightNoiseGaussianSamples;
        } while (result.value < minimum || result.value > maximum);
        return result;
    };

    std::vector<double> heightWave(offsets.size(), 0.0);
    std::vector<double> derivativeWave(offsets.size(), 0.0);

    for (int y = 0; y < workspaceSize; ++y) {
        for (int x = 0; x < workspaceSize; ++x) {
            const Point p{x, y};
            if (!isHeightNoiseCell(p)) {
                continue;
            }

            ++report_.heightNoiseCells;
            const std::size_t current = index(p);
            if (nativeMapgenHalfHeightKeys_.contains(nativeIsoKey(p))) {
                heightWave[current] = 0.5;
            }
            const Point diagonal{x - 1, y - 1};
            const double diagonalHeight = isHeightNoiseCell(diagonal) ? heightWave[index(diagonal)] : 0.0;

            double driftSign = 0.0;
            const Point west{x - 1, y};
            if (isHeightNoiseCell(west)) {
                const std::size_t westIndex = index(west);
                heightWave[current] = nativeX87Chop53Add(heightWave[current], heightWave[westIndex]);
                derivativeWave[current] = derivativeWave[westIndex];
                driftSign = nativeX87Chop53Subtract(heightWave[westIndex], diagonalHeight);
            } else {
                derivativeWave[current] = derivativeSeed;
            }

            const Point north{x, y - 1};
            if (isHeightNoiseCell(north)) {
                const std::size_t northIndex = index(north);
                heightWave[current] = nativeX87Chop53Add(heightWave[northIndex], heightWave[current]);
                derivativeWave[current] = nativeX87Chop53Add(derivativeWave[northIndex], derivativeWave[current]);
                driftSign = nativeX87Chop53Add(
                    nativeX87Chop53Subtract(heightWave[northIndex], diagonalHeight),
                    driftSign);
            } else {
                derivativeWave[current] = nativeX87Chop53Add(derivativeSeed, derivativeWave[current]);
            }

            heightWave[current] = nativeX87Chop53Multiply(heightWave[current], 0.5);
            derivativeWave[current] = nativeX87Chop53Multiply(derivativeWave[current], 0.5);
            const double averagedHeight = heightWave[current];
            const double averagedDerivative = derivativeWave[current];
            const double accumulatedDrift = driftSign;
            if (driftSign > 0.0) {
                driftSign = driftStep;
            } else if (driftSign < 0.0) {
                driftSign = -driftStep;
            }

            if (nativeMapgenSurfaceBlockedKeys_.contains(nativeIsoKey(p))) {
                heightWave[current] = std::max(0.0, heightWave[current]);
                derivativeWave[current] = 0.0025;
                ++report_.heightNoiseExclusionClamps;
            }
            const double conditionedHeight = heightWave[current];

            const double derivativeMinimum = -derivativeWave[current];
            const double derivativeMaximum = nativeX87Chop53Subtract(derivativeLimit, derivativeWave[current]);
            double derivativeMean = 0.0;
            double derivativeDeviation = 0.025;
            if (-0.025 > derivativeMaximum || derivativeMinimum > 0.025) {
                derivativeDeviation = nativeX87Chop53Multiply(
                    nativeX87Chop53Subtract(derivativeMaximum, derivativeMinimum),
                    0.5);
                derivativeMean = nativeX87Chop53Subtract(derivativeDeviation, derivativeWave[current]);
            }
            const HeightGaussianSample derivativeSample = sampleTruncatedGaussian(
                derivativeMean,
                derivativeDeviation,
                derivativeMinimum,
                derivativeMaximum);
            derivativeWave[current] = nativeX87Chop53Add(derivativeSample.value, derivativeWave[current]);
            if (captureHeightNoiseDetails) {
                derivativeGaussians[current] = derivativeSample.gaussian;
                derivativeGaussianRawDraws[current] = derivativeSample.rawDraws;
                derivativeGaussianAttempts[current] = derivativeSample.attempts;
                derivativeGaussianRaw0[current] = derivativeSample.raw0;
                derivativeGaussianRaw1[current] = derivativeSample.raw1;
            }

            const double heightMinimum = nativeX87Chop53Subtract(-2.0, heightWave[current]);
            const double heightMaximum = nativeX87Chop53Subtract(2.0, heightWave[current]);
            double heightMean = std::clamp(driftSign, heightMinimum, heightMaximum);
            double heightDeviation = derivativeWave[current];
            if (nativeX87Chop53Subtract(heightMean, heightDeviation) > heightMaximum
                || nativeX87Chop53Add(heightDeviation, heightMean) < heightMinimum) {
                heightDeviation = nativeX87Chop53Multiply(
                    nativeX87Chop53Subtract(heightMaximum, heightMinimum),
                    0.5);
                heightMean = nativeX87Chop53Add(heightDeviation, heightMinimum);
            }
            const HeightGaussianSample heightSample = sampleTruncatedGaussian(
                heightMean,
                heightDeviation,
                heightMinimum,
                heightMaximum);
            heightWave[current] = nativeX87Chop53Add(heightSample.value, heightWave[current]);
            if (heightNoiseTraceIndex == static_cast<long>(current)) {
                auto traceDouble = [](const char* name, double value) {
                    std::fprintf(
                        stderr,
                        "height-noise %s=%.17g bits=%016llX\n",
                        name,
                        value,
                        static_cast<unsigned long long>(std::bit_cast<std::uint64_t>(value)));
                };
                std::fprintf(stderr, "height-noise index=%zu x=%d y=%d\n", current, x, y);
                traceDouble("averagedHeight", averagedHeight);
                traceDouble("averagedDerivative", averagedDerivative);
                traceDouble("accumulatedDrift", accumulatedDrift);
                traceDouble("driftSign", driftSign);
                traceDouble("conditionedHeight", conditionedHeight);
                traceDouble("derivativeMinimum", derivativeMinimum);
                traceDouble("derivativeMaximum", derivativeMaximum);
                traceDouble("derivativeMean", derivativeMean);
                traceDouble("derivativeDeviation", derivativeDeviation);
                traceDouble("derivativeGaussian", derivativeSample.gaussian);
                traceDouble("derivativeSample", derivativeSample.value);
                traceDouble("finalDerivative", derivativeWave[current]);
                traceDouble("heightMinimum", heightMinimum);
                traceDouble("heightMaximum", heightMaximum);
                traceDouble("heightMean", heightMean);
                traceDouble("heightDeviation", heightDeviation);
                traceDouble("heightGaussian", heightSample.gaussian);
                traceDouble("heightSample", heightSample.value);
                traceDouble("finalHeight", heightWave[current]);
            }
            if (captureHeightNoiseDetails) {
                heightGaussians[current] = heightSample.gaussian;
                heightGaussianRawDraws[current] = heightSample.rawDraws;
                heightGaussianAttempts[current] = heightSample.attempts;
                heightGaussianRaw0[current] = heightSample.raw0;
                heightGaussianRaw1[current] = heightSample.raw1;
            }
        }
    }

    for (int y = 0; y < workspaceSize; ++y) {
        for (int x = 0; x < workspaceSize; ++x) {
            const Point p{x, y};
            if (!isHeightNoiseCell(p)) {
                continue;
            }

            const std::size_t cellIndex = index(p);
            const double wave = heightWave[cellIndex];
            const int offset = nativeDoubleToInt(wave >= 0.0
                    ? nativeX87Chop53Add(wave, 0.5)
                    : nativeX87Chop53Subtract(wave, 0.5));
            offsets[cellIndex] = offset;
            if (offset > 0) {
                ++report_.heightNoiseRaisedCells;
                report_.heightNoiseTotalRaiseSteps += offset;
            } else if (offset < 0) {
                ++report_.heightNoiseLoweredCells;
                report_.heightNoiseTotalLowerSteps += -offset;
            }
        }
    }

    if (captureHeightNoiseDetails) {
        std::ofstream out(heightNoiseDumpPath, std::ios::out | std::ios::trunc);
        if (out.is_open()) {
            out << "index,x,y,active,noise,derivative,offset,surfaceBlocked,halfHeightSeed,derivativeGaussian,derivativeRawDraws,derivativeAttempts,derivativeRaw0,derivativeRaw1,heightGaussian,heightRawDraws,heightAttempts,heightRaw0,heightRaw1\n";
            out << std::setprecision(17);
            for (int y = 0; y < workspaceSize; ++y) {
                for (int x = 0; x < workspaceSize; ++x) {
                    const Point p{x, y};
                    const std::size_t cellIndex = index(p);
                    const bool active = isHeightNoiseCell(p);
                    out << cellIndex << ','
                        << x << ','
                        << y << ','
                        << (active ? 1 : 0) << ','
                        << heightWave[cellIndex] << ','
                        << derivativeWave[cellIndex] << ','
                        << offsets[cellIndex] << ','
                        << (nativeMapgenSurfaceBlockedKeys_.contains(nativeIsoKey(p)) ? 1 : 0) << ','
                        << (nativeMapgenHalfHeightKeys_.contains(nativeIsoKey(p)) ? 1 : 0) << ','
                        << derivativeGaussians[cellIndex] << ','
                        << derivativeGaussianRawDraws[cellIndex] << ','
                        << derivativeGaussianAttempts[cellIndex] << ','
                        << derivativeGaussianRaw0[cellIndex] << ','
                        << derivativeGaussianRaw1[cellIndex] << ','
                        << heightGaussians[cellIndex] << ','
                        << heightGaussianRawDraws[cellIndex] << ','
                        << heightGaussianAttempts[cellIndex] << ','
                        << heightGaussianRaw0[cellIndex] << ','
                        << heightGaussianRaw1[cellIndex] << '\n';
                }
            }
        }
    }

    return offsets;
}
