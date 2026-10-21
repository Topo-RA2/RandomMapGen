#include "ra2formats/tmp.h"

#include "ra2formats/byte_reader.h"
#include "ra2formats/error.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace ra2formats {
namespace {

constexpr std::size_t FileHeaderSize = 16;
constexpr std::size_t TileHeaderSize = 52;
constexpr std::uint32_t ExtraDataFlag = 0x01;
constexpr std::uint32_t ZDataFlag = 0x02;
constexpr std::uint32_t DamagedDataFlag = 0x04;

std::size_t checkedMultiply(std::size_t left, std::size_t right, const char* field)
{
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        throw FormatError(ErrorCode::SizeLimitExceeded, std::string(field) + " 大小溢出");
    }
    return left * right;
}

std::uint32_t positiveInt32(std::uint32_t raw, const char* field)
{
    const auto value = static_cast<std::int32_t>(raw);
    if (value <= 0) {
        throw FormatError(ErrorCode::InvalidData, std::string("TMP ") + field + " 必须大于零");
    }
    return static_cast<std::uint32_t>(value);
}

std::int32_t readI32Le(ByteReader& reader)
{
    return static_cast<std::int32_t>(reader.readU32Le());
}

std::size_t extraPixelCount(std::int32_t width, std::int32_t height, std::uint64_t limit)
{
    if (width <= 0 || height <= 0) {
        throw FormatError(ErrorCode::InvalidData, "TMP ExtraData 尺寸必须大于零");
    }
    const std::uint64_t count = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (count > limit || count > std::numeric_limits<std::size_t>::max()) {
        throw FormatError(ErrorCode::SizeLimitExceeded, "TMP ExtraData 尺寸超过安全限制");
    }
    return static_cast<std::size_t>(count);
}

TmpTile decodeTile(
    std::span<const std::uint8_t> bytes,
    std::uint32_t offset,
    std::size_t tilePixelCount,
    const TmpDecodeLimits& limits,
    std::uint64_t remainingDecodedBytes)
{
    if (offset > bytes.size() || TileHeaderSize > bytes.size() - offset) {
        throw FormatError(ErrorCode::InvalidData, "TMP tile 头被截断");
    }
    ByteReader reader(bytes);
    reader.seek(offset);
    TmpTile tile;
    tile.x = readI32Le(reader);
    tile.y = readI32Le(reader);
    static_cast<void>(reader.readU32Le());
    static_cast<void>(reader.readU32Le());
    const std::int32_t extraZDataOffset = readI32Le(reader);
    tile.extraX = readI32Le(reader);
    tile.extraY = readI32Le(reader);
    tile.extraWidth = readI32Le(reader);
    tile.extraHeight = readI32Le(reader);
    const std::uint32_t flags = reader.readU32Le();
    tile.hasExtraData = (flags & ExtraDataFlag) != 0;
    tile.hasZData = (flags & ZDataFlag) != 0;
    tile.hasDamagedData = (flags & DamagedDataFlag) != 0;
    tile.height = reader.readU8();
    tile.terrainType = reader.readU8();
    tile.rampType = reader.readU8();
    reader.skip(9);

    std::size_t extraCount = 0;
    if (tile.hasExtraData) {
        extraCount = extraPixelCount(tile.extraWidth, tile.extraHeight, limits.maximumTileBytes);
    }
    std::uint64_t tileBytes = tilePixelCount;
    if (tile.hasZData) {
        tileBytes += tilePixelCount;
    }
    if (tile.hasExtraData) {
        tileBytes += extraCount;
        if (tile.hasZData && extraZDataOffset > 0 && static_cast<std::size_t>(extraZDataOffset) < bytes.size()) {
            tileBytes += extraCount;
        }
    }
    if (tileBytes > remainingDecodedBytes) {
        throw FormatError(ErrorCode::SizeLimitExceeded, "TMP 累计解码数据超过安全限制");
    }

    const std::span<const std::uint8_t> tileData = reader.readBytes(tilePixelCount);
    tile.tileData.assign(tileData.begin(), tileData.end());
    if (tile.hasZData) {
        const std::span<const std::uint8_t> zData = reader.readBytes(tilePixelCount);
        tile.zData.assign(zData.begin(), zData.end());
    }
    if (tile.hasExtraData) {
        const std::span<const std::uint8_t> extraData = reader.readBytes(extraCount);
        tile.extraData.assign(extraData.begin(), extraData.end());
        if (tile.hasZData && extraZDataOffset > 0 && static_cast<std::size_t>(extraZDataOffset) < bytes.size()) {
            const std::span<const std::uint8_t> extraZData = reader.readBytes(extraCount);
            tile.extraZData.assign(extraZData.begin(), extraZData.end());
        }
    }
    return tile;
}

std::uint64_t decodedTileBytes(const TmpTile& tile)
{
    return static_cast<std::uint64_t>(tile.tileData.capacity())
        + tile.zData.capacity()
        + tile.extraData.capacity()
        + tile.extraZData.capacity();
}

void addDecodedBytes(std::uint64_t& total, std::uint64_t amount, const TmpDecodeLimits& limits)
{
    if (amount > limits.maximumDecodedBytes || total > limits.maximumDecodedBytes - amount) {
        throw FormatError(ErrorCode::SizeLimitExceeded, "TMP 累计解码数据超过安全限制");
    }
    total += amount;
}

void setPalettePixel(
    RgbaImage& image,
    const Palette& palette,
    std::int64_t x,
    std::int64_t y,
    std::uint8_t paletteIndex)
{
    if (paletteIndex == 0
        || x < 0
        || y < 0
        || x >= image.width
        || y >= image.height) {
        return;
    }
    const RgbColor color = palette[paletteIndex];
    const std::size_t destination = (static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)) * 4;
    image.pixels[destination] = color.red;
    image.pixels[destination + 1] = color.green;
    image.pixels[destination + 2] = color.blue;
    image.pixels[destination + 3] = 255;
}

}

TmpFile decodeTmp(std::span<const std::uint8_t> bytes, const TmpDecodeLimits& limits)
{
    if (bytes.size() < FileHeaderSize) {
        throw FormatError(ErrorCode::InvalidData, "TMP 文件头被截断");
    }
    ByteReader reader(bytes);
    TmpFile tmp;
    tmp.width = positiveInt32(reader.readU32Le(), "width");
    tmp.height = positiveInt32(reader.readU32Le(), "height");
    tmp.blockWidth = positiveInt32(reader.readU32Le(), "blockWidth");
    tmp.blockHeight = positiveInt32(reader.readU32Le(), "blockHeight");
    const std::uint64_t cellCount64 = static_cast<std::uint64_t>(tmp.width) * tmp.height;
    if (tmp.width > limits.maximumGridDimension
        || tmp.height > limits.maximumGridDimension
        || tmp.blockWidth > limits.maximumBlockDimension
        || tmp.blockHeight > limits.maximumBlockDimension
        || cellCount64 > limits.maximumGridCells) {
        throw FormatError(ErrorCode::SizeLimitExceeded, "TMP 尺寸超过安全限制");
    }
    const std::uint64_t blockArea = static_cast<std::uint64_t>(tmp.blockWidth) * tmp.blockHeight;
    if ((blockArea & 1U) != 0 || blockArea / 2 > limits.maximumTileBytes) {
        throw FormatError(ErrorCode::InvalidData, "TMP block 尺寸不能形成有效菱形数据");
    }
    const std::size_t tilePixelCount = static_cast<std::size_t>(blockArea / 2);
    const std::size_t cellCount = static_cast<std::size_t>(cellCount64);
    const std::size_t indexBytes = checkedMultiply(cellCount, 4, "TMP 索引表");
    if (indexBytes > reader.remaining()) {
        throw FormatError(ErrorCode::InvalidData, "TMP tile 索引表被截断");
    }
    const std::size_t tileDataStart = FileHeaderSize + indexBytes;

    const std::uint64_t tileDirectoryBytes = static_cast<std::uint64_t>(cellCount) * sizeof(std::optional<TmpTile>);
    const std::uint64_t offsetCacheBytes = static_cast<std::uint64_t>(cellCount)
        * (sizeof(std::pair<const std::int32_t, std::size_t>) + 4 * sizeof(void*));
    std::uint64_t decodedBytes = 0;
    addDecodedBytes(decodedBytes, tileDirectoryBytes, limits);
    addDecodedBytes(decodedBytes, offsetCacheBytes, limits);

    tmp.tiles.reserve(cellCount);
    std::unordered_map<std::int32_t, std::size_t> decodedOffsets;
    decodedOffsets.reserve(cellCount);
    for (std::size_t index = 0; index < cellCount; ++index) {
        const std::int32_t offset = readI32Le(reader);
        if (offset <= 0) {
            tmp.tiles.push_back(std::nullopt);
            continue;
        }
        if (static_cast<std::size_t>(offset) < tileDataStart) {
            throw FormatError(ErrorCode::InvalidData, "TMP tile 偏移与文件目录重叠");
        }
        const auto existing = decodedOffsets.find(offset);
        if (existing != decodedOffsets.end()) {
            const TmpTile& tile = *tmp.tiles[existing->second];
            addDecodedBytes(decodedBytes, decodedTileBytes(tile), limits);
            tmp.tiles.push_back(tile);
            continue;
        }
        TmpTile tile = decodeTile(
            bytes,
            static_cast<std::uint32_t>(offset),
            tilePixelCount,
            limits,
            limits.maximumDecodedBytes - decodedBytes);
        addDecodedBytes(decodedBytes, decodedTileBytes(tile), limits);
        tmp.tiles.push_back(std::move(tile));
        decodedOffsets.emplace(offset, tmp.tiles.size() - 1);
    }
    // offset cache 仅在解码期间存在；对外报告缓存持久持有的 tile 目录和像素缓冲。
    tmp.estimatedDecodedBytes = decodedBytes - offsetCacheBytes;
    return tmp;
}

RgbaImage makeTmpSheet(const TmpFile& tmp, const Palette& palette, std::uint64_t maximumPixels)
{
    std::int64_t maximumExtraBottom = 0;
    for (const std::optional<TmpTile>& optionalTile : tmp.tiles) {
        if (!optionalTile.has_value() || !optionalTile->hasExtraData || optionalTile->extraData.empty()) {
            continue;
        }
        const TmpTile& tile = *optionalTile;
        const std::int64_t bottom = static_cast<std::int64_t>(tile.extraY) - tile.y + tile.extraHeight - tmp.blockHeight;
        maximumExtraBottom = std::max(maximumExtraBottom, bottom);
    }
    const std::uint64_t cellHeight = static_cast<std::uint64_t>(tmp.blockHeight) + static_cast<std::uint64_t>(maximumExtraBottom);
    const std::uint64_t outputWidth = static_cast<std::uint64_t>(tmp.width) * tmp.blockWidth;
    const std::uint64_t outputHeight = static_cast<std::uint64_t>(tmp.height) * cellHeight;
    if (outputWidth == 0
        || outputHeight == 0
        || outputWidth > std::numeric_limits<std::uint32_t>::max()
        || outputHeight > std::numeric_limits<std::uint32_t>::max()
        || outputWidth * outputHeight > maximumPixels) {
        throw FormatError(ErrorCode::SizeLimitExceeded, "TMP sheet 尺寸超过安全限制");
    }

    RgbaImage image;
    image.width = static_cast<std::uint32_t>(outputWidth);
    image.height = static_cast<std::uint32_t>(outputHeight);
    image.pixels.assign(checkedMultiply(checkedMultiply(image.width, image.height, "TMP sheet"), 4, "TMP sheet RGBA"), 0);
    const std::uint32_t halfWidth = tmp.blockWidth / 2;
    const std::uint32_t halfHeight = tmp.blockHeight / 2;
    for (std::size_t tileIndex = 0; tileIndex < tmp.tiles.size(); ++tileIndex) {
        if (!tmp.tiles[tileIndex].has_value()) {
            continue;
        }
        const TmpTile& tile = *tmp.tiles[tileIndex];
        const std::uint32_t column = static_cast<std::uint32_t>(tileIndex % tmp.width);
        const std::uint32_t row = static_cast<std::uint32_t>(tileIndex / tmp.width);
        const std::int64_t originX = static_cast<std::int64_t>(column) * tmp.blockWidth;
        const std::int64_t originY = static_cast<std::int64_t>(row) * cellHeight;
        std::size_t sourceOffset = 0;
        for (std::uint32_t y = 0; y < tmp.blockHeight; ++y) {
            const std::uint32_t pixelCount = y < halfHeight
                ? 4 * (y + 1)
                : 4 * (tmp.blockHeight - 1 - y);
            const std::int64_t startX = static_cast<std::int64_t>(halfWidth) - 2 * static_cast<std::int64_t>(y < halfHeight ? y + 1 : tmp.blockHeight - 1 - y);
            if (pixelCount > tile.tileData.size() - sourceOffset) {
                throw FormatError(ErrorCode::InvalidData, "TMP TileData 长度与 block 尺寸不匹配");
            }
            for (std::uint32_t x = 0; x < pixelCount; ++x) {
                setPalettePixel(image, palette, originX + startX + x, originY + y, tile.tileData[sourceOffset++]);
            }
        }
        if (sourceOffset != tile.tileData.size()) {
            throw FormatError(ErrorCode::InvalidData, "TMP TileData 存在未消费数据");
        }

        if (!tile.hasExtraData || tile.extraData.empty()) {
            continue;
        }
        const std::int64_t extraOriginX = originX + static_cast<std::int64_t>(tile.extraX) - tile.x;
        const std::int64_t extraOriginY = originY + static_cast<std::int64_t>(tile.extraY) - tile.y;
        std::size_t extraOffset = 0;
        for (std::int32_t y = 0; y < tile.extraHeight; ++y) {
            for (std::int32_t x = 0; x < tile.extraWidth; ++x) {
                setPalettePixel(image, palette, extraOriginX + x, extraOriginY + y, tile.extraData[extraOffset++]);
            }
        }
    }
    return image;
}

}
