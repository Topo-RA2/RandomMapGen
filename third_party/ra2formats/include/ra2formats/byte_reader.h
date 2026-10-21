#pragma once

#include "ra2formats/error.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ra2formats {

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> bytes)
        : bytes_(bytes)
    {
    }

    [[nodiscard]] std::size_t position() const noexcept
    {
        return position_;
    }

    [[nodiscard]] std::size_t remaining() const noexcept
    {
        return bytes_.size() - position_;
    }

    void seek(std::size_t position)
    {
        if (position > bytes_.size()) {
            throw FormatError(ErrorCode::InvalidData, "读取位置超出数据边界");
        }
        position_ = position;
    }

    void skip(std::size_t count)
    {
        require(count);
        position_ += count;
    }

    [[nodiscard]] std::uint8_t readU8()
    {
        require(1);
        return bytes_[position_++];
    }

    [[nodiscard]] std::uint16_t readU16Le()
    {
        require(2);
        const std::uint16_t result = static_cast<std::uint16_t>(bytes_[position_])
            | static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes_[position_ + 1]) << 8);
        position_ += 2;
        return result;
    }

    [[nodiscard]] std::uint32_t readU32Le()
    {
        require(4);
        const std::uint32_t result = static_cast<std::uint32_t>(bytes_[position_])
            | (static_cast<std::uint32_t>(bytes_[position_ + 1]) << 8)
            | (static_cast<std::uint32_t>(bytes_[position_ + 2]) << 16)
            | (static_cast<std::uint32_t>(bytes_[position_ + 3]) << 24);
        position_ += 4;
        return result;
    }

    [[nodiscard]] std::int32_t readI32Le()
    {
        return std::bit_cast<std::int32_t>(readU32Le());
    }

    [[nodiscard]] float readF32Le()
    {
        static_assert(sizeof(float) == sizeof(std::uint32_t));
        return std::bit_cast<float>(readU32Le());
    }

    [[nodiscard]] std::span<const std::uint8_t> readBytes(std::size_t count)
    {
        require(count);
        const std::span<const std::uint8_t> result = bytes_.subspan(position_, count);
        position_ += count;
        return result;
    }

private:
    void require(std::size_t count) const
    {
        if (count > remaining()) {
            throw FormatError(ErrorCode::InvalidData, "数据在读取过程中被截断");
        }
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t position_ = 0;
};

}
