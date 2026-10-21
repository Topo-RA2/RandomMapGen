#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace ra2formats {

enum class ErrorCode {
    InvalidData,
    UnsupportedFormat,
    SizeLimitExceeded,
    NoRenderableFrames,
};

class FormatError : public std::runtime_error {
public:
    FormatError(ErrorCode code, std::string message)
        : std::runtime_error(std::move(message))
        , code_(code)
    {
    }

    [[nodiscard]] ErrorCode code() const noexcept
    {
        return code_;
    }

private:
    ErrorCode code_;
};

}
