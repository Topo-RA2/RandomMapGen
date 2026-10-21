#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace rmg {

float nativeApproximateSqrt(double value) noexcept;

// gamemd's atan_4CADE0 uses a 4097-entry single-precision lookup table.
float nativeApproximateAtan(double value) noexcept;

class NativeRandomizer {
public:
    explicit NativeRandomizer(std::uint32_t seed);

    std::uint32_t random();
    int randomRanged(int minValue, int maxValue);
    int randomScaledInclusive(int minValue, int maxValue);
    double randomUnit();
    double gaussian();
    double gaussianWithRawTrace(std::vector<std::uint32_t>* rawValues);
    std::uint64_t drawCount() const noexcept { return draws_; }
    std::uint32_t lastRandom() const noexcept { return lastRandom_; }
    std::uint32_t next1() const noexcept { return static_cast<std::uint32_t>(next1_); }
    std::uint32_t next2() const noexcept { return static_cast<std::uint32_t>(next2_); }
    bool hasGaussianSpare() const noexcept { return hasGaussianSpare_; }
    std::uint64_t gaussianSpareBits() const noexcept;
    std::uint64_t stateFingerprint() const noexcept;

private:
    bool disabled_ = false;
    int next1_ = 0;
    int next2_ = 103;
    std::uint64_t draws_ = 0;
    std::uint32_t lastRandom_ = 0;
    std::array<std::uint32_t, 250> table_{};
    bool hasGaussianSpare_ = false;
    double gaussianSpare_ = 0.0;
};

} // namespace rmg
