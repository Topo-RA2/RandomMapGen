void NativeMapGenerator::initializeNativeMapgenData()
{
    nativeMapgenDataActive_ = true;
    nativeHighResourceActiveZone_ = 0;
    nativeHighResourceLevel_ = kNativeBaseMapLevel;
    nativeHighResourcePlacedCellCount_ = 0;
    nativeOreChainAttempt_ = 0;
    nativeMapgenDefaultTempTag_ = 0;
    nativeMapgenDefaultZoneId_ = 0;
    nativeMapgenDefaultCacheFlags_ = -1;
    nativeMapgenSurfaceBlockedKeys_.clear();
    nativeMapgenHalfHeightKeys_.clear();
    nativeMapgenTempTags_.clear();
    nativeMapgenZoneIds_.clear();
    nativeMapgenCacheFlags_.clear();
    nativeRegionScratchRegions_.clear();
    nativeMapgenScratchZoneLevels_.clear();
    nativeMapgenScratchZoneCellCounts_.clear();
    nativeMapgenScratchZoneWetFlags_.clear();
}

void NativeMapGenerator::resetNativeZonesAndRegionsScratch()
{
    nativeMapgenDataActive_ = true;
    nativeMapgenDefaultTempTag_ = -1;
    nativeMapgenDefaultZoneId_ = -1;
    nativeMapgenTempTags_.clear();
    nativeMapgenZoneIds_.clear();
    nativeRegionScratchRegions_.clear();
    nativeMapgenScratchZoneLevels_.clear();
    nativeMapgenScratchZoneCellCounts_.clear();
    nativeMapgenScratchZoneWetFlags_.clear();
}

void NativeMapGenerator::cleanupNativeMapgenData()
{
    nativeMapgenDataActive_ = false;
    nativeMapgenDefaultTempTag_ = 0;
    nativeMapgenDefaultZoneId_ = 0;
    nativeMapgenDefaultCacheFlags_ = -1;
    nativeMapgenSurfaceBlockedKeys_.clear();
    nativeMapgenHalfHeightKeys_.clear();
    nativeMapgenTempTags_.clear();
    nativeMapgenZoneIds_.clear();
    nativeMapgenCacheFlags_.clear();
    nativeRegionScratchRegions_.clear();
    nativeMapgenScratchZoneLevels_.clear();
    nativeMapgenScratchZoneCellCounts_.clear();
    nativeMapgenScratchZoneWetFlags_.clear();
    nativeHighResourceWorkspaceActive_ = false;
    nativeHighResourceVisibleProjectionReady_ = false;
    nativeHighResourceLatMissingNeighborSame_ = false;
    nativeHighResourceActiveZone_ = 0;
    nativeHighResourceLevel_ = kNativeBaseMapLevel;
    nativeHighResourcePlacedCellCount_ = 0;
}

void NativeMapGenerator::writeNativeRegionScratch(const std::vector<NativeRegion>& regions)
{
    if (!nativeMapgenDataActive_) {
        initializeNativeMapgenData();
    }
    nativeMapgenDefaultTempTag_ = -1;
    nativeMapgenDefaultZoneId_ = -1;
    nativeMapgenTempTags_.clear();
    nativeMapgenZoneIds_.clear();
    nativeMapgenScratchZoneLevels_.clear();
    nativeMapgenScratchZoneCellCounts_.clear();
    nativeMapgenScratchZoneWetFlags_.clear();
    for (const NativeRegion& region : regions) {
        const int nativeZoneId = std::max(0, region.id - 1);
        for (Point nativeCell : region.nativeCells) {
            const std::uint64_t key = nativeIsoKey(nativeCell);
            nativeMapgenTempTags_[key] = nativeZoneId;
            nativeMapgenZoneIds_[key] = nativeZoneId;
        }
    }
}

void NativeMapGenerator::writeNativeInitialMapZonesScratch(const MapGrid& map)
{
    nativeMapgenDataActive_ = true;
    nativeMapgenDefaultTempTag_ = -1;
    nativeMapgenDefaultZoneId_ = -1;
    nativeMapgenTempTags_.clear();
    nativeMapgenZoneIds_.clear();
    nativeMapgenScratchZoneLevels_.clear();
    nativeMapgenScratchZoneCellCounts_.clear();
    nativeMapgenScratchZoneWetFlags_.clear();

    const auto captureZoneTraceSnapshot = [&](bool includeDetail) {
        return captureNativeZoneWorkspaceTraceSnapshot(
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
            includeDetail);
    };

    const auto cellInfo = buildNativeMapgenScratchCellInfo(map, report_.isoTileOverrides);
    const std::vector<Point> scanOrder = nativeMapgenWorkspaceScanOrder(map);
    constexpr std::array<Point, 8> kNativeMapgenNeighborOrder{{
        {0, -1}, {1, -1}, {1, 0}, {1, 1},
        {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
    }};

    auto zoneFor = [&](std::uint64_t key) {
        const auto it = nativeMapgenZoneIds_.find(key);
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };
    auto tempFor = [&](std::uint64_t key) {
        const auto it = nativeMapgenTempTags_.find(key);
        return it == nativeMapgenTempTags_.end() ? nativeMapgenDefaultTempTag_ : it->second;
    };

    // The native initialization loop visits every MapSize^2 slot before it
    // starts generating zones. Its last zeroed slot is looked up through
    // MouseClass::operator[], leaving InvalidCell at (0, 0).
    nativeInvalidCellPoint_ = {};

    for (Point start : scanOrder) {
        const std::uint64_t startKey = nativeIsoKey(start);
        const auto startIt = cellInfo.find(startKey);
        if (startIt == cellInfo.end()
            || zoneFor(startKey) != -1
            || !isNativeMapgenWetOrGreenTileIndex(startIt->second.isoTileTypeIndex)) {
            continue;
        }

        const int zoneId = static_cast<int>(nativeMapgenScratchZoneLevels_.size());
        const int zoneLevel = startIt->second.level;
        const bool wetOrGreen = true;
        const bool traceZone = native_trace::active();
        if (traceZone) {
            native_trace::EventData traceData;
            traceData.regionCount = zoneId;
            traceData.cellX = static_cast<std::int16_t>(start.x);
            traceData.cellY = static_cast<std::int16_t>(start.y);
            traceData.args[0] = packNativeCell(start);
            traceData.resultKind = native_trace::ResultNone;
            native_trace::enterDetailed(0x58C800, native_trace::GenerateMapZone, traceData);
            if (native_trace::detailRequested()) {
                const NativeZoneWorkspaceTraceSnapshot snapshot = captureZoneTraceSnapshot(true);
                native_trace::detailCurrent(native_trace::DetailOrderedZoneWorkspace, snapshot.detail);
            }
        }
        int committed = 0;
        std::vector<Point> stack;
        stack.push_back(start);
        while (!stack.empty()) {
            const Point current = stack.back();
            stack.pop_back();
            const std::uint64_t currentKey = nativeIsoKey(current);
            nativeMapgenTempTags_[currentKey] = zoneId;
            nativeMapgenZoneIds_[currentKey] = zoneId;
            ++committed;

            for (Point offset : kNativeMapgenNeighborOrder) {
                const Point next{current.x + offset.x, current.y + offset.y};
                const std::uint64_t nextKey = nativeIsoKey(next);
                const auto nextIt = cellInfo.find(nextKey);
                if (nextIt == cellInfo.end() || zoneFor(nextKey) != -1 || tempFor(nextKey) == zoneId) {
                    continue;
                }

                nativeMapgenTempTags_[nextKey] = zoneId;
                if (nextIt->second.level == zoneLevel
                    && isNativeMapgenWetOrGreenTileIndex(nextIt->second.isoTileTypeIndex) == wetOrGreen) {
                    stack.push_back(next);
                }
            }
        }

        nativeMapgenScratchZoneLevels_.push_back(zoneLevel);
        nativeMapgenScratchZoneCellCounts_.push_back(committed);
        nativeMapgenScratchZoneWetFlags_.push_back(1);
        if (traceZone) {
            native_trace::EventData traceData;
            traceData.regionCount = zoneId;
            traceData.cellX = static_cast<std::int16_t>(start.x);
            traceData.cellY = static_cast<std::int16_t>(start.y);
            traceData.args[0] = packNativeCell(start);
            traceData.result[0] = 1;
            NativeZoneWorkspaceTraceSnapshot snapshot;
            if (native_trace::detailRequested()) {
                snapshot = captureZoneTraceSnapshot(true);
            }
            native_trace::exitDetailed(
                0x58CE86,
                native_trace::GenerateMapZone,
                traceData,
                native_trace::DetailOrderedZoneWorkspace,
                snapshot.detail);
        }
    }

}

void NativeMapGenerator::expandNativeMapZonesScratch(const MapGrid& map, const MapSeedOptions& options)
{
    if (nativeMapgenScratchZoneLevels_.empty()) {
        return;
    }

    const auto cellInfo = buildNativeMapgenScratchCellInfo(map, report_.isoTileOverrides);
    const Rect mapRect = nativeSavedMapRect(map);
    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(
        map,
        report_.isoTileOverrides);
    constexpr std::array<Point, 8> kNativeMapgenNeighborOrder{{
        {0, -1}, {1, -1}, {1, 0}, {1, 1},
        {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
    }};

    auto zoneFor = [&](std::uint64_t key) {
        const auto it = nativeMapgenZoneIds_.find(key);
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };
    auto collectBoundary = [&](int zoneId, int regionCellCount) {
        return collectNativeZoneBoundaryCells(
            iteratorCells,
            mapRect,
            nativeMapgenZoneIds_,
            nativeMapgenDefaultZoneId_,
            zoneId,
            zoneId,
            static_cast<int>(nativeMapgenScratchZoneLevels_.size()),
            regionCellCount,
            {0, 0},
            &nativeInvalidCellPoint_,
            true);
    };

    const bool highResources = isNativeHighResourceMode(options);
    for (std::size_t zoneIndex = 0; zoneIndex < nativeMapgenScratchZoneLevels_.size(); ++zoneIndex) {
        const bool wetOrGreen = zoneIndex < nativeMapgenScratchZoneWetFlags_.size()
            && nativeMapgenScratchZoneWetFlags_[zoneIndex] != 0;
        if (!wetOrGreen) {
            continue;
        }
        const int zoneId = static_cast<int>(zoneIndex);
        const int zoneLevel = nativeMapgenScratchZoneLevels_[zoneIndex];
        const int originalCellCount = zoneIndex < nativeMapgenScratchZoneCellCounts_.size()
            ? nativeMapgenScratchZoneCellCounts_[zoneIndex]
            : 0;
        const int passes = (originalCellCount > 8000 ? 1 : 0) + (!highResources ? 1 : 0) + 4;
        const auto captureWorkspace = [&](bool includeDetail) {
            return captureNativeZoneWorkspaceTraceSnapshot(
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
                includeDetail);
        };
        const auto makeTraceData = [&](int expandPasses) {
            native_trace::EventData data;
            data.args[0] = static_cast<std::uint32_t>(expandPasses);
            data.args[1] = static_cast<std::uint32_t>(zoneLevel);
            data.args[2] = 1;
            data.args[3] = static_cast<std::uint32_t>(originalCellCount);
            data.args[4] = 0;
            data.args[5] = static_cast<std::uint32_t>(nativeMapgenScratchZoneLevels_.size());
            data.regionIndex = static_cast<std::int32_t>(zoneIndex);
            data.regionId = zoneId;
            data.regionCount = static_cast<std::int32_t>(nativeMapgenScratchZoneLevels_.size());
            data.regionCellCount = originalCellCount;
            data.cellX = 0;
            data.cellY = 0;
            data.resultKind = native_trace::ResultNone;
            return data;
        };
        const bool traceExpand = native_trace::coreFunctionEnabled(native_trace::ExpandRegionBorder);
        native_trace::EventData expandTraceData = makeTraceData(passes);
        if (traceExpand) {
            native_trace::enterDetailed(0x58E740, native_trace::ExpandRegionBorder, expandTraceData);
            if (native_trace::detailRequested()) {
                const NativeZoneWorkspaceTraceSnapshot snapshot = captureWorkspace(true);
                native_trace::detailCurrent(native_trace::DetailOrderedZoneWorkspace, snapshot.detail);
            }
        }
        std::vector<Point> frontier = collectBoundary(zoneId, originalCellCount);
        bool aborted = false;
        for (int pass = 0; pass < passes && !frontier.empty() && !aborted; ++pass) {
            std::vector<Point> nextFrontier;
            nextFrontier.reserve(frontier.size() * 2);
            for (Point boundary : frontier) {
                for (Point offset : kNativeMapgenNeighborOrder) {
                    const Point next{boundary.x + offset.x, boundary.y + offset.y};
                    const std::uint64_t nextKey = nativeIsoKey(next);
                    const auto nextIt = cellInfo.find(nextKey);
                    if (nextIt == cellInfo.end() || nextIt->second.level != zoneLevel) {
                        continue;
                    }

                    const int nextZone = zoneFor(nextKey);
                    if (nextZone == -1 && isNativeMapgenBlankOrGreenTileIndex(nextIt->second.isoTileTypeIndex)) {
                        nativeMapgenZoneIds_[nextKey] = zoneId;
                        nextFrontier.push_back(next);
                    } else if (nextZone != zoneId) {
                        aborted = true;
                        break;
                    }
                }
                if (aborted) {
                    break;
                }
            }
            if (aborted) {
                break;
            }
            frontier = std::move(nextFrontier);
        }
        if (traceExpand) {
            expandTraceData.result[0] = aborted ? 0u : 1u;
            expandTraceData.resultKind = native_trace::ResultLegacy;
            NativeZoneWorkspaceTraceSnapshot snapshot;
            if (native_trace::detailRequested()) {
                snapshot = captureWorkspace(true);
            }
            native_trace::exitDetailed(
                0x58E740,
                native_trace::ExpandRegionBorder,
                expandTraceData,
                native_trace::DetailOrderedZoneWorkspace,
                snapshot.detail);
        }

        const bool traceCounterclockwise = native_trace::coreFunctionEnabled(
            native_trace::AssignCounterclockwiseBorder);
        native_trace::EventData counterclockwiseTraceData = makeTraceData(0);
        if (traceCounterclockwise) {
            native_trace::enterDetailed(
                0x58E9B0,
                native_trace::AssignCounterclockwiseBorder,
                counterclockwiseTraceData);
            if (native_trace::detailRequested()) {
                const NativeZoneWorkspaceTraceSnapshot snapshot = captureWorkspace(true);
                native_trace::detailCurrent(native_trace::DetailOrderedZoneWorkspace, snapshot.detail);
            }
        }
        (void)collectBoundary(zoneId, originalCellCount);
        if (traceCounterclockwise) {
            NativeZoneWorkspaceTraceSnapshot snapshot;
            if (native_trace::detailRequested()) {
                snapshot = captureWorkspace(true);
            }
            native_trace::exitDetailed(
                0x58E9B0,
                native_trace::AssignCounterclockwiseBorder,
                counterclockwiseTraceData,
                native_trace::DetailOrderedZoneWorkspace,
                snapshot.detail);
        }
    }
}

void NativeMapGenerator::assignRemainingNativeMapZonesScratch(MapGrid& map, NativeRandomizer& rng)
{
    nativeMapgenDataActive_ = true;
    nativeMapgenDefaultTempTag_ = -1;
    nativeMapgenDefaultZoneId_ = -1;

    const auto captureZoneTraceSnapshot = [&](bool includeDetail) {
        return captureNativeZoneWorkspaceTraceSnapshot(
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
            includeDetail);
    };

    auto cellInfo = buildNativeMapgenScratchCellInfo(map, report_.isoTileOverrides);
    const std::vector<Point> scanOrder = nativeMapgenWorkspaceScanOrder(map);
    constexpr std::array<Point, 8> kNativeMapgenNeighborOrder{{
        {0, -1}, {1, -1}, {1, 0}, {1, 1},
        {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
    }};

    auto zoneFor = [&](std::uint64_t key) {
        const auto it = nativeMapgenZoneIds_.find(key);
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };
    auto tempFor = [&](std::uint64_t key) {
        const auto it = nativeMapgenTempTags_.find(key);
        return it == nativeMapgenTempTags_.end() ? nativeMapgenDefaultTempTag_ : it->second;
    };

    // Keep committed cells separately from the scratch map.  The original
    // GenerateMapZone may discard a tiny dry zone or merge it into an older
    // zone before the region vector is materialized.
    auto setClearedCell = [&](Point cell, int level) {
        const auto overrideIt = std::find_if(
            report_.isoTileOverrides.begin(),
            report_.isoTileOverrides.end(),
            [cell](const IsoTileOverride& value) { return value.iso == cell; });
        if (overrideIt != report_.isoTileOverrides.end()) {
            overrideIt->nativeTileTypeIndex = kNativeTileClear;
            overrideIt->variant = 0;
            overrideIt->subTile = 0;
            overrideIt->slopeIndex = 0;
            overrideIt->level = level;
        }
        const auto infoIt = cellInfo.find(nativeIsoKey(cell));
        if (infoIt != cellInfo.end()) {
            infoIt->second.isoTileTypeIndex = kNativeTileClear;
            infoIt->second.level = level;
        }
        const Point source = nativeCellToSourcePoint(map, cell);
        if (map.inBounds(source) && sourcePointToNativeCell(map, source) == cell) {
            map.at(source).height = 0;
            map.at(source).level = level;
            map.at(source).slope = -1;
        }
    };
    auto mergeSmallZone = [&](int zoneId, const std::vector<Point>& cells) {
        if (zoneId <= 0 || cells.empty()) {
            return false;
        }
        const Point root = cells.front();
        const Point left{root.x - 1, root.y};
        const Point target = cellInfo.contains(nativeIsoKey(left)) ? left : Point{root.x, root.y - 1};
        const auto targetInfo = cellInfo.find(nativeIsoKey(target));
        if (targetInfo == cellInfo.end()) {
            return false;
        }
        const int targetZone = zoneFor(nativeIsoKey(target));
        const int targetLevel = targetInfo->second.level;
        for (Point cell : cells) {
            nativeMapgenZoneIds_[nativeIsoKey(cell)] = targetZone;
            setClearedCell(cell, targetLevel);
        }
        ++report_.regionProcessSmallMerges;
        return true;
    };
    auto clearFirstSmallZone = [&](const std::vector<Point>& cells) {
        if (cells.empty()) {
            return false;
        }
        std::vector<Point> border;
        for (Point cell : cells) {
            for (Point offset : kNativeMapgenNeighborOrder) {
                const Point adjacent{cell.x + offset.x, cell.y + offset.y};
                if (cellInfo.contains(nativeIsoKey(adjacent))
                    && zoneFor(nativeIsoKey(adjacent)) != 0) {
                    border.push_back(cell);
                    break;
                }
            }
        }
        if (border.empty()) {
            return false;
        }
        const Point selected = border[static_cast<std::size_t>(rng.randomRanged(0, static_cast<int>(border.size()) - 1))];
        int replacementLevel = -1;
        const int currentLevel = cellInfo.at(nativeIsoKey(selected)).level;
        for (Point offset : kNativeMapgenNeighborOrder) {
            const Point adjacent{selected.x + offset.x, selected.y + offset.y};
            const auto adjacentInfo = cellInfo.find(nativeIsoKey(adjacent));
            if (adjacentInfo == cellInfo.end()
                || adjacentInfo->second.level == currentLevel
                || isNativeShoreTileIndex(adjacentInfo->second.isoTileTypeIndex)
                || (adjacentInfo->second.isoTileTypeIndex >= kNativeTileWaterBase
                    && adjacentInfo->second.isoTileTypeIndex < kNativeTileDirtBase)) {
                continue;
            }
            replacementLevel = adjacentInfo->second.level;
            break;
        }
        if (replacementLevel < 0) {
            return false;
        }
        for (Point cell : cells) {
            nativeMapgenZoneIds_[nativeIsoKey(cell)] = -1;
            setClearedCell(cell, replacementLevel);
        }
        return true;
    };

    for (Point start : scanOrder) {
        const std::uint64_t startKey = nativeIsoKey(start);
        const auto startIt = cellInfo.find(startKey);
        const int zoneId = static_cast<int>(nativeMapgenScratchZoneLevels_.size());
        if (startIt == cellInfo.end()
            || zoneFor(startKey) != -1
            || tempFor(startKey) == zoneId) {
            continue;
        }

        const int zoneLevel = startIt->second.level;
        const bool wetOrGreen = isNativeMapgenWetOrGreenTileIndex(startIt->second.isoTileTypeIndex);
        const bool traceZone = native_trace::active();
        if (traceZone) {
            native_trace::EventData traceData;
            traceData.regionCount = zoneId;
            traceData.cellX = static_cast<std::int16_t>(start.x);
            traceData.cellY = static_cast<std::int16_t>(start.y);
            traceData.args[0] = packNativeCell(start);
            traceData.resultKind = native_trace::ResultNone;
            native_trace::enterDetailed(0x58C800, native_trace::GenerateMapZone, traceData);
            if (native_trace::detailRequested()) {
                const NativeZoneWorkspaceTraceSnapshot snapshot = captureZoneTraceSnapshot(true);
                native_trace::detailCurrent(native_trace::DetailOrderedZoneWorkspace, snapshot.detail);
            }
        }
        int committed = 0;
        std::vector<Point> committedCells;
        committedCells.reserve(75);
        std::vector<Point> stack;
        stack.push_back(start);
        while (!stack.empty()) {
            const Point current = stack.back();
            stack.pop_back();
            const std::uint64_t currentKey = nativeIsoKey(current);
            nativeMapgenTempTags_[currentKey] = zoneId;
            nativeMapgenZoneIds_[currentKey] = zoneId;
            ++committed;
            committedCells.push_back(current);

            for (Point offset : kNativeMapgenNeighborOrder) {
                const Point next{current.x + offset.x, current.y + offset.y};
                const std::uint64_t nextKey = nativeIsoKey(next);
                const auto nextIt = cellInfo.find(nextKey);
                if (nextIt == cellInfo.end() || zoneFor(nextKey) != -1 || tempFor(nextKey) == zoneId) {
                    continue;
                }

                nativeMapgenTempTags_[nextKey] = zoneId;
                if (nextIt->second.level == zoneLevel
                    && isNativeMapgenWetOrGreenTileIndex(nextIt->second.isoTileTypeIndex) == wetOrGreen) {
                    stack.push_back(next);
                }
            }
        }

        if (!wetOrGreen && committed < 75) {
            if (mergeSmallZone(zoneId, committedCells)
                || (zoneId == 0 && clearFirstSmallZone(committedCells))) {
                if (traceZone) {
                    native_trace::EventData traceData;
                    traceData.regionCount = zoneId;
                    traceData.cellX = static_cast<std::int16_t>(start.x);
                    traceData.cellY = static_cast<std::int16_t>(start.y);
                    traceData.args[0] = packNativeCell(start);
                    NativeZoneWorkspaceTraceSnapshot snapshot;
                    if (native_trace::detailRequested()) {
                        snapshot = captureZoneTraceSnapshot(true);
                    }
                    native_trace::exitDetailed(
                        0x58CC96,
                        native_trace::GenerateMapZone,
                        traceData,
                        native_trace::DetailOrderedZoneWorkspace,
                        snapshot.detail);
                }
                continue;
            }
        }

        nativeMapgenScratchZoneLevels_.push_back(zoneLevel);
        nativeMapgenScratchZoneCellCounts_.push_back(committed);
        nativeMapgenScratchZoneWetFlags_.push_back(wetOrGreen ? 1 : 0);
        if (traceZone) {
            native_trace::EventData traceData;
            traceData.regionCount = zoneId;
            traceData.cellX = static_cast<std::int16_t>(start.x);
            traceData.cellY = static_cast<std::int16_t>(start.y);
            traceData.args[0] = packNativeCell(start);
            traceData.result[0] = 1;
            NativeZoneWorkspaceTraceSnapshot snapshot;
            if (native_trace::detailRequested()) {
                snapshot = captureZoneTraceSnapshot(true);
            }
            native_trace::exitDetailed(
                0x58CE86,
                native_trace::GenerateMapZone,
                traceData,
                native_trace::DetailOrderedZoneWorkspace,
                snapshot.detail);
        }
    }
}

std::vector<NativeMapGenerator::NativeRegion> NativeMapGenerator::buildNativeMapgenRegions(
    const MapGrid& map,
    bool committedOnly) const
{
    std::vector<NativeRegion> regions(nativeMapgenScratchZoneLevels_.size());
    for (std::size_t index = 0; index < regions.size(); ++index) {
        NativeRegion& region = regions[index];
        region.id = static_cast<int>(index);
        region.level = nativeMapgenScratchZoneLevels_[index];
        region.nativeConstructedCellCount = index < nativeMapgenScratchZoneCellCounts_.size()
            ? nativeMapgenScratchZoneCellCounts_[index]
            : 0;
        region.wet = index < nativeMapgenScratchZoneWetFlags_.size()
            && nativeMapgenScratchZoneWetFlags_[index] != 0;
    }

    for (const NativeIteratorCell& nativeCell : buildNativeIteratorCells(map, report_.isoTileOverrides)) {
        const auto zoneIt = nativeMapgenZoneIds_.find(nativeIsoKey(nativeCell.cell));
        if (zoneIt == nativeMapgenZoneIds_.end()
            || zoneIt->second < 0
            || zoneIt->second >= static_cast<int>(regions.size())) {
            continue;
        }
        // Before ProcessAllRegions the native region object contains only
        // cells committed by its flood pass. Border expansion may already
        // have assigned those cells the same ZoneId, but they are not part of
        // the object's vector until the post-process rebuild.
        if (committedOnly && !nativeMapgenTempTags_.empty()) {
            const auto tempIt = nativeMapgenTempTags_.find(nativeIsoKey(nativeCell.cell));
            if (tempIt == nativeMapgenTempTags_.end() || tempIt->second != zoneIt->second) {
                continue;
            }
        }
        if (committedOnly) {
            const int tile = nativeCell.isoTileTypeIndex;
            const bool wetOrGreen = isNativeMapgenWetOrGreenTileIndex(tile);
            const bool regionWet = regions[static_cast<std::size_t>(zoneIt->second)].wet;
            if (wetOrGreen != regionWet) {
                continue;
            }
        }
        regions[static_cast<std::size_t>(zoneIt->second)].nativeCells.push_back(nativeCell.cell);
    }

    std::erase_if(regions, [](const NativeRegion& region) {
        return region.nativeCells.empty();
    });
    for (NativeRegion& region : regions) {
        // ProcessAllRegions repopulates each region by scanning g_MapgenData
        // backwards, which is descending Y then descending X.
        std::sort(region.nativeCells.begin(), region.nativeCells.end(), [](Point lhs, Point rhs) {
            return lhs.y != rhs.y ? lhs.y > rhs.y : lhs.x > rhs.x;
        });

        region.nativeBounds = {region.nativeCells.front().x, region.nativeCells.front().y, 1, 1};
        std::unordered_set<std::uint64_t> sourceSeen;
        sourceSeen.reserve(region.nativeCells.size());
        for (Point nativeCell : region.nativeCells) {
            const int minX = std::min(region.nativeBounds.x, nativeCell.x);
            const int minY = std::min(region.nativeBounds.y, nativeCell.y);
            const int maxX = std::max(region.nativeBounds.x + region.nativeBounds.w - 1, nativeCell.x);
            const int maxY = std::max(region.nativeBounds.y + region.nativeBounds.h - 1, nativeCell.y);
            region.nativeBounds = {minX, minY, maxX - minX + 1, maxY - minY + 1};

            const Point source = nativeCellToSourcePoint(map, nativeCell);
            if (!map.inBounds(source)
                || sourcePointToNativeCell(map, source) != nativeCell
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
    }
    return regions;
}
