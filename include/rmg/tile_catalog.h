#pragma once

#include "rmg/map_types.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rmg {

struct TileMapping {
    std::uint32_t tileNum = 0;
    std::uint16_t tileCount = 1;
    std::uint8_t subTileBase = 0;
    std::uint8_t subTileCount = 1;
    std::vector<int> variantRemap;
};

struct TileCatalog {
    TileMapping land{0, 1, 0, 16};
    TileMapping ramp{1, 18, 0, 1};
    TileMapping water{1, 1, 0, 8};
    TileMapping shore{2, 1, 0, 8};
    TileMapping dirt{3, 1, 0, 16};
    TileMapping rough{4, 1, 0, 16};
    TileMapping pave{5, 1, 0, 16};
    TileMapping green{6, 1, 0, 16};
    TileMapping cliff{7, 1, 0, 16};
    TileMapping dirtLat{3, 1, 0, 16};
    TileMapping roughLat{4, 1, 0, 16};
    TileMapping paveLat{5, 1, 0, 16};
    TileMapping greenLat{6, 1, 0, 16};
};

TileCatalog loadTileCatalog(const std::filesystem::path& path, TileCatalog fallback = {});
TileCatalog loadTileCatalogFromTheaterIni(const std::filesystem::path& path, TileCatalog fallback = {});
bool isKnownTileCatalogPreset(const std::string& preset);
TileCatalog loadTileCatalogPreset(const std::string& preset, int theater);
TileMapping tileMappingForTerrain(const TileCatalog& catalog, Terrain terrain);
TileMapping latTileMappingForTerrain(const TileCatalog& catalog, Terrain terrain);
std::uint32_t mappedTileNum(const TileMapping& mapping, int variant);
std::uint8_t mappedSubTile(const TileMapping& mapping, int variant);

} // namespace rmg
