std::vector<Rect> NativeMapGenerator::subdivideRegion(Rect region, int requestedCount, NativeRandomizer& rng)
{
    const bool traceEnabled = native_trace::coreFunctionEnabled(native_trace::SubdivideRegionIntoRects);
    native_trace::EventData traceData;
    if (traceEnabled) {
        traceData.args[0] = static_cast<std::uint32_t>(requestedCount);
        traceData.args[1] = static_cast<std::uint32_t>(region.x);
        traceData.args[2] = static_cast<std::uint32_t>(region.y);
        traceData.args[3] = static_cast<std::uint32_t>(region.w);
        traceData.args[4] = static_cast<std::uint32_t>(region.h);
        traceData.regionCellCount = region.w * region.h;
        traceData.resultKind = native_trace::ResultNone;
        native_trace::enterDetailed(0x59A8F0, native_trace::SubdivideRegionIntoRects, traceData);
    }
    requestedCount = std::max(1, requestedCount);
    const int root = nativeDoubleToInt(nativePatchedSqrt(static_cast<double>(requestedCount)));
    int columns = root * root == requestedCount ? root : root + 1;
    int rows = (root * columns < requestedCount) ? columns : root;
    columns = std::max(1, columns);
    rows = std::max(1, rows);

    int cellWidth = region.w / columns;
    int cellHeight = region.h / columns;
    int xStep = 0;
    int yStep = 0;
    int rowXStep = 0;
    int rowYStep = 0;
    if (rng.randomUnit() >= 0.5) {
        xStep = cellWidth;
        cellHeight = region.h / rows;
        rowYStep = cellHeight;
    } else {
        yStep = cellHeight;
        cellWidth = region.w / rows;
        rowXStep = cellWidth;
    }

    const int totalSlots = columns * rows;
    const int emptySlots = std::max(0, totalSlots - requestedCount);
    std::vector<int> rowCounts(static_cast<std::size_t>(rows), columns);
    std::vector<int> selectableRows;
    selectableRows.reserve(static_cast<std::size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        selectableRows.push_back(row);
    }
    for (int i = 0; i < emptySlots; ++i) {
        if (selectableRows.empty()) {
            break;
        }

        const int index = rng.randomScaledInclusive(0, static_cast<int>(selectableRows.size()) - 1);
        rowCounts[static_cast<std::size_t>(selectableRows[static_cast<std::size_t>(index)])] = root;
        selectableRows.erase(selectableRows.begin() + index);
    }

    std::vector<Rect> rects;
    rects.reserve(static_cast<std::size_t>(requestedCount));
    int baseX = region.x;
    int baseY = region.y;
    for (int row = 0; row < rows; ++row) {
        int x = baseX;
        int y = baseY;
        const int count = rowCounts[static_cast<std::size_t>(row)];
        if (count < columns) {
            if (xStep <= 0) {
                y += cellHeight / 2;
            } else {
                x += cellWidth / 2;
            }
        }

        for (int column = 0; column < count; ++column) {
            rects.push_back({x + 2, y + 2, cellWidth - 4, cellHeight - 4});
            x += xStep;
            y += yStep;
        }

        baseX += rowXStep;
        baseY += rowYStep;
        if (xStep <= 0) {
            baseY = region.y;
        } else {
            baseX = region.x;
        }
    }
    if (traceEnabled) {
        std::uint64_t hash = kFnvOffset;
        std::vector<std::uint32_t> detail;
        if (native_trace::detailRequested()) {
            detail.reserve(rects.size() * 4);
        }
        for (const Rect& rect : rects) {
            for (int value : {rect.x, rect.y, rect.w, rect.h}) {
                hashI32(hash, value);
                if (native_trace::detailRequested()) {
                    detail.push_back(static_cast<std::uint32_t>(value));
                }
            }
        }
        traceData.result[0] = static_cast<std::uint32_t>(rects.size());
        traceData.result[1] = static_cast<std::uint32_t>(hash);
        traceData.result[2] = static_cast<std::uint32_t>(hash >> 32);
        traceData.result[3] = rects.empty()
            ? 0u
            : static_cast<std::uint32_t>(static_cast<std::uint16_t>(rects.back().x))
                | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(rects.back().y)) << 16);
        traceData.resultKind = native_trace::ResultLegacy;
        native_trace::exitDetailed(
            0x59A8F0,
            native_trace::SubdivideRegionIntoRects,
            traceData,
            native_trace::DetailOrderedRects,
            detail);
    }
    return rects;
}

int NativeMapGenerator::floodFillTerrainPatchNativeCellWorkspace(MapGrid& map, NativeRandomizer& rng, const FloodOptions& options, int tag)
{
    ++report_.floodFillAttempts;

    int target = std::max(1, options.targetCells);
    if (target <= options.minimumTargetCells) {
        target = options.minimumTargetCells;
        ++report_.floodFillMinimumClamps;
    }
    Point seed = clampPoint(options.seed, map.bounds());
    const int nativeMapWidth = options.nativeMapWidth > 0 ? options.nativeMapWidth : map.width();
    if (options.seed.x == 0 && options.seed.y == 0) {
        bool foundWetSeed = false;
        for (int attempts = 0; attempts < 200; ++attempts) {
            const Point candidate{rng.randomRanged(0, map.width() - 1), rng.randomRanged(0, map.height() - 1)};
            if (isWet(map.at(candidate)) && contains(options.region, offsetPoint(candidate, options.mapOffset))) {
                seed = candidate;
                foundWetSeed = true;
                break;
            }
        }
        if (!foundWetSeed) {
            ++report_.floodFillFailures;
            return 0;
        }
    }

    auto sourceFromNativeCell = [&](Point nativeCell) {
        const Point mapPoint = nativeCellToMapPoint(nativeCell, nativeMapWidth);
        return Point{mapPoint.x - options.mapOffset.x, mapPoint.y - options.mapOffset.y};
    };
    auto isWetNativeCell = [&](Point nativeCell) {
        const Point source = sourceFromNativeCell(nativeCell);
        return map.inBounds(source) && isWet(map.at(source));
    };
    auto commitNativeCell = [&](Point nativeCell) {
        const Point source = sourceFromNativeCell(nativeCell);
        if (!map.inBounds(source)) {
            return false;
        }
        Cell& cell = map.at(source);
        if (!isWet(cell)) {
            return false;
        }
        cell.terrain = Terrain::Land;
        cell.tempTag = tag;
        return true;
    };

    static constexpr std::array<Point, 4> kNativeCardinalOffsets{
        Point{0, -1},
        Point{1, 0},
        Point{0, 1},
        Point{-1, 0},
    };

    const Point seedNative = toNativeCellCoords(offsetPoint(seed, options.mapOffset), nativeMapWidth);
    const Point anchorNative = toNativeCellCoords(offsetPoint(options.hasDistanceAnchor ? options.distanceAnchor : seed, options.mapOffset), nativeMapWidth);
    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> heap;
    std::unordered_map<std::uint64_t, bool> queuedNative;
    queuedNative.reserve(static_cast<std::size_t>(std::max(100, target * 8 + 2)));
    std::vector<Point> committed;
    committed.reserve(static_cast<std::size_t>(target));
    const int maxQueued = std::max(100, target * 8 + 2);
    int queued = 1;

    queuedNative[nativeIsoKey(seedNative)] = true;
    heap.push({seedNative, 0.0});

    double noiseX = 0.0;
    double noiseY = 0.0;
    int guard = 0;
    int processed = 0;
    while (!heap.empty() && static_cast<int>(committed.size()) < target && guard < target * 16) {
        ++guard;
        const HeapNode node = heap.top();
        heap.pop();
        ++processed;

        if (commitNativeCell(node.point)) {
            committed.push_back(node.point);
        }

        for (Point offset : kNativeCardinalOffsets) {
            if (queued >= maxQueued) {
                continue;
            }

            const Point next{node.point.x + offset.x, node.point.y + offset.y};
            const std::uint64_t key = nativeIsoKey(next);
            if (queuedNative.contains(key) || !isWetNativeCell(next)) {
                continue;
            }

            bool inside = options.ellipseRegion
                ? isInsideNativeEllipseCell(next, options.region, nativeMapWidth)
                : contains(options.region, nativeCellToMapPoint(next, nativeMapWidth));
            if (!inside) {
                continue;
            }

            const Point noisyNative{
                static_cast<int>(std::lround(static_cast<double>(seedNative.x) + noiseX)),
                static_cast<int>(std::lround(static_cast<double>(seedNative.y) + noiseY)),
            };
            double priority = options.distanceToRegion
                ? nativeRegionDistanceCells(noisyNative, next, anchorNative, options.region)
                : euclidean(sourceFromNativeCell(next), seed);
            priority += rng.randomUnit() * 2.5;
            heap.push({next, priority});
            ++queued;
            queuedNative[key] = true;
        }

        noiseX += rng.gaussian() * options.noiseScale;
        noiseY += rng.gaussian() * options.noiseScale;
    }

    if (options.commitFringe) {
        while (!heap.empty()) {
            const Point point = heap.top().point;
            heap.pop();
            ++processed;
            if (commitNativeCell(point)) {
                committed.push_back(point);
                ++report_.floodFillFringeCells;
            }
        }
    }

    if (committed.empty()) {
        ++report_.floodFillFailures;
        clearTags(map, tag);
        return 0;
    }

    ++report_.terrainPatches;
    return processed;
}

int NativeMapGenerator::floodFillTerrainPatch(MapGrid& map, NativeRandomizer& rng, const FloodOptions& options, int tag)
{
    if (options.nativeCellWorkspace) {
        return floodFillTerrainPatchNativeCellWorkspace(map, rng, options, tag);
    }

    ++report_.floodFillAttempts;

    int target = std::max(1, options.targetCells);
    if (target <= options.minimumTargetCells) {
        target = options.minimumTargetCells;
        ++report_.floodFillMinimumClamps;
    }
    Point seed = clampPoint(options.seed, map.bounds());
    const int nativeMapWidth = options.nativeMapWidth > 0 ? options.nativeMapWidth : map.width();
    if (options.seed.x == 0 && options.seed.y == 0) {
        bool foundWetSeed = false;
        for (int attempts = 0; attempts < 200; ++attempts) {
            const Point candidate{rng.randomRanged(0, map.width() - 1), rng.randomRanged(0, map.height() - 1)};
            if (isWet(map.at(candidate)) && contains(options.region, offsetPoint(candidate, options.mapOffset))) {
                seed = candidate;
                foundWetSeed = true;
                break;
            }
        }
        if (!foundWetSeed) {
            ++report_.floodFillFailures;
            return 0;
        }
    }

    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> heap;
    std::vector<Point> committed;
    committed.reserve(static_cast<std::size_t>(target));
    const int maxQueued = std::max(100, target * 8 + 2);
    int queued = 1;

    map.at(seed).tempTag = tag;
    heap.push({seed, 0.0});

    double noiseX = 0.0;
    double noiseY = 0.0;
    int guard = 0;
    int processed = 0;
    while (!heap.empty() && static_cast<int>(committed.size()) < target && guard < target * 16) {
        ++guard;
        const HeapNode node = heap.top();
        heap.pop();
        ++processed;

        Cell& cell = map.at(node.point);
        cell.terrain = Terrain::Land;
        cell.tempTag = tag;
        committed.push_back(node.point);

        const std::vector<Point> neighbors = options.nativeCellCardinalNeighbors
            ? nativeCellCardinalSourceNeighbors(map, node.point)
            : map.neighbors8(node.point);
        for (const Point next : neighbors) {
            if (queued >= maxQueued) {
                continue;
            }
            Cell& neighbor = map.at(next);
            if (neighbor.tempTag == tag || !isWet(neighbor)) {
                continue;
            }

            bool inside = options.ellipseRegion
                ? isInsideNativeEllipse(next, options.region, nativeMapWidth, options.mapOffset)
                : contains(options.region, offsetPoint(next, options.mapOffset));
            if (!inside) {
                continue;
            }

            const Point noisyCenter{
                static_cast<int>(std::lround(static_cast<double>(seed.x) + noiseX)),
                static_cast<int>(std::lround(static_cast<double>(seed.y) + noiseY)),
            };
            const Point regionAnchor = options.hasDistanceAnchor ? options.distanceAnchor : seed;
            double priority = options.distanceToRegion
                ? nativeRegionDistance(noisyCenter, next, regionAnchor, options.region, nativeMapWidth, options.mapOffset)
                : euclidean(next, noisyCenter);
            priority += rng.randomUnit() * 2.5;
            heap.push({next, priority});
            ++queued;
            neighbor.tempTag = tag;
        }

        noiseX += rng.gaussian() * options.noiseScale;
        noiseY += rng.gaussian() * options.noiseScale;
    }

    if (options.commitFringe) {
        while (!heap.empty()) {
            const Point point = heap.top().point;
            heap.pop();
            ++processed;
            Cell& cell = map.at(point);
            if (isWet(cell)) {
                cell.terrain = Terrain::Land;
                cell.tempTag = tag;
                committed.push_back(point);
                ++report_.floodFillFringeCells;
            }
        }
    }

    if (committed.empty()) {
        ++report_.floodFillFailures;
        clearTags(map, tag);
        return 0;
    }

    ++report_.terrainPatches;
    return processed;
}
