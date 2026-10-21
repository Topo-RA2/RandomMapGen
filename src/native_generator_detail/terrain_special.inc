bool NativeMapGenerator::placeCliffsAndOre(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng)
{
    // PlaceCliffsAndOre increments g_NextZoneId once on entry.  CliffLine
    // and OreChain then share this active id until the corresponding pass
    // succeeds, after which the native code advances it for the next pass.
    nativeHighResourceActiveZone_ = 1;
    nativeHighResourceLevel_ = kNativeBaseMapLevel;
    nativeHighResourcePlacedCellCount_ = 0;
    bool placed = false;
    if (isNativeHighResourceMode(options) && options.waterAmount > 20) {
        for (int tries = 0; tries < 10; ++tries) {
            Point endPoint;
            if (placeCliffLine(map, options, rng, &endPoint)) {
                if (!nativeHighResourceWorkspaceActive_) {
                    synchronizeHighResourceMapToWorkspace(map, false);
                }
                ++nativeHighResourceActiveZone_;
                placed = true;
                break;
            }
        }
    }

    for (int tries = 0; tries < 10; ++tries) {
        if (placeOreChain(map, options, rng, {0, 0})) {
            ++nativeHighResourceActiveZone_;
            placed = true;
            break;
        }
    }
    return placed;
}

std::vector<Point> NativeMapGenerator::collectZoneBorderCells(const MapGrid& map, const std::vector<Point>& zoneCells) const
{
    std::vector<Point> border;
    border.reserve(zoneCells.size());
    std::vector<unsigned char> inZone(static_cast<std::size_t>(map.width() * map.height()), 0);
    auto index = [&map](Point p) {
        return static_cast<std::size_t>(p.y * map.width() + p.x);
    };

    for (Point p : zoneCells) {
        if (map.inBounds(p)) {
            inZone[index(p)] = 1;
        }
    }

    for (Point p : zoneCells) {
        if (!map.inBounds(p)) {
            continue;
        }
        const auto alreadyAdded = std::find(border.begin(), border.end(), p) != border.end();
        if (alreadyAdded) {
            continue;
        }
        for (Point neighbor : map.neighbors8(p)) {
            if (!inZone[index(neighbor)]) {
                border.push_back(p);
                break;
            }
        }
    }
    return border;
}

NativeMapGenerator::ZoneExpansionResult NativeMapGenerator::expandZoneOutward(MapGrid& map, const std::vector<Point>& zoneCells, int passes, Rect bounds, int targetLevel, bool shoreMode)
{
    ZoneExpansionResult result;
    ++report_.zoneOutwardCalls;
    passes = std::max(0, passes);
    report_.zoneOutwardPasses += passes;
    if (passes == 0 || zoneCells.empty()) {
        return result;
    }

    std::vector<unsigned char> inZone(static_cast<std::size_t>(map.width() * map.height()), 0);
    auto index = [&map](Point p) {
        return static_cast<std::size_t>(p.y * map.width() + p.x);
    };

    std::vector<Point> currentZone;
    currentZone.reserve(zoneCells.size());
    for (Point p : zoneCells) {
        if (!map.inBounds(p) || inZone[index(p)]) {
            continue;
        }
        inZone[index(p)] = 1;
        currentZone.push_back(p);
    }

    for (int pass = 0; pass < passes; ++pass) {
        std::vector<Point> border = collectZoneBorderCells(map, currentZone);
        result.borderCells += static_cast<int>(border.size());
        report_.zoneOutwardBorderCells += static_cast<int>(border.size());

        std::vector<Point> nextAccepted;
        for (Point p : border) {
            for (Point neighbor : map.neighbors8(p)) {
                ++result.candidateChecks;
                ++report_.zoneOutwardCandidateChecks;
                if (inZone[index(neighbor)] || !contains(bounds, neighbor)) {
                    ++result.rejectedCells;
                    ++report_.zoneOutwardRejectedCells;
                    continue;
                }

                Cell& cell = map.at(neighbor);
                const bool blank = shoreMode ? isNativeBlankTile(cell) : isMapgenBlankCell(cell);
                if (!blank) {
                    ++result.rejectedCells;
                    ++report_.zoneOutwardRejectedCells;
                    continue;
                }

                inZone[index(neighbor)] = 1;
                nextAccepted.push_back(neighbor);
                result.accepted.push_back(neighbor);
                ++result.acceptedCells;
                ++report_.zoneOutwardAcceptedCells;
                if (shoreMode) {
                    if (cell.terrain != Terrain::Green) {
                        ++result.shoreCells;
                        ++report_.zoneOutwardShoreCells;
                    }
                    cell.terrain = Terrain::Green;
                    cell.slope = -1;
                } else {
                    const int oldLevel = cell.level;
                    cell.height = std::max(cell.height, targetLevel);
                    cell.level = std::max(cell.level, targetLevel);
                    if (cell.level > oldLevel) {
                        ++result.raisedCells;
                        ++report_.zoneOutwardRaisedCells;
                    }
                }
            }
        }

        if (nextAccepted.empty()) {
            break;
        }
        currentZone.insert(currentZone.end(), nextAccepted.begin(), nextAccepted.end());
    }

    return result;
}

NativeMapGenerator::ZoneExpansionResult NativeMapGenerator::expandZoneDirectional(MapGrid& map, NativeRandomizer& rng, const std::vector<Point>& zoneCells, Point center, Rect bounds, double factor, bool finalize, int targetLevel)
{
    ZoneExpansionResult result;
    ++report_.zoneDirectionalCalls;
    if (zoneCells.empty()) {
        return result;
    }

    const std::vector<Point> border = collectZoneBorderCells(map, zoneCells);
    result.borderCells = static_cast<int>(border.size());
    report_.zoneDirectionalBorderCells += result.borderCells;
    if (border.empty()) {
        return result;
    }

    double directionAngle = 0.0;
    if (bounds.x > 0) {
        directionAngle = 0.0;
    } else if (bounds.x + bounds.w < map.width()) {
        directionAngle = 3.14159265358979323846;
    } else if (bounds.y > 0) {
        directionAngle = 4.71238898038468985769;
    } else if (bounds.y + bounds.h < map.height()) {
        directionAngle = 1.57079632679489661923;
    }

    auto anglePriority = [&directionAngle, center, &rng](Point p) {
        const int dx = p.x - center.x;
        const int dy = p.y - center.y;
        double angle = dx == 0 ? 1.57079632679489661923 : std::atan(-static_cast<double>(dy) / static_cast<double>(dx));
        if (dx < 0) {
            angle += 3.14159265358979323846;
        }
        double diff = std::fabs(angle - directionAngle);
        while (diff >= 6.28318530717958647692) {
            diff -= 6.28318530717958647692;
        }
        if (diff > 3.14159265358979323846) {
            diff = 6.28318530717958647692 - diff;
        }
        return diff * 1.5 + rng.randomUnit() * 2.0;
    };

    std::vector<unsigned char> inZone(static_cast<std::size_t>(map.width() * map.height()), 0);
    std::vector<unsigned char> queued(static_cast<std::size_t>(map.width() * map.height()), 0);
    auto index = [&map](Point p) {
        return static_cast<std::size_t>(p.y * map.width() + p.x);
    };
    for (Point p : zoneCells) {
        if (map.inBounds(p)) {
            inZone[index(p)] = 1;
        }
    }

    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> heap;
    for (Point p : border) {
        heap.push({p, anglePriority(p)});
        queued[index(p)] = 1;
        ++result.heapPushes;
        ++report_.zoneDirectionalHeapPushes;
    }

    const double logBorder = std::max(1.0, std::log(static_cast<double>(border.size())));
    int target = static_cast<int>(std::lround((1.0 / ((1.0 / logBorder) * std::max(0.001, factor))) * 0.5));
    target += rng.randomRanged(0, std::max(0, target / 2));
    target = std::clamp(target, 1, map.width() * map.height());

    int accepted = 0;
    int stepsSinceDrift = 0;
    while (!heap.empty() && accepted < target) {
        const Point current = heap.top().point;
        heap.pop();
        ++result.heapPops;
        ++report_.zoneDirectionalHeapPops;

        if (!map.inBounds(current)) {
            continue;
        }

        Cell& currentCell = map.at(current);
        if (!inZone[index(current)] && isMapgenBlankCell(currentCell)) {
            inZone[index(current)] = 1;
            currentCell.height = std::max(currentCell.height, targetLevel);
            currentCell.level = std::max(currentCell.level, targetLevel);
            result.accepted.push_back(current);
            ++accepted;
            ++result.acceptedCells;
            ++report_.zoneDirectionalAcceptedCells;
        }

        for (Point neighbor : map.neighbors8(current)) {
            ++result.candidateChecks;
            ++report_.zoneDirectionalCandidateChecks;
            const auto neighborIndex = index(neighbor);
            if (inZone[neighborIndex] || queued[neighborIndex] || !contains(bounds, neighbor)) {
                ++result.rejectedCells;
                ++report_.zoneDirectionalRejectedCells;
                continue;
            }
            if (!isMapgenBlankCell(map.at(neighbor))) {
                ++result.rejectedCells;
                ++report_.zoneDirectionalRejectedCells;
                continue;
            }
            queued[neighborIndex] = 1;
            heap.push({neighbor, anglePriority(neighbor)});
            ++result.heapPushes;
            ++report_.zoneDirectionalHeapPushes;
        }

        ++stepsSinceDrift;
        if (stepsSinceDrift >= 8) {
            stepsSinceDrift = 0;
            directionAngle += rng.gaussian() * 0.7853981633974483;
            ++result.angleDrifts;
            ++report_.zoneDirectionalAngleDrifts;
        }
    }

    if (finalize) {
        while (!heap.empty()) {
            const Point p = heap.top().point;
            heap.pop();
            if (!map.inBounds(p) || inZone[index(p)] || !isMapgenBlankCell(map.at(p))) {
                continue;
            }
            Cell& cell = map.at(p);
            cell.height = std::max(cell.height, targetLevel);
            cell.level = std::max(cell.level, targetLevel);
            ++result.finalizedCells;
            ++report_.zoneDirectionalFinalizedCells;
        }
    }

    return result;
}

bool NativeMapGenerator::placeCliffLine(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, Point* endPoint, bool allowBranch, Point seed, double forcedAngle)
{
    native_trace::EventData cliffTraceData;
    const std::uint64_t angleBits = std::bit_cast<std::uint64_t>(forcedAngle);
    cliffTraceData.args[0] = static_cast<std::uint32_t>(static_cast<std::uint16_t>(seed.x))
        | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(seed.y)) << 16);
    cliffTraceData.args[1] = static_cast<std::uint32_t>(angleBits);
    cliffTraceData.args[2] = static_cast<std::uint32_t>(angleBits >> 32);
    cliffTraceData.args[3] = allowBranch ? 1u : 0u;
    cliffTraceData.args[4] = static_cast<std::uint32_t>(nativeHighResourcePlacedCellCount_);
    cliffTraceData.args[5] = static_cast<std::uint32_t>(nativeHighResourceActiveZone_);
    cliffTraceData.regionId = nativeHighResourceActiveZone_;
    cliffTraceData.cellX = static_cast<std::int16_t>(seed.x);
    cliffTraceData.cellY = static_cast<std::int16_t>(seed.y);
    const bool traceCliffLine = native_trace::active();
    if (traceCliffLine) {
        native_trace::enterDetailed(0x59D510, native_trace::PlaceCliffLine, cliffTraceData);
    }
    struct CliffLineTraceScope {
        bool active = false;
        bool result = false;
        native_trace::EventData data;
        const int* placedCellCount = nullptr;
        const int* activeZone = nullptr;
        const int* level = nullptr;

        ~CliffLineTraceScope()
        {
            if (!active) {
                return;
            }
            data.result[0] = result ? 1u : 0u;
            data.result[1] = static_cast<std::uint32_t>(*placedCellCount);
            data.result[2] = static_cast<std::uint32_t>(*activeZone);
            data.result[3] = static_cast<std::uint32_t>(*level);
            native_trace::exitDetailed(0x59D510, native_trace::PlaceCliffLine, data);
        }
    } cliffLineTraceScope{
        traceCliffLine,
        false,
        cliffTraceData,
        &nativeHighResourcePlacedCellCount_,
        &nativeHighResourceActiveZone_,
        &nativeHighResourceLevel_,
    };

    if (nativeHighResourceWorkspaceActive_) {
        ++report_.cliffLineAttempts;
        const int attemptId = report_.cliffLineAttempts;
        std::ofstream cliffLineTrace;
        if (const char* tracePath = std::getenv(kNativeCliffLineTracePathEnv);
            tracePath != nullptr && tracePath[0] != '\0') {
            const std::filesystem::path path{tracePath};
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path());
            }
            std::error_code ec;
            const bool writeHeader = !std::filesystem::exists(path, ec)
                || std::filesystem::file_size(path, ec) == 0;
            cliffLineTrace.open(path, std::ios::out | std::ios::app);
            if (cliffLineTrace.is_open() && writeHeader) {
                cliffLineTrace << "attempt,event,allowBranch,seedX,seedY,side,cursorX,cursorY,angleBase,initialAngle,initialWidth,targetLength,steps,touched,invalid,reason,recalcCalled,recalcSucceeded,directional,raiseSide,raised,rollback,branchTried,branchSucceeded,rngBefore,rngAfter\n";
            }
        }
        auto dumpCliffLineTrace = [
                                       &cliffLineTrace,
                                       attemptId,
                                       allowBranch,
                                       seed](const char* event,
                                             int side,
                                             Point cursor,
                                             double angleBase,
                                             double initialAngle,
                                             double initialWidth,
                                             int targetLength,
                                             int steps,
                                             int touched,
                                             bool invalid,
                                             const char* reason,
                                             bool recalcCalled,
                                             bool recalcSucceeded,
                                             bool directional,
                                             int raiseSide,
                                             int raised,
                                             bool rollback,
                                             bool branchTried,
                                             bool branchSucceeded,
                                             std::uint64_t rngBefore,
                                             std::uint64_t rngAfter) {
            if (!cliffLineTrace.is_open()) {
                return;
            }
            cliffLineTrace << attemptId << ','
                << (event != nullptr ? event : "") << ','
                << (allowBranch ? 1 : 0) << ','
                << seed.x << ',' << seed.y << ',' << side << ','
                << cursor.x << ',' << cursor.y << ','
                << std::setprecision(17) << angleBase << ','
                << initialAngle << ',' << initialWidth << ','
                << targetLength << ',' << steps << ',' << touched << ','
                << (invalid ? 1 : 0) << ',' << (reason != nullptr ? reason : "") << ','
                << (recalcCalled ? 1 : 0) << ',' << (recalcSucceeded ? 1 : 0) << ','
                << (directional ? 1 : 0) << ',' << raiseSide << ',' << raised << ','
                << (rollback ? 1 : 0) << ',' << (branchTried ? 1 : 0) << ','
                << (branchSucceeded ? 1 : 0) << ',' << rngBefore << ',' << rngAfter << '\n';
            cliffLineTrace.flush();
        };
        std::ofstream cliffLinePostDump;
        if (const char* dumpPath = std::getenv(kNativeCliffLinePostDumpPathEnv);
            dumpPath != nullptr && dumpPath[0] != '\0') {
            const std::filesystem::path path{dumpPath};
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path());
            }
            std::error_code ec;
            const bool writeHeader = !std::filesystem::exists(path, ec)
                || std::filesystem::file_size(path, ec) == 0;
            cliffLinePostDump.open(path, std::ios::out | std::ios::app);
            if (cliffLinePostDump.is_open() && writeHeader) {
                cliffLinePostDump << "attempt,stage,index,x,y,isoTileTypeIndex,height,level,slopeIndex,zoneId,scratchId\n";
            }
        }
        auto dumpCliffLinePostState = [this, &cliffLinePostDump, attemptId](const char* stage) {
            if (!cliffLinePostDump.is_open()) {
                return;
            }

            int index = 0;
            for (const IsoTileOverride& override : report_.isoTileOverrides) {
                const std::uint64_t key = nativeIsoKey(override.iso);
                const auto zoneIt = nativeMapgenZoneIds_.find(key);
                const int zoneId = zoneIt == nativeMapgenZoneIds_.end()
                    ? nativeMapgenDefaultZoneId_
                    : zoneIt->second;
                const auto scratchIt = nativeMapgenTempTags_.find(key);
                const int scratchId = scratchIt == nativeMapgenTempTags_.end()
                    ? nativeMapgenDefaultTempTag_
                    : scratchIt->second;
                cliffLinePostDump << attemptId << ','
                    << (stage != nullptr ? stage : "") << ','
                    << index++ << ','
                    << override.iso.x << ',' << override.iso.y << ','
                    << nativeTileTypeIndexForOverride(override) << ','
                    << override.subTile << ',' << override.level << ','
                    << override.slopeIndex << ',' << zoneId << ',' << scratchId << '\n';
            }
            cliffLinePostDump.flush();
        };
        const std::uint64_t rngBefore = rng.drawCount();

        const Rect mapRect = nativeSavedMapRect(map);
        const std::vector<IsoTileOverride> overridesBefore = report_.isoTileOverrides;
        const auto tempTagsBefore = nativeMapgenTempTags_;
        const auto zonesBefore = nativeMapgenZoneIds_;
        const int defaultTempTagBefore = nativeMapgenDefaultTempTag_;
        const int defaultZoneIdBefore = nativeMapgenDefaultZoneId_;

        std::unordered_map<std::uint64_t, std::size_t> overrideIndex;
        overrideIndex.reserve(report_.isoTileOverrides.size());
        for (std::size_t index = 0; index < report_.isoTileOverrides.size(); ++index) {
            overrideIndex[nativeIsoKey(report_.isoTileOverrides[index].iso)] = index;
        }

        auto overrideAt = [&](Point cell) -> IsoTileOverride* {
            const auto it = overrideIndex.find(nativeIsoKey(cell));
            return it == overrideIndex.end() ? nullptr : &report_.isoTileOverrides[it->second];
        };
        auto tileAt = [&](Point cell) {
            const IsoTileOverride* override = overrideAt(cell);
            return override == nullptr ? -1 : nativeTileTypeIndexForOverride(*override);
        };
        auto zoneAt = [&](Point cell) {
            const auto it = nativeMapgenZoneIds_.find(nativeIsoKey(cell));
            return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
        };
        auto isBlank = [&](Point cell) {
            const int tile = tileAt(cell);
            return tile == kNativeTileClear || tile == 0xFFFF;
        };
        bool rollbackPerformed = false;
        auto rollback = [&]() {
            report_.isoTileOverrides = overridesBefore;
            nativeMapgenTempTags_ = tempTagsBefore;
            nativeMapgenZoneIds_ = zonesBefore;
            nativeMapgenDefaultTempTag_ = defaultTempTagBefore;
            nativeMapgenDefaultZoneId_ = defaultZoneIdBefore;
            rollbackPerformed = true;
            ++report_.cliffLineRollbacks;
        };
        auto writeWater = [&](Point cell) {
            IsoTileOverride* override = overrideAt(cell);
            if (override == nullptr) {
                return false;
            }
            override->terrain = Terrain::Water;
            override->variant = 0;
            override->subTile = 0;
            override->landType = 0;
            override->slopeIndex = 0;
            override->nativeTileTypeIndex = kNativeTileWaterBase;
            nativeMapgenZoneIds_[nativeIsoKey(cell)] = nativeHighResourceActiveZone_;
            return true;
        };
        int nativePlateauAttempt = 0;
        int nativePlateauSuccesses = 0;
        auto dumpNativePlateauState = [&](const char* stage) {
            const char* tracePath = std::getenv("RMG_NATIVE_CLIFF_PLATEAU_DUMP_PATH");
            if (tracePath == nullptr || tracePath[0] == '\0') {
                return;
            }

            const std::filesystem::path path{tracePath};
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path());
            }
            std::error_code ec;
            const bool writeHeader = !std::filesystem::exists(path, ec)
                || std::filesystem::file_size(path, ec) == 0;
            std::ofstream out(path, std::ios::out | std::ios::app);
            if (!out.is_open()) {
                return;
            }
            if (writeHeader) {
                out << "attempt,stage,index,x,y,isoTileTypeIndex,level,slopeIndex,zoneId,scratchId\n";
            }

            int index = 0;
            for (const IsoTileOverride& override : report_.isoTileOverrides) {
                const auto zoneIt = nativeMapgenZoneIds_.find(nativeIsoKey(override.iso));
                const int zoneId = zoneIt == nativeMapgenZoneIds_.end()
                    ? nativeMapgenDefaultZoneId_
                    : zoneIt->second;
                const auto scratchIt = nativeMapgenTempTags_.find(nativeIsoKey(override.iso));
                const int scratchId = scratchIt == nativeMapgenTempTags_.end()
                    ? nativeMapgenDefaultTempTag_
                    : scratchIt->second;
                out << nativePlateauAttempt << ','
                    << (stage != nullptr ? stage : "") << ','
                    << index++ << ','
                    << override.iso.x << ',' << override.iso.y << ','
                    << nativeTileTypeIndexForOverride(override) << ','
                    << override.level << ','
                    << override.slopeIndex << ','
                    << zoneId << ','
                    << scratchId << '\n';
            }
        };
        auto placeNativePlateauPrelude = [&](Point first, Point last, int direction) {
            ++nativePlateauAttempt;
            ++report_.cliffPlateauAttempts;
            dumpNativePlateauState("plateau_before");
            Point checkOrigin;
            Point fillOrigin;
            Point outerFillOrigin;
            Point center;
            Rect bounds;
            int checkWidth = 0;
            int checkHeight = 0;
            int fillWidth = 0;
            int fillHeight = 0;
            int outerFillWidth = 0;
            int outerFillHeight = 0;

            switch (direction) {
            case 0: {
                const int span = last.x - first.x;
                checkOrigin = {first.x - 2, first.y - 12};
                checkWidth = span + 5;
                checkHeight = 12;
                fillOrigin = {first.x, first.y - 4};
                fillWidth = span + 1;
                fillHeight = 4;
                outerFillOrigin = {first.x, first.y - 12};
                outerFillWidth = span + 1;
                outerFillHeight = 8;
                center = {first.x, first.y - 4};
                bounds = {0, first.y - 4, 512, 512 - (first.y - 4)};
                break;
            }
            case 2: {
                const int span = last.y - first.y;
                checkOrigin = {first.x + 1, first.y - 2};
                checkWidth = 12;
                checkHeight = span + 5;
                fillOrigin = {first.x + 1, first.y};
                fillWidth = 4;
                fillHeight = span + 1;
                outerFillOrigin = {first.x + 5, first.y};
                outerFillWidth = 8;
                outerFillHeight = span + 1;
                center = {first.x + 5, first.y};
                bounds = {0, 0, first.x + 4, 512};
                break;
            }
            case 4: {
                const int span = first.x - last.x;
                checkOrigin = {last.x - 2, last.y + 1};
                checkWidth = span + 5;
                checkHeight = 12;
                fillOrigin = {last.x, last.y + 1};
                fillWidth = span + 1;
                fillHeight = 4;
                outerFillOrigin = {last.x, last.y + 5};
                outerFillWidth = span + 1;
                outerFillHeight = 8;
                center = {last.x, last.y + 1};
                bounds = {0, 0, 512, last.y + 4};
                break;
            }
            case 6: {
                const int span = first.y - last.y;
                checkOrigin = {last.x - 12, last.y - 2};
                checkWidth = 12;
                checkHeight = span + 5;
                fillOrigin = {last.x - 4, last.y};
                fillWidth = 4;
                fillHeight = span + 1;
                outerFillOrigin = {last.x - 12, last.y};
                outerFillWidth = 8;
                outerFillHeight = span + 1;
                center = {last.x - 4, last.y};
                bounds = {last.x - 4, 0, 512 - (last.x - 4), 512};
                break;
            }
            default:
                dumpNativePlateauState("plateau_after");
                return false;
            }

            if (checkWidth <= 0 || checkHeight <= 0
                || fillWidth <= 0 || fillHeight <= 0
                || outerFillWidth <= 0 || outerFillHeight <= 0) {
                dumpNativePlateauState("plateau_after");
                return false;
            }
            for (int py = checkOrigin.y; py < checkOrigin.y + checkHeight; ++py) {
                for (int px = checkOrigin.x; px < checkOrigin.x + checkWidth; ++px) {
                    const Point cell{px, py};
                    if (!isInsideNativeCellDiamond(cell, mapRect)) {
                        continue;
                    }
                    if (zoneAt(cell) != 0 || !isBlank(cell)) {
                        dumpNativePlateauState("plateau_after");
                        return false;
                    }
                }
            }

            auto fillWaterRect = [&](Point origin, int width, int height) {
                for (int py = origin.y; py < origin.y + height; ++py) {
                    for (int px = origin.x; px < origin.x + width; ++px) {
                        const Point cell{px, py};
                        if (!isInsideNativeCellDiamond(cell, mapRect)) {
                            continue;
                        }
                        IsoTileOverride* override = overrideAt(cell);
                        if (override == nullptr) {
                            continue;
                        }
                        const int waterVariant = rng.randomScaledInclusive(0, 5);
                        const int subTile = rng.randomScaledInclusive(0, 3);
                        override->terrain = Terrain::Water;
                        override->variant = waterVariant;
                        override->subTile = subTile;
                        override->landType = 0;
                        override->slopeIndex = 0;
                        override->nativeTileTypeIndex = kNativeTileWaterBase + waterVariant;
                        nativeMapgenZoneIds_[nativeIsoKey(cell)] = nativeHighResourceActiveZone_;
                    }
                }
            };

            fillWaterRect(fillOrigin, fillWidth, fillHeight);
            if (!expandNativeZoneDirectional(
                    map,
                    rng,
                    nativeHighResourceActiveZone_,
                    0.003f,
                    bounds,
                    center,
                    false)
                || !recalculateHighResourceCliffLevels(map, rng, false)
                || !expandNativeZoneOutward(
                    map,
                    nativeHighResourceActiveZone_,
                    2,
                    bounds,
                    false,
                    0)) {
                dumpNativePlateauState("plateau_after");
                return false;
            }

            for (IsoTileOverride& override : report_.isoTileOverrides) {
                if (zoneAt(override.iso) == nativeHighResourceActiveZone_) {
                    override.level += 4;
                }
            }
            fillWaterRect(outerFillOrigin, outerFillWidth, outerFillHeight);

            if (direction == 2
                && nativeTheaterCatalog_ != nullptr
                && nativeTheaterCatalog_->waterfallEastBase >= 0) {
                const int cliffCornerBBase = nativeTheaterCatalog_->waterfallEastBase;

                const int spanCells = last.y - first.y + 1;
                const Point lowerCorner{first.x + 5, first.y + spanCells};
                const IsoTileOverride* levelSource = overrideAt({lowerCorner.x + 2, lowerCorner.y});
                if (levelSource == nullptr) {
                    dumpNativePlateauState("plateau_after");
                    return false;
                }
                const int baseLevel = levelSource->level + kNativeBaseMapLevel;

                if (!placeNativeIsoTile(
                        map,
                        cliffCornerBBase,
                        lowerCorner,
                        nativeHighResourceActiveZone_,
                        baseLevel)
                    || !placeNativeIsoTile(
                        map,
                        cliffCornerBBase + 3,
                        {lowerCorner.x, first.y - 2},
                        nativeHighResourceActiveZone_,
                        baseLevel)) {
                    dumpNativePlateauState("plateau_after");
                    return false;
                }

                const auto placeBlankCliffEnd = [&](Point cell) {
                    IsoTileOverride* override = overrideAt(cell);
                    if (override == nullptr) {
                        return false;
                    }
                    override->nativeTileTypeIndex = 0xFFFF;
                    override->subTile = 0;
                    override->level += 4;
                    override->slopeIndex = 0;
                    nativeMapgenZoneIds_[nativeIsoKey(cell)] = nativeHighResourceActiveZone_;

                    const Point source = nativeCellToSourcePoint(map, cell);
                    if (map.inBounds(source) && sourcePointToNativeCell(map, source) == cell) {
                        Cell& productCell = map.at(source);
                        productCell.height = 0;
                        productCell.level = override->level;
                        productCell.slope = 0;
                    }
                    return true;
                };
                if (!placeBlankCliffEnd({lowerCorner.x, first.y - 3})
                    || !placeBlankCliffEnd({lowerCorner.x, lowerCorner.y + 2})) {
                    dumpNativePlateauState("plateau_after");
                    return false;
                }

                for (int placedSpan = 0; placedSpan < spanCells;) {
                    const int remaining = spanCells - placedSpan;
                    const bool useSinglePiece = (remaining & 1) != 0;
                    const int pieceSpan = useSinglePiece ? 1 : 2;
                    if (!placeNativeIsoTile(
                            map,
                            cliffCornerBBase + (useSinglePiece ? 1 : 2),
                            {lowerCorner.x, lowerCorner.y - placedSpan - pieceSpan},
                            nativeHighResourceActiveZone_,
                            baseLevel)) {
                        dumpNativePlateauState("plateau_after");
                        return false;
                    }
                    placedSpan += pieceSpan;
                }
            }

            ++report_.cliffPlateauAccepted;
            dumpNativePlateauState("plateau_after");
            return true;
        };

        Point cursor{};
        int side = -1;
        double angleBase = 0.0;
        if (seed.x != 0 || seed.y != 0) {
            cursor = seed;
            angleBase = forcedAngle;
        } else {
            // Origin always consumes all three draws, regardless of the edge:
            // edge, width-axis coordinate, then height-axis coordinate.
            side = rng.randomScaledInclusive(0, 3);
            const int widthCoordinate = rng.randomScaledInclusive(0, mapRect.w - 1);
            const int heightCoordinate = rng.randomScaledInclusive(0, mapRect.h - 1);
            const std::array<Point, 4> edgeSeeds{{
                {widthCoordinate + 1, mapRect.w - widthCoordinate},
                {mapRect.w + mapRect.h - 1 - heightCoordinate, mapRect.h - heightCoordinate},
                {mapRect.w + mapRect.h - 1 - widthCoordinate, mapRect.h + widthCoordinate},
                {heightCoordinate + 1, mapRect.w + heightCoordinate},
            }};
            cursor = edgeSeeds[static_cast<std::size_t>(side)];
            angleBase = nativeX87Chop53Add(
                kNativeSevenPiOverFour,
                -nativeX87Chop53Multiply(static_cast<double>(side), kNativePiOverTwo));
        }

        if ((seed.x != 0 || seed.y != 0) && !isInsideNativeCellDiamond(cursor, mapRect)) {
            dumpCliffLineTrace(
                "failed",
                side,
                cursor,
                angleBase,
                forcedAngle,
                0.0,
                0,
                0,
                0,
                true,
                "seed_out_of_bounds",
                false,
                false,
                false,
                0,
                0,
                false,
                allowBranch,
                false,
                rngBefore,
                rng.drawCount());
            return false;
        }

        double angle = (seed.x != 0 || seed.y != 0)
            ? forcedAngle
            : clampCliffLineGaussian(
                  rng,
                  angleBase,
                  0.5235987755982988,
                  nativeX87Chop53Add(angleBase, -0.7853981633974483),
                  nativeX87Chop53Add(angleBase, 0.7853981633974483),
                  0x59D7B4,
                  angleBase,
                  cliffTraceData.regionId,
                  seed);
        const double initialAngle = angle;
        const int maximumWidth = std::max(1, nativeDoubleToInt(static_cast<double>(options.waterAmount) * 0.07));
        double width = static_cast<double>(rng.randomScaledInclusive(1, maximumWidth));
        const double initialWidth = width;
        const int initialHalfWidth = nativeDoubleToInt(initialWidth) / 2;
        const double minimumWidth = initialWidth - static_cast<double>(initialHalfWidth);
        const double maximumLineWidth = initialWidth + static_cast<double>(initialHalfWidth);
        const int targetLength = rng.randomScaledInclusive(35, 125);
        std::vector<Point> touched;
        touched.reserve(static_cast<std::size_t>((mapRect.w + mapRect.h) * static_cast<int>(width + 2.0)));

        double x = static_cast<double>(cursor.x) + 0.5;
        double y = static_cast<double>(cursor.y) + 0.5;
        int localSteps = 0;
        bool invalid = false;
        bool branchTried = allowBranch;
        bool branchSucceeded = false;
        bool randomlyTerminated = false;
        bool recalcCalled = false;
        bool recalcSucceeded = false;
        bool directional = false;
        int raiseSide = 0;
        int raised = 0;
        std::string failureReason;
        while (true) {
            const Point center{
                nativeDoubleToInt(x),
                nativeDoubleToInt(y),
            };
            if (!isInsideNativeCellDiamond(center, mapRect)) {
                failureReason = "center_out_of_bounds";
                break;
            }

            const double cosine = nativeLegacyCos(angle);
            const double sine = nativeLegacySin(angle);
            const int sampleCount = std::max(1, nativeDoubleToInt(width + 0.5));
            const double sampleOffset = static_cast<double>(sampleCount - 1);
            const double sampleXOffset = nativeX87Chop53Multiply(
                nativeX87Chop53Multiply(sampleOffset, sine),
                0.5);
            const double sampleYOffset = nativeX87Chop53Multiply(
                nativeX87Chop53Multiply(sampleOffset, cosine),
                0.5);
            double sampleX = nativeX87Chop53Add(x, -sampleXOffset);
            double sampleY = nativeX87Chop53Add(y, -sampleYOffset);
            const Point firstSample{nativeDoubleToInt(sampleX), nativeDoubleToInt(sampleY)};
            Point lastSample = firstSample;
            bool sameSampleX = true;
            bool sameSampleY = true;
            for (int sample = 0; sample < sampleCount; ++sample) {
                const Point cell{nativeDoubleToInt(sampleX), nativeDoubleToInt(sampleY)};
                lastSample = cell;
                if (!isInsideNativeCellDiamond(cell, mapRect)) {
                    sampleX = nativeX87Chop53Add(sampleX, sine);
                    sampleY = nativeX87Chop53Add(sampleY, cosine);
                    continue;
                }
                const int zone = zoneAt(cell);
                if (zone != 0 && zone != nativeHighResourceActiveZone_) {
                    invalid = true;
                    failureReason = "foreign_zone";
                } else if (!isBlank(cell) && zone != nativeHighResourceActiveZone_) {
                    invalid = true;
                    failureReason = "occupied";
                } else {
                    if (std::find(touched.begin(), touched.end(), cell) == touched.end()) {
                        touched.push_back(cell);
                    }
                    if (!writeWater(cell)) {
                        invalid = true;
                        failureReason = "missing_override";
                    }
                }
                const double nextSampleX = nativeX87Chop53Add(sampleX, sine);
                const double nextSampleY = nativeX87Chop53Add(sampleY, cosine);
                sameSampleX = sameSampleX
                    && nativeDoubleToInt(nextSampleX) == nativeDoubleToInt(sampleX);
                sameSampleY = sameSampleY
                    && nativeDoubleToInt(nextSampleY) == nativeDoubleToInt(sampleY);
                sampleX = nextSampleX;
                sampleY = nextSampleY;
                if (invalid) {
                    break;
                }
            }

            if (std::fabs(sine) >= std::fabs(cosine)) {
                sameSampleX = false;
            } else {
                sameSampleY = false;
            }
            if (!allowBranch
                && nativeCliffPlateauEnabled_
                && (sameSampleX || sameSampleY)
                && localSteps > targetLength
                && nativePlateauSuccesses < 1
                && std::abs(nativeDoubleToInt(angle - initialAngle)) < 0.7853981633974483) {
                const int direction = sameSampleX
                    ? (cosine <= 0.0 ? 6 : 2)
                    : (sine <= 0.0 ? 4 : 0);
                if (placeNativePlateauPrelude(firstSample, lastSample, direction)) {
                    const Point plateauOffset = directionOffset(direction);
                    x = nativeX87Chop53Add(x, static_cast<double>(plateauOffset.x * 12));
                    y = nativeX87Chop53Add(y, static_cast<double>(plateauOffset.y * 12));
                    ++nativeHighResourceActiveZone_;
                    ++nativePlateauSuccesses;
                }
            }

            x = nativeX87Chop53Add(x, cosine);
            y = nativeX87Chop53Add(y, -sine);
            const double branchRoll = rng.randomUnit();
            if (branchRoll < 0.01
                && !invalid
                && !branchTried
                && nativePlateauSuccesses == 0) {
                branchTried = true;
                ++report_.cliffLineBranchAttempts;
                const double branchMean = nativeX87Chop53Add(angle, kNativePiOverTwo);
                const double branchAngle = clampCliffLineGaussian(
                    rng,
                    branchMean,
                    0.5235987755982988,
                    nativeX87Chop53Add(angle, 0.5235987755982988),
                    nativeX87Chop53Add(angle, 2.617993877991494),
                    0x59DF38,
                    angle,
                    cliffTraceData.regionId,
                    seed);
                Point branchEnd;
                if (placeCliffLine(
                        map,
                        options,
                        rng,
                        &branchEnd,
                        true,
                        {nativeDoubleToInt(sampleX), nativeDoubleToInt(sampleY)},
                        branchAngle)) {
                    ++report_.cliffLineBranchSuccesses;
                    branchSucceeded = true;
                } else {
                    invalid = true;
                    failureReason = "branch_failed";
                }
            }
            if (localSteps > 5) {
                const double angleDelta = clampCliffLineGaussian(
                    rng,
                    0.0,
                    0.3141592653589793,
                    nativeX87Chop53Add(
                        nativeX87Chop53Add(initialAngle, -kNativePiOverTwo),
                        -angle),
                    nativeX87Chop53Add(
                        nativeX87Chop53Add(initialAngle, kNativePiOverTwo),
                        -angle),
                    0x59E043,
                    angle,
                    cliffTraceData.regionId,
                    seed);
                angle = nativeX87Chop53Add(angle, angleDelta);
                ++report_.cliffLineAngleAdjustments;
            }
            // 0x59E123 runs on every step; only the angle drift at 0x59E048
            // is gated by the five-step warmup.
            if (initialHalfWidth > 0) {
                const double widthDelta = clampGaussian(
                    rng,
                    0.0,
                    0.5,
                    minimumWidth - width,
                    maximumLineWidth - width);
                width = nativeX87Chop53Add(width, widthDelta);
                ++report_.cliffLineWidthAdjustments;
            }
            randomlyTerminated = rng.randomUnit() < 0.005;
            ++localSteps;
            if (invalid || randomlyTerminated) {
                break;
            }
        }

        report_.cliffLineSteps += localSteps;
        report_.cliffLineTouchedCells += static_cast<int>(touched.size());
        if (invalid || localSteps < 40) {
            if (failureReason.empty()) {
                failureReason = invalid ? "invalid" : "short_steps";
            }
            rollback();
            dumpCliffLineTrace("failed", side, cursor, angleBase, initialAngle, initialWidth, targetLength, localSteps, static_cast<int>(touched.size()), invalid, failureReason.c_str(), recalcCalled, recalcSucceeded, directional, raiseSide, raised, rollbackPerformed, branchTried, branchSucceeded, rngBefore, rng.drawCount());
            return false;
        }

        if (randomlyTerminated) {
            const Point oreSeed{nativeDoubleToInt(x), nativeDoubleToInt(y)};
            if (isInsideNativeCellDiamond(oreSeed, mapRect)
                && !placeOreChain(map, options, rng, oreSeed)) {
                failureReason = "ore_chain_failed";
                rollback();
                dumpCliffLineTrace("failed", side, cursor, angleBase, initialAngle, initialWidth, targetLength, localSteps, static_cast<int>(touched.size()), true, failureReason.c_str(), recalcCalled, recalcSucceeded, directional, raiseSide, raised, rollbackPerformed, branchTried, branchSucceeded, rngBefore, rng.drawCount());
                return false;
            }
        }

        if (endPoint) {
            *endPoint = {nativeDoubleToInt(x), nativeDoubleToInt(y)};
        }

        if (allowBranch) {
            nativeHighResourcePlacedCellCount_ += localSteps;
            ++report_.cliffLines;
            dumpCliffLineTrace("success", side, cursor, angleBase, initialAngle, initialWidth, targetLength, localSteps, static_cast<int>(touched.size()), invalid, "", recalcCalled, recalcSucceeded, directional, raiseSide, raised, rollbackPerformed, branchTried, branchSucceeded, rngBefore, rng.drawCount());
            cliffLineTraceScope.result = true;
            return true;
        }

        recalcCalled = true;
        recalcSucceeded = recalculateHighResourceCliffLevels(map, rng, false);
        if (!recalcSucceeded) {
            failureReason = "recalculate_failed";
            rollback();
            dumpCliffLineTrace("failed", side, cursor, angleBase, initialAngle, initialWidth, targetLength, localSteps, static_cast<int>(touched.size()), invalid, failureReason.c_str(), recalcCalled, recalcSucceeded, directional, raiseSide, raised, rollbackPerformed, branchTried, branchSucceeded, rngBefore, rng.drawCount());
            return false;
        }
        dumpCliffLinePostState("after_recalculate");
        for (std::size_t index = 0; index < report_.isoTileOverrides.size(); ++index) {
            const IsoTileOverride& before = overridesBefore[index];
            const IsoTileOverride& after = report_.isoTileOverrides[index];
            if (nativeTileTypeIndexForOverride(before) != nativeTileTypeIndexForOverride(after)) {
                nativeMapgenZoneIds_[nativeIsoKey(after.iso)] = nativeHighResourceActiveZone_;
            }
        }
        dumpCliffLinePostState("after_recalculate_zone_mark");

        if (!expandNativeZoneOutward(
                map,
                nativeHighResourceActiveZone_,
                1,
                {0, 0, 512, 512},
                false,
                0)) {
            failureReason = "initial_outward_failed";
            rollback();
            dumpCliffLineTrace("failed", side, cursor, angleBase, initialAngle, initialWidth, targetLength, localSteps, static_cast<int>(touched.size()), invalid, failureReason.c_str(), recalcCalled, recalcSucceeded, directional, raiseSide, raised, rollbackPerformed, branchTried, branchSucceeded, rngBefore, rng.drawCount());
            return false;
        }
        dumpCliffLinePostState("after_initial_outward");
        convertNativeZoneBlankToGreen(nativeHighResourceActiveZone_);

        directional = nativePlateauSuccesses == 0
            && nativeHighResourceLevel_ == kNativeBaseMapLevel
            && rng.randomUnit() < 0.7;
        if (directional) {
            ZoneExpansionResult directionalExpansion;
            if (!expandNativeZoneDirectional(
                    map,
                    rng,
                    nativeHighResourceActiveZone_,
                    0.01f,
                    {0, 0, 512, 512},
                    cursor,
                    true,
                    &directionalExpansion)) {
                failureReason = "directional_expand_failed";
                rollback();
                dumpCliffLineTrace("failed", side, cursor, angleBase, initialAngle, initialWidth, targetLength, localSteps, static_cast<int>(touched.size()), invalid, failureReason.c_str(), recalcCalled, recalcSucceeded, directional, raiseSide, raised, rollbackPerformed, branchTried, branchSucceeded, rngBefore, rng.drawCount());
                return false;
            }
            dumpCliffLinePostState("after_directional");
            if (!expandNativeZoneOutward(
                    map,
                    nativeHighResourceActiveZone_,
                    6,
                    {0, 0, 512, 512},
                    false,
                    0)) {
                failureReason = "directional_expand_failed";
                rollback();
                dumpCliffLineTrace("failed", side, cursor, angleBase, initialAngle, initialWidth, targetLength, localSteps, static_cast<int>(touched.size()), invalid, failureReason.c_str(), recalcCalled, recalcSucceeded, directional, raiseSide, raised, rollbackPerformed, branchTried, branchSucceeded, rngBefore, rng.drawCount());
                return false;
            }
            dumpCliffLinePostState("after_directional_outward");

            for (IsoTileOverride& override : report_.isoTileOverrides) {
                if (zoneAt(override.iso) != nativeHighResourceActiveZone_) {
                    override.level += 4;
                    ++raised;
                }
            }
            nativeHighResourceLevel_ += 4;
            dumpCliffLinePostState("after_raise_non_active_zone");
        } else if (!expandNativeZoneOutward(
                       map,
                       nativeHighResourceActiveZone_,
                       2,
                       {0, 0, 512, 512},
                       nativePlateauSuccesses != 0,
                       nativePlateauSuccesses != 0 ? nativeHighResourceLevel_ : 0)) {
            failureReason = "final_outward_failed";
            rollback();
            dumpCliffLineTrace("failed", side, cursor, angleBase, initialAngle, initialWidth, targetLength, localSteps, static_cast<int>(touched.size()), invalid, failureReason.c_str(), recalcCalled, recalcSucceeded, directional, raiseSide, raised, rollbackPerformed, branchTried, branchSucceeded, rngBefore, rng.drawCount());
            return false;
        } else {
            dumpCliffLinePostState("after_final_outward");
        }

        nativeHighResourcePlacedCellCount_ += localSteps;
        report_.cliffLineRaisedCells += raised;
        ++report_.cliffLines;
        dumpCliffLineTrace("success", side, cursor, angleBase, initialAngle, initialWidth, targetLength, localSteps, static_cast<int>(touched.size()), invalid, "", recalcCalled, recalcSucceeded, directional, raiseSide, raised, rollbackPerformed, branchTried, branchSucceeded, rngBefore, rng.drawCount());
        cliffLineTraceScope.result = true;
        return true;
    }

    ++report_.cliffLineAttempts;
    const std::vector<Cell> fullSnapshot = allowBranch ? std::vector<Cell>{} : map.cells();
    Point cursor;
    double angleBase = 0.0;
    if (seed.x != 0 || seed.y != 0) {
        cursor = clampPoint(seed, map.bounds());
        angleBase = forcedAngle;
    } else {
        const int side = rng.randomRanged(0, 3);
        switch (side) {
        case 0:
            cursor = {rng.randomRanged(0, map.width() - 1), 0};
            angleBase = 3.14159265358979323846 * 0.5;
            break;
        case 1:
            cursor = {map.width() - 1, rng.randomRanged(0, map.height() - 1)};
            angleBase = 3.14159265358979323846;
            break;
        case 2:
            cursor = {rng.randomRanged(0, map.width() - 1), map.height() - 1};
            angleBase = 3.14159265358979323846 * 1.5;
            break;
        default:
            cursor = {0, rng.randomRanged(0, map.height() - 1)};
            angleBase = 0.0;
            break;
        }
    }

    double angle = (seed.x != 0 || seed.y != 0)
        ? forcedAngle
        : clampGaussian(rng, angleBase, 0.5235987755982988, angleBase - 0.7853981633974483, angleBase + 0.7853981633974483);
    const double initialAngle = angle;
    const int maximumWidth = std::max(1, nativeDoubleToInt(static_cast<double>(options.waterAmount) * 0.07));
    double width = static_cast<double>(rng.randomScaledInclusive(1, maximumWidth));
    const int targetLength = rng.randomRanged(35, 125);
    std::vector<Point> touched;
    touched.reserve(static_cast<std::size_t>(targetLength * static_cast<int>(width + 2.0)));
    struct Snapshot {
        Point point;
        Cell cell;
    };
    std::vector<Snapshot> snapshots;
    snapshots.reserve(static_cast<std::size_t>(targetLength * static_cast<int>(width + 2.0)));

    auto snapshotCell = [&](Point p) {
        if (std::find(touched.begin(), touched.end(), p) == touched.end()) {
            snapshots.push_back({p, map.at(p)});
        }
    };

    double x = static_cast<double>(cursor.x) + 0.5;
    double y = static_cast<double>(cursor.y) + 0.5;
    int localSteps = 0;
    bool branchTried = allowBranch;
    bool plateauAccepted = allowBranch;
    int plateauAttempts = 0;
    for (int step = 0; step < targetLength; ++step) {
        if (!map.inBounds({static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))})) {
            break;
        }
        ++localSteps;

        const double perpX = -std::sin(angle);
        const double perpY = std::cos(angle);
        const int halfWidth = std::max(0, static_cast<int>(std::lround(width * 0.5)));
        for (int offset = -halfWidth; offset <= halfWidth; ++offset) {
            const Point p{
                static_cast<int>(std::lround(x + perpX * offset)),
                static_cast<int>(std::lround(y + perpY * offset)),
            };
            if (!map.inBounds(p)) {
                continue;
            }
            Cell& cell = map.at(p);
            if (cell.overlay == Overlay::Start) {
                continue;
            }
            snapshotCell(p);
            cell.terrain = Terrain::Cliff;
            cell.overlay = Overlay::None;
            cell.height = kNativeBaseMapLevel;
            cell.level = kNativeBaseMapLevel;
            touched.push_back(p);
        }

        const double angleDrift = std::abs(std::remainder(angle - initialAngle, 6.28318530717958647692));
        if (!allowBranch && !plateauAccepted && localSteps >= 40 && plateauAttempts < 3 && angleDrift < 0.7853981633974483) {
            ++plateauAttempts;
            const int direction = cliffPlateauDirection(angle);
            const Point anchor{static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))};
            if (placeCliffPlateau(map, options, rng, anchor, direction, width, &x, &y)) {
                plateauAccepted = true;
            }
        }

        x += std::cos(angle);
        y += std::sin(angle);
        if (!branchTried && rng.randomUnit() < 0.01) {
            branchTried = true;
            ++report_.cliffLineBranchAttempts;
            const double branchAngle = clampGaussian(
                rng,
                angle + 1.570796326794897,
                0.5235987755982988,
                angle + 0.5235987755982988,
                angle + 2.617993877991494);
            Point branchEnd;
            if (placeCliffLine(map, options, rng, &branchEnd, true, {static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))}, branchAngle)) {
                ++report_.cliffLineBranchSuccesses;
            }
        }
        if (step > 5) {
            angle += clampGaussian(rng, 0.0, 0.3141592653589793, -0.3141592653589793, 0.3141592653589793);
            ++report_.cliffLineAngleAdjustments;
            width = std::max(1.0, width + clampGaussian(rng, 0.0, 0.5, -0.5, 0.5));
            ++report_.cliffLineWidthAdjustments;
        }

        if (rng.randomUnit() < 0.005) {
            break;
        }
    }

    report_.cliffLineSteps += localSteps;
    report_.cliffLineTouchedCells += static_cast<int>(touched.size());
    if (localSteps < 40 || static_cast<int>(touched.size()) < 40) {
        if (!allowBranch) {
            map.cells() = fullSnapshot;
        } else {
            for (auto it = snapshots.rbegin(); it != snapshots.rend(); ++it) {
                map.at(it->point) = it->cell;
            }
        }
        if (!snapshots.empty()) {
            ++report_.cliffLineRollbacks;
        }
        return false;
    }

    if (endPoint) {
        *endPoint = clampPoint({static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))}, map.bounds());
    }

    ZoneExpansionResult directionalExpansion;
    if (!allowBranch && !plateauAccepted && rng.randomUnit() < 0.7) {
        const Point center = clampPoint({static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))}, map.bounds());
        directionalExpansion = expandZoneDirectional(map, rng, touched, center, map.bounds(), 0.01, false, kNativeBaseMapLevel + 1);
    }

    const int raiseSide = rng.randomRanged(0, 1) == 0 ? -1 : 1;
    int raised = 0;
    for (auto& cell : map.cells()) {
        const double signedDistance = (static_cast<double>(cell.x) - x) * std::sin(angle) - (static_cast<double>(cell.y) - y) * std::cos(angle);
        if (signedDistance * raiseSide > 0.0 && cell.terrain != Terrain::Water && cell.terrain != Terrain::Shore) {
            const int oldLevel = cell.level;
            cell.height = std::max(cell.height, kNativeBaseMapLevel + 1);
            cell.level = std::max(cell.level, kNativeBaseMapLevel + 1);
            if (cell.level > oldLevel) {
                ++raised;
            }
        }
    }

    raised += directionalExpansion.acceptedCells + directionalExpansion.finalizedCells;
    report_.cliffLineRaisedCells += raised;
    ++report_.cliffLines;
    cliffLineTraceScope.result = true;
    return true;
}

bool NativeMapGenerator::placeCliffPlateau(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, Point anchor, int direction, double lineWidth, double* x, double* y)
{
    ++report_.cliffPlateauAttempts;
    const int parallel = std::clamp(static_cast<int>(std::lround(lineWidth)) + 5, 6, 18);
    constexpr int depth = 12;
    Rect rect;
    switch (direction) {
    case 0:
        rect = {anchor.x - parallel / 2, anchor.y - depth, parallel, depth};
        break;
    case 2:
        rect = {anchor.x + 1, anchor.y - parallel / 2, depth, parallel};
        break;
    case 4:
        rect = {anchor.x - parallel / 2, anchor.y + 1, parallel, depth};
        break;
    case 6:
        rect = {anchor.x - depth, anchor.y - parallel / 2, depth, parallel};
        break;
    default:
        ++report_.cliffPlateauRejectedBounds;
        return false;
    }

    const Point outward = directionOffset(direction);
    std::vector<Point> waterStrip;
    waterStrip.reserve(static_cast<std::size_t>(std::max(rect.w, rect.h)));
    if (direction == 0 || direction == 4) {
        const int waterY = direction == 0 ? rect.y - 1 : rect.y + rect.h;
        for (int px = rect.x; px < rect.x + rect.w; ++px) {
            waterStrip.push_back({px, waterY});
        }
    } else {
        const int waterX = direction == 2 ? rect.x + rect.w : rect.x - 1;
        for (int py = rect.y; py < rect.y + rect.h; ++py) {
            waterStrip.push_back({waterX, py});
        }
    }

    auto rectInsideMap = [&map](Rect candidate) {
        return candidate.x >= 0
            && candidate.y >= 0
            && candidate.x + candidate.w <= map.width()
            && candidate.y + candidate.h <= map.height();
    };

    if (!rectInsideMap(rect)) {
        ++report_.cliffPlateauRejectedBounds;
        return false;
    }
    for (Point p : waterStrip) {
        if (!map.inBounds(p)) {
            ++report_.cliffPlateauRejectedBounds;
            return false;
        }
    }

    auto isPlateauBlank = [&map](Point p) {
        const Cell& cell = map.at(p);
        return isMapgenBlankCell(cell) && !cell.spawnExclusion && !cell.cliffShoreExclusion;
    };

    for (int py = rect.y; py < rect.y + rect.h; ++py) {
        for (int px = rect.x; px < rect.x + rect.w; ++px) {
            if (!isPlateauBlank({px, py})) {
                ++report_.cliffPlateauRejectedOccupied;
                return false;
            }
        }
    }
    for (Point p : waterStrip) {
        if (!isPlateauBlank(p)) {
            ++report_.cliffPlateauRejectedOccupied;
            return false;
        }
    }

    struct Snapshot {
        Point point;
        Cell cell;
    };
    std::vector<Snapshot> snapshots;
    snapshots.reserve(static_cast<std::size_t>((rect.w + 2) * (rect.h + 2)));
    auto snapshotCell = [&](Point p) {
        if (!map.inBounds(p)) {
            return;
        }
        const auto existing = std::find_if(snapshots.begin(), snapshots.end(), [p](const Snapshot& snapshot) {
            return snapshot.point == p;
        });
        if (existing == snapshots.end()) {
            snapshots.push_back({p, map.at(p)});
        }
    };
    auto rollback = [&]() {
        for (auto it = snapshots.rbegin(); it != snapshots.rend(); ++it) {
            map.at(it->point) = it->cell;
        }
        if (!snapshots.empty()) {
            ++report_.cliffPlateauRollbacks;
        }
    };

    int raisedCells = 0;
    int cliffCells = 0;
    int waterCells = 0;
    int shoreCells = 0;
    int outwardCells = 0;
    const int plateauLevel = kNativeBaseMapLevel + 4;
    for (int py = rect.y; py < rect.y + rect.h; ++py) {
        for (int px = rect.x; px < rect.x + rect.w; ++px) {
            const Point p{px, py};
            snapshotCell(p);
            Cell& cell = map.at(p);
            const bool border = px == rect.x || py == rect.y || px == rect.x + rect.w - 1 || py == rect.y + rect.h - 1;
            cell.overlay = Overlay::None;
            cell.overlayValue = 0;
            cell.terrainObject = TerrainObject::None;
            cell.terrainObjectValue = 0;
            cell.height = plateauLevel;
            cell.level = plateauLevel;
            cell.slope = -1;
            if (border) {
                cell.terrain = Terrain::Cliff;
                ++cliffCells;
            } else {
                cell.terrain = Terrain::Land;
                ++raisedCells;
            }
        }
    }

    for (Point p : waterStrip) {
        snapshotCell(p);
        Cell& cell = map.at(p);
        cell.terrain = Terrain::Water;
        cell.overlay = Overlay::None;
        cell.overlayValue = 0;
        cell.terrainObject = TerrainObject::None;
        cell.terrainObjectValue = 0;
        cell.height = 0;
        cell.level = 0;
        cell.slope = rng.randomRanged(0, 5);
        ++waterCells;
    }

    std::vector<Point> shoreCandidates;
    for (Point p : waterStrip) {
        for (Point neighbor : map.neighbors4(p)) {
            const Cell& cell = map.at(neighbor);
            if (isSolidLand(cell)) {
                shoreCandidates.push_back(neighbor);
            }
        }
    }
    for (Point p : shoreCandidates) {
        Cell& cell = map.at(p);
        if (!isNativeBlankTile(cell)) {
            continue;
        }
        snapshotCell(p);
        if (cell.terrain != Terrain::Green) {
            ++shoreCells;
        }
        cell.terrain = Terrain::Green;
        cell.slope = -1;
    }

    std::vector<Point> plateauCells;
    plateauCells.reserve(static_cast<std::size_t>(rect.w * rect.h));
    for (int py = rect.y; py < rect.y + rect.h; ++py) {
        for (int px = rect.x; px < rect.x + rect.w; ++px) {
            plateauCells.push_back({px, py});
        }
    }
    const Rect expanded{
        std::max(0, rect.x - 2),
        std::max(0, rect.y - 2),
        std::min(map.width(), rect.x + rect.w + 2) - std::max(0, rect.x - 2),
        std::min(map.height(), rect.y + rect.h + 2) - std::max(0, rect.y - 2),
    };
    const ZoneExpansionResult outwardExpansion = expandZoneOutward(map, plateauCells, 2, expanded, kNativeBaseMapLevel + 1, false);
    outwardCells += outwardExpansion.raisedCells;

    if (raisedCells < 8 || cliffCells < 8 || waterCells <= 0) {
        rollback();
        return false;
    }

    if (x && y) {
        *x += static_cast<double>(outward.x * depth);
        *y += static_cast<double>(outward.y * depth);
    }

    report_.cliffPlateauRaisedCells += raisedCells;
    report_.cliffPlateauCliffCells += cliffCells;
    report_.cliffPlateauWaterCells += waterCells;
    report_.cliffPlateauShoreCells += shoreCells;
    report_.cliffPlateauOutwardCells += outwardCells;
    ++report_.cliffPlateauAccepted;
    return true;
}

void NativeMapGenerator::initializeHighResourceNativeWorkspace(const MapGrid& map)
{
    report_.isoTileOverrides.clear();
    const Rect mapRect = nativeSavedMapRect(map);
    const int workspaceSize = mapRect.w + mapRect.h + 1;
    report_.isoTileOverrides.reserve(static_cast<std::size_t>(workspaceSize * workspaceSize / 2));

    for (int y = 0; y < workspaceSize; ++y) {
        for (int x = 0; x < workspaceSize; ++x) {
            const Point nativeCell{x, y};
            if (!isInsideNativeCellDiamond(nativeCell, mapRect)) {
                continue;
            }

            IsoTileOverride override;
            override.iso = nativeCell;
            override.terrain = Terrain::Land;
            override.variant = 0;
            override.subTile = 0;
            override.level = kNativeBaseMapLevel;
            override.landType = 0;
            override.nativeTileTypeIndex = kNativeTileClear;
            report_.isoTileOverrides.push_back(override);
        }
    }
}

void NativeMapGenerator::synchronizeHighResourceWorkspaceToMap(MapGrid& map)
{
    std::unordered_map<std::uint64_t, const IsoTileOverride*> overrideByIso;
    overrideByIso.reserve(report_.isoTileOverrides.size());
    for (const IsoTileOverride& override : report_.isoTileOverrides) {
        overrideByIso[nativeIsoKey(override.iso)] = &override;
    }

    auto terrainForTile = [](int tile) {
        if (tile >= kNativeTileWaterBase && tile < kNativeTileWaterBase + 14) {
            return Terrain::Water;
        }
        if (tile >= kNativeTileCliffBase && tile < kNativeTileShoreBase) {
            return Terrain::Cliff;
        }
        if (tile >= kNativeTileShoreBase && tile < kNativeTileRoughBase) {
            return Terrain::Shore;
        }
        if (tile >= kNativeTileRoughBase && tile < kNativeTileRoughBase + 17) {
            return Terrain::Rough;
        }
        if (tile >= kNativeTileDirtBase && tile < kNativeTileDirtBase + 17) {
            return Terrain::Dirt;
        }
        if (tile >= nativeGreenTileBase() && tile < nativeGreenTileBase() + 17) {
            return Terrain::Green;
        }
        if (tile >= kNativeTilePaveBase && tile < kNativeTilePaveBase + 17) {
            return Terrain::Pave;
        }
        return Terrain::Land;
    };

    for (Cell& cell : map.cells()) {
        const Point nativeCell = sourcePointToNativeCell(map, {cell.x, cell.y});
        const auto it = overrideByIso.find(nativeIsoKey(nativeCell));
        if (it == overrideByIso.end()) {
            cell.terrain = Terrain::Water;
            cell.height = 0;
            cell.level = kNativeBaseMapLevel;
            cell.slope = -1;
            cell.landType = 0;
            continue;
        }

        const IsoTileOverride& override = *it->second;
        const int tile = nativeTileTypeIndexForOverride(override);
        cell.terrain = terrainForTile(tile);
        cell.height = 0;
        cell.level = std::clamp(override.level, 0, 255);
        cell.slope = -1;
        cell.landType = override.landType;
        cell.iceGrowthAllowed = override.iceGrowthAllowed;

        if (tile >= kNativeTileWaterBase && tile < kNativeTileWaterBase + 14) {
            cell.slope = tile - kNativeTileWaterBase;
        } else if (tile >= kNativeTileShoreBase && tile < kNativeTileRoughBase) {
            cell.slope = encodeShorePiece({tile - kNativeTileShoreBase, override.subTile});
        } else if (tile >= kNativeTileCliffBase && tile < kNativeTileShoreBase) {
            cell.slope = encodeCliffPiece({tile - kNativeTileCliffBase, override.subTile});
        } else if (isNativeHeightSlopeIndex(override.slopeIndex)) {
            cell.slope = override.slopeIndex;
            cell.landType = 3;
        }
    }
}

void NativeMapGenerator::synchronizeHighResourceMapToWorkspace(const MapGrid& map, bool preserveNativePieces)
{
    std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
    overrideIndexByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t i = 0; i < report_.isoTileOverrides.size(); ++i) {
        overrideIndexByIso[nativeIsoKey(report_.isoTileOverrides[i].iso)] = i;
    }

    auto tileVariant = [](const Cell& cell) {
        if (isEncodedShorePiece(cell.slope)) {
            return decodeShorePiece(cell.slope).tile;
        }
        if (isEncodedCliffPiece(cell.slope)) {
            return decodeCliffPiece(cell.slope).tile;
        }
        switch (cell.terrain) {
        case Terrain::Water:
        case Terrain::Shore:
        case Terrain::Dirt:
        case Terrain::Rough:
        case Terrain::Pave:
        case Terrain::Green:
        case Terrain::Cliff:
            return std::max(0, cell.slope);
        case Terrain::Land:
            return 0;
        }
        return 0;
    };
    auto tileSubTile = [](const Cell& cell) {
        if (isEncodedShorePiece(cell.slope)) {
            return decodeShorePiece(cell.slope).subTile;
        }
        if (isEncodedCliffPiece(cell.slope)) {
            return decodeCliffPiece(cell.slope).subTile;
        }
        return std::clamp(cell.height, 0, 255);
    };

    for (const Cell& cell : map.cells()) {
        const Point nativeCell = sourcePointToNativeCell(map, {cell.x, cell.y});
        const auto it = overrideIndexByIso.find(nativeIsoKey(nativeCell));
        if (it == overrideIndexByIso.end()) {
            continue;
        }

        IsoTileOverride& override = report_.isoTileOverrides[it->second];
        const int oldTile = nativeTileTypeIndexForOverride(override);
        const int newTile = nativeTileTypeIndexForTerrain(cell);
        const bool keepsNativePiece = preserveNativePieces
            && ((isNativeShoreTileIndex(oldTile) && isNativeShoreTileIndex(newTile))
                || (isNativeCliffRangeTileIndex(oldTile) && isNativeCliffRangeTileIndex(newTile))
                || nativePlacedIsoTileKeys_.contains(nativeIsoKey(nativeCell)));
        if (keepsNativePiece) {
            override.level = std::clamp(cell.level, 0, 255);
            override.iceGrowthAllowed = cell.iceGrowthAllowed;
            continue;
        }

        override.terrain = cell.terrain;
        override.variant = tileVariant(cell);
        override.subTile = tileSubTile(cell);
        override.level = std::clamp(cell.level, 0, 255);
        override.iceGrowthAllowed = cell.iceGrowthAllowed;
        override.landType = nativeLandTypeForCell(cell);
        override.slopeIndex = nativeSlopeIndexForCell(cell);
        override.nativeTileTypeIndex = newTile;
    }
}

bool NativeMapGenerator::recalculateHighResourceCliffLevels(
    MapGrid& map,
    NativeRandomizer& rng,
    bool includeRandomizedSmoothPasses)
{
    if (report_.isoTileOverrides.empty()) {
        return true;
    }

    std::unordered_map<std::uint64_t, std::size_t> overrideIndex;
    overrideIndex.reserve(report_.isoTileOverrides.size());
    for (std::size_t i = 0; i < report_.isoTileOverrides.size(); ++i) {
        overrideIndex[nativeIsoKey(report_.isoTileOverrides[i].iso)] = i;
    }

    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, report_.isoTileOverrides);
    if (iteratorCells.empty()) {
        return true;
    }

    auto overrideAt = [&](Point cell) -> IsoTileOverride* {
        const auto it = overrideIndex.find(nativeIsoKey(cell));
        return it == overrideIndex.end() ? nullptr : &report_.isoTileOverrides[it->second];
    };
    auto tileAt = [&](Point cell) {
        const IsoTileOverride* override = overrideAt(cell);
        return override == nullptr ? -1 : nativeTileTypeIndexForOverride(*override);
    };
    auto levelAt = [&](Point cell) {
        const IsoTileOverride* override = overrideAt(cell);
        return override == nullptr ? nativeHighResourceLevel_ : override->level;
    };
    auto isWater = [&](Point cell) {
        const int tile = tileAt(cell);
        return tile >= kNativeTileWaterBase && tile < kNativeTileWaterBase + 14;
    };
    auto isBlank = [&](Point cell) {
        const int tile = tileAt(cell);
        return tile == kNativeTileClear || tile == 0xFFFF;
    };
    auto setTile = [&](Point cell, int tile, int subTile = 0) {
        IsoTileOverride* override = overrideAt(cell);
        if (override == nullptr) {
            return false;
        }

        override->nativeTileTypeIndex = tile;
        override->slopeIndex = 0;
        if (tile >= kNativeTileWaterBase && tile < kNativeTileWaterBase + 14) {
            override->terrain = Terrain::Water;
            override->variant = tile - kNativeTileWaterBase;
            override->subTile = 0;
        } else if (tile >= kNativeTileShoreBase && tile < kNativeTileShoreBase + 42) {
            override->terrain = Terrain::Shore;
            override->variant = tile - kNativeTileShoreBase;
            override->subTile = std::max(0, subTile);
        } else if (tile >= kNativeTileCliffBase && tile < kNativeTileShoreBase) {
            override->terrain = Terrain::Cliff;
            override->variant = tile - kNativeTileCliffBase;
            override->subTile = std::max(0, subTile);
        } else {
            override->terrain = Terrain::Land;
            override->variant = 0;
            override->subTile = 0;
        }
        override->landType = nativeLandTypeForTile(override->terrain, override->variant, override->subTile);
        return true;
    };

    std::ofstream oreChainRecalculateDump;
    const char* recalculateDumpEnv = includeRandomizedSmoothPasses
        ? kNativeOreChainRecalculateDumpPathEnv
        : kNativeCliffLineRecalculateDumpPathEnv;
    if (const char* dumpPath = std::getenv(recalculateDumpEnv);
        dumpPath != nullptr && dumpPath[0] != '\0') {
        const std::filesystem::path path{dumpPath};
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::error_code ec;
        const bool writeHeader = !std::filesystem::exists(path, ec)
            || std::filesystem::file_size(path, ec) == 0;
        oreChainRecalculateDump.open(path, std::ios::binary | std::ios::app);
        if (oreChainRecalculateDump.is_open() && writeHeader) {
            oreChainRecalculateDump << "attempt,stage,index,x,y,isoTileTypeIndex,zoneId,scratchId\n";
        }
    }
    const int recalculateAttempt = includeRandomizedSmoothPasses
        ? report_.waterChains + 1
        : report_.cliffLines + 1;
    auto dumpOreChainRecalculateState = [&](const char* stage) {
        if (!oreChainRecalculateDump.is_open()) {
            return;
        }

        const char* stagePrefix = includeRandomizedSmoothPasses ? "ore_chain_" : "cliff_line_";
        std::string outputStage = stage != nullptr ? stage : "";
        const std::string expectedPrefix = includeRandomizedSmoothPasses ? "ore_chain_" : "cliff_line_";
        if (!includeRandomizedSmoothPasses && outputStage.rfind("ore_chain_", 0) == 0) {
            outputStage.erase(0, std::string("ore_chain_").size());
        }
        if (outputStage.rfind(expectedPrefix, 0) != 0) {
            outputStage = std::string(stagePrefix) + outputStage;
        }

        int index = 0;
        for (const NativeIteratorCell& nativeCell : iteratorCells) {
            const Point cell = nativeCell.cell;
            const auto zoneIt = nativeMapgenZoneIds_.find(nativeIsoKey(cell));
            const int zoneId = zoneIt == nativeMapgenZoneIds_.end()
                ? nativeMapgenDefaultZoneId_
                : zoneIt->second;
            const auto scratchIt = nativeMapgenTempTags_.find(nativeIsoKey(cell));
            const int scratchId = scratchIt == nativeMapgenTempTags_.end()
                ? nativeMapgenDefaultTempTag_
                : scratchIt->second;
            oreChainRecalculateDump << recalculateAttempt << ','
                << outputStage << ','
                << index++ << ','
                << cell.x << ','
                << cell.y << ','
                << tileAt(cell) << ','
                << zoneId << ','
                << scratchId << '\n';
        }
    };

    constexpr int kFlagNorthWest = 0x40;
    constexpr int kFlagNorth = 0x80;
    constexpr int kFlagNorthEast = 0x01;
    constexpr int kFlagWest = 0x20;
    constexpr int kFlagEast = 0x02;
    constexpr int kFlagSouthWest = 0x10;
    constexpr int kFlagSouth = 0x08;
    constexpr int kFlagSouthEast = 0x04;
    constexpr std::array<std::pair<Point, int>, 8> kWaterFlagOffsets{{
        {Point{-1, -1}, kFlagNorthWest},
        {Point{0, -1}, kFlagNorth},
        {Point{1, -1}, kFlagNorthEast},
        {Point{-1, 0}, kFlagWest},
        {Point{1, 0}, kFlagEast},
        {Point{-1, 1}, kFlagSouthWest},
        {Point{0, 1}, kFlagSouth},
        {Point{1, 1}, kFlagSouthEast},
    }};
    constexpr std::array<Point, 8> kNeighborOffsets{{
        {0, -1}, {1, -1}, {1, 0}, {1, 1},
        {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
    }};

    std::unordered_map<std::uint64_t, int> slopeFlagsCache;
    slopeFlagsCache.reserve(report_.isoTileOverrides.size());
    auto waterFlags = [&](Point cell) {
        if (overrideAt(cell) == nullptr) {
            return 0;
        }

        const bool currentIsWater = isWater(cell);
        int flags = 0;
        for (const auto& [offset, flag] : kWaterFlagOffsets) {
            const Point neighbor{cell.x + offset.x, cell.y + offset.y};
            if (isWater(neighbor) || (currentIsWater && overrideAt(neighbor) == nullptr)) {
                flags |= flag;
            }
        }
        return flags;
    };
    auto cliffFlags = [&](Point cell, int mode) {
        if (overrideAt(cell) == nullptr) {
            return 0;
        }
        if (mode == 0 && !isBlank(cell)) {
            return 0;
        }
        if (mode == 1 && isWater(cell)) {
            return 0;
        }

        const auto cached = slopeFlagsCache.find(nativeIsoKey(cell));
        return cached == slopeFlagsCache.end() ? waterFlags(cell) : cached->second;
    };
    auto invalidateNeighbors = [&](Point cell) {
        for (Point offset : kNeighborOffsets) {
            slopeFlagsCache.erase(nativeIsoKey({cell.x + offset.x, cell.y + offset.y}));
        }
    };
    auto shouldRestoreWater = [&](Point cell) {
        const int flags = cliffFlags(cell, 0);
        if (flags == 0) {
            return false;
        }

        bool forceWater = flags == 11 || flags == 26;
        forceWater = forceWater
            || ((flags & (kFlagNorth | kFlagWest)) == (kFlagNorth | kFlagWest)
                && (flags & (kFlagNorthEast | kFlagSouthWest)) == 0)
            || ((flags & (kFlagNorth | kFlagEast)) == (kFlagNorth | kFlagEast)
                && (flags & (kFlagNorthWest | kFlagSouthEast)) == 0)
            || ((flags & (kFlagSouth | kFlagEast)) == (kFlagSouth | kFlagEast)
                && (flags & (kFlagNorthEast | kFlagSouthWest)) == 0)
            || ((flags & (kFlagSouth | kFlagWest)) == (kFlagSouth | kFlagWest)
                && (flags & (kFlagNorthWest | kFlagSouthEast)) == 0);

        if ((flags & kFlagWest) != 0) {
            forceWater = forceWater
                || (cliffFlags({cell.x + 1, cell.y}, 0) & kFlagEast) != 0
                || (cliffFlags({cell.x + 2, cell.y}, 0) & kFlagEast) != 0;
        }
        if ((flags & kFlagEast) != 0) {
            forceWater = forceWater
                || (cliffFlags({cell.x - 1, cell.y}, 0) & kFlagWest) != 0
                || (cliffFlags({cell.x - 2, cell.y}, 0) & kFlagWest) != 0;
        }
        if ((flags & kFlagSouth) != 0) {
            forceWater = forceWater
                || (cliffFlags({cell.x, cell.y - 1}, 0) & kFlagNorth) != 0
                || (cliffFlags({cell.x, cell.y - 2}, 0) & kFlagNorth) != 0;
        }
        if ((flags & kFlagNorth) != 0) {
            forceWater = forceWater
                || (cliffFlags({cell.x, cell.y + 1}, 0) & kFlagSouth) != 0
                || (cliffFlags({cell.x, cell.y + 2}, 0) & kFlagSouth) != 0;
        }

        return ((flags & (kFlagNorth | kFlagSouth)) == (kFlagNorth | kFlagSouth))
            || ((flags & (kFlagEast | kFlagWest)) == (kFlagEast | kFlagWest))
            || forceWater;
    };

    dumpOreChainRecalculateState("ore_chain_before_recalc");

    std::function<void(Point)> calculateCliffLevel = [&](Point cell) {
        if (overrideAt(cell) == nullptr) {
            return;
        }

        slopeFlagsCache[nativeIsoKey(cell)] = waterFlags(cell);
        if (!shouldRestoreWater(cell) || isWater(cell)) {
            return;
        }
        if (!setTile(cell, kNativeTileWaterBase)) {
            return;
        }
        nativeMapgenZoneIds_[nativeIsoKey(cell)] = nativeHighResourceActiveZone_;
        invalidateNeighbors(cell);
        for (Point offset : kNeighborOffsets) {
            calculateCliffLevel({cell.x + offset.x, cell.y + offset.y});
        }
    };
    for (const NativeIteratorCell& nativeCell : iteratorCells) {
        calculateCliffLevel(nativeCell.cell);
    }
    dumpOreChainRecalculateState("ore_chain_after_calculate_cliff_level");

    constexpr std::array<int, 8> kClearWaterFlags{{199, 124, 241, 31, 198, 108, 177, 27}};
    for (const NativeIteratorCell& nativeCell : iteratorCells) {
        const Point cell = nativeCell.cell;
        const int tile = tileAt(cell);
        if (tile < kNativeTileWaterBase || tile >= kNativeTileWaterBase + 12) {
            continue;
        }

        const auto cached = slopeFlagsCache.find(nativeIsoKey(cell));
        const int flags = cached == slopeFlagsCache.end() ? waterFlags(cell) : cached->second;
        if (std::find(kClearWaterFlags.begin(), kClearWaterFlags.end(), flags) == kClearWaterFlags.end()) {
            continue;
        }
        if (!setTile(cell, 0xFFFF)) {
            return false;
        }

        for (Point offset : kNeighborOffsets) {
            const Point neighbor{cell.x + offset.x, cell.y + offset.y};
            if (overrideAt(neighbor) == nullptr) {
                continue;
            }
            slopeFlagsCache.erase(nativeIsoKey(neighbor));
            slopeFlagsCache[nativeIsoKey(neighbor)] = waterFlags(neighbor);
        }
    }
    dumpOreChainRecalculateState("ore_chain_after_process_cliff_cell_height");

    auto smoothRunLength = [&](Point cell, int direction) {
        int run = 1;
        Point land;
        Point water;
        Point step;
        switch (direction) {
        case kFlagSouth:
            land = {cell.x + 1, cell.y};
            water = {cell.x + 1, cell.y + 1};
            step = {1, 0};
            break;
        case kFlagEast:
            land = {cell.x, cell.y + 1};
            water = {cell.x + 1, cell.y + 1};
            step = {0, 1};
            break;
        case kFlagWest:
            land = {cell.x, cell.y + 1};
            water = {cell.x - 1, cell.y + 1};
            step = {0, 1};
            break;
        case kFlagNorth:
            land = {cell.x + 1, cell.y};
            water = {cell.x + 1, cell.y - 1};
            step = {1, 0};
            break;
        default:
            return run;
        }

        while (!isWater(land) && isWater(water)) {
            ++run;
            land = {land.x + step.x, land.y + step.y};
            water = {water.x + step.x, water.y + step.y};
        }
        return run;
    };
    auto smoothPiece = [&](Point cell, int pass, int flags, int roll) {
        if (flags == 0) {
            return 0;
        }
        if (pass == 2) {
            if ((flags & (kFlagNorth | kFlagWest)) == (kFlagNorth | kFlagWest)) {
                if ((flags & (kFlagNorthEast | kFlagSouthWest)) == (kFlagNorthEast | kFlagSouthWest)) {
                    return (roll & 1) + 23;
                }
                return (flags & kFlagNorthEast) != 0 ? 21 : 30;
            }
            if ((flags & (kFlagNorth | kFlagEast)) == (kFlagNorth | kFlagEast)) {
                if ((flags & (kFlagNorthWest | kFlagSouthEast)) == (kFlagNorthWest | kFlagSouthEast)) {
                    return (roll & 1) + 15;
                }
                return (flags & kFlagSouthEast) != 0 ? 14 : 22;
            }
            if ((flags & (kFlagSouth | kFlagEast)) == (kFlagSouth | kFlagEast)) {
                if ((flags & (kFlagNorthEast | kFlagSouthWest)) == (kFlagNorthEast | kFlagSouthWest)) {
                    return (roll & 1) + 7;
                }
                return (flags & kFlagNorthEast) != 0 ? 13 : 6;
            }
            if ((flags & (kFlagSouth | kFlagWest)) != (kFlagSouth | kFlagWest)) {
                return 0;
            }
            if ((flags & (kFlagNorthWest | kFlagSouthEast)) == (kFlagNorthWest | kFlagSouthEast)) {
                return (roll & 1) + 31;
            }
            return (flags & kFlagSouthEast) != 0 ? 5 : 29;
        }

        if ((flags & kFlagEast) != 0) {
            const int run = smoothRunLength(cell, kFlagEast);
            if (((run & 1) != 0 && (flags & kFlagNorth) == 0)
                || (flags & kFlagSouthEast) == 0
                || (flags & (kFlagSouth | kFlagSouthWest)) != 0) {
                return 12;
            }
            return roll % 3 + 9;
        }
        if ((flags & kFlagWest) != 0) {
            const int run = smoothRunLength(cell, kFlagWest);
            if (((run & 1) != 0 && (flags & kFlagNorth) == 0)
                || (flags & kFlagSouthWest) == 0
                || (flags & (kFlagSouth | kFlagSouthEast)) != 0) {
                return 28;
            }
            return roll % 3 + 25;
        }
        if ((flags & kFlagSouth) != 0) {
            const int run = smoothRunLength(cell, kFlagSouth);
            if ((run & 1) != 0
                || (flags & kFlagSouthEast) == 0
                || (flags & (kFlagNorthEast | kFlagEast)) != 0) {
                return 4;
            }
            return roll % 3 + 1;
        }
        if ((flags & kFlagNorth) != 0) {
            const int run = smoothRunLength(cell, kFlagNorth);
            if ((run & 1) != 0
                || (flags & kFlagNorthEast) == 0
                || (flags & (kFlagEast | kFlagSouthEast)) != 0) {
                return 20;
            }
            return roll % 3 + 17;
        }
        if ((flags & kFlagNorthEast) != 0) {
            return (roll & 1) + 35;
        }
        if ((flags & kFlagSouthEast) != 0) {
            return (roll & 1) + 33;
        }
        if ((flags & kFlagSouthWest) != 0) {
            return (roll & 1) + 39;
        }
        if ((flags & kFlagNorthWest) != 0) {
            return (roll & 1) + 37;
        }
        return 0;
    };
    auto templateAnchor = [](Point cell, int piece) {
        static constexpr std::array<Point, 43> kTemplateOffsets{{
            Point{0, 0}, Point{0, -1}, Point{0, -1}, Point{0, -1}, Point{0, -1},
            Point{0, -2}, Point{-1, -2}, Point{-1, -1}, Point{-1, -1}, Point{-1, 0},
            Point{-1, 0}, Point{-1, 0}, Point{-1, 0}, Point{-2, -1}, Point{-2, 0},
            Point{-1, 0}, Point{-1, 0}, Point{0, 0}, Point{0, 0}, Point{0, 0},
            Point{0, 0}, Point{0, 0}, Point{-1, 0}, Point{0, 0}, Point{0, 0},
            Point{0, 0}, Point{0, 0}, Point{0, 0}, Point{0, 0}, Point{0, -1},
            Point{0, 0}, Point{0, -1}, Point{0, -1}, Point{-1, -1}, Point{-1, -1},
            Point{-1, 0}, Point{-1, 0}, Point{0, 0}, Point{0, 0}, Point{0, -1},
            Point{0, -1}, Point{0, 0}, Point{0, 0},
        }};
        const Point offset = kTemplateOffsets[static_cast<std::size_t>(std::clamp(piece, 0, 42))];
        return Point{cell.x + offset.x, cell.y + offset.y};
    };
    auto placeSmoothTemplate = [&](Point cell, int piece, int pass) {
        const int allowedMin = pass == 1 ? kNativeTileClear : kNativeTileShoreBase;
        const int allowedMax = pass == 1 ? kNativeTileClear : kNativeTileShoreBase + 41;
        const int tile = kNativeTileShoreBase + piece - 1;
        const Point anchor = templateAnchor(cell, piece);
        const int width = shoreTemplateWidth(piece);
        const int height = shoreTemplateHeight(piece);
        const int placementLevel = levelAt(cell);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (shoreTemplateSlot(piece, x, y) == '.') {
                    continue;
                }
                const Point target{anchor.x + x, anchor.y + y};
                const int currentTile = tileAt(target);
                if (currentTile < 0) {
                    continue;
                }

                const int currentIndex = currentTile - kNativeTileShoreBase;
                const int newIndex = tile - kNativeTileShoreBase;
                const auto zoneIt = nativeMapgenZoneIds_.find(nativeIsoKey(target));
                const int targetZone = zoneIt == nativeMapgenZoneIds_.end()
                    ? nativeMapgenDefaultZoneId_
                    : zoneIt->second;
                const bool currentShore = currentIndex >= 0
                    && currentIndex < static_cast<int>(kNativePlaceCliffBandCompat.size());
                const bool blankTarget = isBlank(target);
                const int comparableTargetZone = targetZone > 0 ? targetZone : 0;
                const bool differentZone = comparableTargetZone != nativeHighResourceActiveZone_;

                if (differentZone && !blankTarget) {
                    if (!currentShore
                        || kNativePlaceCliffGroupCompat[static_cast<std::size_t>(currentIndex)]
                            != kNativePlaceCliffGroupCompat[static_cast<std::size_t>(newIndex)]) {
                        return false;
                    }
                    return true;
                }

                if (!differentZone && currentShore) {
                    const int diff = std::abs(kNativePlaceCliffBandCompat[static_cast<std::size_t>(currentIndex)]
                        - kNativePlaceCliffBandCompat[static_cast<std::size_t>(newIndex)]);
                    if (diff >= 3 && diff <= 5) {
                        return false;
                    }
                }

                const bool canWrite = blankTarget || (currentTile >= allowedMin && currentTile <= allowedMax);
                if (!canWrite) {
                    return true;
                }
                if (differentZone && blankTarget) {
                    nativeMapgenZoneIds_[nativeIsoKey(target)] = nativeHighResourceActiveZone_;
                }
                if (!setTile(target, tile, y * width + x)) {
                    return false;
                }
                overrideAt(target)->level = placementLevel;
            }
        }
        return true;
    };

    for (int pass = 1; pass <= 2; ++pass) {
        for (const NativeIteratorCell& nativeCell : iteratorCells) {
            // Native SmoothCliffCell consumes the 0..5 variant roll for every
            // iterator cell in both passes, including PlaceCliffLine's first
            // recalculation before the later ore-chain path.
            const int roll = rng.randomScaledInclusive(0, 5);
            const int flags = cliffFlags(nativeCell.cell, pass == 1 ? 0 : 1);
            const int piece = smoothPiece(nativeCell.cell, pass, flags, roll);
            if (piece > 0 && !placeSmoothTemplate(nativeCell.cell, piece, pass)) {
                return false;
            }
        }
        dumpOreChainRecalculateState(pass == 1
            ? "ore_chain_after_smooth_pass1"
            : "ore_chain_after_smooth_pass2");
    }
    return true;
}

std::vector<unsigned char> NativeMapGenerator::buildOreChainEligibilityMask(const MapGrid& map)
{
    ++report_.oreChainMaskBuilds;
    constexpr int kNativeErasePasses = 2;
    constexpr std::array<Point, 8> kNativeNeighborOrder{{
        {0, -1}, {1, -1}, {1, 0}, {1, 1},
        {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
    }};
    report_.oreChainMaskPasses += kNativeErasePasses;

    const Rect mapRect = nativeSavedMapRect(map);
    const int workspaceSize = mapRect.w + mapRect.h + 1;
    std::vector<unsigned char> allowed(static_cast<std::size_t>(workspaceSize * workspaceSize), 0);
    auto index = [workspaceSize](Point p) {
        return static_cast<std::size_t>(p.y * workspaceSize + p.x);
    };

    std::unordered_map<std::uint64_t, IsoTileOverride*> overrideByIso;
    overrideByIso.reserve(report_.isoTileOverrides.size());
    for (IsoTileOverride& override : report_.isoTileOverrides) {
        overrideByIso[nativeIsoKey(override.iso)] = &override;
    }
    auto zoneFor = [&](Point nativeCell) {
        const auto it = nativeMapgenZoneIds_.find(nativeIsoKey(nativeCell));
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };

    const std::vector<Point> firstLayer = collectNativeZoneBorderCells(map, 0);
    std::unordered_set<std::uint64_t> erasedKeys;
    erasedKeys.reserve(firstLayer.size() * 4);
    for (Point cell : firstLayer) {
        erasedKeys.insert(nativeIsoKey(cell));
    }
    for (Point borderCell : firstLayer) {
        for (Point offset : kNativeNeighborOrder) {
            const Point neighbor{borderCell.x + offset.x, borderCell.y + offset.y};
            if (isInsideNativeCellDiamond(neighbor, mapRect) && zoneFor(neighbor) == 0) {
                erasedKeys.insert(nativeIsoKey(neighbor));
            }
        }
    }

    // Origin temporarily marks both layers as zone -2 while erasing them,
    // then restores zone 0. The cleared tile state remains and excludes these
    // cells from the ore-chain eligibility mask.
    for (const std::uint64_t key : erasedKeys) {
        const auto overrideIt = overrideByIso.find(key);
        if (overrideIt == overrideByIso.end()) {
            continue;
        }
        IsoTileOverride& override = *overrideIt->second;
        override.terrain = Terrain::Land;
        override.variant = 0;
        override.subTile = 0;
        override.level = nativeHighResourceLevel_;
        override.landType = 0;
        override.slopeIndex = 0;
        override.nativeTileTypeIndex = kNativeTileClear;
    }

    for (int y = 0; y < workspaceSize; ++y) {
        for (int x = 0; x < workspaceSize; ++x) {
            const Point nativeCell{x, y};
            if (!isInsideNativeCellDiamond(nativeCell, mapRect)) {
                continue;
            }
            const auto overrideIt = overrideByIso.find(nativeIsoKey(nativeCell));
            const bool blank = overrideIt != overrideByIso.end()
                && nativeTileTypeIndexForOverride(*overrideIt->second) == kNativeTileClear;
            if (erasedKeys.contains(nativeIsoKey(nativeCell)) || !blank || zoneFor(nativeCell) != 0) {
                ++report_.oreChainMaskRejectedCells;
                continue;
            }

            allowed[index(nativeCell)] = 1;
            ++report_.oreChainMaskAllowedCells;
        }
    }

    return allowed;
}

std::vector<Point> NativeMapGenerator::collectNativeZoneBorderCells(const MapGrid& map, int zoneId) const
{
    constexpr std::array<Point, 8> kNativeNeighborOrder{{
        {0, -1}, {1, -1}, {1, 0}, {1, 1},
        {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
    }};
    const Rect mapRect = nativeSavedMapRect(map);
    const int workspaceSize = mapRect.w + mapRect.h + 1;
    auto zoneFor = [&](Point nativeCell) {
        const auto it = nativeMapgenZoneIds_.find(nativeIsoKey(nativeCell));
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };

    std::vector<Point> border;
    border.reserve(report_.isoTileOverrides.size() / 4);
    for (int y = 0; y < workspaceSize; ++y) {
        for (int x = 0; x < workspaceSize; ++x) {
            const Point nativeCell{x, y};
            if (!isInsideNativeCellDiamond(nativeCell, mapRect) || zoneFor(nativeCell) != zoneId) {
                continue;
            }
            for (Point offset : kNativeNeighborOrder) {
                const Point neighbor{nativeCell.x + offset.x, nativeCell.y + offset.y};
                if (isInsideNativeCellDiamond(neighbor, mapRect) && zoneFor(neighbor) != zoneId) {
                    border.push_back(nativeCell);
                    break;
                }
            }
        }
    }
    return border;
}

bool NativeMapGenerator::expandNativeZoneOutward(
    const MapGrid& map,
    int zoneId,
    int passes,
    Rect bounds,
    bool allowPreviousZone,
    int previousZoneBlankLevel,
    std::vector<Point>* acceptedCells)
{
    constexpr std::array<Point, 8> kNativeNeighborOrder{{
        {0, -1}, {1, -1}, {1, 0}, {1, 1},
        {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
    }};
    ++report_.zoneOutwardCalls;
    report_.zoneOutwardPasses += std::max(0, passes);

    const Rect mapRect = nativeSavedMapRect(map);
    std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
    overrideIndexByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t i = 0; i < report_.isoTileOverrides.size(); ++i) {
        overrideIndexByIso[nativeIsoKey(report_.isoTileOverrides[i].iso)] = i;
    }
    auto zoneFor = [&](Point nativeCell) {
        const auto it = nativeMapgenZoneIds_.find(nativeIsoKey(nativeCell));
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };
    auto tileFor = [&](Point nativeCell) {
        const auto it = overrideIndexByIso.find(nativeIsoKey(nativeCell));
        return it == overrideIndexByIso.end()
            ? -1
            : nativeTileTypeIndexForOverride(report_.isoTileOverrides[it->second]);
    };
    auto isBlank = [&](Point nativeCell) {
        const int tile = tileFor(nativeCell);
        return tile == kNativeTileClear || tile == 0xFFFF;
    };
    auto isWetTile = [&](Point nativeCell) {
        const int tile = tileFor(nativeCell);
        return (tile >= kNativeTileShoreBase && tile < kNativeTileRoughBase)
            || (tile >= kNativeTileWaterCliffBase && tile < kNativeTileBridgeBase)
            || (tile >= kNativeTileWaterBase && tile < kNativeTileWaterBase + 14);
    };

    std::vector<Point> border = collectNativeZoneBorderCells(map, zoneId);
    std::vector<Point> accepted;
    for (int pass = 0; pass < std::max(0, passes); ++pass) {
        report_.zoneOutwardBorderCells += static_cast<int>(border.size());
        std::vector<Point> nextBorder;
        nextBorder.reserve(border.size() * 3);
        for (Point borderCell : border) {
            for (Point offset : kNativeNeighborOrder) {
                ++report_.zoneOutwardCandidateChecks;
                const Point neighbor{borderCell.x + offset.x, borderCell.y + offset.y};
                if (!isInsideNativeCellDiamond(neighbor, mapRect) || !contains(bounds, neighbor)) {
                    ++report_.zoneOutwardRejectedCells;
                    continue;
                }

                const int neighborZone = zoneFor(neighbor);
                if (neighborZone == zoneId) {
                    continue;
                }
                const bool previousZone = allowPreviousZone && neighborZone == zoneId - 1;
                if ((!neighborZone || previousZone)
                    && (isBlank(neighbor) || (previousZone && isWetTile(neighbor)))) {
                    nativeMapgenZoneIds_[nativeIsoKey(neighbor)] = zoneId;
                    if (allowPreviousZone && isBlank(neighbor)) {
                        const auto it = overrideIndexByIso.find(nativeIsoKey(neighbor));
                        if (it != overrideIndexByIso.end()) {
                            report_.isoTileOverrides[it->second].level = previousZoneBlankLevel;
                        }
                    }
                    nextBorder.push_back(neighbor);
                    accepted.push_back(neighbor);
                    ++report_.zoneOutwardAcceptedCells;
                    continue;
                }

                ++report_.zoneOutwardRejectedCells;
                return false;
            }
        }
        border = std::move(nextBorder);
    }

    if (acceptedCells != nullptr) {
        *acceptedCells = std::move(accepted);
    }
    return true;
}

bool NativeMapGenerator::expandNativeZoneDirectional(
    const MapGrid& map,
    NativeRandomizer& rng,
    int zoneId,
    float factor,
    Rect bounds,
    Point center,
    bool finalize,
    ZoneExpansionResult* expansionResult)
{
    struct DirectionalNode {
        Point point;
        float priority = 0.0f;
    };
    class DirectionalHeap {
    public:
        explicit DirectionalHeap(int capacity)
            : capacity_(std::max(1, capacity)), nodes_(static_cast<std::size_t>(std::max(1, capacity)) + 1)
        {
        }

        bool empty() const noexcept { return count_ == 0; }

        bool push(const DirectionalNode& node) noexcept
        {
            int insertion = count_ + 1;
            if (insertion >= capacity_) {
                return false;
            }
            while (insertion > 1) {
                const int parent = insertion >> 1;
                if (nodes_[parent].priority <= node.priority) {
                    break;
                }
                nodes_[insertion] = nodes_[parent];
                insertion = parent;
            }
            nodes_[insertion] = node;
            ++count_;
            return true;
        }

        DirectionalNode pop() noexcept
        {
            const DirectionalNode result = nodes_[1];
            if (count_ == 0) {
                return result;
            }
            nodes_[1] = nodes_[count_];
            nodes_[count_] = DirectionalNode{};
            --count_;
            int current = 1;
            while (true) {
                const int left = current * 2;
                const int right = left + 1;
                int selected = current;
                if (left <= count_ && nodes_[selected].priority > nodes_[left].priority) {
                    selected = left;
                }
                if (right <= count_ && nodes_[selected].priority > nodes_[right].priority) {
                    selected = right;
                }
                if (selected == current) {
                    break;
                }
                std::swap(nodes_[current], nodes_[selected]);
                current = selected;
            }
            return result;
        }

    private:
        int capacity_ = 0;
        int count_ = 0;
        std::vector<DirectionalNode> nodes_;
    };

    ZoneExpansionResult result;
    ++report_.zoneDirectionalCalls;
    const int directionalCall = report_.zoneDirectionalCalls;
    std::ofstream directionalTrace;
    if (const char* tracePath = std::getenv("RMG_NATIVE_DIRECTIONAL_ZONE_TRACE_PATH");
        tracePath != nullptr && tracePath[0] != '\0') {
        const std::filesystem::path path{tracePath};
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::error_code ec;
        const bool writeHeader = !std::filesystem::exists(path, ec)
            || std::filesystem::file_size(path, ec) == 0;
        directionalTrace.open(path, std::ios::out | std::ios::app);
        if (directionalTrace.is_open() && writeHeader) {
            directionalTrace << "call,event,x,y,dx,dy,priority,raw,iteration\n";
        }
    }
    const Rect mapRect = nativeSavedMapRect(map);
    const int workspaceSize = mapRect.w + mapRect.h + 1;
    const int heapCapacity = std::max(100, 2 * mapRect.w * mapRect.h);
    DirectionalHeap heap(heapCapacity);

    std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
    overrideIndexByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t i = 0; i < report_.isoTileOverrides.size(); ++i) {
        overrideIndexByIso[nativeIsoKey(report_.isoTileOverrides[i].iso)] = i;
    }
    auto zoneFor = [&](Point nativeCell) {
        const auto it = nativeMapgenZoneIds_.find(nativeIsoKey(nativeCell));
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };
    auto isBlank = [&](Point nativeCell) {
        const auto it = overrideIndexByIso.find(nativeIsoKey(nativeCell));
        if (it == overrideIndexByIso.end()) {
            return false;
        }
        const int tile = nativeTileTypeIndexForOverride(report_.isoTileOverrides[it->second]);
        return tile == kNativeTileClear || tile == 0xFFFF;
    };
    auto scratchFor = [&](Point nativeCell) {
        const auto it = nativeMapgenTempTags_.find(nativeIsoKey(nativeCell));
        return it == nativeMapgenTempTags_.end() ? nativeMapgenDefaultTempTag_ : it->second;
    };

    double directionAngle = 0.0;
    if (bounds.x != 0) {
        directionAngle = 0.0;
    } else if (bounds.x + bounds.w != 512) {
        directionAngle = 3.14159265358979323846;
    }
    if (bounds.y != 0) {
        directionAngle = 4.71238898038468985769;
    } else if (bounds.y + bounds.h != 512) {
        directionAngle = 1.57079632679489661923;
    }
    auto priorityFor = [&](Point point) {
        const int dx = point.x - center.x;
        const int dy = point.y - center.y;
        double angle = dx == 0
            ? 1.57079632679489661923
            : static_cast<double>(nativeApproximateAtan(-static_cast<double>(dy) / static_cast<double>(dx)));
        if (dx < 0) {
            angle += 3.14159265358979323846;
        }
        double difference = std::fabs(angle - directionAngle);
        while (difference >= 6.28318530717958647692) {
            difference -= 6.28318530717958647692;
        }
        if (difference > 3.14159265358979323846) {
            difference = 6.28318530717958647692 - difference;
        }
        const std::uint32_t raw = rng.random();
        const double randomTerm = nativeX87Chop53Multiply(
            nativeX87Chop53Multiply(static_cast<double>(raw), kNativeUInt32Unit),
            2.0);
        const double angleTerm = nativeX87Chop53Multiply(difference, 1.5);
        const float priority = nativeDoubleToFloatChop(nativeX87Chop53Add(randomTerm, angleTerm));
        if (directionalTrace.is_open()) {
            directionalTrace << directionalCall << ",priority,"
                << point.x << ',' << point.y << ',' << dx << ',' << dy << ','
                << std::setprecision(9) << priority << ',' << raw << ",-1\n";
        }
        return priority;
    };

    nativeMapgenDefaultTempTag_ = 0;
    nativeMapgenTempTags_.clear();
    const std::vector<Point> border = collectNativeZoneBorderCells(map, zoneId);
    result.borderCells = static_cast<int>(border.size());
    report_.zoneDirectionalBorderCells += result.borderCells;
    for (Point point : border) {
        if (!contains(bounds, point)) {
            continue;
        }
        const DirectionalNode node{point, priorityFor(point)};
        if (heap.push(node)) {
            ++result.heapPushes;
            ++report_.zoneDirectionalHeapPushes;
        }
    }

    const double logBorder = std::max(1.0, std::log(static_cast<double>(border.size())));
    int target = nativeDoubleToInt(logBorder / (2.0 * static_cast<double>(factor)));
    target += rng.randomScaledInclusive(0, std::max(0, target / 2));

    constexpr std::array<Point, 4> kNativeCardinalOrder{{
        {0, -1}, {1, 0}, {0, 1}, {-1, 0},
    }};
    bool valid = true;
    int iterations = 0;
    DirectionalNode currentNode;
    auto popNextNode = [&]() {
        if (heap.empty()) {
            return false;
        }
        currentNode = heap.pop();
        ++result.heapPops;
        ++report_.zoneDirectionalHeapPops;
        return true;
    };
    bool hasCurrentNode = popNextNode();
    while (valid && hasCurrentNode && iterations < target) {
        const DirectionalNode node = currentNode;
        if (directionalTrace.is_open()) {
            directionalTrace << directionalCall << ",pop,"
                << node.point.x << ',' << node.point.y << ','
                << (node.point.x - center.x) << ',' << (node.point.y - center.y) << ','
                << std::setprecision(9) << node.priority << ",0," << iterations << '\n';
        }

        if (zoneFor(node.point) == 0 && isBlank(node.point)) {
            nativeMapgenZoneIds_[nativeIsoKey(node.point)] = zoneId;
            result.accepted.push_back(node.point);
            ++result.acceptedCells;
            ++report_.zoneDirectionalAcceptedCells;
        }

        for (Point offset : kNativeCardinalOrder) {
            ++result.candidateChecks;
            ++report_.zoneDirectionalCandidateChecks;
            const Point neighbor{node.point.x + offset.x, node.point.y + offset.y};
            if (!isInsideNativeCellDiamond(neighbor, mapRect)) {
                ++result.rejectedCells;
                ++report_.zoneDirectionalRejectedCells;
                continue;
            }

            const int neighborZone = zoneFor(neighbor);
            if (neighborZone == 0
                && scratchFor(neighbor) != zoneId
                && contains(bounds, neighbor)
                && isBlank(neighbor)) {
                const DirectionalNode candidate{neighbor, priorityFor(neighbor)};
                nativeMapgenTempTags_[nativeIsoKey(neighbor)] = zoneId;
                if (heap.push(candidate)) {
                    ++result.heapPushes;
                    ++report_.zoneDirectionalHeapPushes;
                }
                continue;
            }
            if (neighborZone != 0 && neighborZone != zoneId) {
                valid = false;
            }
            ++result.rejectedCells;
            ++report_.zoneDirectionalRejectedCells;
        }

        ++iterations;
        directionAngle += rng.gaussian() * 0.78539816339744830962;
        ++result.angleDrifts;
        ++report_.zoneDirectionalAngleDrifts;
        // The native loop prefetches the next heap node before checking whether
        // the iteration target was reached. That prefetched node is discarded
        // when the loop exits and is therefore not part of finalization.
        hasCurrentNode = popNextNode();
    }

    if (finalize) {
        while (valid && !heap.empty()) {
            const Point point = heap.pop().point;
            ++result.heapPops;
            ++report_.zoneDirectionalHeapPops;
            const int zone = zoneFor(point);
            if (zone == 0) {
                nativeMapgenZoneIds_[nativeIsoKey(point)] = zoneId;
                ++result.finalizedCells;
                ++report_.zoneDirectionalFinalizedCells;
            } else if (zone != zoneId) {
                valid = false;
            }
        }
    }

    if (expansionResult != nullptr) {
        *expansionResult = std::move(result);
    }
    (void)workspaceSize;
    return valid;
}

void NativeMapGenerator::convertNativeZoneBlankToGreen(int zoneId)
{
    for (IsoTileOverride& override : report_.isoTileOverrides) {
        const auto zoneIt = nativeMapgenZoneIds_.find(nativeIsoKey(override.iso));
        const int zone = zoneIt == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : zoneIt->second;
        const int tile = nativeTileTypeIndexForOverride(override);
        if (zone != zoneId || (tile != kNativeTileClear && tile != 0xFFFF)) {
            continue;
        }
        override.terrain = Terrain::Green;
        override.variant = 0;
        override.subTile = 0;
        override.landType = nativeLandTypeForTile(Terrain::Green, 0, 0);
        override.slopeIndex = 0;
        override.nativeTileTypeIndex = nativeGreenTileBase();
    }
}

bool NativeMapGenerator::expandNativeOreZone(const MapGrid& map, int zoneId, std::vector<Point>* acceptedCells)
{
    if (!expandNativeZoneOutward(map, zoneId, 1, {0, 0, 512, 512}, false, 0, acceptedCells)) {
        return false;
    }
    convertNativeZoneBlankToGreen(zoneId);
    return true;
}

bool NativeMapGenerator::placeOreChain(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, Point seed)
{
    ++nativeOreChainAttempt_;
    std::ofstream oreChainTrace;
    std::ofstream oreChainSeedTrace;
    if (const char* tracePath = std::getenv(kNativeOreChainTracePathEnv);
        tracePath != nullptr && tracePath[0] != '\0') {
        const std::filesystem::path path{tracePath};
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::error_code ec;
        const bool writeHeader = !std::filesystem::exists(path, ec)
            || std::filesystem::file_size(path, ec) == 0;
        oreChainTrace.open(path, std::ios::out | std::ios::app);
        if (oreChainTrace.is_open() && writeHeader) {
            oreChainTrace << "attempt,event,seedX,seedY,activeZone,rngBefore,rngAfter,budget,remaining,allowed,seedSearches,targetLength,committed,shoreCells,reason\n";
        }
    }
    const std::uint64_t oreChainRngBefore = rng.drawCount();
    if (const char* tracePath = std::getenv(kNativeOreChainSeedDumpPathEnv);
        tracePath != nullptr && tracePath[0] != '\0') {
        const std::filesystem::path path{tracePath};
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::error_code ec;
        const bool writeHeader = !std::filesystem::exists(path, ec)
            || std::filesystem::file_size(path, ec) == 0;
        oreChainSeedTrace.open(path, std::ios::out | std::ios::app);
        if (oreChainSeedTrace.is_open() && writeHeader) {
            oreChainSeedTrace << "attempt,search,candidateX,candidateY,inside,eligible,zoneId,blank,accepted\n";
        }
    }
    auto dumpOreChainTrace = [
                                 &oreChainTrace,
                                 &seed,
                                 this,
                                 oreChainRngBefore,
                                 &rng](const char* event,
                                       int budget,
                                       int remaining,
                                       int allowed,
                                       int targetLength,
                                       int committed,
                                       int shoreCells,
                                       const char* reason) {
        if (!oreChainTrace.is_open()) {
            return;
        }
        oreChainTrace << nativeOreChainAttempt_ << ','
            << (event != nullptr ? event : "") << ','
            << seed.x << ',' << seed.y << ',' << nativeHighResourceActiveZone_ << ','
            << oreChainRngBefore << ',' << rng.drawCount() << ','
            << budget << ',' << remaining << ',' << allowed << ','
            << report_.oreChainSeedSearches << ',' << targetLength << ','
            << committed << ',' << shoreCells << ','
            << (reason != nullptr ? reason : "") << '\n';
        oreChainTrace.flush();
    };
    const int chainBudget = options.waterAmount > 0
        ? nativeDoubleToInt(
            static_cast<double>(map.width())
            * static_cast<double>(map.height())
            * static_cast<double>(options.waterAmount)
            * 0.008
            + 100.0)
        : 0;
    const int remainingBudget = chainBudget - nativeHighResourcePlacedCellCount_;
    report_.oreChainBudget = std::max(report_.oreChainBudget, remainingBudget);
    if (remainingBudget <= 75) {
        dumpOreChainTrace("failed", chainBudget, remainingBudget, 0, 0, 0, 0, "budget");
        return false;
    }

    const Rect mapRect = nativeSavedMapRect(map);
    const int workspaceSize = mapRect.w + mapRect.h + 1;
    const bool searchSeed = seed.x == 0 && seed.y == 0;
    std::vector<unsigned char> eligibilityMask;
    if (searchSeed) {
        eligibilityMask = buildOreChainEligibilityMask(map);
    } else {
        eligibilityMask.assign(static_cast<std::size_t>(workspaceSize * workspaceSize), 1);
    }
    const int allowedCellCount = static_cast<int>(std::count(eligibilityMask.begin(), eligibilityMask.end(), static_cast<unsigned char>(1)));
    auto index = [workspaceSize](Point p) {
        return static_cast<std::size_t>(p.y * workspaceSize + p.x);
    };
    std::unordered_map<std::uint64_t, std::size_t> overrideIndexByIso;
    overrideIndexByIso.reserve(report_.isoTileOverrides.size());
    for (std::size_t i = 0; i < report_.isoTileOverrides.size(); ++i) {
        overrideIndexByIso[nativeIsoKey(report_.isoTileOverrides[i].iso)] = i;
    }
    auto zoneFor = [&](Point nativeCell) {
        const auto it = nativeMapgenZoneIds_.find(nativeIsoKey(nativeCell));
        return it == nativeMapgenZoneIds_.end() ? nativeMapgenDefaultZoneId_ : it->second;
    };
    auto scratchFor = [&](Point nativeCell) {
        const auto it = nativeMapgenTempTags_.find(nativeIsoKey(nativeCell));
        return it == nativeMapgenTempTags_.end() ? nativeMapgenDefaultTempTag_ : it->second;
    };
    auto isBlank = [&](Point nativeCell) {
        const auto it = overrideIndexByIso.find(nativeIsoKey(nativeCell));
        return it != overrideIndexByIso.end()
            && nativeTileTypeIndexForOverride(report_.isoTileOverrides[it->second]) == kNativeTileClear;
    };
    auto canUseOreChainCell = [&](Point nativeCell) {
        return isInsideNativeCellDiamond(nativeCell, mapRect)
            && eligibilityMask[index(nativeCell)]
            && zoneFor(nativeCell) == 0
            && isBlank(nativeCell);
    };
    auto setNativeTile = [&](Point nativeCell, Terrain terrain, int nativeTileTypeIndex) {
        const auto it = overrideIndexByIso.find(nativeIsoKey(nativeCell));
        if (it == overrideIndexByIso.end()) {
            return false;
        }
        IsoTileOverride& override = report_.isoTileOverrides[it->second];
        override.terrain = terrain;
        override.variant = 0;
        override.subTile = 0;
        override.landType = nativeLandTypeForTile(terrain, 0, 0);
        override.slopeIndex = 0;
        override.nativeTileTypeIndex = nativeTileTypeIndex;
        return true;
    };

    if (searchSeed) {
        bool found = false;
        for (int attempt = 0; attempt < 200; ++attempt) {
            ++report_.oreChainSeedSearches;
            const int displayX = nativeDoubleToInt(
                static_cast<double>(rng.random()) * static_cast<double>(mapRect.w) * kNativeUInt32Unit);
            const int displayY = nativeDoubleToInt(
                static_cast<double>(rng.random()) * static_cast<double>(mapRect.h) * kNativeUInt32Unit);
            const Point candidate{
                displayX + displayY + 1,
                mapRect.w - displayX + displayY,
            };
            const bool inside = isInsideNativeCellDiamond(candidate, mapRect);
            const bool eligible = inside && eligibilityMask[index(candidate)];
            const int zoneId = inside ? zoneFor(candidate) : nativeMapgenDefaultZoneId_;
            const bool blank = inside && isBlank(candidate);
            const bool accepted = inside && eligible && zoneId == 0 && blank;
            if (oreChainSeedTrace.is_open()) {
                oreChainSeedTrace << nativeOreChainAttempt_ << ','
                    << (attempt + 1) << ','
                    << candidate.x << ',' << candidate.y << ','
                    << static_cast<int>(inside) << ','
                    << static_cast<int>(eligible) << ','
                    << zoneId << ','
                    << static_cast<int>(blank) << ','
                    << static_cast<int>(accepted) << '\n';
            }
            if (accepted) {
                seed = candidate;
                found = true;
                break;
            }
            if (isInsideNativeCellDiamond(candidate, mapRect) && !eligibilityMask[index(candidate)]) {
                ++report_.oreChainRejectedByMask;
            }
            ++report_.oreChainRejectedCells;
        }
        if (!found) {
            dumpOreChainTrace("failed", chainBudget, remainingBudget, allowedCellCount, 0, 0, 0, "seed_search");
            return false;
        }
    } else {
        if (!canUseOreChainCell(seed)) {
            if (isInsideNativeCellDiamond(seed, mapRect) && !eligibilityMask[index(seed)]) {
                ++report_.oreChainRejectedByMask;
            }
            ++report_.oreChainRejectedCells;
            dumpOreChainTrace("failed", chainBudget, remainingBudget, allowedCellCount, 0, 0, 0, "seed_invalid");
            return false;
        }
    }

    int maxLength = std::max(76, remainingBudget);
    double deviation = static_cast<double>(remainingBudget / 6);
    double mean = static_cast<double>(remainingBudget / 3);
    if (mean - deviation > static_cast<double>(maxLength) || mean + deviation < 75.0) {
        deviation = (static_cast<double>(maxLength) - 75.0) * 0.5;
        mean = deviation + 75.0;
    }
    const int length = nativeDoubleToInt(clampGaussian(rng, mean, deviation, 75.0, static_cast<double>(maxLength)));
    report_.oreChainTargetLength = std::max(report_.oreChainTargetLength, length);
    const std::vector<IsoTileOverride> overridesBefore = report_.isoTileOverrides;
    struct OreHeapNode {
        Point point;
        float priority = 0.0f;
    };
    class NativeOreChainHeap {
    public:
        explicit NativeOreChainHeap(int capacity)
            : capacity_(capacity), nodes_(static_cast<std::size_t>(capacity) + 1)
        {
        }

        bool empty() const noexcept
        {
            return count_ == 0;
        }

        int size() const noexcept
        {
            return count_;
        }

        const OreHeapNode& top() const noexcept
        {
            return nodes_[1];
        }

        bool push(const OreHeapNode& node) noexcept
        {
            const int insertionIndex = count_ + 1;
            // Native MinHeap_Insert uses the strict condition newCount < capacity.
            if (insertionIndex >= capacity_) {
                return false;
            }

            int index = insertionIndex;
            while (index > 1) {
                const int parent = index >> 1;
                if (nodes_[parent].priority <= node.priority) {
                    break;
                }
                nodes_[index] = nodes_[parent];
                index = parent;
            }
            nodes_[index] = node;
            ++count_;
            return true;
        }

        OreHeapNode pop() noexcept
        {
            const OreHeapNode result = nodes_[1];
            if (count_ == 0) {
                return result;
            }

            nodes_[1] = nodes_[count_];
            nodes_[count_] = OreHeapNode{};
            --count_;
            siftDown(1);
            return result;
        }

    private:
        void siftDown(int index) noexcept
        {
            int left = index * 2;
            int right = left + 1;
            int selected = index;
            if (left <= count_ && nodes_[index].priority > nodes_[left].priority) {
                selected = left;
            }
            if (right <= count_ && nodes_[selected].priority > nodes_[right].priority) {
                selected = right;
            }
            if (selected == index) {
                return;
            }

            int current = index;
            do {
                std::swap(nodes_[current], nodes_[selected]);
                current = selected;
                left = current * 2;
                right = left + 1;
                selected = current;
                if (left <= count_ && nodes_[current].priority > nodes_[left].priority) {
                    selected = left;
                }
                if (right <= count_ && nodes_[selected].priority > nodes_[right].priority) {
                    selected = right;
                }
            } while (selected != current);
        }

        int capacity_ = 0;
        int count_ = 0;
        std::vector<OreHeapNode> nodes_;
    };
    const int heapCapacity = std::max(100, 2 * remainingBudget + 2);
    NativeOreChainHeap heap(heapCapacity);
    std::vector<Point> committed;
    committed.reserve(static_cast<std::size_t>(heapCapacity));
    struct OreQueueTrace {
        int index = 0;
        Point point;
        int zoneId = 0;
        int scratchId = 0;
        int oreEligible = 0;
        int isoTileTypeIndex = -1;
        std::uint32_t priorityBits = 0;
        int heapCount = 0;
    };
    std::vector<OreQueueTrace> queueTraces;
    struct OrePlacementTrace {
        int index = 0;
        const char* phase = nullptr;
        Point point;
        int zoneId = 0;
        int scratchId = 0;
        int oreEligible = 0;
        int isoTileTypeIndex = -1;
    };
    std::vector<OrePlacementTrace> placementTraces;

    const int tag = nativeHighResourceWorkspaceActive_
        ? nativeHighResourceActiveZone_
        : report_.waterChains + report_.cliffLines + 1;
    nativeMapgenDefaultTempTag_ = 0;
    nativeMapgenDefaultZoneId_ = 0;
    nativeMapgenTempTags_.clear();
    nativeMapgenTempTags_[nativeIsoKey(seed)] = tag;
    if (heap.push({seed, 0.0f})) {
        ++report_.oreChainHeapPushes;
    }

    auto dumpQueueTracesIfRequested = [&]() {
        const char* dumpPath = std::getenv(kNativeOreChainQueueDumpPathEnv);
        if (dumpPath == nullptr || dumpPath[0] == '\0') {
            return;
        }

        const std::filesystem::path path{dumpPath};
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream out(path, std::ios::binary);
        if (!out.is_open()) {
            return;
        }

        out << "index,x,y,packed,zoneId,scratchId,oreEligible,isoTileTypeIndex,priorityBits,heapCount,heapCapacity\n";
        for (const OreQueueTrace& trace : queueTraces) {
            const std::uint32_t packed = (static_cast<std::uint32_t>(static_cast<std::uint16_t>(trace.point.y)) << 16)
                | static_cast<std::uint16_t>(trace.point.x);
            out << trace.index << ','
                << trace.point.x << ','
                << trace.point.y << ','
                << packed << ','
                << trace.zoneId << ','
                << trace.scratchId << ','
                << trace.oreEligible << ','
                << trace.isoTileTypeIndex << ','
                << trace.priorityBits << ','
                << trace.heapCount << ','
                << heapCapacity << '\n';
        }
    };

    auto rollback = [&]() {
        for (IsoTileOverride& override : report_.isoTileOverrides) {
            const std::uint64_t key = nativeIsoKey(override.iso);
            const auto zoneIt = nativeMapgenZoneIds_.find(key);
            if (zoneIt == nativeMapgenZoneIds_.end() || zoneIt->second != tag) {
                continue;
            }

            zoneIt->second = 0;
            override.terrain = Terrain::Land;
            override.variant = 0;
            override.subTile = 0;
            override.level = nativeHighResourceLevel_;
            override.landType = 0;
            override.slopeIndex = 0;
            override.nativeTileTypeIndex = kNativeTileClear;
        }
        if (!committed.empty()) {
            ++report_.oreChainRollbacks;
        }
    };

    auto recordPlacement = [&](Point nativeCell, const char* phase, int zoneId) {
        const auto overrideIt = overrideIndexByIso.find(nativeIsoKey(nativeCell));
        placementTraces.push_back({
            static_cast<int>(placementTraces.size()) + 1,
            phase,
            nativeCell,
            zoneId,
            scratchFor(nativeCell),
            static_cast<int>(eligibilityMask[index(nativeCell)]),
            overrideIt == overrideIndexByIso.end()
                ? -1
                : nativeTileTypeIndexForOverride(report_.isoTileOverrides[overrideIt->second]),
        });
    };

    auto dumpPlacementTracesIfRequested = [&]() {
        const char* dumpPath = std::getenv(kNativeOreChainPlacementDumpPathEnv);
        if (dumpPath == nullptr || dumpPath[0] == '\0') {
            return;
        }

        const std::filesystem::path path{dumpPath};
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream out(path, std::ios::binary);
        if (!out.is_open()) {
            return;
        }

        out << "index,phase,x,y,packed,zoneId,scratchId,oreEligible,isoTileTypeIndex\n";
        for (const OrePlacementTrace& trace : placementTraces) {
            const std::uint32_t packed = (static_cast<std::uint32_t>(static_cast<std::uint16_t>(trace.point.y)) << 16)
                | static_cast<std::uint16_t>(trace.point.x);
            out << trace.index << ','
                << (trace.phase != nullptr ? trace.phase : "") << ','
                << trace.point.x << ','
                << trace.point.y << ','
                << packed << ','
                << trace.zoneId << ','
                << trace.scratchId << ','
                << trace.oreEligible << ','
                << trace.isoTileTypeIndex << '\n';
        }
    };

    auto commitWater = [&](Point nativeCell) {
        nativeMapgenZoneIds_[nativeIsoKey(nativeCell)] = tag;
        if (!setNativeTile(nativeCell, Terrain::Water, kNativeTileWaterBase)) {
            return false;
        }
        committed.push_back(nativeCell);
        return true;
    };

    auto pushCandidate = [&](Point p, float priority) {
        // The native code writes ScratchId before checking its strict heap bound.
        // A full heap therefore leaves this cell tagged but does not enqueue it.
        nativeMapgenTempTags_[nativeIsoKey(p)] = tag;
        const auto overrideIt = overrideIndexByIso.find(nativeIsoKey(p));
        std::uint32_t priorityBits = 0;
        static_assert(sizeof(priorityBits) == sizeof(priority));
        std::memcpy(&priorityBits, &priority, sizeof(priorityBits));
        queueTraces.push_back({
            static_cast<int>(queueTraces.size()) + 1,
            p,
            zoneFor(p),
            scratchFor(p),
            static_cast<int>(eligibilityMask[index(p)]),
            overrideIt == overrideIndexByIso.end()
                ? -1
                : nativeTileTypeIndexForOverride(report_.isoTileOverrides[overrideIt->second]),
            priorityBits,
            static_cast<int>(heap.size()),
        });
        if (static_cast<int>(heap.size()) + 1 >= heapCapacity) {
            ++report_.oreChainRejectedCells;
            return;
        }
        if (heap.push({p, priority})) {
            ++report_.oreChainHeapPushes;
        }
    };

    constexpr std::array<Point, 4> kNativeCardinalOrder{{
        {0, -1}, {1, 0}, {0, 1}, {-1, 0},
    }};
    bool failed = false;
    while (!heap.empty() && static_cast<int>(committed.size()) < length && !failed) {
        Point p = heap.top().point;
        heap.pop();
        ++report_.oreChainHeapPops;
        recordPlacement(p, "main_commit", tag);
        if (!commitWater(p)) {
            failed = true;
            break;
        }

        for (Point offset : kNativeCardinalOrder) {
            const Point next{p.x + offset.x, p.y + offset.y};
            ++report_.oreChainCandidateChecks;
            if (!isInsideNativeCellDiamond(next, mapRect)) {
                ++report_.oreChainRejectedCells;
                continue;
            }
            const int neighborZone = zoneFor(next);
            if (neighborZone != 0 || scratchFor(next) == tag || !isBlank(next) || !eligibilityMask[index(next)]) {
                if (!eligibilityMask[index(next)]) {
                    ++report_.oreChainRejectedByMask;
                }
                ++report_.oreChainRejectedCells;
                if (neighborZone != 0 && neighborZone != tag) {
                    failed = true;
                }
                continue;
            }
            const int dx = seed.x - next.x;
            const int dy = seed.y - next.y;
            const float distance = nativeOreChainSqrt(static_cast<double>(dx * dx + dy * dy));
            const float priority = nativeOreChainPriority(
                rng.random(),
                distance,
                static_cast<int>(committed.size() - 1));
            pushCandidate(next, priority);
        }
    }

    // The native loop pops the next node before checking targetLength. When the
    // target is reached (or expansion fails), that node is discarded rather
    // than handed to the tail-validation loop.
    if (!heap.empty() && (failed || static_cast<int>(committed.size()) >= length)) {
        heap.pop();
        ++report_.oreChainHeapPops;
    }

    while (!heap.empty() && !failed) {
        Point p = heap.top().point;
        heap.pop();
        ++report_.oreChainHeapPops;
        if (!canUseOreChainCell(p)) {
            failed = true;
        } else {
            recordPlacement(p, "tail_check", zoneFor(p));
            if (!commitWater(p)) {
                failed = true;
            }
        }
    }

    dumpPlacementTracesIfRequested();

    if (failed || static_cast<int>(committed.size()) <= 75 || static_cast<int>(committed.size()) <= length / 4) {
        rollback();
        dumpOreChainTrace("failed", chainBudget, remainingBudget, allowedCellCount, length, static_cast<int>(committed.size()), 0, failed ? "chain_failed" : "short_chain");
        return false;
    }

    std::vector<Point> expandedCells;
    if (searchSeed) {
        dumpOreChainTrace(
            "before_recalculate",
            chainBudget,
            remainingBudget,
            allowedCellCount,
            length,
            static_cast<int>(committed.size()),
            0,
            "");
        if (!recalculateHighResourceCliffLevels(map, rng, true)) {
            rollback();
            dumpOreChainTrace("failed", chainBudget, remainingBudget, allowedCellCount, length, static_cast<int>(committed.size()), 0, "recalculate_failed");
            return false;
        }

        for (std::size_t i = 0; i < report_.isoTileOverrides.size() && i < overridesBefore.size(); ++i) {
            const IsoTileOverride& before = overridesBefore[i];
            const IsoTileOverride& after = report_.isoTileOverrides[i];
            if (before.iso.x == after.iso.x
                && before.iso.y == after.iso.y
                && nativeTileTypeIndexForOverride(before) != nativeTileTypeIndexForOverride(after)) {
                nativeMapgenZoneIds_[nativeIsoKey(after.iso)] = tag;
            }
        }

        if (!expandNativeOreZone(map, tag, &expandedCells)) {
            rollback();
            dumpOreChainTrace("failed", chainBudget, remainingBudget, allowedCellCount, length, static_cast<int>(committed.size()), static_cast<int>(expandedCells.size()), "expand_failed");
            return false;
        }
    }

    dumpQueueTracesIfRequested();

    // This mirrors the native ore-chain hook's full mapgen workspace dump.  The
    // visible MapGrid has only the canonical projection, so it cannot expose
    // tile/zone differences which later change ProcessAllRegions behavior.
    const char* oreChainDumpPath = std::getenv(kNativeOreChainDumpPathEnv);
    if (oreChainDumpPath != nullptr && oreChainDumpPath[0] != '\0') {
        const std::filesystem::path path{oreChainDumpPath};
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream out(path, std::ios::binary);
        if (out.is_open()) {
            out << "attempt,index,x,y,packed,zoneId,eligible,isoTileTypeIndex\n";
            const int attempt = report_.waterChains + 1;
            int cellIndex = 0;
            for (int y = 0; y < workspaceSize; ++y) {
                for (int x = 0; x < workspaceSize; ++x) {
                    const Point nativeCell{x, y};
                    if (!isInsideNativeCellDiamond(nativeCell, mapRect)) {
                        continue;
                    }
                    const auto overrideIt = overrideIndexByIso.find(nativeIsoKey(nativeCell));
                    const int tile = overrideIt == overrideIndexByIso.end()
                        ? -1
                        : nativeTileTypeIndexForOverride(report_.isoTileOverrides[overrideIt->second]);
                    const std::uint32_t packed = (static_cast<std::uint32_t>(static_cast<std::uint16_t>(y)) << 16)
                        | static_cast<std::uint16_t>(x);
                    out << attempt << ','
                        << cellIndex++ << ','
                        << x << ','
                        << y << ','
                        << packed << ','
                        << zoneFor(nativeCell) << ','
                        << static_cast<int>(eligibilityMask[index(nativeCell)]) << ','
                        << tile << '\n';
                }
            }
        }
    }

    nativeHighResourcePlacedCellCount_ += static_cast<int>(committed.size());
    report_.oreChainCommittedCells += static_cast<int>(committed.size());
    report_.oreChainShoreCells += static_cast<int>(expandedCells.size());
    ++report_.waterChains;
    dumpOreChainTrace("success", chainBudget, remainingBudget, allowedCellCount, length, static_cast<int>(committed.size()), static_cast<int>(expandedCells.size()), "");
    return true;
}
