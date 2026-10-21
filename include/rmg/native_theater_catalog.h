#pragma once

#include "rmg/tile_catalog.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rmg {

struct NativeIsoTileSlot {
    bool present = false;
    std::uint8_t heightOffset = 0;
    std::uint8_t terrainType = 0;
    std::uint8_t slope = 0;
};

struct NativeIsoTileTemplate {
    int tileTypeIndex = -1;
    std::string fileName;
    bool morphable = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<NativeIsoTileSlot> slots;
};

struct NativeTheaterCatalog {
    int theater = 0;
    std::string theaterName;
    TileCatalog exportCatalog;
    int rampBase = -1;
    int rampSmoothBase = -1;
    int bridgeSetBase = -1;
    int woodBridgeSetBase = -1;
    int cliffSetBase = -1;
    int shorePiecesBase = -1;
    int waterBase = -1;
    int paveBase = -1;
    int miscPaveBase = -1;
    int waterfallEastBase = -1;
    int waterfallWestBase = -1;
    int waterfallNorthBase = -1;
    int waterfallSouthBase = -1;
    int cliffRampsBase = -1;
    int waterCavesBase = -1;
    int waterCliffsBase = -1;
    int destroyableCliffsBase = -1;
    int pavedRoadsBase = -1;
    int pavedRoadEndsBase = -1;
    int slopeSetPiecesBase = -1;
    int slopeSetPieces2Base = -1;
    std::vector<NativeIsoTileTemplate> tileTypes;

    [[nodiscard]] const NativeIsoTileTemplate* tileType(int tileTypeIndex) const noexcept;
};

[[nodiscard]] NativeTheaterCatalog loadNativeTheaterCatalog(
    const std::filesystem::path& gameResourceDirectory,
    int theater);

} // namespace rmg
