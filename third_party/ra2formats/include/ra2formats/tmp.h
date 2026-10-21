#pragma once

#include "ra2formats/image.h"
#include "ra2formats/palette.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ra2formats {

struct TmpTile {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t extraX = 0;
    std::int32_t extraY = 0;
    std::int32_t extraWidth = 0;
    std::int32_t extraHeight = 0;
    std::uint8_t height = 0;
    std::uint8_t terrainType = 0;
    std::uint8_t rampType = 0;
    bool hasExtraData = false;
    bool hasZData = false;
    bool hasDamagedData = false;
    std::vector<std::uint8_t> tileData;
    std::vector<std::uint8_t> zData;
    std::vector<std::uint8_t> extraData;
    std::vector<std::uint8_t> extraZData;
};

struct TmpFile {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t blockWidth = 0;
    std::uint32_t blockHeight = 0;
    std::uint64_t estimatedDecodedBytes = 0;
    std::vector<std::optional<TmpTile>> tiles;
};

struct TmpDecodeLimits {
    std::uint32_t maximumGridDimension = 1024;
    std::uint32_t maximumBlockDimension = 4096;
    std::uint64_t maximumGridCells = 262144;
    std::uint64_t maximumTileBytes = 67108864;
    std::uint64_t maximumDecodedBytes = 268435456;
};

[[nodiscard]] TmpFile decodeTmp(
    std::span<const std::uint8_t> bytes,
    const TmpDecodeLimits& limits = {});

[[nodiscard]] RgbaImage makeTmpSheet(
    const TmpFile& tmp,
    const Palette& palette,
    std::uint64_t maximumPixels = 134217728);

}
