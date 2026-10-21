#include "ra2formats/palette.h"

#include "ra2formats/error.h"

#include <cstddef>

namespace ra2formats {
namespace {

constexpr std::size_t PaletteByteCount = 256 * 3;

void requirePaletteSize(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != PaletteByteCount) {
        throw FormatError(ErrorCode::InvalidData, "调色板必须恰好包含 768 字节");
    }
}

std::uint8_t expandSixBitColor(std::uint8_t value)
{
    if (value > 63) {
        throw FormatError(ErrorCode::InvalidData, "6-bit 调色板包含超出 0..63 的颜色分量");
    }
    return static_cast<std::uint8_t>(static_cast<unsigned int>(value) * 255U / 63U);
}

}

Palette decodePalette6Bit(std::span<const std::uint8_t> bytes)
{
    requirePaletteSize(bytes);
    Palette palette;
    for (std::size_t index = 0; index < palette.size(); ++index) {
        palette[index] = {
            .red = expandSixBitColor(bytes[index * 3]),
            .green = expandSixBitColor(bytes[index * 3 + 1]),
            .blue = expandSixBitColor(bytes[index * 3 + 2]),
        };
    }
    return palette;
}

Palette decodePalette8Bit(std::span<const std::uint8_t> bytes)
{
    requirePaletteSize(bytes);
    Palette palette;
    for (std::size_t index = 0; index < palette.size(); ++index) {
        palette[index] = {
            .red = bytes[index * 3],
            .green = bytes[index * 3 + 1],
            .blue = bytes[index * 3 + 2],
        };
    }
    return palette;
}

}
