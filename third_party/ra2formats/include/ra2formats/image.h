#pragma once

#include <cstdint>
#include <vector>

namespace ra2formats {

struct RgbaImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] bool empty() const noexcept
    {
        return width == 0 || height == 0 || pixels.empty();
    }
};

}
