bool NativeMapGenerator::isDecorationCandidateCell(const MapGrid& map, Point p, bool allowExistingPaving) const
{
    if (!map.inBounds(p)) {
        return false;
    }

    const Cell& cell = map.at(p);
    if (!isPassable(cell) || cell.spawnExclusion || cell.cliffShoreExclusion || cell.terrain == Terrain::Shore || cell.terrain == Terrain::Cliff) {
        return false;
    }
    if (cell.overlay != Overlay::None) {
        return false;
    }
    if (!allowExistingPaving && cell.terrain == Terrain::Pave) {
        return false;
    }
    return true;
}

bool NativeMapGenerator::isPavedAndEmptyRect(const MapGrid& map, Point topLeft, int width, int height) const
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    for (int y = topLeft.y; y < topLeft.y + height; ++y) {
        for (int x = topLeft.x; x < topLeft.x + width; ++x) {
            const Point p{x, y};
            if (!map.inBounds(p)) {
                return false;
            }
            const Cell& cell = map.at(p);
            if (cell.overlay != Overlay::None || cell.terrainObject != TerrainObject::None || cell.spawnExclusion || cell.cliffShoreExclusion) {
                return false;
            }
            if (cell.terrain != Terrain::Pave) {
                return false;
            }
        }
    }
    return true;
}

bool NativeMapGenerator::canPlaceDecorationFootprint(const MapGrid& map, Point anchor, int width, int height, bool requirePave, bool allowExistingPaving, int requiredLevel)
{
    ++report_.decorationFootprintChecks;
    if (width <= 0 || height <= 0) {
        ++report_.decorationFootprintFailures;
        ++report_.decorationFootprintRejectedBounds;
        return false;
    }

    for (int y = anchor.y; y < anchor.y + height; ++y) {
        for (int x = anchor.x; x < anchor.x + width; ++x) {
            const Point p{x, y};
            if (!map.inBounds(p)) {
                ++report_.decorationFootprintFailures;
                ++report_.decorationFootprintRejectedBounds;
                return false;
            }
            const Cell& cell = map.at(p);
            if (cell.spawnExclusion || cell.cliffShoreExclusion) {
                ++report_.decorationFootprintFailures;
                ++report_.decorationFootprintRejectedExclusion;
                return false;
            }
            if (cell.overlay != Overlay::None || cell.terrainObject != TerrainObject::None) {
                ++report_.decorationFootprintFailures;
                ++report_.decorationFootprintRejectedOccupied;
                return false;
            }
            if (cell.terrain == Terrain::Rough || cell.terrain == Terrain::Cliff) {
                ++report_.decorationFootprintFailures;
                ++report_.decorationFootprintRejectedRock;
                return false;
            }
            if (requiredLevel >= 0 && cell.level != requiredLevel) {
                ++report_.decorationFootprintFailures;
                ++report_.decorationFootprintRejectedLevel;
                return false;
            }
            const bool terrainOk = requirePave
                ? cell.terrain == Terrain::Pave
                : (allowExistingPaving ? isPassable(cell) : isNativeBlankTile(cell));
            if (!terrainOk || (!allowExistingPaving && cell.terrain == Terrain::Pave)) {
                ++report_.decorationFootprintFailures;
                ++report_.decorationFootprintRejectedTerrain;
                return false;
            }
        }
    }
    return true;
}

NativeMapGenerator::UrbanRegion NativeMapGenerator::createUrbanRegion(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, Point center, double targetScale)
{
    UrbanRegion urban;
    urban.center = center;
    urban.bounds = {center.x, center.y, 1, 1};
    if (!isDecorationCandidateCell(map, center, true) || map.at(center).urbanRegionMark) {
        ++report_.urbanRegionRejected;
        return urban;
    }

    double mean = static_cast<double>(2 * map.width() * map.height()) / 30.0;
    double deviation = 300.0;
    if (mean - deviation > 2000.0 || mean + deviation < 100.0) {
        mean = 1050.0;
        deviation = 950.0;
    }
    const int targetCells = std::clamp(
        static_cast<int>(std::lround(clampGaussian(rng, mean, deviation, 100.0, 2000.0) * std::clamp(targetScale, 0.25, 1.0))),
        100,
        2000);
    const int tag = 700000 + report_.urbanRegions + 1;

    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> heap;
    map.at(center).tempTag = tag;
    heap.push({center, 0.0});
    ++report_.urbanRegionHeapPushes;

    while (!heap.empty() && static_cast<int>(urban.cells.size()) < targetCells) {
        const Point p = heap.top().point;
        heap.pop();
        ++report_.urbanRegionHeapPops;

        if (!isDecorationCandidateCell(map, p, true) || map.at(p).urbanRegionMark) {
            continue;
        }

        urban.cells.push_back(p);
        for (Point next : map.neighbors8(p)) {
            Cell& candidate = map.at(next);
            if (candidate.tempTag == tag || candidate.urbanRegionMark || !isDecorationCandidateCell(map, next, true)) {
                continue;
            }

            candidate.tempTag = tag;
            const int chebyshev = std::max(std::abs(next.x - center.x), std::abs(next.y - center.y));
            heap.push({next, static_cast<double>(chebyshev) + rng.randomUnit() * 3.0});
            ++report_.urbanRegionHeapPushes;
        }
    }

    clearTags(map, tag);
    if (static_cast<int>(urban.cells.size()) <= 100) {
        ++report_.urbanRegionRejected;
        ++report_.urbanRegionRejectedSmall;
        urban.cells.clear();
        return urban;
    }

    int minX = map.width();
    int minY = map.height();
    int maxX = 0;
    int maxY = 0;
    for (Point p : urban.cells) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }
    urban.bounds = {minX, minY, maxX - minX + 1, maxY - minY + 1};
    const int boundsArea = std::max(1, urban.bounds.w * urban.bounds.h);
    const int densityNumerator = targetScale < 1.0 ? 1 : 3;
    const int densityDenominator = targetScale < 1.0 ? 2 : 5;
    if (static_cast<int>(urban.cells.size()) * densityDenominator <= boundsArea * densityNumerator) {
        ++report_.urbanRegionRejected;
        ++report_.urbanRegionRejectedSparse;
        urban.cells.clear();
        return urban;
    }

    const int regionTag = 800000 + report_.urbanRegions + 1;
    for (Point p : urban.cells) {
        Cell& cell = map.at(p);
        cell.tempTag = regionTag;
        if (cell.terrain != Terrain::Pave) {
            cell.terrain = Terrain::Pave;
            ++report_.urbanPavedCells;
        }
        ++report_.urbanRegionAnchorPavedCells;
    }
    std::vector<Point> compact;
    compact.reserve(urban.cells.size());
    for (Point p : urban.cells) {
        bool boundary = false;
        for (Point next : map.neighbors8(p)) {
            if (map.at(next).tempTag != regionTag) {
                boundary = true;
                break;
            }
        }
        if (boundary) {
            urban.boundaryCells.push_back(p);
        } else {
            compact.push_back(p);
        }
    }
    urban.cells = std::move(compact);
    for (Point p : urban.cells) {
        Cell& cell = map.at(p);
        if (!cell.urbanRegionMark) {
            cell.urbanRegionMark = true;
            ++report_.urbanRegionMarkedCells;
        }
    }
    for (Point p : urban.boundaryCells) {
        Cell& cell = map.at(p);
        if (cell.tempTag == regionTag) {
            cell.tempTag = 0;
        }
    }

    const int initialPaveAttempts = static_cast<int>(urban.cells.size()) / 3;
    for (int attempt = 0; attempt < initialPaveAttempts; ++attempt) {
        ++report_.urbanRegionInitialPaveAttempts;
        const Point base = urban.cells[static_cast<std::size_t>(rng.randomRanged(0, static_cast<int>(urban.cells.size()) - 1))];
        const int variant = rng.randomRanged(0, 6);
        int coveredCells = 1;
        bool canPlace = true;

        if (variant > 3) {
            coveredCells = 4;
            for (int y = base.y; y < base.y + 2; ++y) {
                for (int x = base.x; x < base.x + 2; ++x) {
                    const Point p{x, y};
                    if (!map.inBounds(p)) {
                        canPlace = false;
                        continue;
                    }
                    const Cell& cell = map.at(p);
                    if (cell.tempTag != regionTag || cell.terrain != Terrain::Pave || cell.overlay != Overlay::None || cell.terrainObject != TerrainObject::None) {
                        canPlace = false;
                    }
                }
            }
        }

        if (!canPlace) {
            continue;
        }

        if (variant > 3) {
            for (int y = base.y; y < base.y + 2; ++y) {
                for (int x = base.x; x < base.x + 2; ++x) {
                    map.at({x, y}).tempTag = 0;
                }
            }
            ++report_.urbanRegionInitialPave2x2;
        } else if (map.inBounds(base)) {
            map.at(base).tempTag = 0;
        }
        report_.urbanRegionInitialPavedCells += coveredCells;
    }
    for (Point p : urban.cells) {
        Cell& cell = map.at(p);
        if (cell.tempTag == regionTag) {
            cell.tempTag = 0;
        }
    }

    report_.urbanRegionCells += static_cast<int>(urban.cells.size());
    report_.urbanRegionBoundaryCells += static_cast<int>(urban.boundaryCells.size());
    return urban;
}

void NativeMapGenerator::stampDecorationFootprint(MapGrid& map, Point anchor, int width, int height, Terrain terrain)
{
    for (int y = anchor.y; y < anchor.y + height; ++y) {
        for (int x = anchor.x; x < anchor.x + width; ++x) {
            if (!map.inBounds({x, y})) {
                continue;
            }
            Cell& cell = map.at({x, y});
            cell.terrain = terrain;
        }
    }
}

void NativeMapGenerator::placeCliffEdgeTiles(MapGrid& map, NativeRandomizer& rng, const UrbanRegion& urban)
{
    if (urban.cells.empty()) {
        return;
    }

    std::vector<bool> inRegion(static_cast<std::size_t>(map.width() * map.height()), false);
    auto index = [&map](Point p) {
        return static_cast<std::size_t>(p.y * map.width() + p.x);
    };
    auto isUrbanCell = [&](Point p) {
        return map.inBounds(p) && inRegion[index(p)];
    };
    auto canStampEdgeRun = [&](const std::vector<Point>& segment, int begin, int end) {
        if (end - begin + 1 < 6) {
            return false;
        }
        for (int i = begin; i <= end; ++i) {
            const Point p = segment[static_cast<std::size_t>(i)];
            if (!map.inBounds(p)) {
                ++report_.urbanCliffEdgeAreaFailures;
                return false;
            }
            const Cell& cell = map.at(p);
            if (cell.terrain != Terrain::Pave || cell.overlay != Overlay::None || cell.terrainObject != TerrainObject::None || cell.spawnExclusion) {
                ++report_.urbanCliffEdgeAreaFailures;
                return false;
            }
        }
        return true;
    };
    auto isOpenEdgeArea = [&](Point anchor, int width, int height) {
        if (width <= 0 || height <= 0) {
            ++report_.urbanCliffEdgeAreaFailures;
            return false;
        }
        for (int y = anchor.y; y < anchor.y + height; ++y) {
            for (int x = anchor.x; x < anchor.x + width; ++x) {
                const Point p{x, y};
                if (!map.inBounds(p)) {
                    ++report_.urbanCliffEdgeAreaFailures;
                    return false;
                }
                const Cell& cell = map.at(p);
                if (cell.terrain == Terrain::Water || cell.spawnExclusion) {
                    ++report_.urbanCliffEdgeAreaFailures;
                    return false;
                }
            }
        }
        return true;
    };
    auto stampEdgeCell = [&](Point p, int variant) {
        if (!map.inBounds(p)) {
            return;
        }
        Cell& cell = map.at(p);
        if (cell.terrain != Terrain::Pave || cell.overlay != Overlay::None || cell.terrainObject != TerrainObject::None) {
            return;
        }
        cell.slope = std::clamp(variant, 0, 15);
        ++report_.urbanCliffEdgeCells;
    };
    auto stampDecor = [&](Point p, int variant) {
        if (!map.inBounds(p)) {
            return;
        }
        Cell& cell = map.at(p);
        if (!isDecorationCandidateCell(map, p, true) || cell.terrain != Terrain::Pave) {
            return;
        }
        cell.slope = std::clamp(variant, 0, 15);
        ++report_.urbanCliffEdgeDecorTiles;
    };

    for (Point p : urban.cells) {
        if (map.inBounds(p)) {
            inRegion[index(p)] = true;
        }
    }
    for (Point p : urban.boundaryCells) {
        if (map.inBounds(p)) {
            inRegion[index(p)] = true;
        }
    }

    auto processSegment = [&](std::vector<Point> segment, bool horizontal) {
        if (static_cast<int>(segment.size()) < 6) {
            return;
        }

        ++report_.urbanCliffEdgeCandidates;
        int startOffset = 0;
        if (segment.size() > 6) {
            startOffset = rng.randomRanged(0, std::min<int>(3, static_cast<int>(segment.size()) - 6));
            if (startOffset > 0) {
                ++report_.urbanCliffEdgeRandomShortens;
            }
        }
        int endOffset = 0;
        if (static_cast<int>(segment.size()) - startOffset > 6) {
            endOffset = rng.randomRanged(0, std::min<int>(3, static_cast<int>(segment.size()) - startOffset - 6));
            if (endOffset > 0) {
                ++report_.urbanCliffEdgeRandomShortens;
            }
        }

        int begin = startOffset;
        int end = static_cast<int>(segment.size()) - endOffset - 1;
        if (end - begin + 1 < 6) {
            return;
        }

        auto edgeAreaIsOpen = [&]() {
            if (!canStampEdgeRun(segment, begin, end)) {
                return false;
            }
            const Point first = segment[static_cast<std::size_t>(begin)];
            const Rect check{
                horizontal ? first.x - 4 : first.x,
                horizontal ? first.y : first.y - 4,
                horizontal ? 4 : 3,
                horizontal ? 3 : 4,
            };
            return isOpenEdgeArea({check.x, check.y}, check.w, check.h);
        };

        int retries = 0;
        while (!edgeAreaIsOpen() && retries < 6 && end - begin + 1 > 6) {
            if ((retries & 1) == 0) {
                ++begin;
            } else {
                --end;
            }
            ++retries;
            ++report_.urbanCliffEdgeRandomShortens;
        }
        if (!edgeAreaIsOpen()) {
            return;
        }

        ++report_.urbanCliffEdgeSegments;
        const bool leftVariant = rng.randomRanged(0, 1) != 0;
        const bool rightVariant = rng.randomRanged(0, 1) != 0;
        if (leftVariant) {
            ++report_.urbanCliffEdgeEndpointVariants;
        }
        if (rightVariant) {
            ++report_.urbanCliffEdgeEndpointVariants;
        }

        for (int i = begin; i <= end; ++i) {
            int variant = horizontal ? 1 : 3;
            if (i == begin) {
                variant = horizontal ? (leftVariant ? 9 : 2) : (leftVariant ? 13 : 5);
            } else if (i == end) {
                variant = horizontal ? (rightVariant ? 10 : 0) : (rightVariant ? 12 : 7);
            }
            stampEdgeCell(segment[static_cast<std::size_t>(i)], variant);
        }

        if (horizontal) {
            if (rng.randomRanged(0, 1) != 0) {
                stampDecor({segment[static_cast<std::size_t>(begin)].x, segment[static_cast<std::size_t>(begin)].y + 1}, 12);
            }
            if (rng.randomRanged(0, 1) != 0) {
                stampDecor({segment[static_cast<std::size_t>(end)].x, segment[static_cast<std::size_t>(end)].y + 1}, 12);
            }
        } else {
            if (rng.randomRanged(0, 1) != 0) {
                stampDecor({segment[static_cast<std::size_t>(begin)].x + 1, segment[static_cast<std::size_t>(begin)].y}, 4);
            }
            if (rng.randomRanged(0, 1) != 0) {
                stampDecor({segment[static_cast<std::size_t>(end)].x + 1, segment[static_cast<std::size_t>(end)].y}, 5);
            }
        }
    };

    for (int y = urban.bounds.y; y < urban.bounds.y + urban.bounds.h; ++y) {
        std::vector<Point> segment;
        for (int x = urban.bounds.x; x < urban.bounds.x + urban.bounds.w; ++x) {
            const Point p{x, y};
            const bool edge = isUrbanCell(p) && (!isUrbanCell({x, y - 1}) || !isUrbanCell({x, y + 1}));
            if (edge) {
                segment.push_back(p);
            } else {
                processSegment(segment, true);
                segment.clear();
            }
        }
        processSegment(segment, true);
    }

    for (int x = urban.bounds.x; x < urban.bounds.x + urban.bounds.w; ++x) {
        std::vector<Point> segment;
        for (int y = urban.bounds.y; y < urban.bounds.y + urban.bounds.h; ++y) {
            const Point p{x, y};
            const bool edge = isUrbanCell(p) && (!isUrbanCell({x - 1, y}) || !isUrbanCell({x + 1, y}));
            if (edge) {
                segment.push_back(p);
            } else {
                processSegment(segment, false);
                segment.clear();
            }
        }
        processSegment(segment, false);
    }
}

void NativeMapGenerator::placeUrbanPatches(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, const UrbanRegion& urban)
{
    if (urban.cells.empty()) {
        return;
    }
    (void)options;
    const int targetRects = static_cast<int>(urban.cells.size()) / 50;
    if (targetRects <= 0) {
        return;
    }
    const int maxAttempts = targetRects * 10;
    const std::pair<int, int> templates[] = {
        {6, 4},
        {4, 6},
        {4, 4},
        {4, 2},
        {2, 4},
    };

    int placedRects = 0;
    for (int attempt = 0; attempt < maxAttempts && placedRects < targetRects; ++attempt) {
        const Point base = urban.cells[static_cast<std::size_t>(rng.randomRanged(0, static_cast<int>(urban.cells.size()) - 1))];
        for (const auto [width, height] : templates) {
            if (!isPavedAndEmptyRect(map, base, width, height)) {
                continue;
            }

            stampDecorationFootprint(map, base, width, height, Terrain::Pave);
            ++placedRects;
            ++report_.urbanPatchRects;
            report_.urbanPatchRectCells += width * height;
            report_.urbanPavedCells += width * height;
            break;
        }
    }
}

void NativeMapGenerator::placeRegionBuildings(MapGrid& map, NativeRandomizer& rng, const UrbanRegion& urban)
{
    if (urban.cells.empty()) {
        return;
    }

    const int minTarget = std::max(0, static_cast<int>(urban.cells.size()) / 20);
    const int maxTarget = std::max(minTarget, static_cast<int>(urban.cells.size()) / 10);
    const int target = rng.randomRanged(minTarget, maxTarget) / 2;
    report_.civilianBuildingTargets += target;
    int placed = 0;
    for (int attempt = 0; attempt < target * 2 && placed < target; ++attempt) {
        ++report_.civilianBuildingAttempts;
        const int typeRoll = rng.randomRanged(0, 5);
        const int width = typeRoll >= 4 ? 2 : 1;
        const int height = typeRoll == 5 ? 2 : 1;
        const Point base = urban.cells[static_cast<std::size_t>(rng.randomRanged(0, static_cast<int>(urban.cells.size()) - 1))];
        const Point anchor{std::clamp(base.x - width / 2, 0, map.width() - width), std::clamp(base.y - height / 2, 0, map.height() - height)};
        if (!canPlaceDecorationFootprint(map, anchor, width, height, true, true)) {
            ++report_.civilianBuildingFootprintFailures;
            continue;
        }

        stampDecorationFootprint(map, anchor, width, height, Terrain::Pave);
        Cell& cell = map.at(anchor);
        cell.overlay = Overlay::NeutralBuilding;
        cell.overlayValue = rng.randomRanged(1, 8);
        ++placed;
        ++report_.civilianBuildings;
    }
}

void NativeMapGenerator::placeRegionUnits(MapGrid& map, NativeRandomizer& rng, const UrbanRegion& urban)
{
    if (urban.cells.empty()) {
        return;
    }
    const int target = rng.randomRanged(1, 5);
    report_.civilianUnitTargets += target;
    int placed = 0;
    for (int unit = 0; unit < target * 2 && placed < target; ++unit) {
        const int typeRoll = rng.randomRanged(0, 6);
        for (int attempt = 0; attempt < 60; ++attempt) {
            ++report_.civilianUnitAttempts;
            const Point p = urban.cells[static_cast<std::size_t>(rng.randomRanged(0, static_cast<int>(urban.cells.size()) - 1))];
            const DecorationUnitRejection rejection = decorationUnitRejection(map, p);
            if (rejection != DecorationUnitRejection::None) {
                if (rejection == DecorationUnitRejection::Bounds) {
                    ++report_.civilianUnitRejectedBounds;
                } else if (rejection == DecorationUnitRejection::Occupied) {
                    ++report_.civilianUnitRejectedOccupied;
                } else {
                    ++report_.civilianUnitRejectedExclusion;
                }
                continue;
            }

            Cell& cell = map.at(p);
            cell.overlay = Overlay::NeutralUnit;
            cell.overlayValue = typeRoll + 1;
            if (typeRoll <= 2) {
                ++report_.civilianInfantry;
            } else {
                ++report_.civilianVehicleUnits;
            }
            ++placed;
            ++report_.civilianUnits;
            break;
        }
    }
}

bool NativeMapGenerator::selectDecorationCenter(const MapGrid& map, NativeRandomizer& rng, const std::vector<NativeRegion>& regions, Point* center)
{
    if (!center || regions.empty()) {
        return false;
    }

    auto hasNearbyShore = [&map](Point p) {
        constexpr int radius = 7;
        for (int y = p.y - radius; y <= p.y + radius; ++y) {
            for (int x = p.x - radius; x <= p.x + radius; ++x) {
                const Point probe{x, y};
                if (map.inBounds(probe) && map.at(probe).terrain == Terrain::Shore) {
                    return true;
                }
            }
        }
        return false;
    };

    for (int pass = 0; pass < 20; ++pass) {
        for (int attempt = 0; attempt < 200; ++attempt) {
            const NativeRegion& region = regions[static_cast<std::size_t>(rng.randomRanged(0, static_cast<int>(regions.size()) - 1))];
            if (region.cells.empty()) {
                continue;
            }

            ++report_.decorationCentersTried;
            const Point candidate = region.cells[static_cast<std::size_t>(rng.randomRanged(0, static_cast<int>(region.cells.size()) - 1))];
            if (!isDecorationCandidateCell(map, candidate, true)) {
                continue;
            }
            if (map.at(candidate).urbanRegionMark) {
                continue;
            }
            if (map.at(candidate).cliffShoreExclusion && hasNearbyShore(candidate)) {
                continue;
            }

            *center = candidate;
            ++report_.decorationCentersAccepted;
            return true;
        }
    }
    return false;
}

void NativeMapGenerator::placeTerrainChain(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, Point center)
{
    constexpr int kInvalidTile = -1;
    constexpr int kQueueCapacity = 1024;
    static constexpr std::array<Point, 8> kAdjacent{
        Point{1, 0},
        Point{1, 1},
        Point{0, 1},
        Point{-1, 1},
        Point{-1, 0},
        Point{-1, -1},
        Point{0, -1},
        Point{1, -1},
    };

    struct ChainEntry {
        int tile = -1;
        Point cell{};
        int previousTile = -1;
        Point previousCell{};
        int reverseConnection = -1;
    };

    struct ChainConnection {
        int direction = 0;
        Point offset{};
    };

    auto tileSizeClass = [](int tile) {
        if (tile < 0) {
            return 1;
        }
        return std::clamp(tile / 100, 1, 4);
    };

    auto footprintSize = [&](int tile) {
        const int sizeClass = tileSizeClass(tile);
        const int variant = std::abs(tile) % 10;
        switch (sizeClass) {
        case 1:
            return Point{1, 1};
        case 2:
            return variant % 2 == 0 ? Point{2, 1} : Point{1, 2};
        case 3:
            return variant % 2 == 0 ? Point{3, 1} : Point{1, 3};
        default:
            return variant % 3 == 0 ? Point{3, 2} : (variant % 3 == 1 ? Point{2, 3} : Point{2, 2});
        }
    };

    auto isFootprintFoundationCell = [&](int tile, Point offset, Point size) {
        if (size.x <= 1 && size.y <= 1) {
            return true;
        }
        const int variant = std::abs(tile) % 10;
        if (size.x >= 3 && size.y >= 2) {
            return offset.y == 0 || offset.x == 0 || ((offset.x + offset.y + variant) % 3 != 0);
        }
        if (size.x >= 2 && size.y >= 2) {
            return offset.x == offset.y || ((offset.x + offset.y + variant) & 1) == 0;
        }
        if (size.x > 1) {
            return offset.x != size.x - 1 || (variant & 1) == 0;
        }
        return offset.y != size.y - 1 || (variant & 1) == 0;
    };

    auto requiredFoundationSlope = [&](int tile, Point offset) {
        const int variant = std::abs(tile) % 10;
        if (((variant + offset.x + offset.y) % 7) == 0) {
            return 0;
        }
        return -1;
    };

    auto connectionOffset = [&](int tile, int direction) {
        const Point size = footprintSize(tile);
        switch (direction & 7) {
        case 0:
            return Point{size.x - 1, size.y / 2};
        case 1:
            return Point{size.x - 1, size.y - 1};
        case 2:
            return Point{size.x / 2, size.y - 1};
        case 3:
            return Point{0, size.y - 1};
        case 4:
            return Point{0, size.y / 2};
        case 5:
            return Point{0, 0};
        case 6:
            return Point{size.x / 2, 0};
        default:
            return Point{size.x - 1, 0};
        }
    };

    auto tileConnections = [&](int tile) {
        const int selector = std::abs(tile) % 8;
        std::vector<int> directions;
        switch (selector) {
        case 0:
            directions = {0, 4};
            break;
        case 1:
            directions = {2, 6};
            break;
        case 2:
            directions = {1, 5};
            break;
        case 3:
            directions = {3, 7};
            break;
        case 4:
            directions = {0, 2, 4};
            break;
        case 5:
            directions = {0, 4, 6};
            break;
        case 6:
            directions = {1, 3, 5};
            break;
        default:
            directions = {1, 5, 7};
            break;
        }

        std::vector<ChainConnection> result;
        result.reserve(directions.size());
        for (int direction : directions) {
            result.push_back({direction, connectionOffset(tile, direction)});
        }
        return result;
    };

    auto indexOf = [&map](Point p) {
        return static_cast<std::size_t>(p.y * map.width() + p.x);
    };

    std::vector<unsigned char> footprintMarks(static_cast<std::size_t>(map.width() * map.height()), 0);

    auto footprintCells = [&](int tile, Point anchor) {
        std::vector<Point> cells;
        const Point size = footprintSize(tile);
        cells.reserve(static_cast<std::size_t>(std::max(1, size.x * size.y)));
        for (int y = 0; y < size.y; ++y) {
            for (int x = 0; x < size.x; ++x) {
                const Point offset{x, y};
                if (isFootprintFoundationCell(tile, offset, size)) {
                    cells.push_back({anchor.x + x, anchor.y + y});
                }
            }
        }
        return cells;
    };

    auto canPlaceTileAt = [&](int tile, Point anchor, bool rejectMarked, bool strictBounds) {
        const Point size = footprintSize(tile);
        for (int y = 0; y < size.y; ++y) {
            for (int x = 0; x < size.x; ++x) {
                const Point offset{x, y};
                if (!isFootprintFoundationCell(tile, offset, size)) {
                    continue;
                }

                const Point p{anchor.x + x, anchor.y + y};
                ++report_.terrainChainFoundationCellsChecked;
                if (!map.inBounds(p)) {
                    if (strictBounds) {
                        ++report_.terrainChainRejectedPlacements;
                        ++report_.terrainChainRejectedBounds;
                        return false;
                    }
                    continue;
                }

                const Cell& cell = map.at(p);
                if (cell.terrainObject != TerrainObject::None || cell.overlay != Overlay::None) {
                    ++report_.terrainChainRejectedPlacements;
                    ++report_.terrainChainRejectedOccupied;
                    return false;
                }
                if (!isPassable(cell) || cell.spawnExclusion || cell.cliffShoreExclusion || cell.terrain == Terrain::Shore || cell.terrain == Terrain::Cliff || cell.terrain == Terrain::Pave) {
                    ++report_.terrainChainRejectedPlacements;
                    ++report_.terrainChainRejectedTerrain;
                    return false;
                }
                const int requiredSlope = requiredFoundationSlope(tile, offset);
                if (requiredSlope >= 0 && cell.slope >= 0 && cell.slope != requiredSlope) {
                    ++report_.terrainChainRejectedPlacements;
                    ++report_.terrainChainRejectedSlope;
                    return false;
                }
                if (rejectMarked && footprintMarks[indexOf(p)] != 0) {
                    ++report_.terrainChainRejectedPlacements;
                    ++report_.terrainChainRejectedMarkedFootprints;
                    return false;
                }
            }
        }
        return true;
    };

    auto markFootprint = [&](int tile, Point anchor, bool marked) {
        if (tile == kInvalidTile) {
            return;
        }
        if (marked) {
            ++report_.terrainChainFootprintsMarked;
        } else {
            ++report_.terrainChainFootprintsCleared;
        }

        for (Point p : footprintCells(tile, anchor)) {
            if (!map.inBounds(p)) {
                continue;
            }
            footprintMarks[indexOf(p)] = marked ? 1 : 0;
            if (marked) {
                ++report_.terrainChainFootprintCellsMarked;
            } else {
                ++report_.terrainChainFootprintCellsCleared;
            }
        }
    };

    auto tileOverlayValue = [](int tile) {
        return std::abs(tile % 4) + 1;
    };

    auto placeTile = [&](int tile, Point anchor) {
        int placed = 0;
        const int value = tileOverlayValue(tile);
        for (Point p : footprintCells(tile, anchor)) {
            if (!map.inBounds(p)) {
                continue;
            }
            Cell& cell = map.at(p);
            if (cell.overlay == Overlay::None) {
                cell.overlay = Overlay::TerrainChain;
                cell.overlayValue = value;
                ++placed;
            }
        }
        return placed;
    };

    auto clearTile = [&](int tile, Point anchor) {
        if (tile == kInvalidTile) {
            return;
        }
        ++report_.terrainChainTilesCleared;
        for (Point p : footprintCells(tile, anchor)) {
            if (!map.inBounds(p)) {
                continue;
            }
            Cell& cell = map.at(p);
            if (cell.overlay == Overlay::TerrainChain) {
                cell.overlay = Overlay::None;
                cell.overlayValue = 0;
            }
        }
    };

    auto poolCountForSize = [](int sizeClass) {
        switch (sizeClass) {
        case 1:
            return 11;
        case 2:
            return 14;
        case 3:
            return 10;
        default:
            return 8;
        }
    };

    auto selectRandomTerrain = [&](int terrainClass, Point anchor, int count, int& selectedTile, Point& selectedCell, int& reverseConnection) {
        ++report_.terrainChainTerrainSelections;
        selectedTile = kInvalidTile;
        selectedCell = {};
        reverseConnection = -1;

        int sizeClass = 1;
        if (count > 1) {
            double sample = 2.0;
            do {
                ++report_.terrainChainGaussianSizeSamples;
                sample = rng.gaussian() * 0.5 + 2.0;
            } while (sample < 2.0 || sample > static_cast<double>(count));
            sizeClass = std::clamp(static_cast<int>(std::lround(sample)), 2, count);
        }

        const int firstSizeClass = sizeClass;
        do {
            const int poolCount = poolCountForSize(sizeClass);
            int startIndex = 0;
            if (sizeClass == 2 && poolCount > 7 && rng.randomUnit() < 0.7) {
                startIndex = rng.randomRanged(7, poolCount - 1);
                ++report_.terrainChainSize2Skips;
            } else {
                startIndex = rng.randomRanged(0, poolCount - 1);
            }

            for (int attempt = 0; attempt < poolCount; ++attempt) {
                const int variant = (startIndex + attempt) % poolCount;
                const int tile = sizeClass * 100 + (terrainClass & 7) * 10 + variant;
                ++report_.terrainChainTileAttempts;
                const auto connections = tileConnections(tile);
                auto connectionIt = std::find_if(connections.begin(), connections.end(), [terrainClass](const ChainConnection& connection) {
                    return connection.direction == (terrainClass & 7);
                });
                if (connectionIt == connections.end()) {
                    ++report_.terrainChainConnectionSkips;
                    continue;
                }

                const Point childAnchor{
                    anchor.x - connectionIt->offset.x,
                    anchor.y - connectionIt->offset.y,
                };
                if (canPlaceTileAt(tile, childAnchor, true, true)) {
                    selectedTile = tile;
                    selectedCell = childAnchor;
                    reverseConnection = static_cast<int>(std::distance(connections.begin(), connectionIt));
                    return true;
                }
            }

            sizeClass = (sizeClass + 1) % (count + 1);
            if (sizeClass == 0) {
                sizeClass = 1;
            }
            ++report_.terrainChainSizeClassFallbacks;
        } while (sizeClass != firstSizeClass);

        ++report_.terrainChainSelectionFailures;
        return false;
    };

    auto cleanupChildFootprints = [&](const std::vector<ChainEntry>& children) {
        for (const ChainEntry& child : children) {
            markFootprint(child.tile, child.cell, false);
            markFootprint(child.previousTile, child.previousCell, false);
        }
    };

    if (!isDecorationCandidateCell(map, center)) {
        return;
    }

    int startTile = kInvalidTile;
    const int startOffset = rng.randomRanged(0, 10);
    for (int attempt = 0; attempt < 11; ++attempt) {
        const int tile = 100 + (startOffset + attempt) % 11;
        if (canPlaceTileAt(tile, center, false, false)) {
            startTile = tile;
            break;
        }
    }
    if (startTile == kInvalidTile) {
        ++report_.terrainChainSelectionFailures;
        return;
    }

    std::array<ChainEntry, kQueueCapacity> queue{};
    int head = 0;
    int tail = 0;
    int active = 0;
    int placedCells = 0;
    int processed = 0;

    auto enqueue = [&](const ChainEntry& entry) {
        if (active >= kQueueCapacity) {
            ++report_.terrainChainRejectedPlacements;
            ++report_.terrainChainQueueSaturationRejects;
            return false;
        }
        queue[static_cast<std::size_t>(tail)] = entry;
        tail = (tail + 1) & (kQueueCapacity - 1);
        ++active;
        ++report_.terrainChainEntriesQueued;
        report_.terrainChainQueueMax = std::max(report_.terrainChainQueueMax, active);
        return true;
    };

    auto dequeue = [&]() {
        ChainEntry entry = queue[static_cast<std::size_t>(head)];
        head = (head + 1) & (kQueueCapacity - 1);
        --active;
        return entry;
    };

    enqueue({startTile, center, kInvalidTile, center, -1});

    while (active > 0 && active < 1020) {
        ChainEntry entry = dequeue();
        ++processed;
        ++report_.terrainChainEntriesProcessed;

        if (entry.tile == kInvalidTile) {
            if (entry.previousTile != kInvalidTile) {
                placedCells += placeTile(entry.previousTile, entry.previousCell);
                ++report_.terrainChainTerminalFallbacks;
            }
            ++report_.terrainChainFrontierSteps;
            continue;
        }

        if (entry.previousTile != kInvalidTile) {
            markFootprint(entry.previousTile, entry.previousCell, false);
        }

        const int placedThisTile = placeTile(entry.tile, entry.cell);
        placedCells += placedThisTile;

        std::vector<ChainEntry> children;
        const auto connections = tileConnections(entry.tile);
        bool expansionOk = true;
        if (connections.size() > 1) {
            children.reserve(connections.size() - 1);
            for (std::size_t connectionIndex = 0; connectionIndex < connections.size(); ++connectionIndex) {
                if (static_cast<int>(connectionIndex) == entry.reverseConnection) {
                    continue;
                }

                const ChainConnection& connection = connections[connectionIndex];
                const Point targetAnchor{
                    entry.cell.x + connection.offset.x + kAdjacent[static_cast<std::size_t>(connection.direction & 7)].x,
                    entry.cell.y + connection.offset.y + kAdjacent[static_cast<std::size_t>(connection.direction & 7)].y,
                };
                const int terrainClass = (connection.direction - 4) & 7;

                ChainEntry child;
                int fallbackReverse = -1;
                if (!selectRandomTerrain(terrainClass, targetAnchor, 1, child.previousTile, child.previousCell, fallbackReverse)) {
                    expansionOk = false;
                    break;
                }

                int selectedReverse = -1;
                if (selectRandomTerrain(terrainClass, targetAnchor, 4, child.tile, child.cell, selectedReverse)) {
                    child.reverseConnection = selectedReverse;
                    markFootprint(child.tile, child.cell, true);
                } else {
                    child.reverseConnection = fallbackReverse;
                    ++report_.terrainChainTerminalFallbacks;
                }
                markFootprint(child.previousTile, child.previousCell, true);
                children.push_back(child);
            }
        }

        if (!expansionOk) {
            clearTile(entry.tile, entry.cell);
            placedCells -= placedThisTile;
            cleanupChildFootprints(children);
            if (entry.previousTile != kInvalidTile) {
                placedCells += placeTile(entry.previousTile, entry.previousCell);
            }
            ++report_.terrainChainFrontierSteps;
            continue;
        }

        if (children.size() > 1) {
            report_.terrainChainBranchSteps += static_cast<int>(children.size()) - 1;
        }
        for (const ChainEntry& child : children) {
            if (!enqueue(child)) {
                markFootprint(child.tile, child.cell, false);
                markFootprint(child.previousTile, child.previousCell, false);
            }
        }
        ++report_.terrainChainFrontierSteps;
    }

    while (active > 0) {
        ChainEntry entry = dequeue();
        if (entry.previousTile != kInvalidTile) {
            placedCells += placeTile(entry.previousTile, entry.previousCell);
            ++report_.terrainChainTerminalFallbacks;
            ++report_.terrainChainTailRestores;
        }
    }

    if (processed > 1 && placedCells > 0) {
        ++report_.terrainChains;
        report_.terrainChainCells += placedCells;
    }
}

void NativeMapGenerator::placeNeutralDecorationBuildings(MapGrid& map, NativeRandomizer& rng, Point center, int target)
{
    int placed = 0;
    const int nativeTarget = rng.randomRanged(8, 12);
    target = std::max(0, std::min(target, nativeTarget));
    report_.neutralDecorationBuildingTargets += nativeTarget;
    for (int building = 0; building < target; ++building) {
        const int typeRoll = rng.randomRanged(0, 4);
        for (int attempt = 0; attempt < 60; ++attempt) {
            ++report_.neutralDecorationBuildingAttempts;
            const int width = typeRoll >= 3 ? 2 : 1;
            const int height = typeRoll == 4 ? 2 : 1;
            const Point anchor{
                center.x + rng.randomRanged(-7, 7),
                center.y + rng.randomRanged(-7, 7),
            };
            const int requiredLevel = map.inBounds(center) ? map.at(center).level : -1;
            if (!canPlaceDecorationFootprint(map, anchor, width, height, false, false, requiredLevel)) {
                ++report_.neutralDecorationBuildingFootprintFailures;
                continue;
            }

            stampDecorationFootprint(map, anchor, width, height, Terrain::Pave);
            Cell& cell = map.at(anchor);
            cell.overlay = Overlay::NeutralBuilding;
            cell.overlayValue = rng.randomRanged(1, 8);
            ++placed;
            ++report_.neutralDecorationBuildings;
            break;
        }
    }
}

void NativeMapGenerator::placeNeutralDecorationUnits(MapGrid& map, NativeRandomizer& rng, Point center, int target)
{
    int placed = 0;
    const int nativeTarget = rng.randomRanged(1, 4);
    target = std::max(0, std::min(target, nativeTarget));
    report_.neutralDecorationUnitTargets += nativeTarget;
    for (int unit = 0; unit < target * 2 && placed < target; ++unit) {
        const int typeRoll = rng.randomRanged(0, 5);
        for (int attempt = 0; attempt < 60; ++attempt) {
            ++report_.neutralDecorationUnitAttempts;
            const Point p{
                center.x + rng.randomRanged(-8, 8),
                center.y + rng.randomRanged(-8, 8),
            };
            const DecorationUnitRejection rejection = decorationUnitRejection(map, p);
            if (rejection != DecorationUnitRejection::None) {
                if (rejection == DecorationUnitRejection::Bounds) {
                    ++report_.neutralDecorationUnitRejectedBounds;
                } else if (rejection == DecorationUnitRejection::Occupied) {
                    ++report_.neutralDecorationUnitRejectedOccupied;
                } else {
                    ++report_.neutralDecorationUnitRejectedExclusion;
                }
                continue;
            }

            Cell& cell = map.at(p);
            cell.overlay = Overlay::NeutralUnit;
            cell.overlayValue = typeRoll + 1;
            if (typeRoll <= 2) {
                ++report_.neutralDecorationInfantry;
            } else {
                ++report_.neutralDecorationVehicleUnits;
            }
            ++placed;
            ++report_.neutralDecorationUnits;
            break;
        }
    }
}

void NativeMapGenerator::placeNativeDecorations(MapGrid& map, const MapSeedOptions& options, NativeRandomizer& rng, const std::vector<NativeRegion>& regions)
{
    const int presence = std::clamp(options.urbanPresence, 0, 100);
    if (presence <= 0 || regions.empty()) {
        return;
    }

    auto selectTerrainChainCenter = [&](Point* center) {
        if (!center) {
            return false;
        }

        for (int attempt = 0; attempt < 12; ++attempt) {
            Point candidate;
            if (!selectDecorationCenter(map, rng, regions, &candidate)) {
                break;
            }
            if (isDecorationCandidateCell(map, candidate)) {
                *center = candidate;
                return true;
            }
        }

        for (int attempt = 0; attempt < 500; ++attempt) {
            const NativeRegion& region = regions[static_cast<std::size_t>(rng.randomRanged(0, static_cast<int>(regions.size()) - 1))];
            if (region.cells.empty()) {
                continue;
            }

            ++report_.decorationCentersTried;
            const Point candidate = region.cells[static_cast<std::size_t>(rng.randomRanged(0, static_cast<int>(region.cells.size()) - 1))];
            if (!isDecorationCandidateCell(map, candidate)) {
                continue;
            }
            if (map.at(candidate).urbanRegionMark) {
                continue;
            }

            *center = candidate;
            ++report_.decorationCentersAccepted;
            return true;
        }
        return false;
    };

    const int targetEvents = std::max(1, static_cast<int>(std::lround((static_cast<double>(presence) / 100.0 + 0.32) * 3.0)));
    report_.decorationTargetEvents = targetEvents;
    int skippedUrbanAttempts = 0;
    int selectionFailures = 0;
    while (report_.decorationEvents < targetEvents && skippedUrbanAttempts < 10 && selectionFailures < 20) {
        Point center;
        if (!selectDecorationCenter(map, rng, regions, &center)) {
            ++selectionFailures;
            continue;
        }

        if (rng.randomUnit() < static_cast<double>(presence) / 100.0) {
            ++report_.decorationUrbanAttempts;
            UrbanRegion urban = createUrbanRegion(map, options, rng, center);
            if (urban.cells.empty()) {
                urban = createUrbanRegion(map, options, rng, center, 0.55);
            }
            if (urban.cells.empty()) {
                ++skippedUrbanAttempts;
                ++report_.decorationUrbanSkips;
            } else {
                placeCliffEdgeTiles(map, rng, urban);
                placeRegionBuildings(map, rng, urban);
                placeRegionUnits(map, rng, urban);
                placeUrbanPatches(map, options, rng, urban);
                ++report_.urbanRegions;
                ++report_.decorationEvents;
            }
            continue;
        }

        if (!isDecorationCandidateCell(map, center) && !selectTerrainChainCenter(&center)) {
            continue;
        }
        const int chainsBefore = report_.terrainChains;
        placeTerrainChain(map, options, rng, center);
        if (report_.terrainChains > chainsBefore) {
            placeNeutralDecorationBuildings(map, rng, center, rng.randomRanged(1, 2 + presence / 35));
            placeNeutralDecorationUnits(map, rng, center, rng.randomRanged(1, 2 + presence / 28));
            ++report_.decorationEvents;
        }
    }

    if (report_.terrainChains == 0) {
        for (int attempt = 0; attempt < 8 && report_.terrainChains == 0; ++attempt) {
            Point center;
            if (!selectTerrainChainCenter(&center)) {
                break;
            }
            placeTerrainChain(map, options, rng, center);
            if (report_.terrainChains > 0) {
                placeNeutralDecorationBuildings(map, rng, center, std::max(1, presence / 35));
                placeNeutralDecorationUnits(map, rng, center, std::max(1, presence / 30));
            }
        }
    }
}
