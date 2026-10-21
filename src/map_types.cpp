#include "rmg/map_types.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rmg {

MapGrid::MapGrid(int width, int height)
    : width_(width)
    , height_(height)
{
    if (width_ <= 0 || height_ <= 0) {
        throw std::invalid_argument("map dimensions must be positive");
    }
    if (width_ > 512 || height_ > 512) {
        throw std::invalid_argument("map dimensions must be at most 512 cells per axis");
    }

    const auto cellCount = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    cells_.resize(cellCount);

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            auto& cell = at({x, y});
            cell.x = x;
            cell.y = y;
        }
    }
}

void MapGrid::setNativeGeometry(Rect mapRect, Rect localRect) noexcept
{
    native_map_rect_ = mapRect;
    native_local_rect_ = localRect;
}

bool MapGrid::inBounds(Point p) const noexcept
{
    return p.x >= 0 && p.y >= 0 && p.x < width_ && p.y < height_;
}

Cell& MapGrid::at(Point p)
{
    if (!inBounds(p)) {
        throw std::out_of_range("map cell is out of bounds");
    }
    return cells_[static_cast<std::size_t>(p.y * width_ + p.x)];
}

const Cell& MapGrid::at(Point p) const
{
    if (!inBounds(p)) {
        throw std::out_of_range("map cell is out of bounds");
    }
    return cells_[static_cast<std::size_t>(p.y * width_ + p.x)];
}

std::vector<Point> MapGrid::neighbors4(Point p) const
{
    static constexpr Point kOffsets[]{{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    std::vector<Point> result;
    result.reserve(4);
    for (const auto offset : kOffsets) {
        const Point next{p.x + offset.x, p.y + offset.y};
        if (inBounds(next)) {
            result.push_back(next);
        }
    }
    return result;
}

std::vector<Point> MapGrid::neighbors8(Point p) const
{
    static constexpr Point kOffsets[]{{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
    std::vector<Point> result;
    result.reserve(8);
    for (const auto offset : kOffsets) {
        const Point next{p.x + offset.x, p.y + offset.y};
        if (inBounds(next)) {
            result.push_back(next);
        }
    }
    return result;
}

void MapGrid::fill(Terrain terrain, Overlay overlay)
{
    for (auto& cell : cells_) {
        cell.terrain = terrain;
        cell.overlay = overlay;
        cell.overlayValue = 0;
        cell.terrainObject = TerrainObject::None;
        cell.terrainObjectValue = 0;
        cell.height = 0;
        cell.level = 4;
        cell.slope = -1;
        cell.iceGrowthAllowed = false;
        cell.regionTag = 0;
        cell.tempTag = 0;
        cell.spawnExclusion = false;
        cell.cliffShoreExclusion = false;
        cell.urbanRegionMark = false;
        cell.landType = 0;
    }
}

int MapGrid::countTerrain(Terrain terrain) const
{
    return static_cast<int>(std::count_if(cells_.begin(), cells_.end(), [terrain](const Cell& cell) {
        return cell.terrain == terrain;
    }));
}

int MapGrid::countOverlay(Overlay overlay) const
{
    return static_cast<int>(std::count_if(cells_.begin(), cells_.end(), [overlay](const Cell& cell) {
        return cell.overlay == overlay;
    }));
}

int MapGrid::countTerrainObject(TerrainObject object) const
{
    return static_cast<int>(std::count_if(cells_.begin(), cells_.end(), [object](const Cell& cell) {
        return cell.terrainObject == object;
    }));
}

bool isPassable(const Cell& cell) noexcept
{
    return cell.overlay == Overlay::Bridge
        || (cell.terrain != Terrain::Water && cell.terrain != Terrain::Cliff);
}

bool isBuildable(const Cell& cell) noexcept
{
    return cell.terrain == Terrain::Land && (cell.overlay == Overlay::None || cell.overlay == Overlay::Start);
}

bool isWet(const Cell& cell) noexcept
{
    return cell.terrain == Terrain::Water || cell.terrain == Terrain::Shore;
}

bool isSolidLand(const Cell& cell) noexcept
{
    return cell.terrain == Terrain::Land
        || cell.terrain == Terrain::Dirt
        || cell.terrain == Terrain::Rough
        || cell.terrain == Terrain::Pave
        || cell.terrain == Terrain::Green;
}

bool contains(Rect rect, Point p) noexcept
{
    return p.x >= rect.x && p.y >= rect.y && p.x < rect.x + rect.w && p.y < rect.y + rect.h;
}

Point clampPoint(Point p, Rect rect) noexcept
{
    return {
        std::clamp(p.x, rect.x, rect.x + rect.w - 1),
        std::clamp(p.y, rect.y, rect.y + rect.h - 1),
    };
}

int manhattan(Point a, Point b) noexcept
{
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

double euclidean(Point a, Point b) noexcept
{
    const double dx = static_cast<double>(a.x - b.x);
    const double dy = static_cast<double>(a.y - b.y);
    return std::sqrt(dx * dx + dy * dy);
}

char displayChar(const Cell& cell)
{
    switch (cell.overlay) {
    case Overlay::Ore:
        return 'o';
    case Overlay::Gems:
        return 'g';
    case Overlay::Tree:
        return 't';
    case Overlay::Tech:
        return 'T';
    case Overlay::Start:
        return 'S';
    case Overlay::NeutralBuilding:
        return 'B';
    case Overlay::NeutralUnit:
        return 'u';
    case Overlay::TerrainChain:
        return 'c';
    case Overlay::Grass:
        return '*';
    case Overlay::Bridge:
        return 'H';
    case Overlay::None:
        break;
    }

    if (cell.terrainObject == TerrainObject::TiberiumTree) {
        return 'i';
    }
    if (cell.terrainObject == TerrainObject::OrePatchLamp) {
        return 'l';
    }

    switch (cell.terrain) {
    case Terrain::Water:
        return '~';
    case Terrain::Land:
        return '.';
    case Terrain::Shore:
        return ',';
    case Terrain::Dirt:
        return ':';
    case Terrain::Rough:
        return '^';
    case Terrain::Pave:
        return '=';
    case Terrain::Green:
        return '"';
    case Terrain::Cliff:
        return '#';
    }
    return '?';
}

std::string terrainName(Terrain terrain)
{
    switch (terrain) {
    case Terrain::Water:
        return "Water";
    case Terrain::Land:
        return "Land";
    case Terrain::Shore:
        return "Shore";
    case Terrain::Dirt:
        return "Dirt";
    case Terrain::Rough:
        return "Rough";
    case Terrain::Pave:
        return "Pave";
    case Terrain::Green:
        return "Green";
    case Terrain::Cliff:
        return "Cliff";
    }
    return "Unknown";
}

std::string overlayName(Overlay overlay)
{
    switch (overlay) {
    case Overlay::None:
        return "None";
    case Overlay::Ore:
        return "Ore";
    case Overlay::Gems:
        return "Gems";
    case Overlay::Tree:
        return "Tree";
    case Overlay::Tech:
        return "Tech";
    case Overlay::Start:
        return "Start";
    case Overlay::NeutralBuilding:
        return "NeutralBuilding";
    case Overlay::NeutralUnit:
        return "NeutralUnit";
    case Overlay::TerrainChain:
        return "TerrainChain";
    case Overlay::Grass:
        return "Grass";
    case Overlay::Bridge:
        return "Bridge";
    }
    return "Unknown";
}

std::string terrainObjectName(TerrainObject object)
{
    switch (object) {
    case TerrainObject::None:
        return "None";
    case TerrainObject::TiberiumTree:
        return "TiberiumTree";
    case TerrainObject::OrePatchLamp:
        return "OrePatchLamp";
    }
    return "Unknown";
}

} // namespace rmg
