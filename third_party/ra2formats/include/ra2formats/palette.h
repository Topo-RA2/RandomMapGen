#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace ra2formats {

struct RgbColor {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

using Palette = std::array<RgbColor, 256>;

[[nodiscard]] Palette decodePalette6Bit(std::span<const std::uint8_t> bytes);
[[nodiscard]] Palette decodePalette8Bit(std::span<const std::uint8_t> bytes);

}
