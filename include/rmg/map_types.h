#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rmg {

struct Point {
    int x = 0;
    int y = 0;

    friend bool operator==(Point lhs, Point rhs) noexcept
    {
        return lhs.x == rhs.x && lhs.y == rhs.y;
    }
};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

enum class Terrain : std::uint8_t {
    Water,
    Land,
    Shore,
    Dirt,
    Rough,
    Pave,
    Green,
    Cliff
};

struct IsoTileOverride {
    Point iso;
    Terrain terrain = Terrain::Water;
    int variant = 0;
    int subTile = 0;
    int level = 4;
    bool iceGrowthAllowed = false;
    int landType = 0;
    int slopeIndex = 0;
    int nativeTileTypeIndex = -1;
};

enum class Overlay : std::uint8_t {
    None,
    Ore,
    Gems,
    Tree,
    Tech,
    Start,
    NeutralBuilding,
    NeutralUnit,
    TerrainChain,
    Grass,
    Bridge
};

enum class TerrainObject : std::uint8_t {
    None,
    TiberiumTree,
    OrePatchLamp
};

struct Cell {
    int x = 0;
    int y = 0;
    Terrain terrain = Terrain::Water;
    Overlay overlay = Overlay::None;
    int overlayValue = 0;
    TerrainObject terrainObject = TerrainObject::None;
    int terrainObjectValue = 0;
    int height = 0;
    int level = 4;
    int slope = -1;
    bool iceGrowthAllowed = false;
    int regionTag = 0;
    int tempTag = 0;
    bool spawnExclusion = false;
    bool cliffShoreExclusion = false;
    bool urbanRegionMark = false;
    int landType = 0;
};

class MapGrid {
public:
    MapGrid() = default;
    MapGrid(int width, int height);

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    Rect bounds() const noexcept { return {0, 0, width_, height_}; }
    void setNativeGeometry(Rect mapRect, Rect localRect) noexcept;
    bool hasNativeGeometry() const noexcept { return native_map_rect_.w > 0 && native_map_rect_.h > 0; }
    Rect nativeMapRect() const noexcept { return native_map_rect_; }
    Rect nativeLocalRect() const noexcept { return native_local_rect_; }
    bool inBounds(Point p) const noexcept;

    Cell& at(Point p);
    const Cell& at(Point p) const;
    std::vector<Point> neighbors4(Point p) const;
    std::vector<Point> neighbors8(Point p) const;
    std::vector<Cell>& cells() noexcept { return cells_; }
    const std::vector<Cell>& cells() const noexcept { return cells_; }

    void fill(Terrain terrain, Overlay overlay = Overlay::None);
    int countTerrain(Terrain terrain) const;
    int countOverlay(Overlay overlay) const;
    int countTerrainObject(TerrainObject object) const;

private:
    int width_ = 0;
    int height_ = 0;
    Rect native_map_rect_;
    Rect native_local_rect_;
    std::vector<Cell> cells_;
};

bool isPassable(const Cell& cell) noexcept;
bool isBuildable(const Cell& cell) noexcept;
bool isWet(const Cell& cell) noexcept;
bool isSolidLand(const Cell& cell) noexcept;
bool contains(Rect rect, Point p) noexcept;
Point clampPoint(Point p, Rect rect) noexcept;
int manhattan(Point a, Point b) noexcept;
double euclidean(Point a, Point b) noexcept;
char displayChar(const Cell& cell);
std::string terrainName(Terrain terrain);
std::string overlayName(Overlay overlay);
std::string terrainObjectName(TerrainObject object);

} // namespace rmg
