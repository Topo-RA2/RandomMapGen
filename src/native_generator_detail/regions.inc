void NativeMapGenerator::resetRegionSelectionTrace() const
{
    const char* dumpPath = std::getenv(kNativeRegionDumpPathEnv);
    if (dumpPath == nullptr || dumpPath[0] == '\0') {
        return;
    }

    const std::filesystem::path path{dumpPath};
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);

    std::string cellPath = dumpPath;
    const auto dot = cellPath.rfind('.');
    const auto slash = cellPath.find_last_of("\\/");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        cellPath.insert(dot, ".cells");
    } else {
        cellPath += ".cells.csv";
    }
    std::filesystem::remove(std::filesystem::path{cellPath}, ec);

    std::string allCellsPath = dumpPath;
    const auto allDot = allCellsPath.rfind('.');
    const auto allSlash = allCellsPath.find_last_of("\\/");
    if (allDot != std::string::npos && (allSlash == std::string::npos || allDot > allSlash)) {
        allCellsPath.insert(allDot, ".allcells");
    } else {
        allCellsPath += ".allcells.csv";
    }
    std::filesystem::remove(std::filesystem::path{allCellsPath}, ec);
}

void NativeMapGenerator::dumpRegionSelectionTrace(const char* event, const MapSeedOptions& options, const std::vector<NativeRegion>& regions) const
{
    const char* dumpPath = std::getenv(kNativeRegionDumpPathEnv);
    if (dumpPath == nullptr || dumpPath[0] == '\0') {
        return;
    }

    auto traceCells = [](const NativeRegion& region) -> const std::vector<Point>& {
        return region.nativeCells.empty() ? region.cells : region.nativeCells;
    };
    auto traceBounds = [](const NativeRegion& region) {
        return region.nativeBounds.w > 0 && region.nativeBounds.h > 0 ? region.nativeBounds : region.bounds;
    };

    std::unordered_map<std::uint64_t, const IsoTileOverride*> overrideByIso;
    overrideByIso.reserve(report_.isoTileOverrides.size());
    for (const IsoTileOverride& override : report_.isoTileOverrides) {
        overrideByIso[nativeIsoKey(override.iso)] = &override;
    }
    std::unordered_map<std::uint64_t, const NativeOverlayOverride*> overlayByIso;
    overlayByIso.reserve(report_.nativeOverlayOverrides.size());
    for (const NativeOverlayOverride& override : report_.nativeOverlayOverrides) {
        overlayByIso[nativeIsoKey(override.iso)] = &override;
    }

    const std::size_t totalCells = std::accumulate(regions.begin(), regions.end(), std::size_t{0}, [&traceCells](std::size_t total, const NativeRegion& region) {
        return total + traceCells(region).size();
    });

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
        out << "event,index,regionIdOrPtr,zoneId,cellCount,vectorCount,quota,selectedCount,totalCells,numPlayers,minX,minY,maxX,maxY\n";
    }

    std::string cellPath = dumpPath;
    const auto dot = cellPath.rfind('.');
    const auto slash = cellPath.find_last_of("\\/");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        cellPath.insert(dot, ".cells");
    } else {
        cellPath += ".cells.csv";
    }
    const std::filesystem::path cellDumpPath{cellPath};
    const bool writeCellHeader = !std::filesystem::exists(cellDumpPath, ec) || std::filesystem::file_size(cellDumpPath, ec) == 0;
    std::ofstream cellOut(cellDumpPath, std::ios::out | std::ios::app);
    if (writeCellHeader) {
        cellOut << "event,index,regionIdOrPtr,zoneId,cellCount,vectorCount,cellIndex,x,y,packed,"
                   "isoTileTypeIndex,landType,height,level,slopeIndex,overlayTypeIndex,overlayData\n";
    }

    const char* eventName = event ? event : "begin";
    out << eventName << ','
        << -1 << ','
        << 0 << ','
        << 0 << ','
        << 0 << ','
        << 0 << ','
        << 0 << ','
        << static_cast<int>(regions.size()) << ','
        << totalCells << ','
        << options.numPlayers << ','
        << -1 << ','
        << -1 << ','
        << -1 << ','
        << -1 << '\n';

    for (std::size_t index = 0; index < regions.size(); ++index) {
        const NativeRegion& region = regions[index];
        const auto& cells = traceCells(region);
        const Rect bounds = traceBounds(region);
        const int maxX = bounds.w > 0 ? bounds.x + bounds.w - 1 : -1;
        const int maxY = bounds.h > 0 ? bounds.y + bounds.h - 1 : -1;
        out << eventName << ','
            << index << ','
            << region.id << ','
            << region.id << ','
            << cells.size() << ','
            << cells.size() << ','
            << region.nativeWaypointQuota << ','
            << static_cast<int>(regions.size()) << ','
            << totalCells << ','
            << options.numPlayers << ','
            << (bounds.w > 0 ? bounds.x : -1) << ','
            << (bounds.h > 0 ? bounds.y : -1) << ','
            << maxX << ','
            << maxY << '\n';

        if (cellOut.is_open()) {
            for (std::size_t cellIndex = 0; cellIndex < cells.size(); ++cellIndex) {
                const Point cell = cells[cellIndex];
                const std::uint64_t key = nativeIsoKey(cell);
                const int packed = (cell.y << 16) | (cell.x & 0xFFFF);
                const auto overrideIt = overrideByIso.find(key);
                const IsoTileOverride* isoOverride = overrideIt == overrideByIso.end()
                    ? nullptr
                    : overrideIt->second;
                const auto overlayIt = overlayByIso.find(key);
                const NativeOverlayOverride* overlayOverride = overlayIt == overlayByIso.end()
                    ? nullptr
                    : overlayIt->second;
                cellOut << eventName << ','
                    << index << ','
                    << region.id << ','
                    << region.id << ','
                    << cells.size() << ','
                    << cells.size() << ','
                    << cellIndex << ','
                    << cell.x << ','
                    << cell.y << ','
                    << packed << ','
                    << (isoOverride ? nativeTileTypeIndexForOverride(*isoOverride) : -1) << ','
                    << (isoOverride ? isoOverride->landType : -1) << ','
                    << (isoOverride ? isoOverride->subTile : -1) << ','
                    << (isoOverride ? isoOverride->level : -1) << ','
                    << (isoOverride ? isoOverride->slopeIndex : -1) << ','
                    << (overlayOverride ? overlayOverride->overlayTypeIndex : -1) << ','
                    << (overlayOverride ? overlayOverride->overlayData : 0) << '\n';
            }
        }
    }
}

std::vector<NativeMapGenerator::NativeRegion> NativeMapGenerator::buildPassableRegions(
    MapGrid& map,
    const MapSeedOptions& options,
    const char* tracePrefix)
{
    auto traceEvent = [tracePrefix](const char* suffix) {
        std::string event;
        if (tracePrefix && tracePrefix[0] != '\0') {
            event += tracePrefix;
            event += '_';
        }
        event += suffix;
        return event;
    };
    auto regionAllCellsPath = []() -> std::filesystem::path {
        const char* dumpPath = std::getenv(kNativeRegionDumpPathEnv);
        if (dumpPath == nullptr || dumpPath[0] == '\0') {
            return {};
        }
        std::string path = dumpPath;
        const auto dot = path.rfind('.');
        const auto slash = path.find_last_of("\\/");
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
            path.insert(dot, ".allcells");
        } else {
            path += ".allcells.csv";
        }
        return std::filesystem::path{path};
    };
    auto dumpAllCellsTrace = [&](const char* eventName) {
        const std::filesystem::path path = regionAllCellsPath();
        if (path.empty()) {
            return;
        }
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
            out << "event,x,y,insideDiamond,sourceX,sourceY,sourceInBounds,terrain,landType,height,level,slope,passable,candidate,regionTag,"
                   "overrideTerrain,overrideVariant,overrideSubTile,overrideLandType,effectiveTile,effectiveHeight,effectiveLevel,effectiveSlopeIndex,effectiveLandType,"
                   "isoTileTemplateFootprint,initialCliffZoneId,initialCliffTile\n";
        }

        const Rect mapRect = nativeSavedMapRect(map);
        std::unordered_map<std::uint64_t, const IsoTileOverride*> overrideByIso;
        overrideByIso.reserve(report_.isoTileOverrides.size());
        for (const IsoTileOverride& override : report_.isoTileOverrides) {
            overrideByIso[nativeIsoKey(override.iso)] = &override;
        }
        const int workspaceSize = mapRect.w + mapRect.h + 1;
        for (int y = 0; y < workspaceSize; ++y) {
            for (int x = 0; x < workspaceSize; ++x) {
                const Point nativeCell{x, y};
                const bool inside = isInsideNativeCellDiamond(nativeCell, mapRect);
                const Point source = nativeCellToSourcePoint(map, nativeCell);
                const bool inBounds = map.inBounds(source);
                const Cell* cell = inBounds ? &map.at(source) : nullptr;
                const auto overrideIt = overrideByIso.find(nativeIsoKey(nativeCell));
                const IsoTileOverride* isoOverride = overrideIt == overrideByIso.end() ? nullptr : overrideIt->second;
                const bool candidate = isoOverride
                    ? isNativeRegionCandidateOverride(*isoOverride)
                    : (cell && isNativeRegionCandidateCell(*cell, nativeCell));
                const int effectiveTile = isoOverride
                    ? nativeTileTypeIndexForOverride(*isoOverride)
                    : (cell ? nativeTileTypeIndexForTerrain(*cell) : -1);
                const int effectiveHeight = isoOverride
                    ? isoOverride->subTile
                    : (cell ? cell->height : -1);
                const int effectiveLevel = isoOverride
                    ? isoOverride->level
                    : (cell ? cell->level : -1);
                const int effectiveSlopeIndex = isoOverride
                    ? isoOverride->slopeIndex
                    : (cell ? nativeSlopeIndexForCell(*cell) : -1);
                const int effectiveLandType = isoOverride
                    ? isoOverride->landType
                    : (cell ? nativeLandTypeForCell(*cell) : -1);
                const auto initialZoneIt = nativeInitialCliffZoneIds_.find(nativeIsoKey(nativeCell));
                const int initialCliffZoneId = initialZoneIt == nativeInitialCliffZoneIds_.end()
                    ? 0
                    : initialZoneIt->second;
                const auto initialTileIt = nativeInitialCliffTiles_.find(nativeIsoKey(nativeCell));
                const int initialCliffTile = initialTileIt == nativeInitialCliffTiles_.end()
                    ? -1
                    : initialTileIt->second;
                out << (eventName ? eventName : "") << ','
                    << x << ','
                    << y << ','
                    << (inside ? 1 : 0) << ','
                    << source.x << ','
                    << source.y << ','
                    << (inBounds ? 1 : 0) << ','
                    << (cell ? static_cast<int>(cell->terrain) : -1) << ','
                    << (cell ? cell->landType : -1) << ','
                    << (cell ? cell->height : -1) << ','
                    << (cell ? cell->level : -1) << ','
                    << (cell ? cell->slope : -1) << ','
                    << (cell && isPassable(*cell) ? 1 : 0) << ','
                    << (candidate ? 1 : 0) << ','
                    << (cell ? cell->regionTag : 0) << ','
                    << (isoOverride ? static_cast<int>(isoOverride->terrain) : -1) << ','
                    << (isoOverride ? isoOverride->variant : -1) << ','
                    << (isoOverride ? isoOverride->subTile : -1) << ','
                    << (isoOverride ? isoOverride->landType : -1) << ','
                    << effectiveTile << ','
                    << effectiveHeight << ','
                    << effectiveLevel << ','
                    << effectiveSlopeIndex << ','
                    << effectiveLandType << ','
                    << (nativeIsoTileTemplateFootprintKeys_.contains(nativeIsoKey(nativeCell)) ? 1 : 0) << ','
                    << initialCliffZoneId << ','
                    << initialCliffTile << '\n';
            }
        }
    };
    auto dumpTrace = [&](const char* suffix, const std::vector<NativeRegion>& traceRegions) {
        if (!tracePrefix || tracePrefix[0] == '\0') {
            return;
        }
        const std::string event = traceEvent(suffix);
        if (std::strcmp(suffix, "after_flood") == 0 || std::strcmp(suffix, "after_filter") == 0) {
            dumpAllCellsTrace(event.c_str());
        }
        dumpRegionSelectionTrace(event.c_str(), options, traceRegions);
    };

    const int minimumRegionSize = std::max(
        400,
        static_cast<int>(std::lround(static_cast<double>(map.width() * map.height()) * 0.03)));
    report_.regionCandidates = 0;
    report_.regionFilteredSmall = 0;
    report_.regionMinimumSize = minimumRegionSize;
    for (auto& cell : map.cells()) {
        cell.regionTag = 0;
    }
    std::vector<NativeRegion> allRegions;
    std::vector<int> visited(static_cast<std::size_t>(map.width() * map.height()), 0);
    int nextId = 1;

    auto index = [&map](Point p) {
        return static_cast<std::size_t>(p.y * map.width() + p.x);
    };

    for (const auto& source : map.cells()) {
        Point start{source.x, source.y};
        if (visited[index(start)] || !isNativeRegionCandidateCell(source, sourcePointToNativeCell(map, start))) {
            continue;
        }

        NativeRegion region;
        region.id = nextId++;
        region.bounds = {start.x, start.y, 1, 1};
        std::queue<Point> queue;
        visited[index(start)] = 1;
        queue.push(start);

        int minX = start.x;
        int minY = start.y;
        int maxX = start.x;
        int maxY = start.y;
        while (!queue.empty()) {
            const Point current = queue.front();
            queue.pop();
            region.cells.push_back(current);
            minX = std::min(minX, current.x);
            minY = std::min(minY, current.y);
            maxX = std::max(maxX, current.x);
            maxY = std::max(maxY, current.y);
            map.at(current).regionTag = region.id;

            for (Point next : map.neighbors4(current)) {
                if (!visited[index(next)] && isNativeRegionCandidateCell(map.at(next), sourcePointToNativeCell(map, next))) {
                    visited[index(next)] = 1;
                    queue.push(next);
                }
            }
        }

        region.bounds = {minX, minY, maxX - minX + 1, maxY - minY + 1};
        int levelSum = 0;
        int levelCount = 0;
        int wetCells = 0;
        for (Point p : region.cells) {
            const Cell& cell = map.at(p);
            levelSum += cell.level;
            ++levelCount;
            if (isWet(cell)) {
                ++wetCells;
            }
        }
        region.level = levelCount > 0 ? static_cast<int>(std::lround(static_cast<double>(levelSum) / static_cast<double>(levelCount))) : kNativeBaseMapLevel;
        region.wet = wetCells > 0;
        ++report_.regionCandidates;
        allRegions.push_back(std::move(region));
    }

    auto regionCellCount = [](const NativeRegion& region) {
        return region.nativeCells.empty() ? region.cells.size() : region.nativeCells.size();
    };
    auto nativeSeedLess = [](Point lhs, Point rhs) {
        const int lhsSum = lhs.x + lhs.y;
        const int rhsSum = rhs.x + rhs.y;
        if (lhsSum != rhsSum) {
            return lhsSum < rhsSum;
        }
        if (lhs.x != rhs.x) {
            return lhs.x < rhs.x;
        }
        return lhs.y < rhs.y;
    };
    auto refreshNativeCells = [&](std::vector<NativeRegion>& targetRegions) {
        auto reorderNativeRegionCells = [&nativeSeedLess](NativeRegion& region) {
            if (region.nativeCells.size() < 2) {
                return;
            }

            std::vector<Point> seeds = region.nativeCells;
            std::sort(seeds.begin(), seeds.end(), nativeSeedLess);

            std::unordered_set<std::uint64_t> remaining;
            remaining.reserve(region.nativeCells.size() * 2);
            for (Point cell : region.nativeCells) {
                remaining.insert(nativeIsoKey(cell));
            }

            std::vector<Point> ordered;
            ordered.reserve(region.nativeCells.size());
            constexpr std::array<Point, 8> kNativeRegionStackNeighborOrder{{
                {0, -1}, {1, -1}, {1, 0}, {1, 1},
                {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
            }};

            for (Point seed : seeds) {
                const std::uint64_t seedKey = nativeIsoKey(seed);
                if (remaining.erase(seedKey) == 0) {
                    continue;
                }

                std::vector<Point> stack;
                stack.push_back(seed);
                while (!stack.empty()) {
                    const Point current = stack.back();
                    stack.pop_back();
                    ordered.push_back(current);

                    for (Point offset : kNativeRegionStackNeighborOrder) {
                        const Point next{current.x + offset.x, current.y + offset.y};
                        const std::uint64_t key = nativeIsoKey(next);
                        if (remaining.erase(key) != 0) {
                            stack.push_back(next);
                        }
                    }
                }
            }

            if (ordered.size() == region.nativeCells.size()) {
                region.nativeCells = std::move(ordered);
            }
        };

        int maxRegionId = 0;
        for (NativeRegion& region : targetRegions) {
            region.nativeCells.clear();
            region.nativeBounds = {};
            maxRegionId = std::max(maxRegionId, region.id);
        }
        if (maxRegionId <= 0) {
            return;
        }

        std::vector<int> idToIndex(static_cast<std::size_t>(maxRegionId + 1), -1);
        for (std::size_t i = 0; i < targetRegions.size(); ++i) {
            if (targetRegions[i].id > 0 && targetRegions[i].id <= maxRegionId) {
                idToIndex[static_cast<std::size_t>(targetRegions[i].id)] = static_cast<int>(i);
            }
        }

        const Rect mapRect = nativeSavedMapRect(map);
        const bool includeSavedFrame = isNativeHighResourceMode(options);
        std::unordered_map<std::uint64_t, const IsoTileOverride*> overrideByIso;
        overrideByIso.reserve(report_.isoTileOverrides.size());
        for (const IsoTileOverride& override : report_.isoTileOverrides) {
            overrideByIso[nativeIsoKey(override.iso)] = &override;
        }

        auto adjacentRegionIdForShore = [&](Point source) {
            std::vector<int> counts(static_cast<std::size_t>(maxRegionId + 1), 0);
            for (Point neighbor : map.neighbors8(source)) {
                const int neighborId = map.at(neighbor).regionTag;
                if (neighborId > 0 && neighborId <= maxRegionId && idToIndex[static_cast<std::size_t>(neighborId)] >= 0) {
                    ++counts[static_cast<std::size_t>(neighborId)];
                }
            }

            int bestId = 0;
            int bestCount = 0;
            for (int id = 1; id <= maxRegionId; ++id) {
                const int count = counts[static_cast<std::size_t>(id)];
                if (count > bestCount) {
                    bestId = id;
                    bestCount = count;
                }
            }
            return bestId;
        };

        const int workspaceSize = mapRect.w + mapRect.h + 1;
        for (int y = 0; y < workspaceSize; ++y) {
            for (int x = 0; x < workspaceSize; ++x) {
                const Point nativeCell{x, y};
                if (!isInsideNativeCellDiamond(nativeCell, mapRect)) {
                    continue;
                }

                const Point source = nativeCellToSourcePoint(map, nativeCell);
                const Point mapPoint = nativeCellToMapPoint(nativeCell, mapRect.w);
                const bool hasSource = map.inBounds(source);
                if (!hasSource && (!includeSavedFrame || !contains(mapRect, mapPoint))) {
                    continue;
                }
                if (hasSource && source.y <= 1) {
                    continue;
                }

                const Cell* cell = hasSource ? &map.at(source) : nullptr;
                const auto overrideIt = overrideByIso.find(nativeIsoKey(nativeCell));
                const bool candidate = overrideIt == overrideByIso.end()
                    ? (cell != nullptr && isNativeRegionCandidateCell(*cell, nativeCell))
                    : isNativeRegionCandidateOverride(*overrideIt->second);
                int regionId = cell == nullptr ? 0 : cell->regionTag;
                if (regionId <= 0 && candidate && !hasSource && targetRegions.size() == 1) {
                    regionId = targetRegions.front().id;
                }
                if (regionId <= 0
                    && candidate
                    && hasSource
                    && overrideIt != overrideByIso.end()) {
                    regionId = adjacentRegionIdForShore(source);
                }
                if (regionId <= 0 || regionId > maxRegionId || !candidate) {
                    continue;
                }

                const int regionIndex = idToIndex[static_cast<std::size_t>(regionId)];
                if (regionIndex < 0) {
                    continue;
                }

                NativeRegion& region = targetRegions[static_cast<std::size_t>(regionIndex)];
                if (region.nativeCells.empty()) {
                    region.nativeBounds = {nativeCell.x, nativeCell.y, 1, 1};
                } else {
                    const int minX = std::min(region.nativeBounds.x, nativeCell.x);
                    const int minY = std::min(region.nativeBounds.y, nativeCell.y);
                    const int maxX = std::max(region.nativeBounds.x + region.nativeBounds.w - 1, nativeCell.x);
                    const int maxY = std::max(region.nativeBounds.y + region.nativeBounds.h - 1, nativeCell.y);
                    region.nativeBounds = {minX, minY, maxX - minX + 1, maxY - minY + 1};
                }
                region.nativeCells.push_back(nativeCell);
            }
        }

        for (NativeRegion& region : targetRegions) {
            reorderNativeRegionCells(region);
        }
    };
    auto mergeNativeAdjacentRegions = [&](std::vector<NativeRegion>& targetRegions) {
        const int count = static_cast<int>(targetRegions.size());
        if (count < 2) {
            return;
        }

        std::vector<int> parent(static_cast<std::size_t>(count));
        std::iota(parent.begin(), parent.end(), 0);
        auto findRoot = [&](int value) {
            int root = value;
            while (parent[static_cast<std::size_t>(root)] != root) {
                root = parent[static_cast<std::size_t>(root)];
            }
            while (parent[static_cast<std::size_t>(value)] != value) {
                const int next = parent[static_cast<std::size_t>(value)];
                parent[static_cast<std::size_t>(value)] = root;
                value = next;
            }
            return root;
        };
        auto unite = [&](int lhs, int rhs) {
            int leftRoot = findRoot(lhs);
            int rightRoot = findRoot(rhs);
            if (leftRoot == rightRoot) {
                return false;
            }
            if (rightRoot < leftRoot) {
                std::swap(leftRoot, rightRoot);
            }
            parent[static_cast<std::size_t>(rightRoot)] = leftRoot;
            return true;
        };

        std::unordered_map<std::uint64_t, int> ownerByNativeCell;
        std::size_t nativeCellCount = 0;
        for (const NativeRegion& region : targetRegions) {
            nativeCellCount += region.nativeCells.size();
        }
        ownerByNativeCell.reserve(nativeCellCount * 2);
        for (int regionIndex = 0; regionIndex < count; ++regionIndex) {
            for (Point nativeCell : targetRegions[static_cast<std::size_t>(regionIndex)].nativeCells) {
                ownerByNativeCell.emplace(nativeIsoKey(nativeCell), regionIndex);
            }
        }

        constexpr std::array<Point, 8> kNativeRegionMergeNeighbors{{
            {0, -1}, {1, -1}, {1, 0}, {1, 1},
            {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
        }};
        bool mergedAny = false;
        for (int regionIndex = 0; regionIndex < count; ++regionIndex) {
            const NativeRegion& region = targetRegions[static_cast<std::size_t>(regionIndex)];
            for (Point nativeCell : region.nativeCells) {
                for (Point offset : kNativeRegionMergeNeighbors) {
                    const Point neighbor{nativeCell.x + offset.x, nativeCell.y + offset.y};
                    const auto ownerIt = ownerByNativeCell.find(nativeIsoKey(neighbor));
                    if (ownerIt != ownerByNativeCell.end() && ownerIt->second != regionIndex) {
                        mergedAny = unite(regionIndex, ownerIt->second) || mergedAny;
                    }
                }
            }
        }
        if (!mergedAny) {
            return;
        }

        std::vector<NativeRegion> merged;
        std::vector<int> mergedIndexForRoot(static_cast<std::size_t>(count), -1);
        for (int regionIndex = 0; regionIndex < count; ++regionIndex) {
            const int root = findRoot(regionIndex);
            int& mergedIndex = mergedIndexForRoot[static_cast<std::size_t>(root)];
            if (mergedIndex < 0) {
                mergedIndex = static_cast<int>(merged.size());
                NativeRegion region;
                region.id = targetRegions[static_cast<std::size_t>(root)].id;
                merged.push_back(std::move(region));
            }
            NativeRegion& target = merged[static_cast<std::size_t>(mergedIndex)];
            const NativeRegion& sourceRegion = targetRegions[static_cast<std::size_t>(regionIndex)];
            target.cells.insert(target.cells.end(), sourceRegion.cells.begin(), sourceRegion.cells.end());
        }

        for (auto& cell : map.cells()) {
            cell.regionTag = 0;
        }
        for (NativeRegion& region : merged) {
            if (region.cells.empty()) {
                continue;
            }
            int minX = region.cells.front().x;
            int minY = region.cells.front().y;
            int maxX = region.cells.front().x;
            int maxY = region.cells.front().y;
            int levelSum = 0;
            int levelCount = 0;
            int wetCells = 0;
            for (Point p : region.cells) {
                Cell& cell = map.at(p);
                cell.regionTag = region.id;
                minX = std::min(minX, p.x);
                minY = std::min(minY, p.y);
                maxX = std::max(maxX, p.x);
                maxY = std::max(maxY, p.y);
                levelSum += cell.level;
                ++levelCount;
                if (isWet(cell)) {
                    ++wetCells;
                }
            }
            region.bounds = {minX, minY, maxX - minX + 1, maxY - minY + 1};
            region.level = levelCount > 0 ? static_cast<int>(std::lround(static_cast<double>(levelSum) / static_cast<double>(levelCount))) : kNativeBaseMapLevel;
            region.wet = wetCells > 0;
        }

        targetRegions = std::move(merged);
        refreshNativeCells(targetRegions);
    };

    refreshNativeCells(allRegions);
    dumpTrace("before_native_merge", allRegions);
    mergeNativeAdjacentRegions(allRegions);
    std::vector<NativeRegion*> nativeBaseZoneOrder;
    nativeBaseZoneOrder.reserve(allRegions.size());
    for (NativeRegion& region : allRegions) {
        nativeBaseZoneOrder.push_back(&region);
    }
    std::stable_sort(nativeBaseZoneOrder.begin(), nativeBaseZoneOrder.end(), [&nativeSeedLess](const NativeRegion* lhs, const NativeRegion* rhs) {
        if (lhs->nativeCells.empty() || rhs->nativeCells.empty()) {
            if (lhs->nativeCells.empty() != rhs->nativeCells.empty()) {
                return !lhs->nativeCells.empty();
            }
            return lhs->id < rhs->id;
        }
        if (lhs->nativeCells.front() != rhs->nativeCells.front()) {
            return nativeSeedLess(lhs->nativeCells.front(), rhs->nativeCells.front());
        }
        return lhs->id < rhs->id;
    });
    for (std::size_t zone = 0; zone < nativeBaseZoneOrder.size(); ++zone) {
        nativeBaseZoneOrder[zone]->nativeBaseZoneId = static_cast<int>(zone);
    }

    const bool initializingHighResourceRegions = isNativeHighResourceMode(options)
        && tracePrefix != nullptr
        && (std::strcmp(tracePrefix, "starts") == 0
            || std::strcmp(tracePrefix, "probe_before_starts") == 0);
    if (initializingHighResourceRegions) {
        struct NativePathCell {
            Point cell;
            int level = kNativeBaseMapLevel;
            int passability = 7;
            int baseZone = -1;
        };

        const Rect mapRect = nativeSavedMapRect(map);
        const Rect visibleRect = nativeLocalRect(map);
        std::vector<NativePathCell> pathCells;
        std::unordered_map<std::uint64_t, std::size_t> pathCellByIso;
        recalculateNativeHighResourceLandTypes(map);
        const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, report_.isoTileOverrides);
        std::unordered_map<std::uint64_t, std::size_t> iteratorCellByIso;
        iteratorCellByIso.reserve(iteratorCells.size() * 2);
        for (std::size_t index = 0; index < iteratorCells.size(); ++index) {
            iteratorCellByIso[nativeIsoKey(iteratorCells[index].cell)] = index;
        }
        pathCells.reserve(iteratorCells.size());
        pathCellByIso.reserve(iteratorCells.size() * 2);

        auto isWithinUsableArea = [&](const NativeIteratorCell& cell) {
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
        auto nativePassability = [&](const NativeIteratorCell& cell) {
            if (!isWithinUsableArea(cell)) {
                return 7;
            }
            if (cell.landType == 2) {
                return 4;
            }
            if (cell.landType == 6) {
                return 3;
            }
            if (cell.landType == 3) {
                return 6;
            }
            return 0;
        };

        for (const NativeIteratorCell& cell : iteratorCells) {
            const std::size_t cellIndex = pathCells.size();
            pathCellByIso.emplace(nativeIsoKey(cell.cell), cellIndex);
            pathCells.push_back({cell.cell, cell.level, nativePassability(cell), -1});
        }

        constexpr std::array<Point, 4> kPathfindingNeighbors{{
            {0, -1}, {1, 0}, {0, 1}, {-1, 0},
        }};
        std::vector<int> baseZonePassability;
        std::vector<int> baseZoneCellCounts;
        for (std::size_t startIndex = 0; startIndex < pathCells.size(); ++startIndex) {
            NativePathCell& start = pathCells[startIndex];
            if (start.passability == 7 || start.baseZone >= 0) {
                continue;
            }

            const int baseZone = static_cast<int>(baseZonePassability.size());
            baseZonePassability.push_back(start.passability);
            baseZoneCellCounts.push_back(0);
            std::vector<std::size_t> stack;
            start.baseZone = baseZone;
            stack.push_back(startIndex);
            while (!stack.empty()) {
                const std::size_t currentIndex = stack.back();
                stack.pop_back();
                const NativePathCell current = pathCells[currentIndex];
                ++baseZoneCellCounts[static_cast<std::size_t>(baseZone)];
                for (Point offset : kPathfindingNeighbors) {
                    const Point neighbor{current.cell.x + offset.x, current.cell.y + offset.y};
                    const auto neighborIt = pathCellByIso.find(nativeIsoKey(neighbor));
                    if (neighborIt == pathCellByIso.end()) {
                        continue;
                    }
                    NativePathCell& next = pathCells[neighborIt->second];
                    if (next.baseZone >= 0
                        || next.passability != current.passability
                        || std::abs(next.level - current.level) >= 2) {
                        continue;
                    }
                    next.baseZone = baseZone;
                    stack.push_back(neighborIt->second);
                }
            }
        }

        std::vector<std::unordered_set<int>> baseZoneNeighbors(baseZonePassability.size());
        // Region connectivity follows the native map-cell neighborhood.  The
        // path flood itself remains four-way, but adjacent base zones can
        // touch at an isometric diagonal corner and must still be considered
        // connected when selecting the normal movement class.
        constexpr std::array<Point, 4> kConnectionNeighbors{{
            {1, 0}, {0, 1}, {1, 1}, {1, -1},
        }};
        for (const NativePathCell& current : pathCells) {
            if (current.baseZone < 0) {
                continue;
            }
            for (Point offset : kConnectionNeighbors) {
                const Point neighbor{current.cell.x + offset.x, current.cell.y + offset.y};
                const auto neighborIt = pathCellByIso.find(nativeIsoKey(neighbor));
                if (neighborIt == pathCellByIso.end()) {
                    continue;
                }
                const NativePathCell& next = pathCells[neighborIt->second];
                if (next.baseZone < 0 || next.baseZone == current.baseZone) {
                    continue;
                }
                if (std::abs(next.level - current.level) >= 2
                    && current.passability != 6
                    && next.passability != 6) {
                    continue;
                }
                baseZoneNeighbors[static_cast<std::size_t>(current.baseZone)].insert(next.baseZone);
                baseZoneNeighbors[static_cast<std::size_t>(next.baseZone)].insert(current.baseZone);
            }
        }

        // MovementAdjustArray_82A594, Normal row (MovementZone 0).
        constexpr std::array<int, 8> kNormalMovementAdjust{{1, 2, 2, 2, 2, 2, 2, 3}};
        std::vector<bool> selectedMovementZones(baseZonePassability.size(), false);
        int requiredBaseZone = -1;
        int largestBaseZoneCellCount = 0;
        for (std::size_t zone = 0; zone < baseZoneCellCounts.size(); ++zone) {
            if (baseZoneCellCounts[zone] > largestBaseZoneCellCount) {
                largestBaseZoneCellCount = baseZoneCellCounts[zone];
                requiredBaseZone = static_cast<int>(zone);
            }
        }
        if (requiredBaseZone >= 0) {
            const int movementClass = kNormalMovementAdjust[static_cast<std::size_t>(
                std::clamp(baseZonePassability[static_cast<std::size_t>(requiredBaseZone)], 0, 7))];
            std::vector<int> stack{requiredBaseZone};
            selectedMovementZones[static_cast<std::size_t>(requiredBaseZone)] = true;
            while (!stack.empty()) {
                const int current = stack.back();
                stack.pop_back();
                for (int neighbor : baseZoneNeighbors[static_cast<std::size_t>(current)]) {
                    if (selectedMovementZones[static_cast<std::size_t>(neighbor)]
                        || kNormalMovementAdjust[static_cast<std::size_t>(
                            std::clamp(baseZonePassability[static_cast<std::size_t>(neighbor)], 0, 7))] != movementClass) {
                        continue;
                    }
                    selectedMovementZones[static_cast<std::size_t>(neighbor)] = true;
                    stack.push_back(neighbor);
                }
            }
        }

        if (const char* debugPath = std::getenv("RMG_NATIVE_PATH_DEBUG_PATH");
            debugPath != nullptr && debugPath[0] != '\0') {
            std::ofstream debug(debugPath, std::ios::out | std::ios::trunc);
            if (debug.is_open()) {
                debug << "tracePrefix,mapRectX,mapRectY,mapRectW,mapRectH,localRectX,localRectY,localRectW,localRectH,"
                         "x,y,level,slopeIndex,landType,passability,usable,baseZone,baseZonePassability,baseZoneCellCount,selected,"
                         "north,east,south,west\n";
                for (const NativePathCell& current : pathCells) {
                    std::array<int, 4> neighborZones{{-1, -1, -1, -1}};
                    for (std::size_t neighborIndex = 0; neighborIndex < kPathfindingNeighbors.size(); ++neighborIndex) {
                        const Point neighbor{
                            current.cell.x + kPathfindingNeighbors[neighborIndex].x,
                            current.cell.y + kPathfindingNeighbors[neighborIndex].y};
                        const auto neighborIt = pathCellByIso.find(nativeIsoKey(neighbor));
                        if (neighborIt != pathCellByIso.end()) {
                            neighborZones[neighborIndex] = pathCells[neighborIt->second].baseZone;
                        }
                    }
                    const int baseZone = current.baseZone;
                    const int zonePassability = baseZone >= 0
                        ? baseZonePassability[static_cast<std::size_t>(baseZone)]
                        : -1;
                    const int zoneCellCount = baseZone >= 0
                        ? baseZoneCellCounts[static_cast<std::size_t>(baseZone)]
                        : 0;
                    const int selected = baseZone >= 0
                        && selectedMovementZones[static_cast<std::size_t>(baseZone)]
                        ? 1
                        : 0;
                    debug << (tracePrefix ? tracePrefix : "") << ','
                          << mapRect.x << ',' << mapRect.y << ',' << mapRect.w << ',' << mapRect.h << ','
                          << visibleRect.x << ',' << visibleRect.y << ',' << visibleRect.w << ',' << visibleRect.h << ','
                          << current.cell.x << ',' << current.cell.y << ',' << current.level << ','
                          << iteratorCells[pathCellByIso.at(nativeIsoKey(current.cell))].slopeIndex << ','
                          << iteratorCells[pathCellByIso.at(nativeIsoKey(current.cell))].landType << ','
                          << current.passability << ',' << (isWithinUsableArea(iteratorCells[pathCellByIso.at(nativeIsoKey(current.cell))]) ? 1 : 0) << ','
                          << baseZone << ',' << zonePassability << ',' << zoneCellCount << ',' << selected << ','
                          << neighborZones[0] << ',' << neighborZones[1] << ',' << neighborZones[2] << ',' << neighborZones[3] << '\n';
                }
            }
        }

        for (Cell& cell : map.cells()) {
            cell.regionTag = 0;
        }
        allRegions.clear();
        std::vector<bool> assigned(pathCells.size(), false);
        constexpr std::array<Point, 8> kNativeRegionNeighbors{{
            {0, -1}, {1, -1}, {1, 0}, {1, 1},
            {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
        }};
        for (std::size_t startIndex = 0; startIndex < pathCells.size(); ++startIndex) {
            const NativePathCell& start = pathCells[startIndex];
            if (assigned[startIndex]
                || start.passability != 0
                || start.baseZone < 0
                || !selectedMovementZones[static_cast<std::size_t>(start.baseZone)]) {
                continue;
            }

            NativeRegion region;
            region.id = static_cast<int>(allRegions.size()) + 1;
            region.nativeBaseZoneId = start.baseZone;
            std::vector<std::size_t> stack{startIndex};
            assigned[startIndex] = true;
            std::unordered_set<std::uint64_t> sourceCells;
            int levelSum = 0;
            while (!stack.empty()) {
                const std::size_t currentIndex = stack.back();
                stack.pop_back();
                const NativePathCell current = pathCells[currentIndex];
                region.nativeBaseZoneId = std::min(region.nativeBaseZoneId, current.baseZone);
                region.nativeCells.push_back(current.cell);
                levelSum += current.level;
                if (region.nativeCells.size() == 1) {
                    region.nativeBounds = {current.cell.x, current.cell.y, 1, 1};
                } else {
                    const int minX = std::min(region.nativeBounds.x, current.cell.x);
                    const int minY = std::min(region.nativeBounds.y, current.cell.y);
                    const int maxX = std::max(region.nativeBounds.x + region.nativeBounds.w - 1, current.cell.x);
                    const int maxY = std::max(region.nativeBounds.y + region.nativeBounds.h - 1, current.cell.y);
                    region.nativeBounds = {minX, minY, maxX - minX + 1, maxY - minY + 1};
                }

                const Point source = nativeCellToSourcePoint(map, current.cell);
                if (map.inBounds(source) && sourceCells.insert(nativeIsoKey(source)).second) {
                    Cell& sourceCell = map.at(source);
                    sourceCell.regionTag = region.id;
                    region.cells.push_back(source);
                    if (region.cells.size() == 1) {
                        region.bounds = {source.x, source.y, 1, 1};
                    } else {
                        const int minX = std::min(region.bounds.x, source.x);
                        const int minY = std::min(region.bounds.y, source.y);
                        const int maxX = std::max(region.bounds.x + region.bounds.w - 1, source.x);
                        const int maxY = std::max(region.bounds.y + region.bounds.h - 1, source.y);
                        region.bounds = {minX, minY, maxX - minX + 1, maxY - minY + 1};
                    }
                }

                for (Point offset : kNativeRegionNeighbors) {
                    const Point neighbor{current.cell.x + offset.x, current.cell.y + offset.y};
                    const auto neighborIt = pathCellByIso.find(nativeIsoKey(neighbor));
                    if (neighborIt == pathCellByIso.end() || assigned[neighborIt->second]) {
                        continue;
                    }
                    const NativePathCell& next = pathCells[neighborIt->second];
                    if (next.passability != 0
                        || next.baseZone < 0
                        || !selectedMovementZones[static_cast<std::size_t>(next.baseZone)]) {
                        continue;
                    }
                    assigned[neighborIt->second] = true;
                    stack.push_back(neighborIt->second);
                }
            }
            region.level = region.nativeCells.empty()
                ? kNativeBaseMapLevel
                : nativeDoubleToInt(static_cast<double>(levelSum) / static_cast<double>(region.nativeCells.size()));
            region.wet = false;
            allRegions.push_back(std::move(region));
        }
        report_.regionCandidates = static_cast<int>(allRegions.size());
    }
    dumpTrace("after_flood", allRegions);
    nativeRegionScratchRegions_ = allRegions;

    std::vector<NativeRegion> regions;
    regions.reserve(allRegions.size());
    for (NativeRegion& region : allRegions) {
        if (static_cast<int>(regionCellCount(region)) >= minimumRegionSize) {
            regions.push_back(std::move(region));
        } else {
            ++report_.regionFilteredSmall;
            for (Point p : region.cells) {
                map.at(p).regionTag = 0;
            }
        }
    }

    dumpTrace("after_filter", regions);

    std::sort(regions.begin(), regions.end(), [&regionCellCount](const NativeRegion& lhs, const NativeRegion& rhs) {
        return regionCellCount(lhs) > regionCellCount(rhs);
    });

    dumpTrace("selected_before_quota", regions);

    const std::size_t totalCells = std::accumulate(regions.begin(), regions.end(), std::size_t{0}, [&regionCellCount](std::size_t total, const NativeRegion& region) {
        return total + regionCellCount(region);
    });

    auto distributeQuota = [&regions, totalCells, &regionCellCount](int total, int NativeRegion::*field) {
        total = std::max(0, total);
        int assigned = 0;
        int previousAssigned = 0;
        std::size_t cumulativeCells = 0;
        for (std::size_t i = 0; i < regions.size(); ++i) {
            cumulativeCells += regionCellCount(regions[i]);
            int quota = 0;
            if (i + 1 == regions.size()) {
                quota = total - assigned;
            } else if (totalCells > 0) {
                quota = nativeDoubleToInt(
                    static_cast<double>(cumulativeCells) / static_cast<double>(totalCells) * static_cast<double>(total)
                    + 0.5
                    - static_cast<double>(previousAssigned));
            }
            regions[i].*field = quota;
            assigned += quota;
            previousAssigned = assigned;
        }
    };

    distributeQuota(options.numPlayers, &NativeRegion::nativeWaypointQuota);
    dumpTrace("selected_after_quota", regions);
    distributeQuota(options.numPlayers, &NativeRegion::startQuota);
    report_.regionWaypointQuotaTotal = std::accumulate(regions.begin(), regions.end(), 0, [](int total, const NativeRegion& region) {
        return total + region.nativeWaypointQuota;
    });
    report_.regionStartQuotaTotal = std::accumulate(regions.begin(), regions.end(), 0, [](int total, const NativeRegion& region) {
        return total + region.startQuota;
    });

    report_.regions = static_cast<int>(regions.size());
    return regions;
}

void NativeMapGenerator::expandNativeRegionBorders(MapGrid& map, const MapSeedOptions& options, std::vector<NativeRegion>& regions)
{
    if (regions.empty()) {
        return;
    }

    const int maxRegionId = std::accumulate(regions.begin(), regions.end(), 0, [](int value, const NativeRegion& region) {
        return std::max(value, region.id);
    });
    if (maxRegionId < 0) {
        return;
    }

    std::vector<int> regionSizes(static_cast<std::size_t>(maxRegionId + 1), 0);
    std::vector<int> regionLevels(static_cast<std::size_t>(maxRegionId + 1), kNativeBaseMapLevel);
    for (const NativeRegion& region : regions) {
        if (region.id > 0 && region.id <= maxRegionId) {
            regionLevels[static_cast<std::size_t>(region.id)] = region.level;
        }
    }
    for (const auto& cell : map.cells()) {
        if (cell.regionTag <= 0 || cell.regionTag > maxRegionId) {
            continue;
        }
        const auto id = static_cast<std::size_t>(cell.regionTag);
        ++regionSizes[id];
    }

    const bool highResources = isNativeHighResourceMode(options);
    for (std::size_t regionIndex = 0; regionIndex < regions.size(); ++regionIndex) {
        NativeRegion& region = regions[regionIndex];
        const int regionId = region.id;
        if (regionId <= 0 || regionId > maxRegionId || regionSizes[static_cast<std::size_t>(regionId)] <= 0) {
            continue;
        }

        ++report_.regionBorderExpansionCalls;
        const int passes = (regionSizes[static_cast<std::size_t>(regionId)] > 8000 ? 1 : 0) + (!highResources ? 1 : 0) + 4;
        std::vector<Point> frontier;
        for (const auto& cell : map.cells()) {
            if (cell.regionTag != regionId) {
                continue;
            }
            bool boundary = false;
            for (Point neighbor : map.neighbors8({cell.x, cell.y})) {
                if (map.at(neighbor).regionTag != regionId) {
                    boundary = true;
                    break;
                }
            }
            if (boundary) {
                frontier.push_back({cell.x, cell.y});
            }
        }

        for (int pass = 0; pass < passes && !frontier.empty(); ++pass) {
            ++report_.regionBorderExpansionPasses;
            report_.regionBorderExpansionBoundaryCells += static_cast<int>(frontier.size());
            std::vector<Point> nextFrontier;
            std::vector<Point> accepted;
            nextFrontier.reserve(frontier.size() * 2);
            bool aborted = false;

            for (Point boundary : frontier) {
                for (Point neighbor : map.neighbors8(boundary)) {
                    ++report_.regionBorderExpansionCandidateChecks;
                    Cell& target = map.at(neighbor);
                    if (target.regionTag == regionId) {
                        continue;
                    }
                    if (target.level != regionLevels[static_cast<std::size_t>(regionId)]) {
                        continue;
                    }
                    if (target.regionTag > 0) {
                        ++report_.regionBorderExpansionConflictFailures;
                        ++report_.regionBorderExpansionAborts;
                        aborted = true;
                        break;
                    }
                    if (target.terrain == Terrain::Water) {
                        ++report_.regionBorderExpansionRejectedWater;
                        ++report_.regionBorderExpansionAborts;
                        aborted = true;
                        break;
                    }
                    if (!isNativeBlankTile(target) && !isNativeGreenAnchor(target)) {
                        ++report_.regionBorderExpansionRejectedTerrain;
                        ++report_.regionBorderExpansionAborts;
                        aborted = true;
                        break;
                    }

                    target.regionTag = regionId;
                    accepted.push_back(neighbor);
                    nextFrontier.push_back(neighbor);
                    ++report_.regionBorderExpansionAcceptedCells;
                    if (isPassable(target)) {
                        region.cells.push_back(neighbor);
                        region.bounds = {
                            std::min(region.bounds.x, neighbor.x),
                            std::min(region.bounds.y, neighbor.y),
                            std::max(region.bounds.x + region.bounds.w - 1, neighbor.x) - std::min(region.bounds.x, neighbor.x) + 1,
                            std::max(region.bounds.y + region.bounds.h - 1, neighbor.y) - std::min(region.bounds.y, neighbor.y) + 1,
                        };
                        ++regionSizes[static_cast<std::size_t>(regionId)];
                    }
                }
                if (aborted) {
                    break;
                }
            }

            for (Point p : accepted) {
                Cell& cell = map.at(p);
                const int oldLevel = cell.level;
                cell.level = regionLevels[static_cast<std::size_t>(regionId)];
                cell.height = cell.level;
                if (oldLevel != cell.level) {
                    ++report_.regionBorderExpansionLevelAdjustments;
                }
            }

            if (aborted) {
                break;
            }
            frontier = std::move(nextFrontier);
        }
    }
}

void NativeMapGenerator::buildRegionNeighborLists(const MapGrid& map, std::vector<NativeRegion>& regions)
{
    ++report_.regionNeighborBuildCalls;
    for (auto& region : regions) {
        region.neighbors.clear();
    }
    if (regions.empty()) {
        return;
    }
    int maxRegionId = 0;
    for (const NativeRegion& region : regions) {
        maxRegionId = std::max(maxRegionId, region.id);
    }

    if (std::any_of(regions.begin(), regions.end(), [](const NativeRegion& region) {
            return !region.nativeCells.empty();
        })) {
        const Rect mapRect = nativeSavedMapRect(map);
        const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(
            map,
            report_.isoTileOverrides);
        std::unordered_map<int, std::size_t> regionById;
        regionById.reserve(regions.size() * 2);
        for (std::size_t i = 0; i < regions.size(); ++i) {
            regionById[regions[i].id] = i;
        }

        constexpr std::array<Point, 8> kNeighbors{{
            {0, -1}, {1, -1}, {1, 0}, {1, 1},
            {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
        }};

        for (std::size_t regionIndex = 0; regionIndex < regions.size(); ++regionIndex) {
            NativeRegion& region = regions[regionIndex];
            const bool traceRegion = native_trace::active()
                && native_trace::currentFunctionId() == native_trace::InitializeMapRegions;
            native_trace::EventData traceData;
            if (traceRegion) {
                traceData.regionIndex = static_cast<int>(regionIndex);
                traceData.regionId = region.id;
                traceData.regionCount = static_cast<int>(regions.size());
                traceData.regionCellCount = region.nativeConstructedCellCount;
                traceData.cellX = 0;
                traceData.cellY = 0;
                traceData.args[1] = static_cast<std::uint32_t>(region.level);
                traceData.args[2] = region.wet ? 1u : 0u;
                native_trace::enterDetailed(
                    0x58F0C0,
                    native_trace::BuildRegionNeighborList,
                    traceData);
            }
            std::vector<unsigned char> neighborSeen(static_cast<std::size_t>(maxRegionId + 1), 0);
            const std::vector<Point> boundaryCells = collectNativeZoneBoundaryCells(
                iteratorCells,
                mapRect,
                nativeMapgenZoneIds_,
                nativeMapgenDefaultZoneId_,
                region.id,
                static_cast<int>(regionIndex),
                static_cast<int>(regions.size()),
                region.nativeConstructedCellCount,
                region.nativeCells.empty() ? Point{-1, -1} : region.nativeCells.front(),
                &nativeInvalidCellPoint_);
            for (Point cell : boundaryCells) {
                for (Point offset : kNeighbors) {
                    const Point neighbor{cell.x + offset.x, cell.y + offset.y};
                    if (!isInsideNativeCellDiamond(neighbor, mapRect)) {
                        continue;
                    }
                    const auto zoneIt = nativeMapgenZoneIds_.find(nativeIsoKey(neighbor));
                    const int neighborZoneId = zoneIt == nativeMapgenZoneIds_.end()
                        ? nativeMapgenDefaultZoneId_
                        : zoneIt->second;
                    if (neighborZoneId == region.id) {
                        continue;
                    }
                    const auto otherIt = regionById.find(neighborZoneId);
                    if (otherIt == regionById.end()) {
                        continue;
                    }
                    neighborSeen[static_cast<std::size_t>(neighborZoneId)] = 1;
                }
            }
            report_.regionNeighborBoundaryCells += static_cast<int>(boundaryCells.size());
            for (int neighborId = 0; neighborId <= maxRegionId; ++neighborId) {
                if (neighborSeen[static_cast<std::size_t>(neighborId)] == 0 || neighborId == region.id) {
                    continue;
                }
                region.neighbors.push_back(neighborId);
                ++report_.regionNeighborLinks;
            }
            if (traceRegion) {
                std::uint32_t orderedHash = 2166136261u;
                traceData.args[3] = 0xFFFFFFFFu;
                traceData.args[4] = 0xFFFFFFFFu;
                traceData.args[5] = 0xFFFFFFFFu;
                for (std::size_t i = 0; i < region.neighbors.size(); ++i) {
                    const std::uint32_t value = static_cast<std::uint32_t>(region.neighbors[i]);
                    if (i < 3) {
                        traceData.args[3 + i] = value;
                    }
                    for (int shift = 0; shift < 32; shift += 8) {
                        orderedHash ^= (value >> shift) & 0xFFu;
                        orderedHash *= 16777619u;
                    }
                }
                traceData.result[0] = static_cast<std::uint32_t>(region.neighbors.size());
                traceData.result[1] = orderedHash;
                traceData.result[2] = region.neighbors.empty()
                    ? 0xFFFFFFFFu
                    : static_cast<std::uint32_t>(region.neighbors.front());
                traceData.result[3] = region.neighbors.empty()
                    ? 0xFFFFFFFFu
                    : static_cast<std::uint32_t>(region.neighbors.back());
                native_trace::exitDetailed(
                    0x58F2B7,
                    native_trace::BuildRegionNeighborList,
                    traceData);
            }
        }
        return;
    }

    if (maxRegionId <= 0) {
        return;
    }

    std::vector<int> idToIndex(static_cast<std::size_t>(maxRegionId + 1), -1);
    for (std::size_t i = 0; i < regions.size(); ++i) {
        if (regions[i].id >= 0) {
            idToIndex[static_cast<std::size_t>(regions[i].id)] = static_cast<int>(i);
        }
    }

    std::vector<unsigned char> linked(static_cast<std::size_t>((maxRegionId + 1) * (maxRegionId + 1)), 0);
    auto linkIndex = [maxRegionId](int a, int b) {
        return static_cast<std::size_t>(a * (maxRegionId + 1) + b);
    };

    for (const auto& cell : map.cells()) {
        if (cell.regionTag < 0 || cell.regionTag > maxRegionId || idToIndex[static_cast<std::size_t>(cell.regionTag)] < 0) {
            continue;
        }

        bool boundary = false;
        for (Point neighbor : map.neighbors8({cell.x, cell.y})) {
            const int otherId = map.at(neighbor).regionTag;
            if (otherId == cell.regionTag) {
                continue;
            }
            boundary = true;
            if (otherId < 0 || otherId > maxRegionId || idToIndex[static_cast<std::size_t>(otherId)] < 0) {
                continue;
            }

            const auto ab = linkIndex(cell.regionTag, otherId);
            if (linked[ab]) {
                continue;
            }
            linked[ab] = 1;
            linked[linkIndex(otherId, cell.regionTag)] = 1;
            regions[static_cast<std::size_t>(idToIndex[static_cast<std::size_t>(cell.regionTag)])].neighbors.push_back(otherId);
            regions[static_cast<std::size_t>(idToIndex[static_cast<std::size_t>(otherId)])].neighbors.push_back(cell.regionTag);
            ++report_.regionNeighborLinks;
        }
        if (boundary) {
            ++report_.regionNeighborBoundaryCells;
        }
    }
}

bool NativeMapGenerator::placeBridgeBetweenRegions(
    MapGrid& map,
    NativeRandomizer& rng,
    const NativeRegion& wet,
    const NativeRegion& firstBank,
    const NativeRegion& secondBank)
{
    if (wet.nativeCells.empty()) {
        return false;
    }

    const Rect mapRect = nativeSavedMapRect(map);
    const Rect visibleRect = nativeLocalRect(map);
    const int workspaceSize = mapRect.w + mapRect.h + 1;
    const int workspaceCellCount = workspaceSize * workspaceSize;
    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(
        map,
        report_.isoTileOverrides);

    std::unordered_map<std::uint64_t, const NativeIteratorCell*> iteratorByIso;
    std::unordered_map<std::uint64_t, int> tileByIso;
    std::unordered_map<std::uint64_t, int> heightByIso;
    std::unordered_map<std::uint64_t, int> levelByIso;
    std::unordered_map<std::uint64_t, int> overlayByIso;
    std::unordered_set<std::uint64_t> firstObjectKeys;
    iteratorByIso.reserve(iteratorCells.size() * 2);
    tileByIso.reserve(iteratorCells.size() * 2);
    heightByIso.reserve(iteratorCells.size() * 2);
    levelByIso.reserve(iteratorCells.size() * 2);
    overlayByIso.reserve(iteratorCells.size() * 2);
    firstObjectKeys.reserve(report_.nativeObjectOverrides.size() * 2 + 8);
    for (const NativeIteratorCell& cell : iteratorCells) {
        const std::uint64_t key = nativeIsoKey(cell.cell);
        iteratorByIso[key] = &cell;
        tileByIso[key] = cell.isoTileTypeIndex;
        heightByIso[key] = cell.height;
        levelByIso[key] = cell.level;
        overlayByIso[key] = cell.source == nullptr
            ? -1
            : nativeOverlayTypeIndexForCell(*cell.source);
        if (cell.source != nullptr && nativeObjectAbstractTypeForCell(*cell.source) >= 0) {
            firstObjectKeys.insert(key);
        }
    }
    for (const NativeOverlayOverride& overlay : report_.nativeOverlayOverrides) {
        overlayByIso[nativeIsoKey(overlay.iso)] = overlay.overlayTypeIndex;
    }
    for (const NativeObjectOverride& object : report_.nativeObjectOverrides) {
        if (object.objectAbstractType >= 0 || object.altObjectAbstractType >= 0) {
            firstObjectKeys.insert(nativeIsoKey(object.iso));
        }
    }
    for (const NativeStructureOverride& structure : report_.nativeStructureOverrides) {
        firstObjectKeys.insert(nativeIsoKey(structure.iso));
    }

    const int cliffBase = nativeTheaterCatalog_ == nullptr
        ? kNativeTileCliffBase
        : nativeTheaterCatalog_->cliffSetBase;
    const int shoreBase = nativeTheaterCatalog_ == nullptr
        ? kNativeTileShoreBase
        : nativeTheaterCatalog_->shorePiecesBase;
    const int waterBase = nativeTheaterCatalog_ == nullptr
        ? kNativeTileWaterBase
        : nativeTheaterCatalog_->waterBase;
    const int paveBase = nativeTheaterCatalog_ == nullptr
        ? kNativeTilePaveBase
        : nativeTheaterCatalog_->paveBase;
    const int miscPaveBase = nativeTheaterCatalog_ == nullptr
        ? -1
        : nativeTheaterCatalog_->miscPaveBase;
    const int waterfallEastBase = nativeTheaterCatalog_ == nullptr
        ? -1
        : nativeTheaterCatalog_->waterfallEastBase;
    const int waterfallWestBase = nativeTheaterCatalog_ == nullptr
        ? -1
        : nativeTheaterCatalog_->waterfallWestBase;
    const int waterfallNorthBase = nativeTheaterCatalog_ == nullptr
        ? -1
        : nativeTheaterCatalog_->waterfallNorthBase;
    const int waterfallSouthBase = nativeTheaterCatalog_ == nullptr
        ? -1
        : nativeTheaterCatalog_->waterfallSouthBase;
    const int cliffRampsBase = nativeTheaterCatalog_ == nullptr
        ? -1
        : nativeTheaterCatalog_->cliffRampsBase;
    const int pavedRoadsBase = nativeTheaterCatalog_ == nullptr
        ? -1
        : nativeTheaterCatalog_->pavedRoadsBase;
    const int pavedRoadEndsBase = nativeTheaterCatalog_ == nullptr
        ? -1
        : nativeTheaterCatalog_->pavedRoadEndsBase;

    const auto inTileRange = [](int tile, int base, int count) {
        return base >= 0 && tile >= base && tile < base + count;
    };
    const auto zoneFor = [this](Point cell) {
        const auto it = nativeMapgenZoneIds_.find(nativeIsoKey(cell));
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };
    const auto tileFor = [&tileByIso](Point cell) {
        const auto it = tileByIso.find(nativeIsoKey(cell));
        return it == tileByIso.end() ? -1 : it->second;
    };
    const auto heightFor = [&heightByIso](Point cell) {
        const auto it = heightByIso.find(nativeIsoKey(cell));
        return it == heightByIso.end() ? 0 : it->second;
    };
    const auto rectInsideDiamond = [mapRect](Rect rect) {
        if (rect.w <= 0 || rect.h <= 0) {
            return false;
        }
        return isInsideNativeCellDiamond({rect.x, rect.y}, mapRect)
            && isInsideNativeCellDiamond({rect.x + rect.w - 1, rect.y}, mapRect)
            && isInsideNativeCellDiamond({rect.x, rect.y + rect.h - 1}, mapRect)
            && isInsideNativeCellDiamond({rect.x + rect.w - 1, rect.y + rect.h - 1}, mapRect);
    };
    const auto isPavedRoad = [=](int tile) {
        return inTileRange(tile, pavedRoadsBase, 15);
    };
    const auto isPavedRoadEnd = [=](int tile) {
        return inTileRange(tile, pavedRoadEndsBase, 4);
    };
    const auto isAreaTile = [=](int tile) {
        if (isPavedRoad(tile) || isPavedRoadEnd(tile)) {
            return false;
        }
        return isNativeClearTileIndex(tile)
            || inTileRange(tile, miscPaveBase, 14)
            || inTileRange(tile, paveBase, 16);
    };
    const auto isWetTile = [=](int tile) {
        return inTileRange(tile, shoreBase, 42)
            || inTileRange(tile, waterBase, 14)
            || inTileRange(tile, waterfallEastBase, 4)
            || inTileRange(tile, waterfallWestBase, 4)
            || inTileRange(tile, waterfallNorthBase, 4)
            || inTileRange(tile, waterfallSouthBase, 4);
    };
    const auto isAreaSuitable = [&](Rect rect) {
        if (!rectInsideDiamond(rect)) {
            return false;
        }
        for (int y = rect.y; y < rect.y + rect.h; ++y) {
            for (int x = rect.x; x < rect.x + rect.w; ++x) {
                if (!isAreaTile(tileFor({x, y}))) {
                    return false;
                }
            }
        }
        return true;
    };
    const auto isCliffCorner = [&](Point cell, int base, int direction) {
        const int tile = tileFor(cell);
        if (!inTileRange(tile, base, 4)) {
            return false;
        }
        if (tile != base && tile != base + 3) {
            return true;
        }
        const int height = heightFor(cell);
        switch (direction) {
        case 0:
            return height != 0 && height != 4;
        case 1:
            return height != 1 && height != 3;
        case 2:
            return height >= 2;
        default:
            return height != 2 && height != 3;
        }
    };
    const auto isCliffLike = [&](Point cell) {
        const int tile = tileFor(cell);
        return inTileRange(tile, cliffBase, 40)
            || isCliffCorner(cell, waterfallEastBase, 0)
            || isCliffCorner(cell, waterfallWestBase, 1)
            || isCliffCorner(cell, waterfallSouthBase, 2)
            || isCliffCorner(cell, waterfallNorthBase, 3)
            || inTileRange(tile, cliffRampsBase, 20);
    };
    const auto isUsable = [&](Point cell) {
        const auto it = iteratorByIso.find(nativeIsoKey(cell));
        return it != iteratorByIso.end()
            && isNativeCellWithinUsableArea(cell, *it->second, mapRect, visibleRect);
    };
    const auto validateBridge = [&](Rect rect) {
        if (!rectInsideDiamond(rect)) {
            return false;
        }
        const auto firstLevel = levelByIso.find(nativeIsoKey({rect.x, rect.y}));
        if (firstLevel == levelByIso.end()) {
            return false;
        }
        for (int y = rect.y; y <= rect.y + rect.h; ++y) {
            for (int x = rect.x; x <= rect.x + rect.w; ++x) {
                const std::uint64_t key = nativeIsoKey({x, y});
                const auto levelIt = levelByIso.find(key);
                const auto overlayIt = overlayByIso.find(key);
                const int tile = tileFor({x, y});
                if (levelIt == levelByIso.end()
                    || levelIt->second != firstLevel->second
                    || (overlayIt != overlayByIso.end() && overlayIt->second != -1)
                    || (!isNativeClearTileIndex(tile) && !isWetTile(tile))) {
                    return false;
                }
            }
        }
        return true;
    };
    const auto isRectLevelAndClear = [&](Rect rect) {
        if (!rectInsideDiamond(rect)) {
            return false;
        }
        const auto firstLevel = levelByIso.find(nativeIsoKey({rect.x, rect.y}));
        if (firstLevel == levelByIso.end()) {
            return false;
        }
        for (int y = rect.y; y < rect.y + rect.h; ++y) {
            for (int x = rect.x; x < rect.x + rect.w; ++x) {
                const auto levelIt = levelByIso.find(nativeIsoKey({x, y}));
                if (levelIt == levelByIso.end()
                    || levelIt->second != firstLevel->second
                    || !isAreaTile(tileFor({x, y}))) {
                    return false;
                }
            }
        }
        return true;
    };
    const auto zonesMatchBanks = [&](int firstZone, int secondZone) {
        return (firstZone == firstBank.id && secondZone == secondBank.id)
            || (firstZone == secondBank.id && secondZone == firstBank.id);
    };

    const auto searchVertical = [&](Point selected) -> std::optional<Rect> {
        const int x = selected.x - 1;
        int firstY = selected.y;
        while (!isAreaSuitable({x, firstY, 3, 1})) {
            --firstY;
            const Point left{x, firstY};
            const Point right{x + 2, firstY};
            if (!isUsable(left) || !isUsable(right) || isCliffLike(left) || isCliffLike(right)) {
                return std::nullopt;
            }
        }
        if (!isAreaSuitable({x, firstY - 3, 3, 3})) {
            return std::nullopt;
        }

        int secondY = selected.y;
        while (!isAreaSuitable({x, secondY, 3, 1})) {
            ++secondY;
            const Point left{x, secondY};
            const Point right{x + 2, secondY};
            if (!isUsable(left) || !isUsable(right) || isCliffLike(left) || isCliffLike(right)) {
                return std::nullopt;
            }
        }
        if (!isAreaSuitable({x, secondY + 1, 3, 3})) {
            return std::nullopt;
        }
        if (!zonesMatchBanks(zoneFor({x, firstY}), zoneFor({x, secondY}))) {
            return std::nullopt;
        }
        return Rect{x, firstY, 3, secondY - firstY + 1};
    };
    const auto searchHorizontal = [&](Point selected) -> std::optional<Rect> {
        const int y = selected.y - 1;
        int firstX = selected.x;
        while (!isAreaSuitable({firstX, y, 1, 3})) {
            --firstX;
            const Point top{firstX, y};
            const Point bottom{firstX, y + 2};
            if (!isUsable(top) || !isUsable(bottom) || isCliffLike(top) || isCliffLike(bottom)) {
                return std::nullopt;
            }
        }
        if (!isAreaSuitable({firstX - 3, y, 3, 3})) {
            return std::nullopt;
        }

        int secondX = selected.x;
        while (!isAreaSuitable({secondX, y, 1, 3})) {
            ++secondX;
            const Point top{secondX, y};
            const Point bottom{secondX, y + 2};
            if (!isUsable(top) || !isUsable(bottom) || isCliffLike(top) || isCliffLike(bottom)) {
                return std::nullopt;
            }
        }
        if (!isAreaSuitable({secondX + 1, y, 3, 3})) {
            return std::nullopt;
        }
        if (!zonesMatchBanks(zoneFor({firstX, y}), zoneFor({secondX, y}))) {
            return std::nullopt;
        }
        return Rect{firstX, y, secondX - firstX + 1, 3};
    };

    const auto placeNativeTile = [&](int tileTypeIndex, Point anchor) {
        if (tileTypeIndex < 0 || !placeNativeIsoTile(map, tileTypeIndex, anchor, -1, -1)) {
            return false;
        }
        const NativeIsoTileTemplate* tile = nativeTheaterCatalog_ == nullptr
            ? nullptr
            : nativeTheaterCatalog_->tileType(tileTypeIndex);
        if (tile == nullptr) {
            return true;
        }
        for (std::size_t subTile = 0; subTile < tile->slots.size(); ++subTile) {
            if (!tile->slots[subTile].present) {
                continue;
            }
            const Point cell{
                anchor.x + static_cast<int>(subTile % tile->width),
                anchor.y + static_cast<int>(subTile / tile->width),
            };
            tileByIso[nativeIsoKey(cell)] = tileTypeIndex;
            heightByIso[nativeIsoKey(cell)] = static_cast<int>(subTile);
        }
        return true;
    };
    const auto placeGuardStructure = [&](Rect rect) {
        for (int y = rect.y; y <= rect.y + rect.h; ++y) {
            for (int x = rect.x; x <= rect.x + rect.w; ++x) {
                const Point cell{x, y};
                const std::uint64_t key = nativeIsoKey(cell);
                const auto overlayIt = overlayByIso.find(key);
                if (!isNativeClearTileIndex(tileFor(cell))
                    || (overlayIt != overlayByIso.end() && overlayIt->second != -1)
                    || firstObjectKeys.contains(key)) {
                    continue;
                }

                NativeStructureOverride structure;
                structure.iso = cell;
                structure.nativeSaveValue = "Neutral,CABHUT,256,"
                    + std::to_string(cell.x) + ',' + std::to_string(cell.y)
                    + ",0,None,1,0,1,0,0,None,None,None,0,0";
                report_.nativeStructureOverrides.push_back(std::move(structure));
                report_.nativeObjectOverrides.push_back({cell, 6, -1, -1, -1});
                firstObjectKeys.insert(key);
                return true;
            }
        }
        return false;
    };
    const auto placeBridgeOverlay = [&](Rect rect, bool horizontal) {
        for (int y = rect.y; y < rect.y + rect.h; ++y) {
            for (int x = rect.x; x < rect.x + rect.w; ++x) {
                int overlayType = -1;
                int overlayData = 0;
                if (horizontal) {
                    overlayType = x == rect.x ? 94
                        : (x == rect.x + rect.w - 1 ? 92 : x % 4 + 74);
                    overlayData = y - rect.y;
                } else {
                    overlayType = y == rect.y ? 96
                        : (y == rect.y + rect.h - 1 ? 98 : y % 4 + 83);
                    overlayData = x - rect.x;
                }
                const Point cell{x, y};
                report_.nativeOverlayOverrides.push_back({cell, overlayType, overlayData, -1});
                overlayByIso[nativeIsoKey(cell)] = overlayType;
            }
        }
    };
    const auto placeBridgeEnds = [&](Rect rect, bool horizontal) {
        const auto pavedRoadTile = [pavedRoadsBase](int offset) {
            return pavedRoadsBase < 0 ? -1 : pavedRoadsBase + offset;
        };
        const auto pavedRoadEndTile = [pavedRoadEndsBase](int offset) {
            return pavedRoadEndsBase < 0 ? -1 : pavedRoadEndsBase + offset;
        };
        if (horizontal) {
            const int endX = rect.x + rect.w;
            if (isRectLevelAndClear({endX, rect.y - 2, 6, 6})
                && rng.randomScaledInclusive(0, 1) != 0) {
                placeNativeTile(pavedRoadTile(10), {endX, rect.y});
            } else {
                placeNativeTile(pavedRoadEndTile(0), {endX, rect.y});
            }
            if (isRectLevelAndClear({rect.x - 6, rect.y - 2, 6, 6})
                && rng.randomScaledInclusive(0, 1) != 0) {
                placeNativeTile(pavedRoadTile(9), {rect.x - 4, rect.y});
            } else {
                placeNativeTile(pavedRoadEndTile(2), {rect.x - 1, rect.y});
            }
            if (!placeGuardStructure({rect.x, rect.y - 1, 2, 5})) {
                placeGuardStructure({rect.x - 1, rect.y - 2, 3, 7});
            }
            if (!placeGuardStructure({rect.x + rect.w - 2, rect.y - 1, 2, 5})) {
                placeGuardStructure({rect.x + rect.w - 2, rect.y - 2, 3, 7});
            }
            return;
        }

        if (isRectLevelAndClear({rect.x - 2, rect.y - 6, 7, 6})
            && rng.randomScaledInclusive(0, 1) != 0) {
            placeNativeTile(pavedRoadTile(13), {rect.x, rect.y - 4});
        } else {
            placeNativeTile(pavedRoadEndTile(1), {rect.x, rect.y - 1});
        }
        if (isRectLevelAndClear({rect.x - 2, rect.y + rect.h, 7, 6})
            && rng.randomScaledInclusive(0, 1) != 0) {
            placeNativeTile(pavedRoadTile(12), {rect.x, rect.y + rect.h});
        } else {
            placeNativeTile(pavedRoadEndTile(3), {rect.x, rect.y + rect.h});
        }
        if (!placeGuardStructure({rect.x - 1, rect.y, 5, 2})) {
            placeGuardStructure({rect.x - 2, rect.y - 1, 7, 3});
        }
        if (!placeGuardStructure({rect.x - 1, rect.y + rect.h - 2, 5, 2})) {
            placeGuardStructure({rect.x - 2, rect.y + rect.h - 2, 7, 3});
        }
    };

    for (int attempt = 0; attempt < 200; ++attempt) {
        Point selected;
        do {
            const int index = rng.randomScaledInclusive(0, workspaceCellCount - 1);
            selected = {index % workspaceSize, index / workspaceSize};
        } while ((selected.x == 0 && selected.y == 0) || zoneFor(selected) != wet.id);
        ++report_.regionBridgeAttempts;

        std::optional<Rect> vertical = searchVertical(selected);
        std::optional<Rect> horizontal = searchHorizontal(selected);
        if (vertical.has_value() && horizontal.has_value()) {
            const int verticalDistance = vertical->h - 1;
            const int horizontalDistance = horizontal->w - 1;
            if (verticalDistance >= horizontalDistance) {
                vertical.reset();
            } else {
                horizontal.reset();
            }
        }

        const int maximumDistance = attempt / 25 + 8;
        Rect bridge;
        bool horizontalBridge = false;
        if (horizontal.has_value() && horizontal->w - 1 < maximumDistance) {
            bridge = *horizontal;
            horizontalBridge = true;
        } else if (vertical.has_value() && vertical->h - 1 < maximumDistance) {
            bridge = *vertical;
        } else {
            continue;
        }
        if (!validateBridge(bridge)) {
            continue;
        }

        placeBridgeOverlay(bridge, horizontalBridge);
        placeBridgeEnds(bridge, horizontalBridge);
        return true;
    }
    return false;
}

bool NativeMapGenerator::placeNativeIsoTile(
    MapGrid& map,
    int tileTypeIndex,
    Point anchor,
    int zoneId,
    int baseLevel)
{
    const NativeIsoTileTemplate* tile = nativeTheaterCatalog_ == nullptr
        ? nativeTemperateSlopeSetPiece(tileTypeIndex)
        : nativeTheaterCatalog_->tileType(tileTypeIndex);
    if (tile == nullptr
        || tile->width == 0
        || tile->height == 0
        || tile->slots.size() != static_cast<std::size_t>(tile->width) * tile->height) {
        return false;
    }

    std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
    overrideIndexByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t index = 0; index < report_.isoTileOverrides.size(); ++index) {
        overrideIndexByIso[nativeIsoKey(report_.isoTileOverrides[index].iso)] = index;
    }

    for (std::size_t subTile = 0; subTile < tile->slots.size(); ++subTile) {
        const NativeIsoTileSlot& slot = tile->slots[subTile];
        const Point nativeCell{
            anchor.x + static_cast<int>(subTile % tile->width),
            anchor.y + static_cast<int>(subTile / tile->width),
        };
        const auto overrideIt = overrideIndexByIso.find(nativeIsoKey(nativeCell));
        if (overrideIt == overrideIndexByIso.end()) {
            continue;
        }
        nativeIsoTileTemplateFootprintKeys_.insert(nativeIsoKey(nativeCell));
        if (!slot.present) {
            continue;
        }

        IsoTileOverride& override = report_.isoTileOverrides[overrideIt->second];
        const int level = baseLevel == -1
            ? override.level
            : baseLevel + static_cast<int>(slot.heightOffset) - kNativeBaseMapLevel;
        override.terrain = Terrain::Land;
        override.variant = 0;
        override.subTile = static_cast<int>(subTile);
        override.level = level;
        override.slopeIndex = slot.slope;
        override.nativeTileTypeIndex = tileTypeIndex;
        nativeMapgenZoneIds_[nativeIsoKey(nativeCell)] = zoneId;
        nativePlacedIsoTileKeys_.insert(nativeIsoKey(nativeCell));

        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == nativeCell) {
            Cell& productCell = map.at(source);
            productCell.terrain = Terrain::Land;
            productCell.height = static_cast<int>(subTile);
            productCell.level = level;
            productCell.slope = slot.slope;
        }
    }
    return true;
}

bool NativeMapGenerator::placeCliffCornerComplexType1(
    MapGrid& map,
    Point from,
    Point to,
    int higherRegionId,
    int lowerRegionId)
{
    const bool traceType1 = native_trace::active()
        && native_trace::currentFunctionId() == native_trace::PlaceCliffAtCell;
    native_trace::EventData traceData;
    traceData.args[0] = packNativeCell(from);
    traceData.args[1] = packNativeCell(to);
    traceData.args[2] = static_cast<std::uint32_t>(lowerRegionId);
    traceData.regionIndex = higherRegionId;
    traceData.regionId = higherRegionId;
    traceData.regionCount = static_cast<std::int32_t>(nativeMapgenScratchZoneLevels_.size());
    if (higherRegionId >= 0
        && higherRegionId < static_cast<int>(nativeMapgenScratchZoneCellCounts_.size())) {
        traceData.regionCellCount = nativeMapgenScratchZoneCellCounts_[static_cast<std::size_t>(higherRegionId)];
    }
    traceData.cellX = static_cast<std::int16_t>(from.x);
    traceData.cellY = static_cast<std::int16_t>(from.y);
    if (traceType1) {
        native_trace::enterDetailed(
            0x593030,
            native_trace::PlaceCliffCornerComplexType1,
            traceData);
    }

    std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
    overrideIndexByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t index = 0; index < report_.isoTileOverrides.size(); ++index) {
        overrideIndexByIso[nativeIsoKey(report_.isoTileOverrides[index].iso)] = index;
    }
    const auto overrideAt = [&](Point cell) -> IsoTileOverride* {
        const auto it = overrideIndexByIso.find(nativeIsoKey(cell));
        return it == overrideIndexByIso.end() ? nullptr : &report_.isoTileOverrides[it->second];
    };
    const auto constOverrideAt = [&](Point cell) -> const IsoTileOverride* {
        const auto it = overrideIndexByIso.find(nativeIsoKey(cell));
        return it == overrideIndexByIso.end() ? nullptr : &report_.isoTileOverrides[it->second];
    };
    const auto zoneFor = [&](Point cell) {
        const auto it = nativeMapgenZoneIds_.find(nativeIsoKey(cell));
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };

    const int rectX = to.x - 2;
    const int rectY = to.y - 7;
    const int rectWidth = from.x - to.x + 10;
    const int rectHeight = from.y - to.y + 10;
    const auto captureCellState = [&]() {
        std::vector<std::uint32_t> values;
        if (!native_trace::detailRequested() || rectWidth <= 0 || rectHeight <= 0) {
            return values;
        }
        values.reserve(static_cast<std::size_t>(rectWidth * rectHeight) * 6);
        for (int y = rectY; y < rectY + rectHeight; ++y) {
            for (int x = rectX; x < rectX + rectWidth; ++x) {
                const Point cell{x, y};
                const IsoTileOverride* override = constOverrideAt(cell);
                values.push_back(packNativeCell(cell));
                values.push_back(static_cast<std::uint32_t>(zoneFor(cell)));
                values.push_back(static_cast<std::uint32_t>(
                    override == nullptr ? -1 : nativeTileTypeIndexForOverride(*override)));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->subTile));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->level));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->slopeIndex));
            }
        }
        return values;
    };
    if (traceType1 && native_trace::detailRequested()) {
        native_trace::detailCurrent(
            native_trace::DetailOrderedCellState,
            captureCellState());
    }
    const auto finishTrace = [&](bool placed) {
        if (traceType1) {
            traceData.result[0] = placed ? 1u : 0u;
            native_trace::exitDetailed(
                0x593030,
                native_trace::PlaceCliffCornerComplexType1,
                traceData,
                native_trace::DetailOrderedCellState,
                captureCellState());
        }
        return placed;
    };

    if (from.x - to.x < 3 || from.y - to.y < 3 || rectWidth <= 0 || rectHeight <= 0) {
        return finishTrace(false);
    }
    if (higherRegionId < 0
        || lowerRegionId < 0
        || higherRegionId >= static_cast<int>(nativeMapgenScratchZoneLevels_.size())
        || lowerRegionId >= static_cast<int>(nativeMapgenScratchZoneLevels_.size())) {
        return finishTrace(false);
    }

    const Rect mapRect = nativeSavedMapRect(map);
    const Rect visibleRect = nativeLocalRect(map);
    const auto isWithinUsableArea = [&](Point cell) {
        const IsoTileOverride* override = constOverrideAt(cell);
        if (override == nullptr) {
            return false;
        }
        int level = override->level;
        const int sum = cell.x + cell.y;
        if (override->slopeIndex != 0
            && sum < level + mapRect.w + 2 * visibleRect.y + 4) {
            ++level;
        }
        return sum > level + mapRect.w + 2 * visibleRect.y
            && sum <= level + mapRect.w + 2 * (visibleRect.y + visibleRect.h) + 2
            && cell.x - cell.y < 2 * (visibleRect.x + visibleRect.w) - mapRect.w
            && cell.y - cell.x < mapRect.w - 2 * visibleRect.x;
    };
    const std::array<Point, 4> corners{{
        {rectX, rectY},
        {rectX + rectWidth - 1, rectY},
        {rectX, rectY + rectHeight - 1},
        {rectX + rectWidth - 1, rectY + rectHeight - 1},
    }};
    if (std::any_of(corners.begin(), corners.end(), [&](Point corner) {
            return !isWithinUsableArea(corner);
        })) {
        return finishTrace(false);
    }
    for (int y = rectY; y < rectY + rectHeight; ++y) {
        for (int x = rectX; x < rectX + rectWidth; ++x) {
            const Point cell{x, y};
            const IsoTileOverride* override = constOverrideAt(cell);
            const int zoneId = zoneFor(cell);
            if (override == nullptr
                || (zoneId != higherRegionId && zoneId != lowerRegionId)
                || !isNativeClearTileIndex(nativeTileTypeIndexForOverride(*override))) {
                return finishTrace(false);
            }
        }
    }

    const int higherLevel = nativeMapgenScratchZoneLevels_[static_cast<std::size_t>(higherRegionId)];
    const int lowerLevel = nativeMapgenScratchZoneLevels_[static_cast<std::size_t>(lowerRegionId)];
    const auto mirrorLevelToProductMap = [&](Point nativeCell, int level) {
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == nativeCell) {
            map.at(source).level = level;
        }
    };
    const auto setZoneAndLevel = [&](Point cell, int zoneId, int level) {
        if (!isInsideNativeCellDiamond(cell, mapRect)) {
            return;
        }
        nativeMapgenZoneIds_[nativeIsoKey(cell)] = zoneId;
        if (IsoTileOverride* override = overrideAt(cell); override != nullptr) {
            override->level = level;
            mirrorLevelToProductMap(cell, level);
        }
    };
    const auto fillZoneRect = [&](int xBegin, int xEnd, int yBegin, int yEnd, int zoneId, int level) {
        for (int y = yBegin; y < yEnd; ++y) {
            for (int x = xBegin; x < xEnd; ++x) {
                setZoneAndLevel({x, y}, zoneId, level);
            }
        }
    };

    fillZoneRect(rectX, rectX + rectWidth, rectY, rectY + 7, lowerRegionId, lowerLevel);
    fillZoneRect(from.x + 1, from.x + 8, rectY, rectY + rectHeight, lowerRegionId, lowerLevel);
    fillZoneRect(to.x, to.x + 1, to.y, to.y + 2, higherRegionId, higherLevel);
    fillZoneRect(from.x - 1, from.x + 1, from.y, from.y + 1, higherRegionId, higherLevel);
    fillZoneRect(to.x + 1, from.x, to.y + 1, from.y, higherRegionId, higherLevel);

    const int slopeSetPiecesBase = nativeSlopeSetPiecesTileBase(nativeTheaterCatalog_);
    if (slopeSetPiecesBase < 0) {
        return finishTrace(false);
    }
    static_cast<void>(placeNativeIsoTile(
        map,
        slopeSetPiecesBase + 2,
        {from.x, from.y - 2},
        lowerRegionId,
        higherLevel));
    static_cast<void>(placeNativeIsoTile(
        map,
        slopeSetPiecesBase + 4,
        {to.x, to.y - 3},
        lowerRegionId,
        higherLevel));

    const int rampBase = nativeTheaterCatalog_ == nullptr
        ? kNativeTileRampBase
        : nativeTheaterCatalog_->rampBase;
    const auto setRampTile = [&](Point nativeCell, int slopeIndex, int level) {
        IsoTileOverride* override = overrideAt(nativeCell);
        if (override == nullptr || rampBase < 0) {
            return false;
        }
        override->terrain = Terrain::Land;
        override->variant = 0;
        override->subTile = 0;
        override->level = level;
        override->landType = 0;
        override->slopeIndex = slopeIndex;
        override->nativeTileTypeIndex = rampBase + slopeIndex - 1;
        nativePlacedIsoTileKeys_.insert(nativeIsoKey(nativeCell));

        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == nativeCell) {
            Cell& productCell = map.at(source);
            productCell.terrain = Terrain::Land;
            productCell.height = 0;
            productCell.level = level;
            productCell.slope = slopeIndex;
            productCell.landType = 0;
        }
        return true;
    };

    for (int i = 0; i < 4; ++i) {
        if (!setRampTile(
                {from.x + i, to.y - i},
                6,
                higherLevel - i - 1)) {
            return finishTrace(false);
        }
    }
    for (int j = 0; j < 4; ++j) {
        const int length = from.y - to.y + j - 3;
        for (int k = 0; k < length; ++k) {
            if (!setRampTile(
                    {from.x + j, from.y - k - 3},
                    3,
                    higherLevel - j - 1)) {
                return finishTrace(false);
            }
        }
    }
    for (int m = 0; m < 4; ++m) {
        const int length = from.x - to.x + m - 3;
        for (int n = 0; n < length; ++n) {
            if (!setRampTile(
                    {to.x + n + 3, to.y - m},
                    2,
                    higherLevel - m - 1)) {
                return finishTrace(false);
            }
        }
    }
    return finishTrace(true);
}

bool NativeMapGenerator::placeCliffCornerComplexType2(
    MapGrid& map,
    Point from,
    Point to,
    int higherRegionId,
    int lowerRegionId)
{
    const bool traceType2 = native_trace::active()
        && native_trace::currentFunctionId() == native_trace::PlaceCliffAtCell;
    native_trace::EventData traceData;
    traceData.args[0] = packNativeCell(from);
    traceData.args[1] = packNativeCell(to);
    traceData.args[2] = static_cast<std::uint32_t>(lowerRegionId);
    traceData.regionId = higherRegionId;
    traceData.cellX = static_cast<std::int16_t>(from.x);
    traceData.cellY = static_cast<std::int16_t>(from.y);
    if (traceType2) {
        native_trace::enterDetailed(
            0x593550,
            native_trace::PlaceCliffCornerComplexType2,
            traceData);
    }

    std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
    overrideIndexByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t index = 0; index < report_.isoTileOverrides.size(); ++index) {
        overrideIndexByIso[nativeIsoKey(report_.isoTileOverrides[index].iso)] = index;
    }
    const auto overrideAt = [&](Point cell) -> IsoTileOverride* {
        const auto it = overrideIndexByIso.find(nativeIsoKey(cell));
        return it == overrideIndexByIso.end() ? nullptr : &report_.isoTileOverrides[it->second];
    };
    const auto constOverrideAt = [&](Point cell) -> const IsoTileOverride* {
        const auto it = overrideIndexByIso.find(nativeIsoKey(cell));
        return it == overrideIndexByIso.end() ? nullptr : &report_.isoTileOverrides[it->second];
    };
    const auto zoneFor = [&](Point cell) {
        const auto it = nativeMapgenZoneIds_.find(nativeIsoKey(cell));
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };

    const int rectX = to.x - 7;
    const int rectY = from.y - 7;
    const int rectWidth = from.x - to.x + 10;
    const int rectHeight = to.y - from.y + 10;
    const auto captureCellState = [&]() {
        std::vector<std::uint32_t> values;
        if (!native_trace::detailRequested() || rectWidth <= 0 || rectHeight <= 0) {
            return values;
        }
        values.reserve(static_cast<std::size_t>(rectWidth * rectHeight) * 6);
        for (int y = rectY; y < rectY + rectHeight; ++y) {
            for (int x = rectX; x < rectX + rectWidth; ++x) {
                const Point cell{x, y};
                const IsoTileOverride* override = constOverrideAt(cell);
                values.push_back(packNativeCell(cell));
                values.push_back(static_cast<std::uint32_t>(zoneFor(cell)));
                values.push_back(static_cast<std::uint32_t>(
                    override == nullptr ? -1 : nativeTileTypeIndexForOverride(*override)));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->subTile));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->level));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->slopeIndex));
            }
        }
        return values;
    };
    if (traceType2 && native_trace::detailRequested()) {
        native_trace::detailCurrent(
            native_trace::DetailOrderedCellState,
            captureCellState());
    }
    const auto finishTrace = [&](bool placed) {
        if (traceType2) {
            traceData.result[0] = placed ? 1u : 0u;
            native_trace::exitDetailed(
                0x593550,
                native_trace::PlaceCliffCornerComplexType2,
                traceData,
                native_trace::DetailOrderedCellState,
                captureCellState());
        }
        return placed;
    };

    if (from.x - to.x < 3 || to.y - from.y < 3 || rectWidth <= 0 || rectHeight <= 0) {
        return finishTrace(false);
    }
    if (higherRegionId < 0
        || lowerRegionId < 0
        || higherRegionId >= static_cast<int>(nativeMapgenScratchZoneLevels_.size())
        || lowerRegionId >= static_cast<int>(nativeMapgenScratchZoneLevels_.size())) {
        return finishTrace(false);
    }

    const Rect mapRect = nativeSavedMapRect(map);
    const Rect visibleRect = nativeLocalRect(map);
    const auto isWithinUsableArea = [&](Point cell) {
        const IsoTileOverride* override = constOverrideAt(cell);
        if (override == nullptr) {
            return false;
        }
        int level = override->level;
        const int sum = cell.x + cell.y;
        if (override->slopeIndex != 0
            && sum < level + mapRect.w + 2 * visibleRect.y + 4) {
            ++level;
        }
        return sum > level + mapRect.w + 2 * visibleRect.y
            && sum <= level + mapRect.w + 2 * (visibleRect.y + visibleRect.h) + 2
            && cell.x - cell.y < 2 * (visibleRect.x + visibleRect.w) - mapRect.w
            && cell.y - cell.x < mapRect.w - 2 * visibleRect.x;
    };
    const std::array<Point, 4> corners{{
        {rectX, rectY},
        {rectX + rectWidth - 1, rectY},
        {rectX, rectY + rectHeight - 1},
        {rectX + rectWidth - 1, rectY + rectHeight - 1},
    }};
    if (std::any_of(corners.begin(), corners.end(), [&](Point corner) {
            return !isWithinUsableArea(corner);
        })) {
        return finishTrace(false);
    }
    for (int y = rectY; y < rectY + rectHeight; ++y) {
        for (int x = rectX; x < rectX + rectWidth; ++x) {
            const Point cell{x, y};
            const IsoTileOverride* override = constOverrideAt(cell);
            const int zoneId = zoneFor(cell);
            if (override == nullptr
                || (zoneId != higherRegionId && zoneId != lowerRegionId)
                || !isNativeClearTileIndex(nativeTileTypeIndexForOverride(*override))) {
                return finishTrace(false);
            }
        }
    }

    const int higherLevel = nativeMapgenScratchZoneLevels_[static_cast<std::size_t>(higherRegionId)];
    const int lowerLevel = nativeMapgenScratchZoneLevels_[static_cast<std::size_t>(lowerRegionId)];
    const auto mirrorLevelToProductMap = [&](Point nativeCell, int level) {
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == nativeCell) {
            map.at(source).level = level;
        }
    };
    const auto setZoneAndLevel = [&](Point cell, int zoneId, int level) {
        if (!isInsideNativeCellDiamond(cell, mapRect)) {
            return;
        }
        nativeMapgenZoneIds_[nativeIsoKey(cell)] = zoneId;
        if (IsoTileOverride* override = overrideAt(cell); override != nullptr) {
            override->level = level;
            mirrorLevelToProductMap(cell, level);
        }
    };
    const auto fillZoneRect = [&](int xBegin, int xEnd, int yBegin, int yEnd, int zoneId, int level) {
        for (int y = yBegin; y < yEnd; ++y) {
            for (int x = xBegin; x < xEnd; ++x) {
                setZoneAndLevel({x, y}, zoneId, level);
            }
        }
    };

    fillZoneRect(rectX, rectX + rectWidth, rectY, rectY + 7, lowerRegionId, lowerLevel);
    fillZoneRect(rectX, rectX + 7, rectY, rectY + rectHeight, lowerRegionId, lowerLevel);
    fillZoneRect(from.x, from.x + 1, from.y, from.y + 2, higherRegionId, higherLevel);
    fillZoneRect(to.x, to.x + 2, to.y, to.y + 1, higherRegionId, higherLevel);
    fillZoneRect(to.x + 1, from.x, from.y + 1, to.y, higherRegionId, higherLevel);

    const int slopeSetPiecesBase = nativeSlopeSetPiecesTileBase(nativeTheaterCatalog_);
    if (slopeSetPiecesBase < 0) {
        return finishTrace(false);
    }
    static_cast<void>(placeNativeIsoTile(
        map,
        slopeSetPiecesBase + 5,
        {from.x - 2, from.y - 3},
        lowerRegionId,
        higherLevel));
    static_cast<void>(placeNativeIsoTile(
        map,
        slopeSetPiecesBase + 6,
        {from.x - 1, from.y - 1},
        lowerRegionId,
        higherLevel));
    static_cast<void>(placeNativeIsoTile(
        map,
        slopeSetPiecesBase + 8,
        {to.x - 3, to.y - 2},
        lowerRegionId,
        higherLevel));
    static_cast<void>(placeNativeIsoTile(
        map,
        slopeSetPiecesBase + 9,
        {to.x - 1, to.y - 1},
        lowerRegionId,
        higherLevel));

    const int rampBase = nativeTheaterCatalog_ == nullptr
        ? kNativeTileRampBase
        : nativeTheaterCatalog_->rampBase;
    const auto setRampTile = [&](Point nativeCell, int slopeIndex, int level) {
        IsoTileOverride* override = overrideAt(nativeCell);
        if (override == nullptr || rampBase < 0) {
            return false;
        }
        override->terrain = Terrain::Land;
        override->variant = 0;
        override->subTile = 0;
        override->level = level;
        override->landType = 0;
        override->slopeIndex = slopeIndex;
        override->nativeTileTypeIndex = rampBase + slopeIndex - 1;
        nativePlacedIsoTileKeys_.insert(nativeIsoKey(nativeCell));

        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == nativeCell) {
            Cell& productCell = map.at(source);
            productCell.terrain = Terrain::Land;
            productCell.height = 0;
            productCell.level = level;
            productCell.slope = slopeIndex;
            productCell.landType = 0;
        }
        return true;
    };

    for (int i = 0; i < 4; ++i) {
        if (!setRampTile(
                {to.x - i, from.y - i},
                5,
                higherLevel - i - 1)) {
            return finishTrace(false);
        }
    }
    for (int j = 0; j < 4; ++j) {
        const int length = from.x - to.x + j - 3;
        for (int k = 0; k < length; ++k) {
            if (!setRampTile(
                    {from.x - k - 3, from.y - j},
                    2,
                    higherLevel - j - 1)) {
                return finishTrace(false);
            }
        }
    }
    for (int m = 0; m < 4; ++m) {
        const int length = to.y - from.y + m - 3;
        for (int n = 0; n < length; ++n) {
            if (!setRampTile(
                    {to.x - m, to.y - n - 3},
                    1,
                    higherLevel - m - 1)) {
                return finishTrace(false);
            }
        }
    }
    return finishTrace(true);
}

bool NativeMapGenerator::placeCliffCornerComplexType3(
    MapGrid& map,
    Point from,
    Point to,
    int higherRegionId,
    int lowerRegionId)
{
    const bool traceType3 = native_trace::active()
        && native_trace::currentFunctionId() == native_trace::PlaceCliffAtCell;
    native_trace::EventData traceData;
    traceData.args[0] = packNativeCell(from);
    traceData.args[1] = packNativeCell(to);
    traceData.args[2] = static_cast<std::uint32_t>(lowerRegionId);
    traceData.regionId = higherRegionId;
    traceData.cellX = static_cast<std::int16_t>(from.x);
    traceData.cellY = static_cast<std::int16_t>(from.y);
    if (traceType3) {
        native_trace::enterDetailed(
            0x593AF0,
            native_trace::PlaceCliffCornerComplexType3,
            traceData);
    }
    std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
    overrideIndexByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t index = 0; index < report_.isoTileOverrides.size(); ++index) {
        overrideIndexByIso[nativeIsoKey(report_.isoTileOverrides[index].iso)] = index;
    }
    const auto overrideAt = [&](Point cell) -> IsoTileOverride* {
        const auto it = overrideIndexByIso.find(nativeIsoKey(cell));
        return it == overrideIndexByIso.end() ? nullptr : &report_.isoTileOverrides[it->second];
    };
    const auto constOverrideAt = [&](Point cell) -> const IsoTileOverride* {
        const auto it = overrideIndexByIso.find(nativeIsoKey(cell));
        return it == overrideIndexByIso.end() ? nullptr : &report_.isoTileOverrides[it->second];
    };
    const auto zoneFor = [&](Point cell) {
        const auto it = nativeMapgenZoneIds_.find(nativeIsoKey(cell));
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };

    const int rectX = from.x - 7;
    const int rectY = from.y - 4;
    const int rectWidth = to.x - from.x + 10;
    const int rectHeight = to.y - from.y + 10;
    const auto captureCellState = [&]() {
        std::vector<std::uint32_t> values;
        if (!native_trace::detailRequested() || rectWidth <= 0 || rectHeight <= 0) {
            return values;
        }
        values.reserve(static_cast<std::size_t>(rectWidth * rectHeight) * 6);
        for (int y = rectY; y < rectY + rectHeight; ++y) {
            for (int x = rectX; x < rectX + rectWidth; ++x) {
                const Point cell{x, y};
                const IsoTileOverride* override = constOverrideAt(cell);
                values.push_back(packNativeCell(cell));
                values.push_back(static_cast<std::uint32_t>(zoneFor(cell)));
                values.push_back(static_cast<std::uint32_t>(
                    override == nullptr ? -1 : nativeTileTypeIndexForOverride(*override)));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->subTile));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->level));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->slopeIndex));
            }
        }
        return values;
    };
    if (traceType3 && native_trace::detailRequested()) {
        native_trace::detailCurrent(
            native_trace::DetailOrderedCellState,
            captureCellState());
    }
    const auto finishTrace = [&](bool placed) {
        if (traceType3) {
            traceData.result[0] = placed ? 1u : 0u;
            native_trace::exitDetailed(
                0x593AF0,
                native_trace::PlaceCliffCornerComplexType3,
                traceData,
                native_trace::DetailOrderedCellState,
                captureCellState());
        }
        return placed;
    };

    if (to.x - from.x < 3 || to.y - from.y < 3 || rectWidth <= 0 || rectHeight <= 0) {
        return finishTrace(false);
    }
    if (higherRegionId < 0
        || lowerRegionId < 0
        || higherRegionId >= static_cast<int>(nativeMapgenScratchZoneLevels_.size())
        || lowerRegionId >= static_cast<int>(nativeMapgenScratchZoneLevels_.size())) {
        return finishTrace(false);
    }

    const Rect mapRect = nativeSavedMapRect(map);
    const Rect visibleRect = nativeLocalRect(map);
    const auto isWithinUsableArea = [&](Point cell) {
        const IsoTileOverride* override = constOverrideAt(cell);
        if (override == nullptr) {
            return false;
        }
        int level = override->level;
        const int sum = cell.x + cell.y;
        if (override->slopeIndex != 0
            && sum < level + mapRect.w + 2 * visibleRect.y + 4) {
            ++level;
        }
        return sum > level + mapRect.w + 2 * visibleRect.y
            && sum <= level + mapRect.w + 2 * (visibleRect.y + visibleRect.h) + 2
            && cell.x - cell.y < 2 * (visibleRect.x + visibleRect.w) - mapRect.w
            && cell.y - cell.x < mapRect.w - 2 * visibleRect.x;
    };
    const std::array<Point, 4> corners{{
        {rectX, rectY},
        {rectX + rectWidth - 1, rectY},
        {rectX, rectY + rectHeight - 1},
        {rectX + rectWidth - 1, rectY + rectHeight - 1},
    }};
    if (std::any_of(corners.begin(), corners.end(), [&](Point corner) {
            return !isWithinUsableArea(corner);
        })) {
        return finishTrace(false);
    }
    for (int y = rectY; y < rectY + rectHeight; ++y) {
        for (int x = rectX; x < rectX + rectWidth; ++x) {
            const Point cell{x, y};
            const IsoTileOverride* override = constOverrideAt(cell);
            const int zoneId = zoneFor(cell);
            if (override == nullptr
                || (zoneId != higherRegionId && zoneId != lowerRegionId)
                || !isNativeClearTileIndex(nativeTileTypeIndexForOverride(*override))) {
                return finishTrace(false);
            }
        }
    }

    const int higherLevel = nativeMapgenScratchZoneLevels_[static_cast<std::size_t>(higherRegionId)];
    const int lowerLevel = nativeMapgenScratchZoneLevels_[static_cast<std::size_t>(lowerRegionId)];
    const auto mirrorLevelToProductMap = [&](Point nativeCell, int level) {
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == nativeCell) {
            map.at(source).level = level;
        }
    };
    const auto setZoneAndLevel = [&](Point cell, int zoneId, int level) {
        if (!isInsideNativeCellDiamond(cell, mapRect)) {
            return;
        }
        nativeMapgenZoneIds_[nativeIsoKey(cell)] = zoneId;
        if (IsoTileOverride* override = overrideAt(cell); override != nullptr) {
            override->level = level;
            mirrorLevelToProductMap(cell, level);
        }
    };
    const auto fillZoneRect = [&](int xBegin, int xEnd, int yBegin, int yEnd, int zoneId, int level) {
        for (int y = yBegin; y < yEnd; ++y) {
            for (int x = xBegin; x < xEnd; ++x) {
                setZoneAndLevel({x, y}, zoneId, level);
            }
        }
    };

    fillZoneRect(rectX, from.x, rectY, rectY + rectHeight, lowerRegionId, lowerLevel);
    fillZoneRect(rectX, rectX + rectWidth, to.y + 1, to.y + 8, lowerRegionId, lowerLevel);
    fillZoneRect(from.x, from.x + 2, from.y, from.y + 1, higherRegionId, higherLevel);
    fillZoneRect(to.x, to.x + 1, to.y - 1, to.y + 1, higherRegionId, higherLevel);
    fillZoneRect(from.x + 1, to.x, from.y + 1, to.y, higherRegionId, higherLevel);

    const int slopeSetPiecesBase = nativeSlopeSetPiecesTileBase(nativeTheaterCatalog_);
    if (slopeSetPiecesBase < 0) {
        return finishTrace(false);
    }
    static_cast<void>(placeNativeIsoTile(
        map,
        slopeSetPiecesBase + 7,
        {from.x - 3, from.y},
        lowerRegionId,
        higherLevel));
    static_cast<void>(placeNativeIsoTile(
        map,
        slopeSetPiecesBase + 1,
        {to.x - 2, to.y},
        lowerRegionId,
        higherLevel));

    const int rampBase = nativeTheaterCatalog_ == nullptr
        ? kNativeTileRampBase
        : nativeTheaterCatalog_->rampBase;
    const auto setRampTile = [&](Point nativeCell, int tileOffset, int level, int slopeIndex) {
        IsoTileOverride* override = overrideAt(nativeCell);
        if (override == nullptr || rampBase < 0) {
            return false;
        }
        override->terrain = Terrain::Land;
        override->variant = 0;
        override->subTile = 0;
        override->level = level;
        override->landType = 0;
        override->slopeIndex = slopeIndex;
        override->nativeTileTypeIndex = rampBase + tileOffset;
        nativePlacedIsoTileKeys_.insert(nativeIsoKey(nativeCell));

        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == nativeCell) {
            Cell& productCell = map.at(source);
            productCell.terrain = Terrain::Land;
            productCell.height = 0;
            productCell.level = level;
            productCell.slope = slopeIndex;
            productCell.landType = 0;
        }
        return true;
    };
    for (int i = 0; i < 4; ++i) {
        if (!setRampTile(
                {from.x - i, to.y + i},
                7,
                higherLevel - i - 1,
                8)) {
            return finishTrace(false);
        }
    }
    for (int j = 0; j < 4; ++j) {
        const int length = to.y - from.y + j - 3;
        for (int k = 0; k < length; ++k) {
            if (!setRampTile(
                    {from.x - j, from.y + k + 3},
                    0,
                    higherLevel - j - 1,
                    1)) {
                return finishTrace(false);
            }
        }
    }
    for (int m = 0; m < 4; ++m) {
        const int length = to.x - from.x + m - 3;
        for (int n = 0; n < length; ++n) {
            if (!setRampTile(
                    {to.x - n - 3, to.y + m},
                    3,
                    higherLevel - m - 1,
                    4)) {
                return finishTrace(false);
            }
        }
    }
    return finishTrace(true);
}

bool NativeMapGenerator::placeCliffCornerType5(
    MapGrid& map,
    NativeRandomizer& rng,
    Point from,
    Point to,
    int higherRegionId,
    int lowerRegionId)
{
    const bool traceType5 = native_trace::active()
        && native_trace::currentFunctionId() == native_trace::PlaceCliffAtCell;
    native_trace::EventData traceData;
    traceData.args[0] = packNativeCell(from);
    traceData.args[1] = packNativeCell(to);
    traceData.args[2] = static_cast<std::uint32_t>(lowerRegionId);
    traceData.regionId = higherRegionId;
    traceData.cellX = static_cast<std::int16_t>(from.x);
    traceData.cellY = static_cast<std::int16_t>(from.y);
    if (traceType5) {
        native_trace::enterDetailed(
            0x591740,
            native_trace::PlaceCliffCornerType5,
            traceData);
    }

    std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
    overrideIndexByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t index = 0; index < report_.isoTileOverrides.size(); ++index) {
        overrideIndexByIso[nativeIsoKey(report_.isoTileOverrides[index].iso)] = index;
    }
    const auto overrideAt = [&](Point cell) -> IsoTileOverride* {
        const auto it = overrideIndexByIso.find(nativeIsoKey(cell));
        return it == overrideIndexByIso.end() ? nullptr : &report_.isoTileOverrides[it->second];
    };
    const auto constOverrideAt = [&](Point cell) -> const IsoTileOverride* {
        const auto it = overrideIndexByIso.find(nativeIsoKey(cell));
        return it == overrideIndexByIso.end() ? nullptr : &report_.isoTileOverrides[it->second];
    };
    const auto zoneFor = [&](Point cell) {
        const auto it = nativeMapgenZoneIds_.find(nativeIsoKey(cell));
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };

    const int minX = std::min(from.x, to.x);
    const int maxX = std::max(from.x, to.x);
    const int rectX = minX - 3;
    const int rectY = to.y - 4;
    const int rectWidth = maxX - minX + 11;
    const int rectHeight = from.y - to.y + 9;
    const auto captureCellState = [&]() {
        std::vector<std::uint32_t> values;
        if (!native_trace::detailRequested() || rectWidth <= 0 || rectHeight <= 0) {
            return values;
        }
        values.reserve(static_cast<std::size_t>(rectWidth * rectHeight) * 6);
        for (int y = rectY; y < rectY + rectHeight; ++y) {
            for (int x = rectX; x < rectX + rectWidth; ++x) {
                const Point cell{x, y};
                const IsoTileOverride* override = constOverrideAt(cell);
                values.push_back(packNativeCell(cell));
                values.push_back(static_cast<std::uint32_t>(zoneFor(cell)));
                values.push_back(static_cast<std::uint32_t>(
                    override == nullptr ? -1 : nativeTileTypeIndexForOverride(*override)));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->subTile));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->level));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->slopeIndex));
            }
        }
        return values;
    };
    if (traceType5 && native_trace::detailRequested()) {
        native_trace::detailCurrent(
            native_trace::DetailOrderedCellState,
            captureCellState());
    }
    const auto finishTrace = [&](bool placed) {
        if (traceType5) {
            traceData.result[0] = placed ? 1u : 0u;
            native_trace::exitDetailed(
                0x591740,
                native_trace::PlaceCliffCornerType5,
                traceData,
                native_trace::DetailOrderedCellState,
                captureCellState());
        }
        return placed;
    };

    if (rectWidth <= 0 || rectHeight <= 0) {
        return finishTrace(false);
    }
    const Rect mapRect = nativeSavedMapRect(map);
    const Rect visibleRect = nativeLocalRect(map);
    const auto isWithinUsableArea = [&](Point cell) {
        const IsoTileOverride* override = constOverrideAt(cell);
        if (override == nullptr) {
            return false;
        }
        int level = override->level;
        const int sum = cell.x + cell.y;
        if (override->slopeIndex != 0
            && sum < level + mapRect.w + 2 * visibleRect.y + 4) {
            ++level;
        }
        return sum > level + mapRect.w + 2 * visibleRect.y
            && sum <= level + mapRect.w + 2 * (visibleRect.y + visibleRect.h) + 2
            && cell.x - cell.y < 2 * (visibleRect.x + visibleRect.w) - mapRect.w
            && cell.y - cell.x < mapRect.w - 2 * visibleRect.x;
    };
    const std::array<Point, 4> corners{{
        {rectX, rectY},
        {rectX + rectWidth - 1, rectY},
        {rectX, rectY + rectHeight - 1},
        {rectX + rectWidth - 1, rectY + rectHeight - 1},
    }};
    if (std::any_of(corners.begin(), corners.end(), [&](Point corner) {
            return !isWithinUsableArea(corner);
        })) {
        return finishTrace(false);
    }
    for (int y = rectY; y < rectY + rectHeight; ++y) {
        for (int x = rectX; x < rectX + rectWidth; ++x) {
            const Point cell{x, y};
            const IsoTileOverride* override = constOverrideAt(cell);
            const int zoneId = zoneFor(cell);
            if (override == nullptr
                || (zoneId != higherRegionId && zoneId != lowerRegionId)
                || !isNativeClearTileIndex(nativeTileTypeIndexForOverride(*override))) {
                return finishTrace(false);
            }
        }
    }

    const int horizontalDelta = to.x - from.x;
    const int horizontalLength = std::abs(horizontalDelta);
    const int verticalLength = from.y - to.y;
    if (horizontalLength > verticalLength - 5) {
        return finishTrace(false);
    }
    if (higherRegionId < 0
        || lowerRegionId < 0
        || higherRegionId >= static_cast<int>(nativeMapgenScratchZoneLevels_.size())
        || lowerRegionId >= static_cast<int>(nativeMapgenScratchZoneLevels_.size())) {
        return finishTrace(false);
    }
    const int higherLevel = nativeMapgenScratchZoneLevels_[static_cast<std::size_t>(higherRegionId)];
    const int lowerLevel = nativeMapgenScratchZoneLevels_[static_cast<std::size_t>(lowerRegionId)];
    const int halfHeight = (verticalLength + 1) / 2 + 2;
    const int halfCore = (verticalLength + 1) / 2;

    const auto mirrorLevelToProductMap = [&](Point nativeCell, int level) {
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == nativeCell) {
            map.at(source).level = level;
        }
    };
    const auto setZoneAndLevel = [&](Point cell, int zoneId, int level) {
        if (!isInsideNativeCellDiamond(cell, mapRect)) {
            return;
        }
        nativeMapgenZoneIds_[nativeIsoKey(cell)] = zoneId;
        if (IsoTileOverride* override = overrideAt(cell); override != nullptr) {
            override->level = level;
            mirrorLevelToProductMap(cell, level);
        }
    };
    const auto fillZoneRect = [&](int xBegin, int xEnd, int yBegin, int yEnd, int zoneId, int level) {
        for (int y = yBegin; y < yEnd; ++y) {
            for (int x = xBegin; x < xEnd; ++x) {
                setZoneAndLevel({x, y}, zoneId, level);
            }
        }
    };

    const int upperMidY = from.y - halfCore;
    fillZoneRect(
        from.x + 1,
        from.x + 8,
        upperMidY,
        upperMidY + halfHeight + 3,
        lowerRegionId,
        lowerLevel);
    fillZoneRect(
        to.x + 1,
        to.x + 8,
        rectY,
        rectY + halfHeight + 2,
        lowerRegionId,
        lowerLevel);
    fillZoneRect(
        rectX + 2,
        from.x + 1,
        upperMidY,
        from.y + 1,
        higherRegionId,
        higherLevel);
    fillZoneRect(
        rectX + 2,
        to.x + 1,
        to.y,
        to.y + halfCore,
        higherRegionId,
        higherLevel);

    const int slopeSetPiecesBase = nativeSlopeSetPiecesTileBase(nativeTheaterCatalog_);
    if (slopeSetPiecesBase < 0) {
        return finishTrace(false);
    }
    static_cast<void>(placeNativeIsoTile(
        map,
        slopeSetPiecesBase + 2,
        {from.x, from.y - 2},
        lowerRegionId,
        higherLevel));
    static_cast<void>(placeNativeIsoTile(
        map,
        slopeSetPiecesBase + 3,
        to,
        lowerRegionId,
        higherLevel));

    const int rampBase = nativeTheaterCatalog_ == nullptr
        ? kNativeTileRampBase
        : nativeTheaterCatalog_->rampBase;
    const auto setRampTile = [&](Point nativeCell, int slopeIndex, int level) {
        IsoTileOverride* override = overrideAt(nativeCell);
        if (override == nullptr || rampBase < 0) {
            return false;
        }
        override->terrain = Terrain::Land;
        override->variant = 0;
        override->subTile = 0;
        override->level = level;
        override->landType = 0;
        override->slopeIndex = slopeIndex;
        override->nativeTileTypeIndex = rampBase + slopeIndex - 1;
        nativePlacedIsoTileKeys_.insert(nativeIsoKey(nativeCell));

        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == nativeCell) {
            Cell& productCell = map.at(source);
            productCell.terrain = Terrain::Land;
            productCell.height = 0;
            productCell.level = level;
            productCell.slope = slopeIndex;
            productCell.landType = 0;
        }
        return true;
    };

    const int straightLength = verticalLength - horizontalLength - 5;
    int straightOffsetX = 0;
    int straightOffsetY = 0;
    int diagonalOffsetY = 0;
    const int diagonalOffsetX = horizontalDelta < 0 ? -1 : 0;
    if (horizontalDelta != 0) {
        constexpr double kTwoOverUInt32Range = 2.0 / 4294967296.0;
        int randomDirection = 0;
        do {
            randomDirection = nativeDoubleToInt(static_cast<double>(rng.random()) * kTwoOverUInt32Range);
        } while (randomDirection > 1);
        if (randomDirection == 1) {
            straightOffsetX = horizontalDelta;
            straightOffsetY = -horizontalLength;
        } else {
            diagonalOffsetY = -straightLength;
        }

        constexpr std::array<int, 5> kDiagonalLevelOffsets{{3, 2, 1, 0, 0}};
        constexpr std::array<int, 5> kNegativeSlopeIndexes{{10, 14, 14, 14, 6}};
        constexpr std::array<int, 5> kPositiveSlopeIndexes{{11, 15, 15, 15, 7}};
        const auto& slopeIndexes = horizontalDelta > 0
            ? kPositiveSlopeIndexes
            : kNegativeSlopeIndexes;
        for (int step = 0; step < horizontalLength; ++step) {
            const int directedStep = horizontalDelta > 0 ? step : -step;
            for (int across = 0; across < 5; ++across) {
                if (!setRampTile(
                        {
                            from.x + across + diagonalOffsetX + directedStep,
                            from.y - 3 - step + diagonalOffsetY,
                        },
                        slopeIndexes[static_cast<std::size_t>(across)],
                        higherLevel + kDiagonalLevelOffsets[static_cast<std::size_t>(across)] - 4)) {
                    return finishTrace(false);
                }
            }
        }
    }

    for (int step = 0; step < straightLength; ++step) {
        for (int across = 0; across < 4; ++across) {
            const Point cell{
                from.x + across + straightOffsetX,
                from.y - 3 - step + straightOffsetY,
            };
            nativeMapgenZoneIds_[nativeIsoKey(cell)] = lowerRegionId;
            if (!setRampTile(cell, 3, higherLevel - across - 1)) {
                return finishTrace(false);
            }
        }
    }
    return finishTrace(true);
}

bool NativeMapGenerator::placeCliffCornerType7(
    MapGrid& map,
    NativeRandomizer& rng,
    Point from,
    Point to,
    int higherRegionId,
    int lowerRegionId)
{
    const bool traceType7 = native_trace::active()
        && native_trace::currentFunctionId() == native_trace::PlaceCliffAtCell;
    native_trace::EventData traceData;
    traceData.args[0] = packNativeCell(from);
    traceData.args[1] = packNativeCell(to);
    traceData.args[2] = static_cast<std::uint32_t>(lowerRegionId);
    traceData.regionId = higherRegionId;
    traceData.cellX = static_cast<std::int16_t>(from.x);
    traceData.cellY = static_cast<std::int16_t>(from.y);
    if (traceType7) {
        native_trace::enterDetailed(
            0x592440,
            native_trace::PlaceCliffCornerType7,
            traceData);
    }

    std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
    overrideIndexByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t index = 0; index < report_.isoTileOverrides.size(); ++index) {
        overrideIndexByIso[nativeIsoKey(report_.isoTileOverrides[index].iso)] = index;
    }
    const auto overrideAt = [&](Point cell) -> IsoTileOverride* {
        const auto it = overrideIndexByIso.find(nativeIsoKey(cell));
        return it == overrideIndexByIso.end() ? nullptr : &report_.isoTileOverrides[it->second];
    };
    const auto constOverrideAt = [&](Point cell) -> const IsoTileOverride* {
        const auto it = overrideIndexByIso.find(nativeIsoKey(cell));
        return it == overrideIndexByIso.end() ? nullptr : &report_.isoTileOverrides[it->second];
    };
    const auto zoneFor = [&](Point cell) {
        const auto it = nativeMapgenZoneIds_.find(nativeIsoKey(cell));
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };

    const int minX = std::min(from.x, to.x);
    const int maxX = std::max(from.x, to.x);
    const int rectX = minX - 7;
    const int rectY = from.y - 4;
    const int rectWidth = maxX - minX + 11;
    const int rectHeight = to.y - from.y + 9;
    const auto captureCellState = [&]() {
        std::vector<std::uint32_t> values;
        if (!native_trace::detailRequested() || rectWidth <= 0 || rectHeight <= 0) {
            return values;
        }
        values.reserve(static_cast<std::size_t>(rectWidth * rectHeight) * 6);
        for (int y = rectY; y < rectY + rectHeight; ++y) {
            for (int x = rectX; x < rectX + rectWidth; ++x) {
                const Point cell{x, y};
                const IsoTileOverride* override = constOverrideAt(cell);
                values.push_back(packNativeCell(cell));
                values.push_back(static_cast<std::uint32_t>(zoneFor(cell)));
                values.push_back(static_cast<std::uint32_t>(
                    override == nullptr ? -1 : nativeTileTypeIndexForOverride(*override)));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->subTile));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->level));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->slopeIndex));
            }
        }
        return values;
    };
    if (traceType7 && native_trace::detailRequested()) {
        native_trace::detailCurrent(
            native_trace::DetailOrderedCellState,
            captureCellState());
    }
    const auto finishTrace = [&](bool placed) {
        if (traceType7) {
            traceData.result[0] = placed ? 1u : 0u;
            native_trace::exitDetailed(
                0x592440,
                native_trace::PlaceCliffCornerType7,
                traceData,
                native_trace::DetailOrderedCellState,
                captureCellState());
        }
        return placed;
    };

    if (rectWidth <= 0 || rectHeight <= 0) {
        return finishTrace(false);
    }
    const Rect mapRect = nativeSavedMapRect(map);
    const Rect visibleRect = nativeLocalRect(map);
    const auto isWithinUsableArea = [&](Point cell) {
        const IsoTileOverride* override = constOverrideAt(cell);
        if (override == nullptr) {
            return false;
        }
        int level = override->level;
        const int sum = cell.x + cell.y;
        if (override->slopeIndex != 0
            && sum < level + mapRect.w + 2 * visibleRect.y + 4) {
            ++level;
        }
        return sum > level + mapRect.w + 2 * visibleRect.y
            && sum <= level + mapRect.w + 2 * (visibleRect.y + visibleRect.h) + 2
            && cell.x - cell.y < 2 * (visibleRect.x + visibleRect.w) - mapRect.w
            && cell.y - cell.x < mapRect.w - 2 * visibleRect.x;
    };
    const std::array<Point, 4> corners{{
        {rectX, rectY},
        {rectX + rectWidth - 1, rectY},
        {rectX, rectY + rectHeight - 1},
        {rectX + rectWidth - 1, rectY + rectHeight - 1},
    }};
    if (std::any_of(corners.begin(), corners.end(), [&](Point corner) {
            return !isWithinUsableArea(corner);
        })) {
        return finishTrace(false);
    }
    for (int y = rectY; y < rectY + rectHeight; ++y) {
        for (int x = rectX; x < rectX + rectWidth; ++x) {
            const Point cell{x, y};
            const IsoTileOverride* override = constOverrideAt(cell);
            const int zoneId = zoneFor(cell);
            if (override == nullptr
                || (zoneId != higherRegionId && zoneId != lowerRegionId)
                || !isNativeClearTileIndex(nativeTileTypeIndexForOverride(*override))) {
                return finishTrace(false);
            }
        }
    }

    const int horizontalDelta = to.x - from.x;
    const int horizontalLength = std::abs(horizontalDelta);
    const int verticalLength = to.y - from.y;
    if (horizontalLength > verticalLength - 5) {
        return finishTrace(false);
    }
    if (higherRegionId < 0
        || lowerRegionId < 0
        || higherRegionId >= static_cast<int>(nativeMapgenScratchZoneLevels_.size())
        || lowerRegionId >= static_cast<int>(nativeMapgenScratchZoneLevels_.size())) {
        return finishTrace(false);
    }
    const int higherLevel = nativeMapgenScratchZoneLevels_[static_cast<std::size_t>(higherRegionId)];
    const int lowerLevel = nativeMapgenScratchZoneLevels_[static_cast<std::size_t>(lowerRegionId)];
    const int halfHeight = (verticalLength + 1) / 2 + 2;
    const int halfCore = (verticalLength + 1) / 2;

    const auto mirrorLevelToProductMap = [&](Point nativeCell, int level) {
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == nativeCell) {
            map.at(source).level = level;
        }
    };
    const auto setZoneAndLevel = [&](Point cell, int zoneId, int level) {
        if (!isInsideNativeCellDiamond(cell, mapRect)) {
            return;
        }
        nativeMapgenZoneIds_[nativeIsoKey(cell)] = zoneId;
        if (IsoTileOverride* override = overrideAt(cell); override != nullptr) {
            override->level = level;
            mirrorLevelToProductMap(cell, level);
        }
    };
    const auto fillZoneRect = [&](int xBegin, int xEnd, int yBegin, int yEnd, int zoneId, int level) {
        for (int y = yBegin; y < yEnd; ++y) {
            for (int x = xBegin; x < xEnd; ++x) {
                setZoneAndLevel({x, y}, zoneId, level);
            }
        }
    };

    fillZoneRect(
        rectX,
        from.x,
        rectY,
        rectY + halfHeight + 2,
        lowerRegionId,
        lowerLevel);
    const int lowerEndY = to.y + 5;
    fillZoneRect(
        rectX,
        to.x,
        to.y - halfHeight + 3,
        lowerEndY,
        lowerRegionId,
        lowerLevel);
    const int higherEndX = rectX + rectWidth - 1;
    fillZoneRect(
        from.x,
        higherEndX,
        from.y,
        from.y + halfHeight - 1,
        higherRegionId,
        higherLevel);
    fillZoneRect(
        to.x,
        higherEndX,
        to.y - halfHeight + 3,
        to.y - halfHeight + 3 + halfCore,
        higherRegionId,
        higherLevel);

    const int slopeSetPiecesBase = nativeSlopeSetPiecesTileBase(nativeTheaterCatalog_);
    if (slopeSetPiecesBase < 0) {
        return finishTrace(false);
    }
    static_cast<void>(placeNativeIsoTile(
        map,
        slopeSetPiecesBase + 7,
        {from.x - 3, from.y},
        lowerRegionId,
        higherLevel));
    static_cast<void>(placeNativeIsoTile(
        map,
        slopeSetPiecesBase + 8,
        {to.x - 3, to.y - 2},
        lowerRegionId,
        higherLevel));
    static_cast<void>(placeNativeIsoTile(
        map,
        slopeSetPiecesBase + 9,
        {to.x - 1, to.y - 1},
        lowerRegionId,
        higherLevel));

    const int rampBase = nativeTheaterCatalog_ == nullptr
        ? kNativeTileRampBase
        : nativeTheaterCatalog_->rampBase;
    const auto setRampTile = [&](Point nativeCell, int slopeIndex, int level) {
        IsoTileOverride* override = overrideAt(nativeCell);
        if (override == nullptr || rampBase < 0) {
            return false;
        }
        override->terrain = Terrain::Land;
        override->variant = 0;
        override->subTile = 0;
        override->level = level;
        override->landType = 0;
        override->slopeIndex = slopeIndex;
        override->nativeTileTypeIndex = rampBase + slopeIndex - 1;
        nativePlacedIsoTileKeys_.insert(nativeIsoKey(nativeCell));

        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == nativeCell) {
            Cell& productCell = map.at(source);
            productCell.terrain = Terrain::Land;
            productCell.height = 0;
            productCell.level = level;
            productCell.slope = slopeIndex;
            productCell.landType = 0;
        }
        return true;
    };

    const int straightLength = verticalLength - horizontalLength - 5;
    int straightOffsetX = 0;
    int straightOffsetY = 0;
    int diagonalOffsetY = 0;
    const int diagonalOffsetX = horizontalDelta > 0 ? 1 : 0;
    if (horizontalDelta != 0) {
        constexpr double kTwoOverUInt32Range = 2.0 / 4294967296.0;
        int randomDirection = 0;
        do {
            randomDirection = nativeDoubleToInt(static_cast<double>(rng.random()) * kTwoOverUInt32Range);
        } while (randomDirection > 1);
        if (randomDirection == 1) {
            straightOffsetX = horizontalDelta;
            straightOffsetY = horizontalLength;
        } else {
            diagonalOffsetY = straightLength;
        }

        constexpr std::array<int, 5> kDiagonalLevelOffsets{{3, 2, 1, 0, 0}};
        constexpr std::array<int, 5> kNegativeSlopeIndexes{{9, 13, 13, 13, 5}};
        constexpr std::array<int, 5> kPositiveSlopeIndexes{{12, 16, 16, 16, 8}};
        const auto& slopeIndexes = horizontalDelta > 0
            ? kPositiveSlopeIndexes
            : kNegativeSlopeIndexes;
        for (int step = 0; step < horizontalLength; ++step) {
            const int directedStep = horizontalDelta > 0 ? step : -step;
            for (int across = 0; across < 5; ++across) {
                if (!setRampTile(
                        {
                            from.x - across + diagonalOffsetX + directedStep,
                            from.y + 3 + step + diagonalOffsetY,
                        },
                        slopeIndexes[static_cast<std::size_t>(across)],
                        higherLevel + kDiagonalLevelOffsets[static_cast<std::size_t>(across)] - 4)) {
                    return finishTrace(false);
                }
            }
        }
    }

    for (int step = 0; step < straightLength; ++step) {
        for (int across = 0; across < 4; ++across) {
            const Point cell{
                from.x - across + straightOffsetX,
                from.y + 3 + step + straightOffsetY,
            };
            nativeMapgenZoneIds_[nativeIsoKey(cell)] = lowerRegionId;
            if (!setRampTile(cell, 1, higherLevel - across - 1)) {
                return finishTrace(false);
            }
        }
    }
    return finishTrace(true);
}

bool NativeMapGenerator::placeCliffCornerType8(
    MapGrid& map,
    NativeRandomizer& rng,
    Point from,
    Point to,
    int higherRegionId,
    int lowerRegionId)
{
    const bool traceType8 = native_trace::active()
        && native_trace::currentFunctionId() == native_trace::PlaceCliffAtCell;
    native_trace::EventData traceData;
    traceData.args[0] = packNativeCell(from);
    traceData.args[1] = packNativeCell(to);
    traceData.args[2] = static_cast<std::uint32_t>(lowerRegionId);
    traceData.regionId = higherRegionId;
    traceData.cellX = static_cast<std::int16_t>(from.x);
    traceData.cellY = static_cast<std::int16_t>(from.y);
    if (traceType8) {
        native_trace::enterDetailed(
            0x5910F0,
            native_trace::PlaceCliffCornerType8,
            traceData);
    }

    std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
    overrideIndexByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t index = 0; index < report_.isoTileOverrides.size(); ++index) {
        overrideIndexByIso[nativeIsoKey(report_.isoTileOverrides[index].iso)] = index;
    }
    const auto overrideAt = [&](Point cell) -> IsoTileOverride* {
        const auto it = overrideIndexByIso.find(nativeIsoKey(cell));
        return it == overrideIndexByIso.end() ? nullptr : &report_.isoTileOverrides[it->second];
    };
    const auto constOverrideAt = [&](Point cell) -> const IsoTileOverride* {
        const auto it = overrideIndexByIso.find(nativeIsoKey(cell));
        return it == overrideIndexByIso.end() ? nullptr : &report_.isoTileOverrides[it->second];
    };
    const auto zoneFor = [&](Point cell) {
        const auto it = nativeMapgenZoneIds_.find(nativeIsoKey(cell));
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };

    const int rectX = from.x - 5;
    const int rectY = std::min(from.y, to.y) - 3;
    const int rectWidth = to.x - from.x + 9;
    const int rectHeight = std::abs(to.y - from.y) + 11;
    const auto captureCellState = [&]() {
        std::vector<std::uint32_t> values;
        if (!native_trace::detailRequested() || rectWidth <= 0 || rectHeight <= 0) {
            return values;
        }
        values.reserve(static_cast<std::size_t>(rectWidth * rectHeight) * 6);
        for (int y = rectY; y < rectY + rectHeight; ++y) {
            for (int x = rectX; x < rectX + rectWidth; ++x) {
                const Point cell{x, y};
                const IsoTileOverride* override = constOverrideAt(cell);
                values.push_back(packNativeCell(cell));
                values.push_back(static_cast<std::uint32_t>(zoneFor(cell)));
                values.push_back(static_cast<std::uint32_t>(
                    override == nullptr ? -1 : nativeTileTypeIndexForOverride(*override)));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->subTile));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->level));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->slopeIndex));
            }
        }
        return values;
    };
    if (traceType8 && native_trace::detailRequested()) {
        native_trace::detailCurrent(
            native_trace::DetailOrderedCellState,
            captureCellState());
    }
    const auto finishTrace = [&](bool placed) {
        if (traceType8) {
            traceData.result[0] = placed ? 1u : 0u;
            native_trace::exitDetailed(
                0x5910F0,
                native_trace::PlaceCliffCornerType8,
                traceData,
                native_trace::DetailOrderedCellState,
                captureCellState());
        }
        return placed;
    };

    if (rectWidth <= 0 || rectHeight <= 0) {
        return finishTrace(false);
    }
    const Rect mapRect = nativeSavedMapRect(map);
    const Rect visibleRect = nativeLocalRect(map);
    const auto isWithinUsableArea = [&](Point cell) {
        const IsoTileOverride* override = constOverrideAt(cell);
        if (override == nullptr) {
            return false;
        }
        int level = override->level;
        const int sum = cell.x + cell.y;
        if (override->slopeIndex != 0
            && sum < level + mapRect.w + 2 * visibleRect.y + 4) {
            ++level;
        }
        return sum > level + mapRect.w + 2 * visibleRect.y
            && sum <= level + mapRect.w + 2 * (visibleRect.y + visibleRect.h) + 2
            && cell.x - cell.y < 2 * (visibleRect.x + visibleRect.w) - mapRect.w
            && cell.y - cell.x < mapRect.w - 2 * visibleRect.x;
    };
    const std::array<Point, 4> corners{{
        {rectX, rectY},
        {rectX + rectWidth - 1, rectY},
        {rectX, rectY + rectHeight - 1},
        {rectX + rectWidth - 1, rectY + rectHeight - 1},
    }};
    if (std::any_of(corners.begin(), corners.end(), [&](Point corner) {
            return !isWithinUsableArea(corner);
        })) {
        return finishTrace(false);
    }
    for (int y = rectY; y < rectY + rectHeight; ++y) {
        for (int x = rectX; x < rectX + rectWidth; ++x) {
            const Point cell{x, y};
            const IsoTileOverride* override = constOverrideAt(cell);
            const int zoneId = zoneFor(cell);
            if (override == nullptr
                || (zoneId != higherRegionId && zoneId != lowerRegionId)
                || !isNativeClearTileIndex(nativeTileTypeIndexForOverride(*override))) {
                return finishTrace(false);
            }
        }
    }

    const int deltaY = to.y - from.y;
    const int verticalLength = std::abs(deltaY);
    const int horizontalLength = to.x - from.x;
    if (verticalLength > horizontalLength - 5) {
        return finishTrace(false);
    }
    if (higherRegionId < 0
        || lowerRegionId < 0
        || higherRegionId >= static_cast<int>(nativeMapgenScratchZoneLevels_.size())
        || lowerRegionId >= static_cast<int>(nativeMapgenScratchZoneLevels_.size())) {
        return finishTrace(false);
    }
    const int higherLevel = nativeMapgenScratchZoneLevels_[static_cast<std::size_t>(higherRegionId)];
    const int lowerLevel = nativeMapgenScratchZoneLevels_[static_cast<std::size_t>(lowerRegionId)];
    const int halfWidth = (horizontalLength + 1) / 2 + 3;
    const int zoneBottom = rectY + rectHeight + 2;

    const auto mirrorLevelToProductMap = [&](Point nativeCell, int level) {
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == nativeCell) {
            map.at(source).level = level;
        }
    };
    const auto setZoneAndLevel = [&](Point cell, int zoneId, int level) {
        if (!isInsideNativeCellDiamond(cell, mapRect)) {
            return;
        }
        nativeMapgenZoneIds_[nativeIsoKey(cell)] = zoneId;
        if (IsoTileOverride* override = overrideAt(cell); override != nullptr) {
            override->level = level;
            mirrorLevelToProductMap(cell, level);
        }
    };
    const auto fillZoneRect = [&](int xBegin, int xEnd, int yBegin, int yEnd, int zoneId, int level) {
        for (int y = yBegin; y < yEnd; ++y) {
            for (int x = xBegin; x < xEnd; ++x) {
                setZoneAndLevel({x, y}, zoneId, level);
            }
        }
    };

    fillZoneRect(rectX, rectX + halfWidth + 2, from.y + 1, zoneBottom, lowerRegionId, lowerLevel);
    const int secondLowerX = to.x - (horizontalLength + 1) / 2;
    fillZoneRect(secondLowerX, secondLowerX + halfWidth + 2, to.y + 1, zoneBottom, lowerRegionId, lowerLevel);
    fillZoneRect(rectX + 4, rectX + halfWidth + 2, rectY + 2, from.y + 1, higherRegionId, higherLevel);
    fillZoneRect(to.x - halfWidth + 2, to.x + 1, rectY + 2, to.y + 1, higherRegionId, higherLevel);

    const int slopeSetPiecesBase = nativeSlopeSetPiecesTileBase(nativeTheaterCatalog_);
    if (slopeSetPiecesBase < 0) {
        return finishTrace(false);
    }
    static_cast<void>(placeNativeIsoTile(
        map,
        slopeSetPiecesBase,
        from,
        lowerRegionId,
        higherLevel));
    static_cast<void>(placeNativeIsoTile(
        map,
        slopeSetPiecesBase + 1,
        {to.x - 2, to.y},
        lowerRegionId,
        higherLevel));

    const int rampBase = nativeTheaterCatalog_ == nullptr
        ? kNativeTileRampBase
        : nativeTheaterCatalog_->rampBase;
    const auto setRampTile = [&](Point nativeCell, int slopeIndex, int level) {
        IsoTileOverride* override = overrideAt(nativeCell);
        if (override == nullptr || rampBase < 0) {
            return false;
        }
        override->terrain = Terrain::Land;
        override->variant = 0;
        override->subTile = 0;
        override->level = level;
        override->landType = 0;
        override->slopeIndex = slopeIndex;
        override->nativeTileTypeIndex = rampBase + slopeIndex - 1;
        nativePlacedIsoTileKeys_.insert(nativeIsoKey(nativeCell));

        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == nativeCell) {
            Cell& productCell = map.at(source);
            productCell.terrain = Terrain::Land;
            productCell.height = 0;
            productCell.level = level;
            productCell.slope = slopeIndex;
            productCell.landType = 0;
        }
        return true;
    };

    int straightOffsetX = 0;
    int straightOffsetY = 0;
    int diagonalOffsetX = 0;
    const int diagonalOffsetY = deltaY < 0 ? -1 : 0;
    if (deltaY != 0) {
        int randomDirection = 0;
        constexpr double kTwoOverUInt32Range = 2.0 / 4294967296.0;
        do {
            randomDirection = nativeDoubleToInt(static_cast<double>(rng.random()) * kTwoOverUInt32Range);
        } while (randomDirection > 1);
        if (randomDirection == 1) {
            straightOffsetX = verticalLength;
            straightOffsetY = deltaY;
        } else {
            diagonalOffsetX = horizontalLength - verticalLength - 5;
        }

        constexpr std::array<int, 5> kDiagonalLevelOffsets{{3, 2, 1, 0, 0}};
        constexpr std::array<int, 5> kNegativeSlopeIndexes{{11, 15, 15, 15, 7}};
        constexpr std::array<int, 5> kPositiveSlopeIndexes{{12, 16, 16, 16, 8}};
        const auto& slopeIndexes = deltaY > 0 ? kPositiveSlopeIndexes : kNegativeSlopeIndexes;
        for (int step = 0; step < verticalLength; ++step) {
            const int directedStep = deltaY > 0 ? step : -step;
            for (int across = 0; across < 5; ++across) {
                if (!setRampTile(
                        {
                            from.x + 3 + step + diagonalOffsetX,
                            from.y + across + diagonalOffsetY + directedStep,
                        },
                        slopeIndexes[static_cast<std::size_t>(across)],
                        higherLevel + kDiagonalLevelOffsets[static_cast<std::size_t>(across)] - 4)) {
                    return finishTrace(false);
                }
            }
        }
    }

    const int straightLength = horizontalLength - verticalLength - 5;
    for (int step = 0; step < straightLength; ++step) {
        for (int across = 0; across < 4; ++across) {
            const Point cell{
                from.x + 3 + step + straightOffsetX,
                from.y + across + straightOffsetY,
            };
            nativeMapgenZoneIds_[nativeIsoKey(cell)] = lowerRegionId;
            if (!setRampTile(cell, 4, higherLevel - across - 1)) {
                return finishTrace(false);
            }
        }
    }
    return finishTrace(true);
}

bool NativeMapGenerator::placeCliffCornerType6(
    MapGrid& map,
    NativeRandomizer& rng,
    Point from,
    Point to,
    int higherRegionId,
    int lowerRegionId)
{
    const bool traceType6 = native_trace::active()
        && native_trace::currentFunctionId() == native_trace::PlaceCliffAtCell;
    native_trace::EventData traceData;
    traceData.args[0] = packNativeCell(from);
    traceData.args[1] = packNativeCell(to);
    traceData.args[2] = static_cast<std::uint32_t>(lowerRegionId);
    traceData.regionId = higherRegionId;
    traceData.cellX = static_cast<std::int16_t>(from.x);
    traceData.cellY = static_cast<std::int16_t>(from.y);
    if (traceType6) {
        native_trace::enterDetailed(
            0x591D80,
            native_trace::PlaceCliffCornerType6,
            traceData);
    }
    std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
    overrideIndexByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t index = 0; index < report_.isoTileOverrides.size(); ++index) {
        overrideIndexByIso[nativeIsoKey(report_.isoTileOverrides[index].iso)] = index;
    }
    const auto overrideAt = [&](Point cell) -> IsoTileOverride* {
        const auto it = overrideIndexByIso.find(nativeIsoKey(cell));
        return it == overrideIndexByIso.end() ? nullptr : &report_.isoTileOverrides[it->second];
    };
    const auto constOverrideAt = [&](Point cell) -> const IsoTileOverride* {
        const auto it = overrideIndexByIso.find(nativeIsoKey(cell));
        return it == overrideIndexByIso.end() ? nullptr : &report_.isoTileOverrides[it->second];
    };
    const auto zoneFor = [&](Point cell) {
        const auto it = nativeMapgenZoneIds_.find(nativeIsoKey(cell));
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };
    const Rect mapRect = nativeSavedMapRect(map);
    const Rect visibleRect = nativeLocalRect(map);
    const auto isWithinUsableArea = [&](Point cell) {
        const IsoTileOverride* override = constOverrideAt(cell);
        if (override == nullptr) {
            return false;
        }
        int level = override->level;
        const int sum = cell.x + cell.y;
        if (override->slopeIndex != 0
            && sum < level + mapRect.w + 2 * visibleRect.y + 4) {
            ++level;
        }
        return sum > level + mapRect.w + 2 * visibleRect.y
            && sum <= level + mapRect.w + 2 * (visibleRect.y + visibleRect.h) + 2
            && cell.x - cell.y < 2 * (visibleRect.x + visibleRect.w) - mapRect.w
            && cell.y - cell.x < mapRect.w - 2 * visibleRect.x;
    };

    const int rectX = to.x - 4;
    const int rectY = std::min(from.y, to.y) - 7;
    const int rectWidth = from.x - to.x + 8;
    const int rectHeight = std::abs(from.y - to.y) + 11;
    const auto captureCellState = [&]() {
        std::vector<std::uint32_t> values;
        if (!native_trace::detailRequested() || rectWidth <= 0 || rectHeight <= 0) {
            return values;
        }
        values.reserve(static_cast<std::size_t>(rectWidth * rectHeight) * 6);
        for (int y = rectY; y < rectY + rectHeight; ++y) {
            for (int x = rectX; x < rectX + rectWidth; ++x) {
                const Point cell{x, y};
                const IsoTileOverride* override = constOverrideAt(cell);
                values.push_back(packNativeCell(cell));
                values.push_back(static_cast<std::uint32_t>(zoneFor(cell)));
                values.push_back(static_cast<std::uint32_t>(
                    override == nullptr ? -1 : nativeTileTypeIndexForOverride(*override)));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->subTile));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->level));
                values.push_back(static_cast<std::uint32_t>(override == nullptr ? -1 : override->slopeIndex));
            }
        }
        return values;
    };
    if (traceType6 && native_trace::detailRequested()) {
        const std::vector<std::uint32_t> detailValues = captureCellState();
        native_trace::detailCurrent(
            native_trace::DetailOrderedCellState,
            detailValues);
    }
    const auto finishTrace = [&](bool placed) {
        if (traceType6) {
            traceData.result[0] = placed ? 1u : 0u;
            const std::vector<std::uint32_t> detailValues = captureCellState();
            native_trace::exitDetailed(
                0x591D80,
                native_trace::PlaceCliffCornerType6,
                traceData,
                native_trace::DetailOrderedCellState,
                detailValues);
        }
        return placed;
    };
    if (rectWidth <= 0 || rectHeight <= 0) {
        return finishTrace(false);
    }
    const std::array<Point, 4> corners{{
        {rectX, rectY},
        {rectX + rectWidth - 1, rectY},
        {rectX, rectY + rectHeight - 1},
        {rectX + rectWidth - 1, rectY + rectHeight - 1},
    }};
    if (std::any_of(corners.begin(), corners.end(), [&](Point corner) {
            return !isWithinUsableArea(corner);
        })) {
        return finishTrace(false);
    }
    for (int y = rectY; y < rectY + rectHeight; ++y) {
        for (int x = rectX; x < rectX + rectWidth; ++x) {
            const Point cell{x, y};
            const IsoTileOverride* override = constOverrideAt(cell);
            const int zoneId = zoneFor(cell);
            if (override == nullptr
                || (zoneId != higherRegionId && zoneId != lowerRegionId)
                || !isNativeClearTileIndex(nativeTileTypeIndexForOverride(*override))) {
                return finishTrace(false);
            }
        }
    }

    const int deltaY = to.y - from.y;
    const int verticalLength = std::abs(deltaY);
    const int horizontalLength = from.x - to.x;
    if (verticalLength > horizontalLength - 5) {
        return finishTrace(false);
    }
    if (higherRegionId < 0
        || lowerRegionId < 0
        || higherRegionId >= static_cast<int>(nativeMapgenScratchZoneLevels_.size())
        || lowerRegionId >= static_cast<int>(nativeMapgenScratchZoneLevels_.size())) {
        return finishTrace(false);
    }
    const int higherLevel = nativeMapgenScratchZoneLevels_[static_cast<std::size_t>(higherRegionId)];
    const int lowerLevel = nativeMapgenScratchZoneLevels_[static_cast<std::size_t>(lowerRegionId)];
    const int halfWidth = (horizontalLength + 1) / 2 + 3;
    const int zoneBottom = rectY + rectHeight - 2;

    const auto mirrorLevelToProductMap = [&](Point nativeCell, int level) {
        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == nativeCell) {
            map.at(source).level = level;
        }
    };
    const auto setZoneAndLevel = [&](Point cell, int zoneId, int level) {
        if (!isInsideNativeCellDiamond(cell, mapRect)) {
            return;
        }
        nativeMapgenZoneIds_[nativeIsoKey(cell)] = zoneId;
        if (IsoTileOverride* override = overrideAt(cell); override != nullptr) {
            override->level = level;
            mirrorLevelToProductMap(cell, level);
        }
    };
    const auto fillZoneRect = [&](int xBegin, int xEnd, int yBegin, int yEnd, int zoneId, int level) {
        for (int y = yBegin; y < yEnd; ++y) {
            for (int x = xBegin; x < xEnd; ++x) {
                setZoneAndLevel({x, y}, zoneId, level);
            }
        }
    };

    fillZoneRect(rectX, rectX + halfWidth + 2, rectY, to.y, lowerRegionId, lowerLevel);
    fillZoneRect(from.x - halfWidth + 2, from.x + 5, rectY, from.y, lowerRegionId, lowerLevel);
    fillZoneRect(to.x, to.x + halfWidth - 2, to.y, zoneBottom, higherRegionId, higherLevel);
    fillZoneRect(from.x - halfWidth + 2, from.x + 1, from.y, zoneBottom, higherRegionId, higherLevel);

    const auto setNativeTile = [&](Point nativeCell, int tile, int subTile, int level, int slopeIndex) {
        IsoTileOverride* override = overrideAt(nativeCell);
        if (override == nullptr) {
            return;
        }
        const bool cliffTile = tile >= kNativeTileCliffBase && tile < kNativeTileShoreBase;
        override->terrain = cliffTile ? Terrain::Cliff : Terrain::Land;
        override->variant = cliffTile ? tile - kNativeTileCliffBase : 0;
        override->subTile = cliffTile ? subTile : 0;
        override->level = level;
        override->landType = cliffTile ? 3 : 0;
        override->slopeIndex = slopeIndex;
        override->nativeTileTypeIndex = tile;

        const Point source = nativeCellToSourcePoint(map, nativeCell);
        if (!map.inBounds(source) || sourcePointToNativeCell(map, source) != nativeCell) {
            return;
        }
        Cell& productCell = map.at(source);
        productCell.terrain = override->terrain;
        productCell.height = 0;
        productCell.level = level;
        productCell.slope = cliffTile
            ? encodeCliffPiece({override->variant, override->subTile})
            : slopeIndex;
        productCell.landType = override->landType;
    };
    const auto placeLegacyCliffTemplate = [&](int piece, Point anchor) {
        const int width = cliffPieceWidth(piece);
        const int height = cliffPieceHeight(piece);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (!isValidCliffPieceSlot(piece, x, y)) {
                    continue;
                }
                const Point cell{anchor.x + x, anchor.y + y};
                if (!isInsideNativeCellDiamond(cell, mapRect)) {
                    continue;
                }
                const int subTile = cliffPieceSubTile(piece, x, y);
                const int level = higherLevel + cliffPieceCellHeight(piece, subTile) - 4;
                setNativeTile(cell, kNativeTileCliffBase + piece, subTile, level, 0);
                nativeMapgenZoneIds_[nativeIsoKey(cell)] = lowerRegionId;
            }
        }
    };
    const int slopeSetPiecesBase = nativeSlopeSetPiecesTileBase(nativeTheaterCatalog_);
    if (slopeSetPiecesBase >= 0) {
        if (!placeNativeIsoTile(
                map,
                slopeSetPiecesBase + 4,
                {to.x, to.y - 3},
                lowerRegionId,
                higherLevel)
            || !placeNativeIsoTile(
                map,
                slopeSetPiecesBase + 5,
                {from.x - 2, from.y - 3},
                lowerRegionId,
                higherLevel)
            || !placeNativeIsoTile(
                map,
                slopeSetPiecesBase + 6,
                {from.x - 1, from.y - 1},
                lowerRegionId,
                higherLevel)) {
            return finishTrace(false);
        }
    } else {
        placeLegacyCliffTemplate(4, {to.x, to.y - 3});
        placeLegacyCliffTemplate(5, {from.x - 2, from.y - 3});
        placeLegacyCliffTemplate(6, {from.x - 1, from.y - 1});
    }

    int randomDirection = 0;
    int straightOffsetX = 0;
    int straightOffsetY = 0;
    int diagonalOffsetX = 0;
    const int diagonalOffsetY = deltaY < 0 ? 1 : 0;
    if (deltaY != 0) {
        constexpr double kTwoOverUInt32Range = 2.0 / 4294967296.0;
        do {
            randomDirection = nativeDoubleToInt(static_cast<double>(rng.random()) * kTwoOverUInt32Range);
        } while (randomDirection > 1);
        if (randomDirection == 1) {
            straightOffsetX = verticalLength;
            straightOffsetY = -deltaY;
        } else {
            diagonalOffsetX = horizontalLength - verticalLength - 5;
        }

        constexpr std::array<int, 5> kDiagonalLevelOffsets{{3, 2, 1, 0, 0}};
        constexpr std::array<int, 5> kNegativeSlopeIndexes{{10, 14, 14, 14, 6}};
        constexpr std::array<int, 5> kPositiveSlopeIndexes{{9, 13, 13, 13, 5}};
        const auto& slopeIndexes = deltaY > 0 ? kPositiveSlopeIndexes : kNegativeSlopeIndexes;
        for (int step = 0; step < verticalLength; ++step) {
            const int directedStep = deltaY > 0 ? -step : step;
            for (int across = 0; across < 5; ++across) {
                const Point cell{
                    to.x + 3 + step + diagonalOffsetX,
                    to.y - across + diagonalOffsetY + directedStep,
                };
                const int slopeIndex = slopeIndexes[static_cast<std::size_t>(across)];
                setNativeTile(
                    cell,
                    kNativeTileRampBase + slopeIndex - 1,
                    0,
                    higherLevel + kDiagonalLevelOffsets[static_cast<std::size_t>(across)] - 4,
                    slopeIndex);
            }
        }
    }

    const int straightLength = horizontalLength - verticalLength - 5;
    for (int step = 0; step < straightLength; ++step) {
        for (int across = 0; across < 4; ++across) {
            const Point cell{
                to.x + 3 + step + straightOffsetX,
                to.y - across + straightOffsetY,
            };
            nativeMapgenZoneIds_[nativeIsoKey(cell)] = lowerRegionId;
            setNativeTile(
                cell,
                kNativeTileRampBase + 1,
                0,
                higherLevel - across - 1,
                2);
        }
    }
    return finishTrace(true);
}

bool NativeMapGenerator::placeRegionCliffAtCell(
    MapGrid& map,
    NativeRandomizer& rng,
    Point nativeCell,
    int higherRegionId,
    int lowerRegionId,
    float fallbackBias)
{
    const bool traceCliff = native_trace::active()
        && native_trace::currentFunctionId() == native_trace::ProcessRegionBridgesAndCliffs;
    native_trace::EventData traceData;
    traceData.regionId = lowerRegionId;
    traceData.cellX = static_cast<std::int16_t>(nativeCell.x);
    traceData.cellY = static_cast<std::int16_t>(nativeCell.y);
    traceData.args[0] = packNativeCell(nativeCell);
    traceData.args[1] = static_cast<std::uint32_t>(lowerRegionId);
    traceData.args[2] = std::bit_cast<std::uint32_t>(fallbackBias);
    if (traceCliff) {
        native_trace::enterDetailed(0x590970, native_trace::PlaceCliffAtCell, traceData);
    }
    const auto finishTrace = [&](std::uint32_t site, bool placed) {
        if (traceCliff) {
            traceData.result[0] = placed ? 1u : 0u;
            native_trace::exitDetailed(site, native_trace::PlaceCliffAtCell, traceData);
        }
        return placed;
    };

    ++report_.regionCliffMaskChecks;
    const NativeCliffNeighborMaskResult maskResult = nativeCliffNeighborMask(
        nativeCell,
        higherRegionId,
        fallbackBias,
        nativeSavedMapRect(map),
        nativeMapgenZoneIds_,
        nativeMapgenDefaultZoneId_);
    const int cliffNeighborMask = maskResult.mask;
    if (traceCliff) {
        native_trace::EventData maskData = traceData;
        maskData.result[0] = static_cast<std::uint32_t>(cliffNeighborMask);
        maskData.result[1] = maskResult.centerAccepted ? 1u : 0u;
        native_trace::callDetailed(0x590999, native_trace::GetCliffNeighborMask, maskData);
    }

    if (!maskResult.centerAccepted) {
        ++report_.regionCliffMaskFailures;
        return finishTrace(0x5909A6, false);
    }
    if (cliffNeighborMask == 0xFF) {
        ++report_.regionCliffMaskFailures;
        return finishTrace(0x5909BC, false);
    }

    // Corner stamping is traced independently so the first unsupported corner
    // is visible at its original callsite instead of being hidden by later map
    // state. Failed native corner attempts have no map effect.
    const auto attemptCorner = [&](std::uint32_t site, Point from = {-1, -1}, Point to = {-1, -1}, int implementation = 0) {
        if (traceCliff) {
            native_trace::EventData cornerData = traceData;
            cornerData.args[0] = site;
            native_trace::callDetailed(site, native_trace::PlaceCliffCorner, cornerData);
        }
        if (implementation == 1) {
            return placeCliffCornerComplexType1(
                map,
                from,
                to,
                higherRegionId,
                lowerRegionId);
        }
        if (implementation == 2) {
            return placeCliffCornerComplexType2(
                map,
                from,
                to,
                higherRegionId,
                lowerRegionId);
        }
        if (implementation == 3) {
            return placeCliffCornerComplexType3(
                map,
                from,
                to,
                higherRegionId,
                lowerRegionId);
        }
        if (implementation == 5) {
            return placeCliffCornerType5(
                map,
                rng,
                from,
                to,
                higherRegionId,
                lowerRegionId);
        }
        if (implementation == 6) {
            return placeCliffCornerType6(
                map,
                rng,
                from,
                to,
                higherRegionId,
                lowerRegionId);
        }
        if (implementation == 8) {
            return placeCliffCornerType8(
                map,
                rng,
                from,
                to,
                higherRegionId,
                lowerRegionId);
        }
        if (implementation == 7) {
            return placeCliffCornerType7(
                map,
                rng,
                from,
                to,
                higherRegionId,
                lowerRegionId);
        }
        return false;
    };
    const auto randomBinary = [&]() {
        constexpr double kTwoOverUInt32Range = 2.0 / 4294967296.0;
        return nativeDoubleToInt(static_cast<double>(rng.random()) * kTwoOverUInt32Range);
    };

    bool placed = false;
    if ((cliffNeighborMask & 0x82) == 0x82 && (cliffNeighborMask & 0x38) == 0) {
        Point from{nativeCell.x - 1, nativeCell.y - 5};
        Point to{nativeCell.x + 5, nativeCell.y + 1};
        if ((cliffNeighborMask & 0x40) != 0) {
            ++from.y;
        }
        if ((cliffNeighborMask & 0x04) != 0) {
            --to.x;
        }
        placed = attemptCorner(0x590A1D, from, to, 3);
    }
    if (!placed && (cliffNeighborMask & 0x0A) == 0x0A && (cliffNeighborMask & 0x11) == 0) {
        Point from{nativeCell.x + 5, nativeCell.y - 1};
        Point to{nativeCell.x - 1, nativeCell.y + 5};
        if ((cliffNeighborMask & 0x10) != 0) {
            --from.y;
        }
        if ((cliffNeighborMask & 0x01) != 0) {
            --from.x;
        }
        placed = attemptCorner(0x590A81, from, to, 2);
    }
    if (!placed && (cliffNeighborMask & 0x28) == 0x28 && (cliffNeighborMask & 0x83) == 0) {
        Point from{nativeCell.x + 1, nativeCell.y + 5};
        Point to{nativeCell.x - 5, nativeCell.y - 1};
        if ((cliffNeighborMask & 0x40) != 0) {
            --to.x;
        }
        if ((cliffNeighborMask & 0x04) != 0) {
            --from.y;
        }
        placed = attemptCorner(0x590AE5, from, to, 1);
    }
    if (!placed && (cliffNeighborMask & 0xA0) == 0xA0 && (cliffNeighborMask & 0x0E) == 0) {
        Point from{nativeCell.x - 5, nativeCell.y + 1};
        Point to{nativeCell.x + 1, nativeCell.y - 5};
        if ((cliffNeighborMask & 0x01) != 0) {
            ++to.y;
        }
        if ((cliffNeighborMask & 0x10) != 0) {
            ++from.x;
        }
        placed = attemptCorner(0x590B4C, from, to, 1);
    }
    if (!placed && (cliffNeighborMask & 0x70) == 0 && (cliffNeighborMask & 0x88) != 0) {
        Point from{nativeCell.x - 1, nativeCell.y - 4};
        Point to{nativeCell.x - 1, nativeCell.y + 4};
        if ((cliffNeighborMask & 0x80) != 0) {
            ++from.y;
            ++to.y;
        }
        if ((cliffNeighborMask & 0x08) != 0) {
            --from.y;
            --to.y;
        }
        from.x += randomBinary();
        to.x += randomBinary();
        placed = attemptCorner(0x590C19, from, to, 7);
    }
    if (!placed && (cliffNeighborMask & 0x07) == 0 && (cliffNeighborMask & 0x88) != 0) {
        Point from{nativeCell.x + 1, nativeCell.y + 4};
        Point to{nativeCell.x + 1, nativeCell.y - 4};
        if ((cliffNeighborMask & 0x80) != 0) {
            ++from.y;
            ++to.y;
        }
        if ((cliffNeighborMask & 0x08) != 0) {
            --from.y;
            --to.y;
        }
        from.x += randomBinary() - 1;
        to.x += randomBinary() - 1;
        placed = attemptCorner(0x590CE8, from, to, 5);
    }
    if (!placed && (cliffNeighborMask & 0xC1) == 0 && (cliffNeighborMask & 0x22) != 0) {
        Point from{nativeCell.x + 4, nativeCell.y - 1};
        Point to{nativeCell.x - 4, nativeCell.y - 1};
        if ((cliffNeighborMask & 0x20) != 0) {
            ++from.x;
            ++to.x;
        }
        if ((cliffNeighborMask & 0x02) != 0) {
            --from.x;
            --to.x;
        }
        from.y += randomBinary();
        to.y += randomBinary();
        placed = attemptCorner(0x590DB5, from, to, 6);
    }
    if (!placed && (cliffNeighborMask & 0x1C) == 0 && (cliffNeighborMask & 0x22) != 0) {
        Point from{nativeCell.x - 4, nativeCell.y + 1};
        Point to{nativeCell.x + 4, nativeCell.y + 1};
        if ((cliffNeighborMask & 0x20) != 0) {
            ++from.x;
            ++to.x;
        }
        if ((cliffNeighborMask & 0x02) != 0) {
            --from.x;
            --to.x;
        }
        from.y += randomBinary() - 1;
        to.y += randomBinary() - 1;
        placed = attemptCorner(0x590E84, from, to, 8);
    }
    if (fallbackBias > 0.5f && !placed) {
        if ((cliffNeighborMask & 0x08) == 0) {
            placed = attemptCorner(
                0x590EE3,
                {nativeCell.x - 4, nativeCell.y + 1},
                {nativeCell.x + 4, nativeCell.y + 1},
                8);
        }
        if (!placed && (cliffNeighborMask & 0x02) == 0) {
            placed = attemptCorner(
                0x590F2B,
                {nativeCell.x + 1, nativeCell.y + 4},
                {nativeCell.x + 1, nativeCell.y - 4},
                5);
        }
        if (!placed && (cliffNeighborMask & 0x08) == 0) {
            placed = attemptCorner(
                0x590F72,
                {nativeCell.x + 4, nativeCell.y - 1},
                {nativeCell.x - 4, nativeCell.y - 1},
                6);
        }
        if (!placed && (cliffNeighborMask & 0x20) == 0) {
            placed = attemptCorner(
                0x590FB6,
                {nativeCell.x - 1, nativeCell.y - 4},
                {nativeCell.x - 1, nativeCell.y + 4},
                7);
        }
    }
    if (!placed) {
        ++report_.regionCliffMaskFailures;
    }
    return finishTrace(0x590FC0, placed);
}

void NativeMapGenerator::processRegionBridgesAndCliffs(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, std::vector<NativeRegion>& regions)
{
    if (regions.empty()) {
        return;
    }

    ++report_.regionBridgeCliffCalls;
    int maxRegionId = 0;
    for (const NativeRegion& region : regions) {
        maxRegionId = std::max(maxRegionId, region.id);
    }
    if (maxRegionId <= 0) {
        return;
    }

    std::vector<int> idToIndex(static_cast<std::size_t>(maxRegionId + 1), -1);
    for (std::size_t i = 0; i < regions.size(); ++i) {
        if (regions[i].id >= 0) {
            idToIndex[static_cast<std::size_t>(regions[i].id)] = static_cast<int>(i);
        }
    }

    auto regionById = [&](int id) -> NativeRegion* {
        if (id < 0 || id > maxRegionId) {
            return nullptr;
        }
        const int index = idToIndex[static_cast<std::size_t>(id)];
        if (index < 0) {
            return nullptr;
        }
        return &regions[static_cast<std::size_t>(index)];
    };

    const Rect mapRect = nativeSavedMapRect(map);
    const Rect visibleRect = nativeLocalRect(map);
    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(
        map,
        report_.isoTileOverrides);
    std::unordered_map<std::uint64_t, const NativeIteratorCell*> iteratorCellByKey;
    iteratorCellByKey.reserve(iteratorCells.size() * 2);
    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        iteratorCellByKey[nativeIsoKey(iteratorCell.cell)] = &iteratorCell;
    }
    const auto randomExclusiveScaledInclusive = [&](int minimum, int maximum) {
        const int lo = std::min(minimum, maximum);
        const int hi = std::max(minimum, maximum);
        const double span = static_cast<double>(
            static_cast<std::int64_t>(hi) - static_cast<std::int64_t>(lo) + 1);
        constexpr double kExclusiveUInt32Unit = 1.0 / 4294967296.0;
        return nativeDoubleToInt(
            static_cast<double>(rng.random()) * span * kExclusiveUInt32Unit
            + static_cast<double>(lo));
    };

    for (NativeRegion& region : regions) {
        region.processed = false;
    }

    const int accessibility = std::clamp(options.accessibility, 0, 100);
    for (std::size_t regionIndex = 0; regionIndex < regions.size(); ++regionIndex) {
        NativeRegion& region = regions[regionIndex];
        const bool traceRegion = native_trace::active()
            && native_trace::currentFunctionId() == native_trace::InitializeMapRegions;
        native_trace::EventData traceData;
        if (traceRegion) {
            traceData.regionIndex = static_cast<int>(regionIndex);
            traceData.regionId = region.id;
            traceData.regionCount = static_cast<int>(regions.size());
            traceData.regionCellCount = static_cast<int>(region.nativeCells.size());
            traceData.cellX = 0;
            traceData.cellY = 0;
            traceData.args[1] = static_cast<std::uint32_t>(region.level);
            traceData.args[2] = region.wet ? 1u : 0u;
            native_trace::enterDetailed(
                0x5905D0,
                native_trace::ProcessRegionBridgesAndCliffs,
                traceData);
        }
        if (region.wet) {
            for (std::size_t firstIndex = 0;
                 firstIndex + 1 < region.neighbors.size();
                 ++firstIndex) {
                NativeRegion* firstBank = regionById(region.neighbors[firstIndex]);
                if (firstBank == nullptr
                    || (firstBank->neighbors.size() <= 1
                        && firstBank->nativeConstructedCellCount <= 50)) {
                    continue;
                }
                for (std::size_t secondIndex = firstIndex + 1;
                     secondIndex < region.neighbors.size();
                     ++secondIndex) {
                    NativeRegion* secondBank = regionById(region.neighbors[secondIndex]);
                    if (secondBank == nullptr
                        || (secondBank->neighbors.size() <= 1
                            && secondBank->nativeConstructedCellCount <= 50)
                        || secondBank->wet
                        || firstBank->level != secondBank->level
                        || firstBank->level != region.level) {
                        continue;
                    }

                    ++report_.regionBridgePairsChecked;
                    if (placeBridgeBetweenRegions(map, rng, region, *firstBank, *secondBank)) {
                        ++report_.regionBridgesPlaced;
                    }
                }
            }
        } else {
            for (int neighborId : region.neighbors) {
                NativeRegion* neighbor = regionById(neighborId);
                if (!neighbor || neighbor->id <= region.id || neighbor->level == region.level) {
                    continue;
                }

                ++report_.regionCliffPairsChecked;
                const NativeRegion* high = region.level > neighbor->level ? &region : neighbor;
                const NativeRegion* low = region.level > neighbor->level ? neighbor : &region;
                const int randomGate = randomExclusiveScaledInclusive(0, 100);
                const int targetCliffs = randomGate >= accessibility
                    ? 1
                    : 1 + randomExclusiveScaledInclusive(1, 2);
                int placed = 0;
                int attemptBudget = 0;

                const int highIndex = high->id >= 0 && high->id <= maxRegionId
                    ? idToIndex[static_cast<std::size_t>(high->id)]
                    : -1;
                const std::vector<Point> boundary = collectNativeZoneBoundaryCells(
                    iteratorCells,
                    mapRect,
                    nativeMapgenZoneIds_,
                    nativeMapgenDefaultZoneId_,
                    high->id,
                    highIndex,
                    static_cast<int>(regions.size()),
                    static_cast<int>(high->nativeCells.size()),
                    high->nativeCells.empty() ? Point{-1, -1} : high->nativeCells.front(),
                    &nativeInvalidCellPoint_);

                while (!boundary.empty() && placed < targetCliffs) {
                    if (attemptBudget >= 100) {
                        ++report_.regionCliffAttemptLimitHits;
                        break;
                    }
                    const Point candidate = boundary[static_cast<std::size_t>(
                        randomExclusiveScaledInclusive(0, static_cast<int>(boundary.size()) - 1))];
                    ++attemptBudget;
                    ++report_.regionCliffAttempts;
                    const float fallbackBias = static_cast<float>(attemptBudget - 1) * 0.0099999998f;
                    const auto iteratorIt = iteratorCellByKey.find(nativeIsoKey(candidate));
                    if (iteratorIt != iteratorCellByKey.end()
                        && isNativeCellWithinUsableArea(
                            candidate,
                            *iteratorIt->second,
                            mapRect,
                            visibleRect)
                        && placeRegionCliffAtCell(
                            map,
                            rng,
                            candidate,
                            high->id,
                            low->id,
                            fallbackBias)) {
                        ++placed;
                        ++report_.regionCliffsPlaced;
                    }
                }
            }
        }
        if (traceRegion) {
            native_trace::exitDetailed(
                region.wet && region.neighbors.size() > 1 ? 0x590723 : 0x590958,
                native_trace::ProcessRegionBridgesAndCliffs,
                traceData);
        }
    }
}

bool NativeMapGenerator::calculateNativeMapgenLevelsRecursive(MapGrid& map)
{
    struct WorkspaceCell {
        Point cell;
        int level = kNativeBaseMapLevel;
        int tile = kNativeTileClear;
        int height = 0;
        int slopeIndex = 0;
        std::size_t overrideIndex = std::numeric_limits<std::size_t>::max();
    };

    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, report_.isoTileOverrides);
    std::unordered_map<std::uint64_t, std::size_t> overrideByIso;
    overrideByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t i = 0; i < report_.isoTileOverrides.size(); ++i) {
        overrideByIso[nativeIsoKey(report_.isoTileOverrides[i].iso)] = i;
    }

    std::unordered_map<std::uint64_t, WorkspaceCell> cells;
    cells.reserve(iteratorCells.size() * 2);
    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        WorkspaceCell cell;
        cell.cell = iteratorCell.cell;
        cell.level = iteratorCell.level;
        cell.tile = iteratorCell.isoTileTypeIndex;
        cell.height = iteratorCell.height;
        cell.slopeIndex = iteratorCell.slopeIndex;
        if (const auto overrideIt = overrideByIso.find(nativeIsoKey(iteratorCell.cell));
            overrideIt != overrideByIso.end()) {
            cell.overrideIndex = overrideIt->second;
        }
        cells[nativeIsoKey(cell.cell)] = cell;
    }

    constexpr std::array<std::pair<Point, int>, 8> kSlopeNeighbors{{
        {{0, -1}, 0x80}, {{1, -1}, 0x01}, {{1, 0}, 0x02}, {{1, 1}, 0x04},
        {{0, 1}, 0x08}, {{-1, 1}, 0x10}, {{-1, 0}, 0x20}, {{-1, -1}, 0x40},
    }};
    auto cellAt = [&](Point cell) -> WorkspaceCell* {
        const auto it = cells.find(nativeIsoKey(cell));
        return it == cells.end() ? nullptr : &it->second;
    };
    auto slopeFlags = [&](Point cell) {
        const WorkspaceCell* current = cellAt(cell);
        if (current == nullptr) {
            return 0;
        }
        int flags = 0;
        for (const auto& [offset, bit] : kSlopeNeighbors) {
            const WorkspaceCell* neighbor = cellAt({cell.x + offset.x, cell.y + offset.y});
            if (neighbor != nullptr
                && neighbor->level == current->level + 4
                && !isNativeFlatSlopeBlockingTile(
                    nativeTheaterCatalog_,
                    neighbor->tile,
                    neighbor->height)) {
                flags |= bit;
            }
        }
        return flags;
    };
    auto setLevel = [&](WorkspaceCell& cell, int level) {
        cell.level = level;
        if (cell.overrideIndex != std::numeric_limits<std::size_t>::max()) {
            report_.isoTileOverrides[cell.overrideIndex].level = level;
        }
        const Point source = nativeCellToSourcePoint(map, cell.cell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == cell.cell) {
            map.at(source).level = level;
        }
    };
    auto zoneFor = [&](Point cell) {
        const auto it = nativeMapgenZoneIds_.find(nativeIsoKey(cell));
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };

    std::ofstream levelTrace;
    if (const char* tracePath = std::getenv("RMG_NATIVE_LEVEL_TRACE_PATH");
        tracePath != nullptr && tracePath[0] != '\0') {
        const std::filesystem::path path{tracePath};
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        levelTrace.open(path, std::ios::out | std::ios::trunc);
        if (levelTrace.is_open()) {
            levelTrace << "phase,x,y,tile,level,zone,flags\n";
        }
    }
    auto dumpLevelTrace = [&](const char* phase) {
        if (!levelTrace.is_open()) {
            return;
        }
        for (const NativeIteratorCell& iteratorCell : iteratorCells) {
            const WorkspaceCell* cell = cellAt(iteratorCell.cell);
            if (cell == nullptr) {
                continue;
            }
            levelTrace << phase << ',' << cell->cell.x << ',' << cell->cell.y << ','
                       << cell->tile << ',' << cell->level << ',' << zoneFor(cell->cell) << ','
                       << slopeFlags(cell->cell) << '\n';
        }
    };
    dumpLevelTrace("before");

    std::function<bool(Point, int)> calculate;
    calculate = [&](Point cell, int requestedZone) {
        WorkspaceCell* current = cellAt(cell);
        const bool traceRecursiveLevel = native_trace::coreFunctionEnabled(
            native_trace::CalculateCellLevelRecursive);
        native_trace::EventData traceData;
        if (traceRecursiveLevel) {
            traceData.args[0] = packNativeCell(cell);
            traceData.args[1] = static_cast<std::uint32_t>(requestedZone);
            traceData.args[2] = current != nullptr
                ? static_cast<std::uint32_t>(current->tile)
                : 0;
            traceData.args[3] = current != nullptr
                ? static_cast<std::uint32_t>(current->height)
                : 0;
            traceData.args[4] = current != nullptr
                ? static_cast<std::uint32_t>(current->level)
                : 0;
            traceData.args[5] = current != nullptr
                ? static_cast<std::uint32_t>(current->slopeIndex)
                : 0;
            traceData.regionIndex = current != nullptr && nativeMapgenDataActive_ ? 1 : 0;
            traceData.regionId = current != nullptr ? zoneFor(cell) : -1;
            traceData.regionCount = 0;
            traceData.regionCellCount = 0;
            traceData.cellX = static_cast<std::int16_t>(cell.x);
            traceData.cellY = static_cast<std::int16_t>(cell.y);
            traceData.resultKind = native_trace::ResultNone;
            native_trace::enterDetailed(
                0x579010,
                native_trace::CalculateCellLevelRecursive,
                traceData);
        }
        const auto finishTrace = [&](bool result) {
            if (traceRecursiveLevel) {
                traceData.result[0] = result ? 1u : 0u;
                traceData.result[1] = current != nullptr
                    ? static_cast<std::uint32_t>(current->tile)
                    : 0;
                traceData.result[2] = current != nullptr
                    ? static_cast<std::uint32_t>(current->height)
                        | (static_cast<std::uint32_t>(current->level) << 8)
                        | (static_cast<std::uint32_t>(current->slopeIndex) << 16)
                    : 0;
                traceData.result[3] = current != nullptr
                    ? static_cast<std::uint32_t>(zoneFor(cell))
                    : 0xFFFFFFFFu;
                traceData.resultKind = native_trace::ResultLegacy;
                native_trace::exitDetailed(
                    0x579010,
                    native_trace::CalculateCellLevelRecursive,
                    traceData);
            }
            return result;
        };
        if (current == nullptr) {
            return finishTrace(true);
        }

        const int flags = slopeFlags(cell);
        const int westNorth = flags & 0xA0;
        bool raise = westNorth == 0xA0 && (flags & 0x11) == 0x11;
        if ((flags & 0x20) != 0 && (slopeFlags({cell.x + 1, cell.y}) & 0x02) != 0) {
            raise = true;
        }
        if ((flags & 0x02) != 0 && (slopeFlags({cell.x - 1, cell.y}) & 0x20) != 0) {
            raise = true;
        }
        if ((flags & 0x08) != 0 && (slopeFlags({cell.x, cell.y - 1}) & 0x80) != 0) {
            raise = true;
        }
        if ((flags & 0x80) != 0 && (slopeFlags({cell.x, cell.y + 1}) & 0x08) != 0) {
            raise = true;
        }
        if ((flags & 0x11) == 0x11
            && ((flags & 0x0A) != 0x0A || (flags & 0xE0) != 0)
            && (westNorth != 0xA0 || (flags & 0x0E) != 0)) {
            raise = true;
        }
        if ((flags & 0x44) == 0x44
            && ((flags & 0x28) != 0x28 || (flags & 0x83) != 0)
            && ((flags & 0x82) != 0x82 || (flags & 0x38) != 0)) {
            raise = true;
        }
        raise = raise
            || (flags & 0x2C) == 0x24
            || (flags & 0xA1) == 0x21
            || (flags & 0x1A) == 0x12
            || (flags & 0xC2) == 0x42
            || (flags & 0x0B) == 0x09
            || (flags & 0x68) == 0x48
            || (flags & 0x86) == 0x84
            || (flags & 0xB0) == 0x90
            || (flags & 0x88) == 0x88
            || (flags & 0x22) == 0x22;
        if (!raise) {
            return finishTrace(true);
        }

        const int currentZone = zoneFor(cell);
        if (currentZone > 0 && currentZone != requestedZone && requestedZone != -1) {
            return finishTrace(false);
        }
        setLevel(*current, current->level + 4);
        nativeMapgenZoneIds_[nativeIsoKey(cell)] = requestedZone;
        for (const auto& [offset, bit] : kSlopeNeighbors) {
            (void)bit;
            if (!calculate({cell.x + offset.x, cell.y + offset.y}, requestedZone)) {
                return finishTrace(false);
            }
        }
        return finishTrace(true);
    };

    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        if (!calculate(iteratorCell.cell, -1)) {
            dumpLevelTrace("after");
            return false;
        }
    }
    dumpLevelTrace("after");
    return true;
}

void NativeMapGenerator::fixNativeCliffSlopeTransitions(MapGrid& map, NativeRandomizer& rng)
{
    struct WorkspaceCell {
        Point cell;
        int level = kNativeBaseMapLevel;
        int tile = kNativeTileClear;
        int height = 0;
        std::size_t overrideIndex = std::numeric_limits<std::size_t>::max();
    };

    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, report_.isoTileOverrides);
    std::unordered_map<std::uint64_t, std::size_t> overrideByIso;
    overrideByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t i = 0; i < report_.isoTileOverrides.size(); ++i) {
        overrideByIso[nativeIsoKey(report_.isoTileOverrides[i].iso)] = i;
    }
    std::unordered_map<std::uint64_t, WorkspaceCell> cells;
    cells.reserve(iteratorCells.size() * 2);
    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        WorkspaceCell cell;
        cell.cell = iteratorCell.cell;
        cell.level = iteratorCell.level;
        cell.tile = iteratorCell.isoTileTypeIndex;
        cell.height = iteratorCell.height;
        if (const auto overrideIt = overrideByIso.find(nativeIsoKey(iteratorCell.cell));
            overrideIt != overrideByIso.end()) {
            cell.overrideIndex = overrideIt->second;
        }
        cells[nativeIsoKey(cell.cell)] = cell;
    }

    constexpr std::array<std::pair<Point, int>, 8> kSlopeNeighbors{{
        {{0, -1}, 0x80}, {{1, -1}, 0x01}, {{1, 0}, 0x02}, {{1, 1}, 0x04},
        {{0, 1}, 0x08}, {{-1, 1}, 0x10}, {{-1, 0}, 0x20}, {{-1, -1}, 0x40},
    }};
    auto cellAt = [&](Point cell) -> WorkspaceCell* {
        const auto it = cells.find(nativeIsoKey(cell));
        if (it == cells.end()) {
            nativeInvalidCellPoint_ = cell;
            return nullptr;
        }
        return &it->second;
    };
    auto slopeFlags = [&](Point cell) {
        const WorkspaceCell* current = cellAt(cell);
        if (current == nullptr) {
            return 0;
        }
        int flags = 0;
        for (const auto& [offset, bit] : kSlopeNeighbors) {
            const WorkspaceCell* neighbor = cellAt({cell.x + offset.x, cell.y + offset.y});
            if (neighbor != nullptr
                && neighbor->level == current->level + 4
                && !isNativeFlatSlopeBlockingTile(
                    nativeTheaterCatalog_,
                    neighbor->tile,
                    neighbor->height)) {
                flags |= bit;
            }
        }
        return flags;
    };
    auto setLevel = [&](WorkspaceCell& cell, int level) {
        cell.level = level;
        if (cell.overrideIndex != std::numeric_limits<std::size_t>::max()) {
            report_.isoTileOverrides[cell.overrideIndex].level = level;
        }
        const Point source = nativeCellToSourcePoint(map, cell.cell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == cell.cell) {
            map.at(source).level = level;
        }
    };
    auto zoneFor = [&](Point cell) {
        const auto it = nativeMapgenZoneIds_.find(nativeIsoKey(cell));
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };
    auto copyZone = [&](Point target, Point source) {
        nativeMapgenZoneIds_[nativeIsoKey(target)] = zoneFor(source);
    };
    auto randomBinary = [&]() {
        constexpr double kNativeHalfRangeUnit = 1.0 / 2147483648.0;
        return nativeDoubleToInt(static_cast<double>(rng.random()) * kNativeHalfRangeUnit);
    };

    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        const Point currentPoint = iteratorCell.cell;
        WorkspaceCell* current = cellAt(currentPoint);
        if (current == nullptr) {
            continue;
        }
        const Point southWest{currentPoint.x - 1, currentPoint.y + 1};
        const Point northEast{currentPoint.x + 1, currentPoint.y - 1};
        const Point southEast{currentPoint.x + 1, currentPoint.y + 1};
        const Point east{currentPoint.x + 1, currentPoint.y};
        const Point west{currentPoint.x - 1, currentPoint.y};
        const int flags = slopeFlags(currentPoint);
        if (flags == 0x83) {
            WorkspaceCell* eastCell = cellAt(east);
            if (slopeFlags(southEast) == 0x83
                && eastCell != nullptr
                && isNativeClearTileIndex(eastCell->tile)
                && randomBinary() == 0) {
                setLevel(*eastCell, eastCell->level - 4);
                copyZone(east, currentPoint);
            }
        } else if (flags == 0x38) {
            WorkspaceCell* westCell = cellAt(west);
            if (slopeFlags(southEast) == 0x38
                && westCell != nullptr
                && isNativeClearTileIndex(westCell->tile)
                && randomBinary() == 0) {
                setLevel(*westCell, westCell->level - 4);
                copyZone(west, currentPoint);
            }
        } else if (flags == 0xE0
            && slopeFlags(southWest) == 0xE0
            && slopeFlags(northEast) == 0xE0
            && isNativeClearTileIndex(current->tile)) {
            setLevel(*current, current->level + 4);
            copyZone(currentPoint, west);
        }
    }
}

void NativeMapGenerator::calculateNativeFlatCellLevels(MapGrid& map, NativeRandomizer& rng)
{
    std::ofstream flatTrace;
    if (const char* tracePath = std::getenv("RMG_NATIVE_FLAT_TRACE_PATH");
        tracePath != nullptr && tracePath[0] != '\0') {
        const std::filesystem::path path{tracePath};
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        flatTrace.open(path, std::ios::binary);
        flatTrace << "index,x,y,tile_before,level_before,slope_before,eligible,flags,random,piece,placed,"
                     "initial_tile,initial_level,initial_slope\n";
    }
    std::ofstream flatSlotTrace;
    if (const char* tracePath = std::getenv("RMG_NATIVE_FLAT_SLOT_TRACE_PATH");
        tracePath != nullptr && tracePath[0] != '\0') {
        const std::filesystem::path path{tracePath};
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        flatSlotTrace.open(path, std::ios::binary);
        flatSlotTrace << "sequence,piece,slot,xOffset,yOffset,slotPresent,slotHeight,"
                         "targetX,targetY,beforeTile,beforeHeight,beforeLevel,"
                         "afterTile,afterHeight,afterLevel,zone\n";
    }

    struct WorkspaceCell {
        Point cell;
        int level = kNativeBaseMapLevel;
        int tile = kNativeTileClear;
        int height = 0;
        int slopeIndex = 0;
        std::size_t overrideIndex = std::numeric_limits<std::size_t>::max();
    };

    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, report_.isoTileOverrides);
    std::unordered_map<std::uint64_t, std::size_t> overrideByIso;
    overrideByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t i = 0; i < report_.isoTileOverrides.size(); ++i) {
        overrideByIso[nativeIsoKey(report_.isoTileOverrides[i].iso)] = i;
    }
    std::unordered_map<std::uint64_t, WorkspaceCell> cells;
    cells.reserve(iteratorCells.size() * 2);
    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        WorkspaceCell cell;
        cell.cell = iteratorCell.cell;
        cell.level = iteratorCell.level;
        cell.tile = iteratorCell.isoTileTypeIndex;
        cell.height = iteratorCell.height;
        cell.slopeIndex = iteratorCell.slopeIndex;
        if (const auto overrideIt = overrideByIso.find(nativeIsoKey(iteratorCell.cell));
            overrideIt != overrideByIso.end()) {
            cell.overrideIndex = overrideIt->second;
        }
        cells[nativeIsoKey(cell.cell)] = cell;
    }

    constexpr std::array<std::pair<Point, int>, 8> kSlopeNeighbors{{
        {{0, -1}, 0x80}, {{1, -1}, 0x01}, {{1, 0}, 0x02}, {{1, 1}, 0x04},
        {{0, 1}, 0x08}, {{-1, 1}, 0x10}, {{-1, 0}, 0x20}, {{-1, -1}, 0x40},
    }};
    auto cellAt = [&](Point cell) -> WorkspaceCell* {
        const auto it = cells.find(nativeIsoKey(cell));
        return it == cells.end() ? nullptr : &it->second;
    };
    auto slopeFlags = [&](Point cell) {
        const WorkspaceCell* current = cellAt(cell);
        if (current == nullptr) {
            return 0;
        }
        int flags = 0;
        for (const auto& [offset, bit] : kSlopeNeighbors) {
            const WorkspaceCell* neighbor = cellAt({cell.x + offset.x, cell.y + offset.y});
            if (neighbor != nullptr
                && neighbor->level == current->level + 4
                && !isNativeFlatSlopeBlockingTile(
                    nativeTheaterCatalog_,
                    neighbor->tile,
                    neighbor->height)) {
                flags |= bit;
            }
        }
        return flags;
    };
    auto zoneFor = [&](Point cell) {
        const auto it = nativeMapgenZoneIds_.find(nativeIsoKey(cell));
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };
    auto selectPiece = [&](Point cell, int flags, int randomValue) {
        if ((flags & 0xA0) == 0xA0) {
            if ((slopeFlags({cell.x, cell.y + 1}) & 0x20) != 0
                || (slopeFlags({cell.x + 1, cell.y}) & 0x80) != 0) {
                return 34;
            }
            return randomValue % 3 + 9;
        }
        if ((flags & 0x82) == 0x82) {
            return 39;
        }
        if ((flags & 0x0A) == 0x0A) {
            return 33;
        }
        if ((flags & 0x28) == 0x28) {
            return 40;
        }
        if ((flags & 0x02) != 0) {
            const int southFlags = slopeFlags({cell.x, cell.y + 1});
            const int southEastFlags = slopeFlags({cell.x + 1, cell.y + 1});
            if ((flags & 0x04) != 0 && (flags & 0x18) == 0 && (southFlags & 0x0A) != 0x0A) {
                return randomValue % 3 + 35;
            }
            return (flags & 0x0C) != 0 || (southEastFlags & 0x02) != 0 || (randomValue & 1) == 0
                ? 38
                : 1;
        }
        if ((flags & 0x20) != 0) {
            const int southFlags = slopeFlags({cell.x, cell.y + 1});
            return (flags & 0x10) == 0 || (flags & 0x0C) != 0 || (southFlags & 0x28) == 0x28
                ? 18
                : randomValue % 3 + 15;
        }
        if ((flags & 0x08) != 0) {
            const int eastFlags = slopeFlags({cell.x + 1, cell.y});
            const int southEastFlags = slopeFlags({cell.x + 1, cell.y + 1});
            if ((flags & 0x04) == 0 || (flags & 0x03) != 0 || (eastFlags & 0x0A) == 0x0A) {
                return (flags & 0x06) != 0 || (southEastFlags & 0x08) != 0 || (randomValue & 1) == 0
                    ? 26
                    : 21;
            }
            return randomValue % 3 + 23;
        }
        if ((flags & 0x80) != 0) {
            const int eastFlags = slopeFlags({cell.x + 1, cell.y});
            return (flags & 0x01) == 0 || (flags & 0x06) != 0 || (eastFlags & 0x82) == 0x82
                ? 8
                : randomValue % 3 + 5;
        }
        if ((flags & 0x01) != 0) {
            return 2;
        }
        if ((flags & 0x04) != 0) {
            return (randomValue & 1) + 29;
        }
        if ((flags & 0x10) != 0) {
            return 22;
        }
        if ((flags & 0x40) != 0) {
            return randomValue % 3 + 12;
        }
        return 0;
    };
    auto placePiece = [&](Point anchor, int pieceNumber, int sequence) {
        const int piece = pieceNumber - 1;
        if (piece < 0 || piece >= kCliffPieceCount) {
            return false;
        }
        WorkspaceCell* anchorCell = cellAt(anchor);
        if (anchorCell == nullptr) {
            return false;
        }
        const int baseLevel = anchorCell->level;
        const NativeIsoTileTemplate* nativeTemplate = nativeTheaterCatalog_ == nullptr
            ? nullptr
            : nativeTheaterCatalog_->tileType(kNativeTileCliffBase + piece);
        const int width = nativeTemplate != nullptr && nativeTemplate->width > 0
            ? static_cast<int>(nativeTemplate->width)
            : cliffPieceWidth(piece);
        const int height = nativeTemplate != nullptr && nativeTemplate->height > 0
            ? static_cast<int>(nativeTemplate->height)
            : cliffPieceHeight(piece);
        const auto slotAt = [&](int x, int y) -> const NativeIsoTileSlot* {
            if (nativeTemplate == nullptr
                || x < 0
                || y < 0
                || x >= static_cast<int>(nativeTemplate->width)
                || y >= static_cast<int>(nativeTemplate->height)) {
                return nullptr;
            }
            const std::size_t index = static_cast<std::size_t>(y * nativeTemplate->width + x);
            return index < nativeTemplate->slots.size() ? &nativeTemplate->slots[index] : nullptr;
        };
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const NativeIsoTileSlot* nativeSlot = slotAt(x, y);
                const bool slotPresent = nativeSlot != nullptr
                    ? nativeSlot->present
                    : isValidCliffPieceSlot(piece, x, y);
                const int slotHeight = nativeSlot != nullptr && nativeSlot->present
                    ? static_cast<int>(nativeSlot->heightOffset)
                    : (slotPresent ? cliffPieceCellHeight(piece, cliffPieceSubTile(piece, x, y))
                               : -1);
                const Point targetPoint{anchor.x + x, anchor.y + y};
                WorkspaceCell* target = cellAt(targetPoint);
                const int beforeTile = target != nullptr ? target->tile : -1;
                const int beforeHeight = target != nullptr ? target->height : -1;
                const int beforeLevel = target != nullptr ? target->level : -1;
                int afterTile = beforeTile;
                int afterHeight = beforeHeight;
                int afterLevel = beforeLevel;
                if (target == nullptr) {
                    if (flatSlotTrace) {
                        flatSlotTrace << sequence << ',' << pieceNumber << ','
                            << (y * width + x) << ',' << x << ',' << y << ','
                            << static_cast<int>(slotPresent) << ',' << slotHeight << ','
                            << targetPoint.x << ',' << targetPoint.y << ','
                            << beforeTile << ',' << beforeHeight << ',' << beforeLevel << ','
                            << afterTile << ',' << afterHeight << ',' << afterLevel << ','
                            << nativeMapgenDefaultZoneId_ << '\n';
                    }
                    continue;
                }
                nativeIsoTileTemplateFootprintKeys_.insert(nativeIsoKey(target->cell));
                if (!slotPresent) {
                    if (flatSlotTrace) {
                        flatSlotTrace << sequence << ',' << pieceNumber << ','
                            << (y * width + x) << ',' << x << ',' << y << ','
                            << 0 << ',' << slotHeight << ','
                            << targetPoint.x << ',' << targetPoint.y << ','
                            << beforeTile << ',' << beforeHeight << ',' << beforeLevel << ','
                            << beforeTile << ',' << beforeHeight << ',' << beforeLevel << ','
                            << zoneFor(targetPoint) << '\n';
                    }
                    continue;
                }
                // Native PlaceCliffTile handles each template slot independently:
                // occupied slots stay unchanged while blank slots are still placed.
                if (!isNativeClearTileIndex(target->tile)) {
                    if (flatSlotTrace) {
                        flatSlotTrace << sequence << ',' << pieceNumber << ','
                            << (y * width + x) << ',' << x << ',' << y << ','
                            << 1 << ',' << slotHeight << ','
                            << targetPoint.x << ',' << targetPoint.y << ','
                            << beforeTile << ',' << beforeHeight << ',' << beforeLevel << ','
                            << beforeTile << ',' << beforeHeight << ',' << beforeLevel << ','
                            << zoneFor(target->cell) << '\n';
                    }
                    continue;
                }
                const int subTile = y * width + x;
                const int level = nativeSlot != nullptr
                    ? baseLevel + static_cast<int>(nativeSlot->heightOffset)
                    : baseLevel + cliffPieceCellHeight(piece, subTile);
                target->tile = kNativeTileCliffBase + piece;
                target->height = subTile;
                target->level = level;
                target->slopeIndex = 0;
                nativeMapgenZoneIds_[nativeIsoKey(target->cell)] = -1;
                if (target->overrideIndex != std::numeric_limits<std::size_t>::max()) {
                    IsoTileOverride& override = report_.isoTileOverrides[target->overrideIndex];
                    override.terrain = Terrain::Cliff;
                    override.variant = piece;
                    override.subTile = subTile;
                    override.level = level;
                    override.slopeIndex = 0;
                    override.landType = nativeLandTypeForTile(Terrain::Cliff, piece, subTile);
                    override.nativeTileTypeIndex = kNativeTileCliffBase + piece;
                }
                const Point source = nativeCellToSourcePoint(map, target->cell);
                if (map.inBounds(source) && sourcePointToNativeCell(map, source) == target->cell) {
                    Cell& sourceCell = map.at(source);
                    sourceCell.terrain = Terrain::Cliff;
                    sourceCell.height = subTile;
                    sourceCell.level = level;
                    sourceCell.slope = encodeCliffPiece({piece, subTile});
                    sourceCell.landType = nativeLandTypeForTile(Terrain::Cliff, piece, subTile);
                }
                afterTile = target->tile;
                afterHeight = target->height;
                afterLevel = target->level;
                if (flatSlotTrace) {
                    flatSlotTrace << sequence << ',' << pieceNumber << ','
                        << (y * width + x) << ',' << x << ',' << y << ','
                        << 1 << ',' << slotHeight << ','
                        << targetPoint.x << ',' << targetPoint.y << ','
                        << beforeTile << ',' << beforeHeight << ',' << beforeLevel << ','
                        << afterTile << ',' << afterHeight << ',' << afterLevel << ','
                        << zoneFor(target->cell) << '\n';
                }
            }
        }
        return true;
    };

    int traceIndex = 0;
    int flatSequence = 0;
    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        WorkspaceCell* current = cellAt(iteratorCell.cell);
        const bool eligible = current != nullptr
            && current->slopeIndex == 0
            && isNativeClearTileIndex(current->tile);
        if (!eligible) {
            if (flatTrace) {
                flatTrace << traceIndex << ','
                    << iteratorCell.cell.x << ',' << iteratorCell.cell.y << ','
                    << (current != nullptr ? current->tile : -1) << ','
                    << (current != nullptr ? current->level : -1) << ','
                    << (current != nullptr ? current->slopeIndex : -1) << ','
                    << 0 << ",-1,-1,0,0,"
                    << iteratorCell.isoTileTypeIndex << ','
                    << iteratorCell.level << ','
                    << iteratorCell.slopeIndex << '\n';
            }
            ++traceIndex;
            continue;
        }
        const int tileBefore = current->tile;
        const int levelBefore = current->level;
        const int flags = slopeFlags(iteratorCell.cell);
        const int randomValue = rng.randomScaledInclusive(0, 5);
        const int piece = selectPiece(iteratorCell.cell, flags, randomValue);
        const int sequence = ++flatSequence;
        bool placed = false;
        if (piece > 0) {
            placed = placePiece(iteratorCell.cell, piece, sequence);
        }
        if (flatTrace) {
            flatTrace << traceIndex << ','
                << iteratorCell.cell.x << ',' << iteratorCell.cell.y << ','
                << tileBefore << ',' << levelBefore << ',' << current->slopeIndex << ','
                << 1 << ',' << flags << ','
                << randomValue << ',' << piece << ',' << static_cast<int>(placed) << ','
                << iteratorCell.isoTileTypeIndex << ','
                << iteratorCell.level << ','
                << iteratorCell.slopeIndex << '\n';
        }
        ++traceIndex;
    }
}

void NativeMapGenerator::randomizeNativeCliffTileVariants(MapGrid& map, NativeRandomizer& rng)
{
    if (nativeTheaterCatalog_ == nullptr || nativeTheaterCatalog_->cliffSetBase < 0) {
        return;
    }

    struct WorkspaceCell {
        Point cell;
        int tile = -1;
        int height = 0;
    };

    const int cliffBase = nativeTheaterCatalog_->cliffSetBase;
    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(
        map,
        report_.isoTileOverrides);
    std::unordered_map<std::uint64_t, WorkspaceCell> cells;
    cells.reserve(iteratorCells.size() * 2);
    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        cells[nativeIsoKey(iteratorCell.cell)] = {
            iteratorCell.cell,
            iteratorCell.isoTileTypeIndex,
            iteratorCell.height,
        };
    }

    const auto cellAt = [&](Point cell) -> WorkspaceCell* {
        const auto it = cells.find(nativeIsoKey(cell));
        return it == cells.end() ? nullptr : &it->second;
    };
    const auto randomVariant = [&](int tile) {
        const int variant = tile - cliffBase;
        switch (variant) {
        case 4:
            return cliffBase + rng.randomScaledInclusive(1, 2) + 4;
        case 5:
            return cliffBase + 2 * rng.randomScaledInclusive(0, 1) + 4;
        case 6:
            return cliffBase + rng.randomScaledInclusive(0, 1) + 4;
        case 8:
            return cliffBase + rng.randomScaledInclusive(1, 2) + 8;
        case 9:
            return cliffBase + 2 * rng.randomScaledInclusive(0, 1) + 8;
        case 10:
            return cliffBase + rng.randomScaledInclusive(0, 1) + 8;
        case 11:
            return cliffBase + rng.randomScaledInclusive(1, 2) + 11;
        case 12:
            return cliffBase + 2 * rng.randomScaledInclusive(0, 1) + 11;
        case 13:
            return cliffBase + rng.randomScaledInclusive(0, 1) + 11;
        case 14:
            return cliffBase + rng.randomScaledInclusive(1, 2) + 14;
        case 15:
            return cliffBase + 2 * rng.randomScaledInclusive(0, 1) + 14;
        case 16:
            return cliffBase + rng.randomScaledInclusive(0, 1) + 14;
        case 22:
            return cliffBase + rng.randomScaledInclusive(1, 2) + 22;
        case 23:
            return cliffBase + 2 * rng.randomScaledInclusive(0, 1) + 22;
        case 24:
            return cliffBase + rng.randomScaledInclusive(0, 1) + 22;
        case 28:
            return cliffBase + 29;
        case 29:
            return cliffBase + 28;
        case 34:
            return cliffBase + rng.randomScaledInclusive(1, 2) + 34;
        case 35:
            return cliffBase + 2 * rng.randomScaledInclusive(0, 1) + 34;
        case 36:
            return cliffBase + rng.randomScaledInclusive(0, 1) + 34;
        default:
            return tile;
        }
    };
    const auto placeVariant = [&](int tile, Point anchor, int zoneId) {
        if (!placeNativeIsoTile(map, tile, anchor, zoneId, -1)) {
            return;
        }
        const NativeIsoTileTemplate* tileTemplate = nativeTheaterCatalog_->tileType(tile);
        if (tileTemplate == nullptr || tileTemplate->width == 0) {
            return;
        }
        for (std::size_t subTile = 0; subTile < tileTemplate->slots.size(); ++subTile) {
            if (!tileTemplate->slots[subTile].present) {
                continue;
            }
            WorkspaceCell* target = cellAt({
                anchor.x + static_cast<int>(subTile % tileTemplate->width),
                anchor.y + static_cast<int>(subTile / tileTemplate->width),
            });
            if (target != nullptr) {
                target->tile = tile;
                target->height = static_cast<int>(subTile);
            }
        }
    };
    const auto tryRandomizeEdge = [&](WorkspaceCell& current, Point neighborPoint) {
        WorkspaceCell* neighbor = cellAt(neighborPoint);
        if (neighbor == nullptr
            || current.tile != neighbor->tile
            || current.height <= neighbor->height) {
            return;
        }

        const int replacement = randomVariant(current.tile);
        if (replacement == current.tile) {
            return;
        }
        const NativeIsoTileTemplate* currentTemplate = nativeTheaterCatalog_->tileType(current.tile);
        if (currentTemplate == nullptr || currentTemplate->width == 0) {
            return;
        }
        const Point anchor{
            neighbor->cell.x - neighbor->height % static_cast<int>(currentTemplate->width),
            neighbor->cell.y - neighbor->height / static_cast<int>(currentTemplate->width),
        };
        const auto zoneIt = nativeMapgenZoneIds_.find(nativeIsoKey(anchor));
        placeVariant(
            replacement,
            anchor,
            zoneIt == nativeMapgenZoneIds_.end() ? -1 : zoneIt->second);
    };

    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        WorkspaceCell* current = cellAt(iteratorCell.cell);
        if (current == nullptr
            || current->tile < cliffBase
            || current->tile >= cliffBase + kCliffPieceCount) {
            continue;
        }
        tryRandomizeEdge(*current, {iteratorCell.cell.x, iteratorCell.cell.y + 1});
        tryRandomizeEdge(*current, {iteratorCell.cell.x + 1, iteratorCell.cell.y});
    }
}

void NativeMapGenerator::processNativeRegionWorkspace(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, std::vector<NativeRegion>& regions)
{
    ++report_.regionProcessCalls;
    const int minimumRegionSize = nativeRegionReprocessMinimumSize(map, options);
    report_.regionProcessMinimumSize = minimumRegionSize;
    const Rect mapRect = nativeSavedMapRect(map);
    constexpr std::array<Point, 8> kNeighbors{{
        {0, -1}, {1, -1}, {1, 0}, {1, 1},
        {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
    }};
    constexpr std::array<Point, 4> kCardinalNeighbors{{
        {0, -1}, {1, 0}, {0, 1}, {-1, 0},
    }};

    const char* regionRngTracePath = std::getenv("RMG_NATIVE_REGION_RNG_TRACE_PATH");
    std::ofstream regionRngTrace;
    if (regionRngTracePath != nullptr && regionRngTracePath[0] != '\0') {
        regionRngTrace.open(regionRngTracePath, std::ios::out | std::ios::trunc);
        if (regionRngTrace.is_open()) {
            regionRngTrace << "event,process,parentId,parentCount,drawCount,raw,value,x,y,aux0,aux1\n";
        }
    }
    const std::uint64_t traceStartDrawCount = rng.drawCount();
    int traceProcess = -1;
    int traceParentId = -1;
    int traceParentCount = 0;
    auto traceEvent = [&](const char* event, std::uint32_t raw = 0, double value = 0.0, Point point = {-1, -1}, int aux0 = 0, int aux1 = 0) {
        if (!regionRngTrace.is_open()) {
            return;
        }
        regionRngTrace << (event != nullptr ? event : "") << ','
            << traceProcess << ','
            << traceParentId << ','
            << traceParentCount << ','
            << (rng.drawCount() - traceStartDrawCount) << ','
            << raw << ','
            << std::setprecision(17) << value << ','
            << point.x << ','
            << point.y << ','
            << aux0 << ','
            << aux1 << '\n';
    };

    std::unordered_map<std::uint64_t, std::size_t> overrideIndex;
    overrideIndex.reserve(report_.isoTileOverrides.size());
    for (std::size_t i = 0; i < report_.isoTileOverrides.size(); ++i) {
        overrideIndex[nativeIsoKey(report_.isoTileOverrides[i].iso)] = i;
    }
    auto isBlank = [&](Point cell) {
        const auto it = overrideIndex.find(nativeIsoKey(cell));
        return it != overrideIndex.end()
            && nativeTileTypeIndexForOverride(report_.isoTileOverrides[it->second]) == kNativeTileClear;
    };
    auto isWetTile = [&](Point cell) {
        const auto it = overrideIndex.find(nativeIsoKey(cell));
        if (it == overrideIndex.end()) {
            return false;
        }
        const int tile = nativeTileTypeIndexForOverride(report_.isoTileOverrides[it->second]);
        return isNativeShoreTileIndex(tile)
            || (tile >= kNativeTileWaterBase && tile < kNativeTileDirtBase);
    };
    // GenerateRandomMap uses the wrapper's 1 / UINT32_MAX scale and keeps the
    // x87 operation order: raw * span, then * unit, then + base.
    auto scaledInclusive = [&](int base, int span, const char* event) {
        constexpr double kNativeRandomUnit = 1.0 / 4294967295.0;
        const int maximum = base + span - 1;
        while (true) {
            const std::uint32_t raw = rng.random();
            traceEvent(event, raw, static_cast<double>(raw));
            const double rawTimesSpan = nativeX87Chop53Multiply(
                static_cast<double>(raw), static_cast<double>(span));
            const double scaled = nativeX87Chop53Multiply(rawTimesSpan, kNativeRandomUnit);
            const int candidate = nativeDoubleToInt(
                nativeX87Chop53Add(scaled, static_cast<double>(base)));
            if (candidate <= maximum) {
                return candidate;
            }
        }
    };
    auto priorityFor = [&](Point anchor, Point cell, double baseAngle) {
        constexpr double kNativeRandomUnit = 1.0 / 4294967295.0;
        const int dx = cell.x - anchor.x;
        const int dy = cell.y - anchor.y;
        const double distance = nativePatchedSqrt(static_cast<double>(dx * dx + dy * dy));
        native_trace::EventData compareAngleTraceData;
        if (native_trace::active()) {
            const std::uint64_t baseAngleBits = std::bit_cast<std::uint64_t>(baseAngle);
            compareAngleTraceData.args[0] = packNativeCell(anchor);
            compareAngleTraceData.args[1] = packNativeCell(cell);
            compareAngleTraceData.args[2] = static_cast<std::uint32_t>(baseAngleBits);
            compareAngleTraceData.args[3] = static_cast<std::uint32_t>(baseAngleBits >> 32);
            compareAngleTraceData.cellX = static_cast<std::int16_t>(anchor.x);
            compareAngleTraceData.cellY = static_cast<std::int16_t>(anchor.y);
            native_trace::enterDetailed(
                0x58C6F0,
                native_trace::CompareAngle,
                compareAngleTraceData);
        }
        double angle = 1.57079632679489661923;
        if (dx != 0) {
            const double ratio = nativeX87Chop53Divide(
                -static_cast<double>(dy), static_cast<double>(dx));
            angle = static_cast<double>(nativeApproximateAtan(ratio));
        }
        if (dx < 0) {
            angle = nativeX87Chop53Add(angle, 3.14159265358979323846);
        }
        double difference = std::fabs(nativeX87Chop53Add(angle, -baseAngle));
        while (difference >= 6.28318530717958647692) {
            difference = nativeX87Chop53Add(difference, -6.28318530717958647692);
        }
        if (difference > 3.14159265358979323846) {
            difference = nativeX87Chop53Add(6.28318530717958647692, -difference);
        }
        if (native_trace::active()) {
            native_trace::call(0x58C7B5, native_trace::Randomizer, cell.x, cell.y);
        }
        const std::uint32_t raw = rng.random();
        traceEvent("priority_raw", raw, static_cast<double>(raw), cell);
        const double randomTerm = nativeX87Chop53Multiply(
            nativeX87Chop53Multiply(static_cast<double>(raw), kNativeRandomUnit), 2.5);
        const double angleTerm = nativeX87Chop53Multiply(difference, 1.5);
        const double distanceTerm = nativeX87Chop53Multiply(distance, 0.15);
        const double randomAndAngle = nativeX87Chop53Add(randomTerm, angleTerm);
        const double priorityResult = nativeX87Chop53Add(randomAndAngle, distanceTerm);
        const float priority = nativeDoubleToFloatChop(priorityResult);
        if (native_trace::active()) {
            const std::uint64_t priorityBits = std::bit_cast<std::uint64_t>(priorityResult);
            compareAngleTraceData.result[0] = static_cast<std::uint32_t>(priorityBits);
            compareAngleTraceData.result[1] = static_cast<std::uint32_t>(priorityBits >> 32);
            compareAngleTraceData.resultKind = native_trace::ResultBinary64;
            native_trace::exitDetailed(
                0x58C7EE,
                native_trace::CompareAngle,
                compareAngleTraceData);
        }
        if (native_trace::active()) {
            native_trace::call(
                0x58DA41,
                native_trace::GenerateRandomMap,
                cell.x,
                cell.y,
                0,
                0,
                0,
                std::bit_cast<std::uint32_t>(priority));
        }
        traceEvent("priority_value", 0, static_cast<double>(priority), cell);
        return priority;
    };

    struct RegionPart {
        int id = 0;
        std::vector<Point> cells;
        int level = kNativeBaseMapLevel;
        bool wet = false;
        bool stopProcessing = false;
        bool merged = false;
    };
    struct QueueNode {
        Point cell;
        float priority = 0.0f;
    };
    // The game uses a 1-based binary min-heap.  It swaps only when the parent
    // is strictly greater, and chooses the left child on equal priorities.
    struct NativeMinHeap {
        std::vector<QueueNode> nodes{QueueNode{}};

        bool empty() const noexcept { return nodes.size() == 1; }

        void push(QueueNode value)
        {
            std::size_t index = nodes.size();
            nodes.push_back(value);
            while (index > 1) {
                const std::size_t parent = index >> 1;
                if (!(nodes[parent].priority > value.priority)) {
                    break;
                }
                nodes[index] = nodes[parent];
                index = parent;
            }
            nodes[index] = value;
        }

        QueueNode pop()
        {
            const QueueNode result = nodes[1];
            const QueueNode last = nodes.back();
            nodes.pop_back();
            if (nodes.size() == 1) {
                return result;
            }

            std::size_t index = 1;
            const std::size_t count = nodes.size() - 1;
            while (true) {
                const std::size_t left = index << 1;
                if (left > count) {
                    break;
                }
                std::size_t child = left;
                const std::size_t right = left + 1;
                if (right <= count && nodes[right].priority < nodes[left].priority) {
                    child = right;
                }
                if (!(last.priority > nodes[child].priority)) {
                    break;
                }
                nodes[index] = nodes[child];
                index = child;
            }
            nodes[index] = last;
            return result;
        }
    };

    auto levelFor = [&](Point cell) {
        const auto it = overrideIndex.find(nativeIsoKey(cell));
        if (it != overrideIndex.end()) {
            return report_.isoTileOverrides[it->second].level;
        }
        const Point source = nativeCellToSourcePoint(map, cell);
        return map.inBounds(source) ? map.at(source).level : kNativeBaseMapLevel;
    };
    std::vector<RegionPart> parts;
    std::unordered_map<std::uint64_t, int> zone;
    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, report_.isoTileOverrides);
    std::size_t inputCellCapacity = iteratorCells.size();
    int nextZoneId = 0;
    for (const NativeRegion& inputRegion : regions) {
        nextZoneId = std::max(nextZoneId, inputRegion.id + 1);
    }
    parts.reserve(regions.size() * 2 + 8);
    zone.reserve(inputCellCapacity * 2);
    for (const NativeRegion& inputRegion : regions) {
        RegionPart part;
        part.id = inputRegion.id;
        part.level = inputRegion.level;
        part.wet = inputRegion.wet;
        parts.push_back(std::move(part));
    }
    if (parts.empty()) {
        return;
    }

    // ProcessAllRegions first clears every region vector, then repopulates it
    // by scanning the complete mapgen workspace backwards. The pre-process
    // region objects contain only committed flood cells, so using those
    // vectors directly omits border-expansion cells (notably from zone 0).
    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        const std::uint64_t key = nativeIsoKey(iteratorCell.cell);
        const auto zoneIt = nativeMapgenZoneIds_.find(key);
        const int zoneId = zoneIt == nativeMapgenZoneIds_.end()
            ? nativeMapgenDefaultZoneId_
            : zoneIt->second;
        zone[key] = zoneId;
        const auto partIt = std::find_if(parts.begin(), parts.end(), [zoneId](const RegionPart& part) {
            return part.id == zoneId;
        });
        if (partIt != parts.end()) {
            partIt->cells.push_back(iteratorCell.cell);
        }
    }
    for (RegionPart& part : parts) {
        if (part.cells.empty()) {
            const auto inputIt = std::find_if(regions.begin(), regions.end(), [&part](const NativeRegion& input) {
                return input.id == part.id;
            });
            if (inputIt != regions.end()) {
                part.cells = inputIt->nativeCells;
                for (Point cell : part.cells) {
                    zone[nativeIsoKey(cell)] = part.id;
                }
            }
        }
        std::sort(part.cells.begin(), part.cells.end(), [](Point lhs, Point rhs) {
            return lhs.y != rhs.y ? lhs.y > rhs.y : lhs.x > rhs.x;
        });
    }

    auto adjustZoneLevel = [&](int zoneId, int delta) {
        if (delta == 0) {
            return;
        }
        std::unordered_set<std::uint64_t> sourceSeen;
        sourceSeen.reserve(iteratorCells.size());
        for (const NativeIteratorCell& iteratorCell : iteratorCells) {
            const Point nativeCell = iteratorCell.cell;
            const auto zoneIt = zone.find(nativeIsoKey(nativeCell));
            if (zoneIt == zone.end() || zoneIt->second != zoneId) {
                continue;
            }
            const auto overrideIt = overrideIndex.find(nativeIsoKey(nativeCell));
            if (overrideIt != overrideIndex.end()) {
                report_.isoTileOverrides[overrideIt->second].level += delta;
                ++report_.regionProcessLevelAdjustments;
            }
            const Point source = nativeCellToSourcePoint(map, nativeCell);
            if (!map.inBounds(source)
                || sourcePointToNativeCell(map, source) != nativeCell
                || !sourceSeen.insert(nativeIsoKey(source)).second) {
                continue;
            }
            map.at(source).level += delta;
        }
    };

    std::size_t partIndex = 0;
    while (partIndex < parts.size()) {
        RegionPart& inputRegion = parts[partIndex];
        const int inputCount = static_cast<int>(inputRegion.cells.size());
        if (inputRegion.merged || inputRegion.wet || inputRegion.stopProcessing) {
            ++partIndex;
            continue;
        }
        if (inputCount <= minimumRegionSize) {
            inputRegion.stopProcessing = true;
            ++report_.regionProcessSkippedSmall;
            ++partIndex;
            continue;
        }

        report_.regionProcessInputCells += inputCount;
        const RegionPart parent = inputRegion;
        ++traceProcess;
        traceParentId = parent.id;
        traceParentCount = inputCount;
        if (native_trace::active()) {
            native_trace::call(0x58EDE2, native_trace::GenerateRandomMap);
            native_trace::EventData traceData;
            traceData.regionIndex = static_cast<int>(partIndex);
            traceData.regionId = parent.id;
            traceData.regionCount = static_cast<int>(parts.size());
            traceData.regionCellCount = static_cast<int>(parent.cells.size());
            if (!parent.cells.empty()) {
                traceData.cellX = static_cast<std::int16_t>(parent.cells.front().x);
                traceData.cellY = static_cast<std::int16_t>(parent.cells.front().y);
            }
            traceData.args[1] = static_cast<std::uint32_t>(parent.level);
            traceData.args[2] = parent.wet ? 1u : 0u;
            native_trace::enterDetailed(
                0x58D620,
                native_trace::GenerateRandomMap,
                traceData);
        }
        traceEvent("parent_begin", 0, 0.0, parent.cells.empty() ? Point{-1, -1} : parent.cells.front(), static_cast<int>(parent.cells.size()), parent.level);
        for (Point cell : parent.cells) {
            zone[nativeIsoKey(cell)] = -2;
        }

        const int lowTarget = inputCount / 8;
        const int highTarget = inputCount / 3;
        if (native_trace::active()) {
            native_trace::call(0x58D787, native_trace::Randomizer);
        }
        const int target = scaledInclusive(lowTarget, highTarget - lowTarget + 1, "target_raw");
        traceEvent("target_value", 0, static_cast<double>(target), {-1, -1}, lowTarget, highTarget);
        if (native_trace::active()) {
            native_trace::call(0x58D7D2, native_trace::Randomizer);
        }
        const Point seed = parent.cells[static_cast<std::size_t>(scaledInclusive(0, inputCount, "seed_index_raw"))];
        traceEvent("seed_value", 0, 0.0, seed);
        constexpr double kNativeRandomAngleScale = 1.4629180796077717e-9;
        if (native_trace::active()) {
            native_trace::call(0x58D8C6, native_trace::Randomizer);
        }
        const std::uint32_t baseAngleRaw = rng.random();
        traceEvent("base_angle_raw", baseAngleRaw, static_cast<double>(baseAngleRaw));
        double baseAngle = nativeX87Chop53Multiply(
            static_cast<double>(baseAngleRaw), kNativeRandomAngleScale);
        traceEvent("base_angle", 0, baseAngle);
        NativeMinHeap heap;
        std::unordered_set<std::uint64_t> queued;
        queued.reserve(parent.cells.size());
        heap.push({seed, 0.0f});
        queued.insert(nativeIsoKey(seed));

        Point current = heap.pop().cell;
        int processed = 0;
        while (processed < target) {
            zone[nativeIsoKey(current)] = -3;
            for (Point offset : kCardinalNeighbors) {
                const Point next{current.x + offset.x, current.y + offset.y};
                const std::uint64_t key = nativeIsoKey(next);
                const auto zoneIt = zone.find(key);
                if (!isInsideNativeCellDiamond(next, mapRect)
                    || zoneIt == zone.end()
                    || zoneIt->second != -2
                    || queued.contains(key)
                    || !isBlank(next)) {
                    continue;
                }
                heap.push({next, priorityFor(seed, next, baseAngle)});
                queued.insert(key);
            }
            ++processed;
            std::vector<std::uint32_t> gaussianRaw;
            if (native_trace::active()) {
                native_trace::call(0x58DAF0, native_trace::GaussianRandom);
            }
            const double gaussian = rng.gaussianWithRawTrace(&gaussianRaw);
            for (const std::uint32_t raw : gaussianRaw) {
                traceEvent("gaussian_raw", raw, static_cast<double>(raw));
            }
            traceEvent("gaussian_value", 0, gaussian);
            baseAngle = nativeX87Chop53Add(
                baseAngle,
                nativeX87Chop53Multiply(gaussian, 0.39269908169872415481));
            if (native_trace::active()) {
                native_trace::call(
                    0x58DB07,
                    native_trace::GenerateRandomMap,
                    -1,
                    -1,
                    0,
                    0,
                    0,
                    std::bit_cast<std::uint64_t>(baseAngle));
            }
            traceEvent("base_angle_update", 0, baseAngle, current, processed + 1);
            if (heap.empty()) {
                break;
            }
            current = heap.pop().cell;
        }
        while (!heap.empty()) {
            zone[nativeIsoKey(heap.pop().cell)] = -3;
        }

        std::vector<RegionPart> children;
        for (auto it = parent.cells.rbegin(); it != parent.cells.rend(); ++it) {
            const std::uint64_t seedKey = nativeIsoKey(*it);
            const auto zoneIt = zone.find(seedKey);
            if (zoneIt == zone.end() || zoneIt->second >= -1) {
                continue;
            }

            const int componentMarker = zoneIt->second;
            RegionPart child;
            child.id = nextZoneId++;
            child.level = levelFor(*it);
            child.wet = isWetTile(*it);
            std::vector<Point> pending;
            child.cells.push_back(*it);
            pending.push_back(*it);
            zone[seedKey] = child.id;
            while (!pending.empty()) {
                const Point cell = pending.back();
                pending.pop_back();
                for (Point offset : kNeighbors) {
                    const Point next{cell.x + offset.x, cell.y + offset.y};
                    const std::uint64_t key = nativeIsoKey(next);
                    const auto nextZone = zone.find(key);
                    if (nextZone == zone.end() || nextZone->second != componentMarker) {
                        continue;
                    }
                    zone[key] = child.id;
                    child.cells.push_back(next);
                    pending.push_back(next);
                }
            }
            children.push_back(std::move(child));
            ++report_.regionProcessSubregions;
        }
        if (children.empty()) {
            if (native_trace::active()) {
                native_trace::exit(0x58D641, native_trace::GenerateRandomMap);
            }
            parts[partIndex].stopProcessing = true;
            ++partIndex;
            continue;
        }

        const std::size_t firstChild = parts.size();
        parts.insert(
            parts.end(),
            std::make_move_iterator(children.begin()),
            std::make_move_iterator(children.end()));

        auto findPartById = [&](int id) -> std::size_t {
            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (parts[i].id == id && !parts[i].merged) {
                    return i;
                }
            }
            return parts.size();
        };

        for (std::size_t childIndex = parts.size(); childIndex-- > firstChild;) {
            RegionPart& child = parts[childIndex];
            int minimumAdjacentLevel = -1;
            int maximumAdjacentLevel = -1;
            int largestAdjacentCount = -1;
            std::size_t largestAdjacentIndex = parts.size();
            std::unordered_set<int> adjacentIds;

            native_trace::EventData borderTraceData;
            const bool traceBorder = native_trace::coreFunctionEnabled(
                native_trace::CollectBorderCellsForRegion);
            if (traceBorder) {
                borderTraceData.args[1] = static_cast<std::uint32_t>(child.level);
                borderTraceData.args[2] = child.wet ? 1u : 0u;
                borderTraceData.args[3] = static_cast<std::uint32_t>(child.cells.size());
                borderTraceData.args[4] = static_cast<std::uint32_t>(child.cells.size());
                borderTraceData.args[5] = static_cast<std::uint32_t>(nextZoneId);
                borderTraceData.regionIndex = static_cast<int>(childIndex);
                borderTraceData.regionId = child.id;
                borderTraceData.regionCount = static_cast<int>(parts.size());
                borderTraceData.regionCellCount = static_cast<int>(child.cells.size());
                if (!child.cells.empty()) {
                    borderTraceData.cellX = static_cast<std::int16_t>(child.cells.front().x);
                    borderTraceData.cellY = static_cast<std::int16_t>(child.cells.front().y);
                }
                borderTraceData.resultKind = native_trace::ResultNone;
                native_trace::enterDetailed(
                    0x58E5D0,
                    native_trace::CollectBorderCellsForRegion,
                    borderTraceData);
            }

            std::vector<Point> borderCells;
            borderCells.reserve(child.cells.size());
            for (auto cellIt = child.cells.rbegin(); cellIt != child.cells.rend(); ++cellIt) {
                const Point cell = *cellIt;
                bool border = false;
                for (Point offset : kNeighbors) {
                    const Point neighbor{cell.x + offset.x, cell.y + offset.y};
                    if (!isInsideNativeCellDiamond(neighbor, mapRect)) {
                        continue;
                    }
                    const auto neighborIt = zone.find(nativeIsoKey(neighbor));
                    if (neighborIt != zone.end() && neighborIt->second != child.id) {
                        border = true;
                    }
                }
                if (border) {
                    borderCells.push_back(cell);
                }
            }

            if (traceBorder) {
                std::uint32_t orderedHash = 2166136261u;
                std::vector<std::uint32_t> detailValues;
                if (native_trace::detailRequested()) {
                    detailValues.reserve(borderCells.size());
                }
                for (Point cell : borderCells) {
                    hashNativeCell(orderedHash, cell);
                    if (native_trace::detailRequested()) {
                        detailValues.push_back(packNativeCell(cell));
                    }
                }
                borderTraceData.result[0] = static_cast<std::uint32_t>(borderCells.size());
                borderTraceData.result[1] = orderedHash;
                borderTraceData.result[2] = borderCells.empty()
                    ? 0xFFFFFFFFu
                    : packNativeCell(borderCells.front());
                borderTraceData.result[3] = borderCells.empty()
                    ? 0xFFFFFFFFu
                    : packNativeCell(borderCells.back());
                borderTraceData.resultKind = native_trace::ResultLegacy;
                native_trace::exitDetailed(
                    0x58E5D0,
                    native_trace::CollectBorderCellsForRegion,
                    borderTraceData,
                    native_trace::DetailOrderedPackedCells,
                    detailValues);
            }

            report_.regionProcessBorderCells += static_cast<int>(borderCells.size());
            for (Point cell : borderCells) {
                for (Point offset : kNeighbors) {
                    const auto neighborIt = zone.find(nativeIsoKey({cell.x + offset.x, cell.y + offset.y}));
                    if (neighborIt == zone.end() || neighborIt->second < 0 || neighborIt->second == child.id) {
                        continue;
                    }
                    adjacentIds.insert(neighborIt->second);
                }
            }
            for (int adjacentId : adjacentIds) {
                const std::size_t adjacentIndex = findPartById(adjacentId);
                if (adjacentIndex == parts.size()) {
                    continue;
                }
                const RegionPart& adjacent = parts[adjacentIndex];
                minimumAdjacentLevel = minimumAdjacentLevel < 0
                    ? adjacent.level
                    : std::min(minimumAdjacentLevel, adjacent.level);
                maximumAdjacentLevel = maximumAdjacentLevel < 0
                    ? adjacent.level
                    : std::max(maximumAdjacentLevel, adjacent.level);
                ++report_.regionProcessNeighborLinks;
                if (!adjacent.wet && static_cast<int>(adjacent.cells.size()) > largestAdjacentCount) {
                    largestAdjacentCount = static_cast<int>(adjacent.cells.size());
                    largestAdjacentIndex = adjacentIndex;
                }
            }

            std::vector<int> candidates;
            if (minimumAdjacentLevel >= 0) {
                if (minimumAdjacentLevel == maximumAdjacentLevel) {
                    if (minimumAdjacentLevel >= 4) {
                        candidates.push_back(minimumAdjacentLevel - 4);
                    }
                    if (maximumAdjacentLevel <= 7) {
                        candidates.push_back(maximumAdjacentLevel + 4);
                    }
                } else if (maximumAdjacentLevel - minimumAdjacentLevel == 4) {
                    candidates.push_back(maximumAdjacentLevel);
                    candidates.push_back(minimumAdjacentLevel);
                } else if (maximumAdjacentLevel - minimumAdjacentLevel == 8) {
                    candidates.push_back((minimumAdjacentLevel + maximumAdjacentLevel) / 2);
                }
            }
            if (nativeRegionLevelZeroCandidateRemoved(options)) {
                std::erase(candidates, 0);
            }
            report_.regionProcessCandidateLevels += static_cast<int>(candidates.size());

            if (child.cells.size() > 100) {
                if (!candidates.empty()) {
                    const int oldLevel = child.level;
                    const int selectedIndex = scaledInclusive(
                        0,
                        static_cast<int>(candidates.size()),
                        "level_index");
                    child.level = candidates[static_cast<std::size_t>(selectedIndex)];
                    if (native_trace::active()) {
                        native_trace::EventData traceData;
                        traceData.args[0] = static_cast<std::uint32_t>(oldLevel);
                        traceData.args[1] = static_cast<std::uint32_t>(child.level);
                        traceData.args[2] = static_cast<std::uint32_t>(candidates.size());
                        traceData.args[3] = static_cast<std::uint32_t>(candidates[0]);
                        traceData.args[4] = candidates.size() > 1
                            ? static_cast<std::uint32_t>(candidates[1])
                            : 0u;
                        traceData.args[5] = 1u;
                        traceData.result[0] = static_cast<std::uint32_t>(child.level);
                        traceData.regionId = child.id;
                        traceData.regionCellCount = static_cast<int>(child.cells.size());
                        traceData.cellX = static_cast<std::int16_t>(child.cells.front().x);
                        traceData.cellY = static_cast<std::int16_t>(child.cells.front().y);
                        native_trace::callDetailed(
                            0x58E3B1,
                            native_trace::RegionLevelDecision,
                            traceData);
                    }
                    traceEvent("level_value", 0, static_cast<double>(child.level), child.cells.front(), static_cast<int>(candidates.size()));
                    adjustZoneLevel(child.id, child.level - oldLevel);
                }
                continue;
            }
            if (largestAdjacentIndex == parts.size()) {
                continue;
            }

            RegionPart& adjacent = parts[largestAdjacentIndex];
            const int oldLevel = child.level;
            child.level = adjacent.level;
            if (native_trace::active()) {
                native_trace::EventData traceData;
                traceData.args[0] = static_cast<std::uint32_t>(oldLevel);
                traceData.args[1] = static_cast<std::uint32_t>(child.level);
                traceData.args[5] = 0u;
                traceData.result[0] = static_cast<std::uint32_t>(child.level);
                traceData.regionId = child.id;
                traceData.regionCellCount = static_cast<int>(child.cells.size());
                traceData.cellX = static_cast<std::int16_t>(child.cells.front().x);
                traceData.cellY = static_cast<std::int16_t>(child.cells.front().y);
                native_trace::callDetailed(
                    0x58E422,
                    native_trace::RegionLevelDecision,
                    traceData);
            }
            adjustZoneLevel(child.id, child.level - oldLevel);
            for (auto cellIt = child.cells.rbegin(); cellIt != child.cells.rend(); ++cellIt) {
                adjacent.cells.push_back(*cellIt);
                zone[nativeIsoKey(*cellIt)] = adjacent.id;
            }
            child.merged = true;
        }

        std::vector<RegionPart> remaining;
        remaining.reserve(parts.size() - 1);
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i == partIndex || (i >= firstChild && parts[i].merged)) {
                continue;
            }
            remaining.push_back(std::move(parts[i]));
        }
        parts = std::move(remaining);
        if (native_trace::active()) {
            native_trace::exit(0x58E5C5, native_trace::GenerateRandomMap);
        }
        partIndex = 0;
    }

    std::vector<NativeRegion> output;
    output.reserve(parts.size());
    for (RegionPart& part : parts) {
        if (part.cells.empty()) {
            continue;
        }
        NativeRegion region;
        region.id = part.id;
        region.level = part.level;
        region.wet = part.wet;
        region.processed = part.stopProcessing;
        region.nativeCells = std::move(part.cells);
        region.nativeBounds = {region.nativeCells.front().x, region.nativeCells.front().y, 1, 1};
        std::unordered_set<std::uint64_t> sourceSeen;
        for (Point nativeCell : region.nativeCells) {
            const int minX = std::min(region.nativeBounds.x, nativeCell.x);
            const int minY = std::min(region.nativeBounds.y, nativeCell.y);
            const int maxX = std::max(region.nativeBounds.x + region.nativeBounds.w - 1, nativeCell.x);
            const int maxY = std::max(region.nativeBounds.y + region.nativeBounds.h - 1, nativeCell.y);
            region.nativeBounds = {minX, minY, maxX - minX + 1, maxY - minY + 1};

            const Point source = nativeCellToSourcePoint(map, nativeCell);
            // The rectangular MapGrid is only a projection of the full native
            // Iso workspace.  Several non-canonical Iso coordinates round to
            // the same source cell, so only write the reversible projection.
            const Point canonicalNativeCell = sourcePointToNativeCell(map, source);
            if (!map.inBounds(source)
                || canonicalNativeCell.x != nativeCell.x
                || canonicalNativeCell.y != nativeCell.y
                || !sourceSeen.insert(nativeIsoKey(source)).second) {
                continue;
            }
            region.cells.push_back(source);
        }
        if (!region.cells.empty()) {
            int minX = region.cells.front().x;
            int minY = region.cells.front().y;
            int maxX = minX;
            int maxY = minY;
            for (Point source : region.cells) {
                minX = std::min(minX, source.x);
                minY = std::min(minY, source.y);
                maxX = std::max(maxX, source.x);
                maxY = std::max(maxY, source.y);
            }
            region.bounds = {minX, minY, maxX - minX + 1, maxY - minY + 1};
        }
        output.push_back(std::move(region));
    }

    for (Cell& cell : map.cells()) {
        cell.regionTag = 0;
    }
    for (NativeRegion& region : output) {
        for (Point source : region.cells) {
            map.at(source).regionTag = region.id;
        }
    }
    if (!output.empty()) {
        auto selected = std::max_element(output.begin(), output.end(), [](const NativeRegion& lhs, const NativeRegion& rhs) {
            return lhs.nativeCells.size() < rhs.nativeCells.size();
        });
        selected->nativeWaypointQuota = options.numPlayers;
        selected->startQuota = options.numPlayers;
    }
    report_.regionWaypointQuotaTotal = options.numPlayers;
    report_.regionStartQuotaTotal = options.numPlayers;
    report_.regions = static_cast<int>(output.size());
    report_.regionProcessOutputRegions = static_cast<int>(output.size());
    report_.regionProcessOutputCells = static_cast<int>(std::accumulate(
        output.begin(), output.end(), std::size_t{0}, [](std::size_t count, const NativeRegion& region) {
            return count + region.nativeCells.size();
        }));
    nativeMapgenZoneIds_.clear();
    nativeMapgenZoneIds_.reserve(zone.size());
    for (const auto& [key, zoneId] : zone) {
        nativeMapgenZoneIds_[key] = zoneId;
    }
    regions = std::move(output);
    nativeRegionScratchRegions_ = regions;

}

void NativeMapGenerator::processNativeRegions(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, std::vector<NativeRegion>& regions)
{
    if (regions.empty()) {
        return;
    }
    if (std::any_of(regions.begin(), regions.end(), [](const NativeRegion& region) {
            return !region.nativeCells.empty();
        })) {
        processNativeRegionWorkspace(map, options, rng, regions);
        return;
    }

    ++report_.regionProcessCalls;
    std::vector<int> inSource(static_cast<std::size_t>(map.width() * map.height()), 0);
    std::vector<int> subregion(static_cast<std::size_t>(map.width() * map.height()), -1);
    std::vector<int> subregionSizes;
    auto index = [&map](Point p) {
        return static_cast<std::size_t>(p.y * map.width() + p.x);
    };

    int nextSubregionId = 0;
    const int reprocessMinimumSize = nativeRegionReprocessMinimumSize(map, options);
    report_.regionProcessMinimumSize = reprocessMinimumSize;
    for (const NativeRegion& region : regions) {
        if (region.cells.empty()) {
            continue;
        }

        const int regionCellCount = std::max(1, static_cast<int>(region.cells.size()));
        if (regionCellCount <= reprocessMinimumSize) {
            ++report_.regionProcessSkippedSmall;
            continue;
        }

        report_.regionProcessInputCells += regionCellCount;
        for (Point p : region.cells) {
            if (map.inBounds(p)) {
                inSource[index(p)] = 1;
            }
        }

        const int minimumTarget = std::max(1, regionCellCount / 8);
        const int maximumTarget = std::max(minimumTarget, regionCellCount / 3);
        Point seed{-1, -1};
        const int seedAttempts = std::max(8, static_cast<int>(region.cells.size()) * 2);
        for (int attempt = 0; attempt < seedAttempts; ++attempt) {
            const Point candidate = region.cells[static_cast<std::size_t>(rng.randomRanged(0, static_cast<int>(region.cells.size()) - 1))];
            if (map.inBounds(candidate) && inSource[index(candidate)] && isPassable(map.at(candidate))) {
                seed = candidate;
                break;
            }
        }
        if (seed.x < 0) {
            for (Point candidate : region.cells) {
                if (map.inBounds(candidate) && inSource[index(candidate)] && isPassable(map.at(candidate))) {
                    seed = candidate;
                    break;
                }
            }
        }

        if (seed.x >= 0) {
            const int targetCells = rng.randomRanged(minimumTarget, maximumTarget);
            const int tag = 700000 + nextSubregionId + 1;
            std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> heap;
            std::vector<Point> accepted;
            accepted.reserve(static_cast<std::size_t>(targetCells));

            map.at(seed).tempTag = tag;
            heap.push({seed, 0.0});
            while (!heap.empty() && static_cast<int>(accepted.size()) < targetCells) {
                const HeapNode node = heap.top();
                heap.pop();
                const Point p = node.point;
                if (!map.inBounds(p) || !inSource[index(p)] || subregion[index(p)] >= 0 || !isPassable(map.at(p))) {
                    continue;
                }
                accepted.push_back(p);
                subregion[index(p)] = nextSubregionId;
                for (Point next : map.neighbors8(p)) {
                    Cell& cell = map.at(next);
                    if (cell.tempTag == tag || !inSource[index(next)] || subregion[index(next)] >= 0 || !isPassable(cell)) {
                        continue;
                    }
                    cell.tempTag = tag;
                    heap.push({next, euclidean(next, seed) + rng.randomUnit() * 10.0 - static_cast<double>(accepted.size()) * 0.02});
                }
            }

            clearTags(map, tag);
            if (!accepted.empty()) {
                subregionSizes.push_back(static_cast<int>(accepted.size()));
                ++nextSubregionId;
                ++report_.regionProcessSubregions;
            }
        }

        for (Point componentSeed : region.cells) {
            if (!map.inBounds(componentSeed) || !inSource[index(componentSeed)] || subregion[index(componentSeed)] >= 0 || !isPassable(map.at(componentSeed))) {
                continue;
            }

            std::vector<Point> component;
            std::queue<Point> queue;
            subregion[index(componentSeed)] = nextSubregionId;
            queue.push(componentSeed);
            while (!queue.empty()) {
                const Point p = queue.front();
                queue.pop();
                component.push_back(p);
                for (Point next : map.neighbors8(p)) {
                    if (!inSource[index(next)] || subregion[index(next)] >= 0 || !isPassable(map.at(next))) {
                        continue;
                    }
                    subregion[index(next)] = nextSubregionId;
                    queue.push(next);
                }
            }

            subregionSizes.push_back(static_cast<int>(component.size()));
            ++nextSubregionId;
            ++report_.regionProcessSubregions;
        }

        for (Point p : region.cells) {
            if (map.inBounds(p)) {
                inSource[index(p)] = 0;
            }
        }
    }

    if (nextSubregionId <= 0) {
        return;
    }

    std::vector<int> subregionLevels(static_cast<std::size_t>(nextSubregionId), kNativeBaseMapLevel);
    for (int id = 0; id < nextSubregionId; ++id) {
        subregionLevels[static_cast<std::size_t>(id)] = std::clamp(
            kNativeBaseMapLevel + rng.randomRanged(-1, 1) * 4,
            0,
            kNativeBaseMapLevel + 4);
    }

    std::vector<int> neighborLevelMin(static_cast<std::size_t>(nextSubregionId), std::numeric_limits<int>::max());
    std::vector<int> neighborLevelMax(static_cast<std::size_t>(nextSubregionId), std::numeric_limits<int>::min());
    for (const auto& cell : map.cells()) {
        const Point p{cell.x, cell.y};
        const int id = subregion[index(p)];
        if (id < 0) {
            continue;
        }

        bool border = false;
        for (Point neighbor : map.neighbors8(p)) {
            const int neighborId = subregion[index(neighbor)];
            if (neighborId == id) {
                continue;
            }
            border = true;
            const Cell& adjacent = map.at(neighbor);
            if (neighborId >= 0) {
                const int level = subregionLevels[static_cast<std::size_t>(neighborId)];
                neighborLevelMin[static_cast<std::size_t>(id)] = std::min(neighborLevelMin[static_cast<std::size_t>(id)], level);
                neighborLevelMax[static_cast<std::size_t>(id)] = std::max(neighborLevelMax[static_cast<std::size_t>(id)], level);
                ++report_.regionProcessNeighborLinks;
            } else if (isPassable(adjacent)) {
                const int level = adjacent.level;
                neighborLevelMin[static_cast<std::size_t>(id)] = std::min(neighborLevelMin[static_cast<std::size_t>(id)], level);
                neighborLevelMax[static_cast<std::size_t>(id)] = std::max(neighborLevelMax[static_cast<std::size_t>(id)], level);
                ++report_.regionProcessNeighborLinks;
            }
        }
        if (border) {
            ++report_.regionProcessBorderCells;
        }
    }

    for (int id = 0; id < nextSubregionId; ++id) {
        const auto sizeIndex = static_cast<std::size_t>(id);
        const int minNeighbor = neighborLevelMin[sizeIndex];
        const int maxNeighbor = neighborLevelMax[sizeIndex];
        if (minNeighbor == std::numeric_limits<int>::max() || maxNeighbor == std::numeric_limits<int>::min()) {
            continue;
        }
        std::vector<int> candidateLevels;
        if (maxNeighbor == minNeighbor) {
            if (minNeighbor >= 4) {
                candidateLevels.push_back(minNeighbor - 4);
                ++report_.regionProcessCandidateLevels;
            }
            if (maxNeighbor <= kNativeBaseMapLevel + 3) {
                candidateLevels.push_back(maxNeighbor + 4);
                ++report_.regionProcessCandidateLevels;
            }
        } else if (maxNeighbor - minNeighbor == 4) {
            candidateLevels.push_back(maxNeighbor);
            candidateLevels.push_back(minNeighbor);
            report_.regionProcessCandidateLevels += 2;
        } else if (maxNeighbor - minNeighbor == 8) {
            candidateLevels.push_back((maxNeighbor + minNeighbor) / 2);
            ++report_.regionProcessCandidateLevels;
        }

        if (nativeRegionLevelZeroCandidateRemoved(options)) {
            candidateLevels.erase(std::remove(candidateLevels.begin(), candidateLevels.end(), 0), candidateLevels.end());
        }
        if (!candidateLevels.empty()) {
            subregionLevels[sizeIndex] = candidateLevels[static_cast<std::size_t>(rng.randomRanged(0, static_cast<int>(candidateLevels.size()) - 1))];
        }

        if (subregionSizes[sizeIndex] <= 100) {
            subregionLevels[sizeIndex] = maxNeighbor;
            ++report_.regionProcessSmallMerges;
        }
    }

    for (auto& cell : map.cells()) {
        const int id = subregion[index({cell.x, cell.y})];
        if (id < 0 || !isPassable(cell)) {
            continue;
        }
        const int oldLevel = cell.level;
        cell.level = std::clamp(subregionLevels[static_cast<std::size_t>(id)], 0, kNativeBaseMapLevel + 4);
        cell.height = cell.level;
        if (cell.level != oldLevel) {
            ++report_.regionProcessLevelAdjustments;
        }
    }

    std::vector<NativeRegion> rebuiltRegions;
    rebuiltRegions.resize(static_cast<std::size_t>(nextSubregionId));
    for (int id = 0; id < nextSubregionId; ++id) {
        rebuiltRegions[static_cast<std::size_t>(id)].id = id + 1;
        rebuiltRegions[static_cast<std::size_t>(id)].level = subregionLevels[static_cast<std::size_t>(id)];
    }

    std::unordered_map<int, std::size_t> retainedRegionIndexByOldId;
    auto appendCellToRegion = [](NativeRegion& region, Point p, const Cell& cell) {
        if (region.cells.empty()) {
            region.bounds = {p.x, p.y, 1, 1};
        } else {
            const int minX = std::min(region.bounds.x, p.x);
            const int minY = std::min(region.bounds.y, p.y);
            const int maxX = std::max(region.bounds.x + region.bounds.w - 1, p.x);
            const int maxY = std::max(region.bounds.y + region.bounds.h - 1, p.y);
            region.bounds = {minX, minY, maxX - minX + 1, maxY - minY + 1};
        }
        region.cells.push_back(p);
        region.wet = region.wet || isWet(cell);
    };

    for (const auto& cell : map.cells()) {
        const Point p{cell.x, cell.y};
        const int id = subregion[index(p)];
        if (id < 0 || !isPassable(cell)) {
            continue;
        }
        appendCellToRegion(rebuiltRegions[static_cast<std::size_t>(id)], p, cell);
    }

    for (const NativeRegion& sourceRegion : regions) {
        for (Point p : sourceRegion.cells) {
            if (!map.inBounds(p) || subregion[index(p)] >= 0 || !isPassable(map.at(p))) {
                continue;
            }
            auto retainedIt = retainedRegionIndexByOldId.find(sourceRegion.id);
            if (retainedIt == retainedRegionIndexByOldId.end()) {
                NativeRegion retained;
                retained.id = static_cast<int>(rebuiltRegions.size()) + 1;
                retained.level = sourceRegion.level;
                retained.nativeWaypointQuota = 0;
                retained.startQuota = 0;
                rebuiltRegions.push_back(std::move(retained));
                retainedIt = retainedRegionIndexByOldId.emplace(sourceRegion.id, rebuiltRegions.size() - 1).first;
            }
            appendCellToRegion(rebuiltRegions[retainedIt->second], p, map.at(p));
        }
    }

    rebuiltRegions.erase(
        std::remove_if(rebuiltRegions.begin(), rebuiltRegions.end(), [](const NativeRegion& region) {
            return region.cells.empty();
        }),
        rebuiltRegions.end());
    if (rebuiltRegions.empty()) {
        return;
    }

    std::sort(rebuiltRegions.begin(), rebuiltRegions.end(), [](const NativeRegion& lhs, const NativeRegion& rhs) {
        if (lhs.cells.size() != rhs.cells.size()) {
            return lhs.cells.size() > rhs.cells.size();
        }
        if (lhs.bounds.y != rhs.bounds.y) {
            return lhs.bounds.y < rhs.bounds.y;
        }
        return lhs.bounds.x < rhs.bounds.x;
    });

    for (auto& cell : map.cells()) {
        cell.regionTag = 0;
    }

    const Rect mapRect = nativeSavedMapRect(map);
    auto nativeCellLess = [](Point lhs, Point rhs) {
        const int lhsSum = lhs.x + lhs.y;
        const int rhsSum = rhs.x + rhs.y;
        if (lhsSum != rhsSum) {
            return lhsSum < rhsSum;
        }
        if (lhs.x != rhs.x) {
            return lhs.x < rhs.x;
        }
        return lhs.y < rhs.y;
    };
    for (std::size_t regionIndex = 0; regionIndex < rebuiltRegions.size(); ++regionIndex) {
        NativeRegion& region = rebuiltRegions[regionIndex];
        region.id = static_cast<int>(regionIndex) + 1;
        region.nativeCells.clear();
        region.nativeBounds = {};

        int levelSum = 0;
        int levelCount = 0;
        for (Point p : region.cells) {
            Cell& cell = map.at(p);
            cell.regionTag = region.id;
            levelSum += cell.level;
            ++levelCount;

            const Point nativeCell = sourcePointToNativeCell(map, p);
            if (isInsideNativeCellDiamond(nativeCell, mapRect)) {
                if (region.nativeCells.empty()) {
                    region.nativeBounds = {nativeCell.x, nativeCell.y, 1, 1};
                } else {
                    const int minX = std::min(region.nativeBounds.x, nativeCell.x);
                    const int minY = std::min(region.nativeBounds.y, nativeCell.y);
                    const int maxX = std::max(region.nativeBounds.x + region.nativeBounds.w - 1, nativeCell.x);
                    const int maxY = std::max(region.nativeBounds.y + region.nativeBounds.h - 1, nativeCell.y);
                    region.nativeBounds = {minX, minY, maxX - minX + 1, maxY - minY + 1};
                }
                region.nativeCells.push_back(nativeCell);
            }
        }
        if (levelCount > 0) {
            region.level = static_cast<int>(std::lround(static_cast<double>(levelSum) / static_cast<double>(levelCount)));
        }
        std::sort(region.nativeCells.begin(), region.nativeCells.end(), nativeCellLess);
        region.nativeCells.erase(std::unique(region.nativeCells.begin(), region.nativeCells.end()), region.nativeCells.end());
    }

    auto regionCellCount = [](const NativeRegion& region) {
        return region.nativeCells.empty() ? region.cells.size() : region.nativeCells.size();
    };
    const std::size_t totalCells = std::accumulate(
        rebuiltRegions.begin(),
        rebuiltRegions.end(),
        std::size_t{0},
        [&regionCellCount](std::size_t total, const NativeRegion& region) {
            return total + regionCellCount(region);
        });
    auto distributeQuota = [&rebuiltRegions, totalCells, &regionCellCount](int total, int NativeRegion::*field) {
        total = std::max(0, total);
        int assigned = 0;
        for (std::size_t i = 0; i < rebuiltRegions.size(); ++i) {
            int quota = 0;
            if (i + 1 == rebuiltRegions.size()) {
                quota = total - assigned;
            } else if (totalCells > 0) {
                quota = static_cast<int>(std::lround(
                    static_cast<double>(regionCellCount(rebuiltRegions[i])) / static_cast<double>(totalCells) * static_cast<double>(total)));
                quota = std::clamp(quota, 0, total - assigned);
            }
            rebuiltRegions[i].*field = quota;
            assigned += quota;
        }

        for (std::size_t i = 0; assigned < total && !rebuiltRegions.empty(); ++i, ++assigned) {
            ++(rebuiltRegions[i % rebuiltRegions.size()].*field);
        }
    };

    distributeQuota(options.numPlayers, &NativeRegion::nativeWaypointQuota);
    distributeQuota(options.numPlayers, &NativeRegion::startQuota);
    report_.regionWaypointQuotaTotal = std::accumulate(rebuiltRegions.begin(), rebuiltRegions.end(), 0, [](int total, const NativeRegion& region) {
        return total + region.nativeWaypointQuota;
    });
    report_.regionStartQuotaTotal = std::accumulate(rebuiltRegions.begin(), rebuiltRegions.end(), 0, [](int total, const NativeRegion& region) {
        return total + region.startQuota;
    });
    report_.regions = static_cast<int>(rebuiltRegions.size());
    report_.regionProcessOutputRegions = static_cast<int>(rebuiltRegions.size());
    report_.regionProcessOutputCells = static_cast<int>(std::accumulate(
        rebuiltRegions.begin(),
        rebuiltRegions.end(),
        std::size_t{0},
        [](std::size_t total, const NativeRegion& region) {
            return total + region.cells.size();
        }));
    regions = std::move(rebuiltRegions);
}

void NativeMapGenerator::connectNativeRegions(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, const std::vector<NativeRegion>& regions)
{
    (void)map;
    (void)options;
    (void)rng;
    (void)regions;
}
