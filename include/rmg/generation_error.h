#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace rmg {

enum class GenerationFailureCode
{
    StartsInsufficient,
};

constexpr std::string_view generationFailureCodeName(GenerationFailureCode code)
{
    switch (code) {
    case GenerationFailureCode::StartsInsufficient:
        return "RMG_STARTS_INSUFFICIENT";
    }
    return "RMG_GENERATION_FAILED";
}

class GenerationError : public std::runtime_error
{
public:
    GenerationError(GenerationFailureCode code, std::string detail)
        : std::runtime_error(std::move(detail)), code_(code)
    {
    }

    GenerationFailureCode code() const noexcept { return code_; }

private:
    GenerationFailureCode code_;
};

} // namespace rmg
