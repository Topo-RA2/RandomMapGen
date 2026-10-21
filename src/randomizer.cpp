#include "rmg/randomizer.h"

#include "rmg/native_trace.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace rmg {
namespace {

// Wrap_Randomizer_Random_598000 multiplies by dbl_7ED898 = 1 / UINT32_MAX.
constexpr double kUInt32Unit = 1.0 / 4294967295.0;
const double kUInt32UnitStorage = kUInt32Unit;
const double kOne = 1.0;
constexpr float kNativeAtanScale = std::bit_cast<float>(0x3CC7FE84u);
constexpr float kNativeAtanPiOverTwo = std::bit_cast<float>(0x3FC90FDBu);
constexpr std::array<std::uint32_t, 4097> kNativeAtanTableBits{
#include "native_atan_table.inc"
};
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

bool matchesGaussianTracePair(const std::vector<std::uint32_t>* rawValues)
{
    const char* raw0Text = std::getenv("RMG_NATIVE_GAUSSIAN_TRACE_RAW0");
    const char* raw1Text = std::getenv("RMG_NATIVE_GAUSSIAN_TRACE_RAW1");
    if (raw0Text == nullptr || raw1Text == nullptr || rawValues == nullptr || rawValues->size() < 2) {
        return false;
    }
    const std::uint32_t expected0 = static_cast<std::uint32_t>(std::strtoul(raw0Text, nullptr, 10));
    const std::uint32_t expected1 = static_cast<std::uint32_t>(std::strtoul(raw1Text, nullptr, 10));
    return (*rawValues)[rawValues->size() - 2] == expected0
        && (*rawValues)[rawValues->size() - 1] == expected1;
}

double patchedFloatSqrt(double value) noexcept
{
    return static_cast<double>(nativeApproximateSqrt(value));
}

double nativeX87Chop53(double rounded, double exactMinusRounded) noexcept
{
    const bool roundedAwayFromZero = (rounded > 0.0 && exactMinusRounded < 0.0)
        || (rounded < 0.0 && exactMinusRounded > 0.0);
    return roundedAwayFromZero ? std::nextafter(rounded, 0.0) : rounded;
}

double nativeX87Chop53Divide(double left, double right) noexcept
{
    volatile double storedRounded = left / right;
    const double rounded = storedRounded;
    const double remainder = std::fma(-rounded, right, left);
    return nativeX87Chop53(rounded, remainder / right);
}

double nativeX87Chop53Multiply(double left, double right) noexcept
{
    volatile double storedRounded = left * right;
    const double rounded = storedRounded;
    return nativeX87Chop53(rounded, std::fma(left, right, -rounded));
}

double nativeX87Chop53Add(double left, double right) noexcept
{
    volatile double storedRounded = left + right;
    const double rounded = storedRounded;
    volatile double storedVirtualRight = rounded - left;
    const double virtualRight = storedVirtualRight;
    volatile double storedVirtualLeft = rounded - virtualRight;
    const double virtualLeft = storedVirtualLeft;
    volatile double storedLeftError = left - virtualLeft;
    volatile double storedRightError = right - virtualRight;
    return nativeX87Chop53(rounded, storedLeftError + storedRightError);
}

#if defined(RMG_HAS_NATIVE_X87_HELPER)
extern "C" void rmg_native_x87_gaussian_scale_argument(
    std::uint32_t rawX,
    std::uint32_t rawY,
    double* x,
    double* y,
    double* radiusSquared,
    double* scaleArgument);

void x87GaussianScaleArgument(
    std::uint32_t rawX,
    std::uint32_t rawY,
    double* x,
    double* y,
    double* radiusSquared,
    double* scaleArgument)
{
    rmg_native_x87_gaussian_scale_argument(
        rawX,
        rawY,
        x,
        y,
        radiusSquared,
        scaleArgument);
}

double x87Mul(double left, double right)
{
    return nativeX87Chop53Multiply(left, right);
}
#elif defined(_MSC_VER) && defined(_M_IX86)
void x87GaussianScaleArgument(
    std::uint32_t rawX,
    std::uint32_t rawY,
    double* x,
    double* y,
    double* radiusSquared,
    double* scaleArgument)
{
    const std::uint64_t rawX64 = rawX;
    const std::uint64_t rawY64 = rawY;
    __asm {
        mov eax, x
        fild qword ptr [rawX64]
        fmul qword ptr [kUInt32UnitStorage]
        fadd st(0), st(0)
        fsub qword ptr [kOne]
        fstp qword ptr [eax]

        mov eax, y
        fild qword ptr [rawY64]
        fmul qword ptr [kUInt32UnitStorage]
        fadd st(0), st(0)
        fsub qword ptr [kOne]
        fst qword ptr [eax]

        fld st(0)
        fmulp st(1), st(0)
        mov eax, x
        fld qword ptr [eax]
        fmul qword ptr [eax]
        faddp st(1), st(0)
        mov eax, radiusSquared
        fst qword ptr [eax]

        fld st(0)
        fldln2
        fxch st(1)
        fyl2x
        fld st(0)
        fchs
        fsub st(0), st(1)
        fdiv st(0), st(2)
        mov eax, scaleArgument
        fstp qword ptr [eax]
        fstp st(0)
        fstp st(0)
    }
}

double x87Mul(double left, double right)
{
    double result = 0.0;
    __asm {
        fld qword ptr [left]
        fmul qword ptr [right]
        fstp qword ptr [result]
    }
    return result;
}
#endif

constexpr std::array<std::uint32_t, 4> kSeedMixA{
    0xBAA96887u,
    0x1E17D32Cu,
    0x03BCDC3Cu,
    0x0F33D1B2u,
};

constexpr std::array<std::uint32_t, 5> kSeedMixB{
    0x48AAD7E4u,
    0x4B0F3B58u,
    0xE874F0C3u,
    0x6955C5A6u,
    0x55A7CA46u,
};

std::uint32_t mixSeedStep(std::uint32_t tableIndex, std::uint32_t seed, int step)
{
    const std::uint32_t v7 = tableIndex ^ kSeedMixA[static_cast<std::size_t>(step)];
    const std::int32_t lo = static_cast<std::int32_t>(v7 & 0xFFFFu);
    const std::int32_t hi = static_cast<std::int32_t>(v7) >> 16;
    const std::uint32_t loSquare = static_cast<std::uint32_t>(static_cast<std::int64_t>(lo) * lo);
    const std::uint32_t hiSquare = static_cast<std::uint32_t>(hi * hi);
    const std::uint32_t squareFold = loSquare + ~hiSquare;
    const std::uint32_t rotatedFold = static_cast<std::uint32_t>(
        (static_cast<std::int32_t>(squareFold) >> 16) | (squareFold << 16));
    return seed ^ static_cast<std::uint32_t>((lo * hi) + (kSeedMixB[static_cast<std::size_t>(step + 1)] ^ rotatedFold));
}

void hashByte(std::uint64_t& hash, std::uint8_t value)
{
    hash ^= value;
    hash *= kFnvPrime;
}

void hashU32(std::uint64_t& hash, std::uint32_t value)
{
    hashByte(hash, static_cast<std::uint8_t>(value & 0xFFu));
    hashByte(hash, static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    hashByte(hash, static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    hashByte(hash, static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

void hashU64(std::uint64_t& hash, std::uint64_t value)
{
    hashU32(hash, static_cast<std::uint32_t>(value & 0xFFFFFFFFull));
    hashU32(hash, static_cast<std::uint32_t>(value >> 32));
}

} // namespace

float nativeApproximateSqrt(double value) noexcept
{
    const double magnitude = std::fabs(value);
    float input = static_cast<float>(magnitude);
    // sqrt_4CAC40 stores the x87 input to a float while the generator keeps
    // control word 0x0E7F (PC53 + RC_CHOP). x64 casts use nearest rounding, so
    // step back when that cast rounded a positive value upward.
    if (static_cast<double>(input) > magnitude) {
        input = std::nextafter(input, 0.0f);
    }
    if (input == 0.0f) {
        return 0.0f;
    }

    const std::uint32_t inputBits = std::bit_cast<std::uint32_t>(input);
    const int exponent = static_cast<int>((inputBits >> 23) & 0xFFu) - 127;
    std::uint32_t mantissa = inputBits & 0x7FFFFFu;
    if ((exponent & 1) != 0) {
        mantissa |= 0x800000u;
    }

    const std::uint32_t tableIndex = mantissa >> 10;
    const double normalized = tableIndex < 8192u
        ? 1.0 + static_cast<double>(tableIndex) / 8192.0
        : 2.0 + static_cast<double>(tableIndex - 8192u) / 4096.0;
    const std::uint32_t tableValue = static_cast<std::uint32_t>(
        std::floor((std::sqrt(normalized) - 1.0) * 8388608.0));
    const std::uint32_t outputExponent = static_cast<std::uint32_t>(
        (static_cast<std::int16_t>(exponent) >> 1) + 127) << 23;
    return std::bit_cast<float>(outputExponent + tableValue);
}

float nativeApproximateAtan(double value) noexcept
{
    // The original keeps the input as a double for the division, then
    // double2int_7C5F00 truncates the quotient with x87 PC53/RC_CHOP.
    const double scaled = nativeX87Chop53Divide(value, static_cast<double>(kNativeAtanScale));
    if (!std::isfinite(scaled)) {
        return std::signbit(value) ? -kNativeAtanPiOverTwo : kNativeAtanPiOverTwo;
    }

    const std::int64_t truncated = static_cast<std::int64_t>(scaled);
    const std::uint64_t magnitude = truncated < 0
        ? static_cast<std::uint64_t>(-(truncated + 1)) + 1u
        : static_cast<std::uint64_t>(truncated);
    const float result = magnitude >= kNativeAtanTableBits.size()
        ? kNativeAtanPiOverTwo
        : std::bit_cast<float>(kNativeAtanTableBits[static_cast<std::size_t>(magnitude)]);
    return value < 0.0 ? -result : result;
}

NativeRandomizer::NativeRandomizer(std::uint32_t seed)
{
    next1_ = 0;
    next2_ = 103;

    for (std::uint32_t i = 0; i < table_.size(); ++i) {
        std::uint32_t previous = seed;
        std::uint32_t value = i;
        for (int step = 0; step < 4; ++step) {
            const std::uint32_t oldValue = value;
            value = mixSeedStep(value, previous, step);
            previous = oldValue;
        }
        table_[i] = value;
    }
}

std::uint32_t NativeRandomizer::random()
{
    const std::uint32_t traceNext1 = static_cast<std::uint32_t>(next1_);
    const std::uint32_t traceNext2 = static_cast<std::uint32_t>(next2_);
    ++draws_;
    if (disabled_) {
        lastRandom_ = 0;
        native_trace::randomizerRaw(0, traceNext1, traceNext2);
        return 0;
    }

    table_[static_cast<std::size_t>(next1_)] ^= table_[static_cast<std::size_t>(next2_)];
    const std::uint32_t result = table_[static_cast<std::size_t>(next1_)];
    lastRandom_ = result;

    ++next1_;
    ++next2_;
    if (next1_ >= static_cast<int>(table_.size())) {
        next1_ = 0;
    }
    if (next2_ >= static_cast<int>(table_.size())) {
        next2_ = 0;
    }
    native_trace::randomizerRaw(result, traceNext1, traceNext2);
    if (const char* traceRawText = std::getenv("RMG_NATIVE_RANDOM_TRACE_RAW");
        traceRawText != nullptr && traceRawText[0] != '\0') {
        const std::uint32_t traceRaw = static_cast<std::uint32_t>(std::strtoul(traceRawText, nullptr, 0));
        if (result == traceRaw) {
            std::fprintf(
                stderr,
                "native-random raw=0x%08X draw=%llu next=(%u,%u)\n",
                result,
                static_cast<unsigned long long>(draws_),
                traceNext1,
                traceNext2);
        }
    }
    return result;
}

std::uint64_t NativeRandomizer::stateFingerprint() const noexcept
{
    std::uint64_t hash = kFnvOffset;
    hashU32(hash, disabled_ ? 1u : 0u);
    hashU32(hash, static_cast<std::uint32_t>(next1_));
    hashU32(hash, static_cast<std::uint32_t>(next2_));
    hashU64(hash, draws_);
    hashU32(hash, hasGaussianSpare_ ? 1u : 0u);
    hashU64(hash, std::bit_cast<std::uint64_t>(gaussianSpare_));
    for (const std::uint32_t value : table_) {
        hashU32(hash, value);
    }
    return hash;
}

int NativeRandomizer::randomRanged(int minValue, int maxValue)
{
    if (minValue == maxValue) {
        return minValue;
    }

    int lo = std::min(minValue, maxValue);
    int hi = std::max(minValue, maxValue);
    const std::uint32_t span = static_cast<std::uint32_t>(hi - lo);

    int topBit = 31;
    while (topBit > 0 && ((1u << topBit) & span) == 0) {
        --topBit;
    }
    const std::uint32_t mask = ~static_cast<std::uint32_t>(-1 << (topBit + 1));

    std::uint32_t candidate = span + 1;
    while (candidate > span) {
        candidate = random() & mask;
    }
    return lo + static_cast<int>(candidate);
}

int NativeRandomizer::randomScaledInclusive(int minValue, int maxValue)
{
    const int lo = std::min(minValue, maxValue);
    const int hi = std::max(minValue, maxValue);
    const double span = static_cast<double>(static_cast<std::int64_t>(hi) - static_cast<std::int64_t>(lo) + 1);
    const double base = static_cast<double>(lo);

    int candidate = hi + 1;
    while (candidate > hi) {
        const double raw = static_cast<double>(random());
        const double rawTimesSpan = nativeX87Chop53Multiply(raw, span);
        const double scaled = nativeX87Chop53Multiply(rawTimesSpan, kUInt32Unit);
        candidate = static_cast<int>(nativeX87Chop53Add(scaled, base));
    }
    return candidate;
}

std::uint64_t NativeRandomizer::gaussianSpareBits() const noexcept
{
    return std::bit_cast<std::uint64_t>(gaussianSpare_);
}

double NativeRandomizer::randomUnit()
{
    return static_cast<double>(random()) * kUInt32Unit;
}

double NativeRandomizer::gaussian()
{
    return gaussianWithRawTrace(nullptr);
}

double NativeRandomizer::gaussianWithRawTrace(std::vector<std::uint32_t>* rawValues)
{
    const bool trace = native_trace::active();
    if (trace) {
        native_trace::setGaussianState(hasGaussianSpare_, gaussianSpareBits());
        native_trace::enter(0x5980C0, native_trace::GaussianRandom);
    }
    if (hasGaussianSpare_) {
        hasGaussianSpare_ = false;
        if (trace) {
            native_trace::setGaussianState(hasGaussianSpare_, gaussianSpareBits());
            native_trace::exit(
                0x5980D7,
                native_trace::GaussianRandom,
                std::bit_cast<std::uint64_t>(gaussianSpare_));
        }
        return gaussianSpare_;
    }

#if defined(RMG_HAS_NATIVE_X87_HELPER) || (defined(_MSC_VER) && defined(_M_IX86))
    double x = 0.0;
    double y = 0.0;
    double radiusSquared = 0.0;
    double scaleArgument = 0.0;
    do {
        do {
            if (trace) {
                native_trace::gaussianRawCallSite();
            }
            const std::uint32_t rawX = random();
            if (rawValues != nullptr) {
                rawValues->push_back(rawX);
            }
            if (trace) {
                native_trace::gaussianRawCallSite();
            }
            const std::uint32_t rawY = random();
            if (rawValues != nullptr) {
                rawValues->push_back(rawY);
            }
            x87GaussianScaleArgument(rawX, rawY, &x, &y, &radiusSquared, &scaleArgument);
        } while (radiusSquared >= 1.0);
    } while (radiusSquared == 0.0);

    const double scale = patchedFloatSqrt(scaleArgument);
    gaussianSpare_ = x87Mul(scale, y);
    hasGaussianSpare_ = true;
    const double result = x87Mul(scale, x);
    if (matchesGaussianTracePair(rawValues)) {
        std::fprintf(
            stderr,
            "gaussian raw=%u,%u x=%.17g y=%.17g radius=%.17g scaleArgument=%.17g scale=%.17g result=%.17g spare=%.17g\n",
            (*rawValues)[rawValues->size() - 2],
            (*rawValues)[rawValues->size() - 1],
            x,
            y,
            radiusSquared,
            scaleArgument,
            scale,
            result,
            gaussianSpare_);
    }
    if (trace) {
        native_trace::setGaussianState(hasGaussianSpare_, gaussianSpareBits());
        native_trace::exit(
            0x598157,
            native_trace::GaussianRandom,
            std::bit_cast<std::uint64_t>(result));
    }
    return result;
#else
    double x = 0.0;
    double y = 0.0;
    double radiusSquared = 0.0;
    do {
        do {
            if (trace) {
                native_trace::gaussianRawCallSite();
            }
            const std::uint32_t rawX = random();
            if (rawValues != nullptr) {
                rawValues->push_back(rawX);
            }
            x = nativeX87Chop53Multiply(static_cast<double>(rawX), kUInt32Unit);
            x = nativeX87Chop53Add(x, x);
            x = nativeX87Chop53Add(x, -1.0);

            if (trace) {
                native_trace::gaussianRawCallSite();
            }
            const std::uint32_t rawY = random();
            if (rawValues != nullptr) {
                rawValues->push_back(rawY);
            }
            y = nativeX87Chop53Multiply(static_cast<double>(rawY), kUInt32Unit);
            y = nativeX87Chop53Add(y, y);
            y = nativeX87Chop53Add(y, -1.0);
            const double ySquared = nativeX87Chop53Multiply(y, y);
            const double xSquared = nativeX87Chop53Multiply(x, x);
            radiusSquared = nativeX87Chop53Add(ySquared, xSquared);
        } while (radiusSquared >= 1.0);
    } while (radiusSquared == 0.0);

    const double logValue = std::log(radiusSquared);
    const double scaleArgument = nativeX87Chop53Divide(
        nativeX87Chop53Add(-logValue, -logValue),
        radiusSquared);
    const double scale = static_cast<double>(nativeApproximateSqrt(scaleArgument));
    gaussianSpare_ = nativeX87Chop53Multiply(scale, y);
    hasGaussianSpare_ = true;
    const double result = nativeX87Chop53Multiply(scale, x);
    if (matchesGaussianTracePair(rawValues)) {
        std::fprintf(
            stderr,
            "gaussian raw=%u,%u x=%.17g y=%.17g radius=%.17g scaleArgument=%.17g scale=%.17g result=%.17g spare=%.17g\n",
            (*rawValues)[rawValues->size() - 2],
            (*rawValues)[rawValues->size() - 1],
            x,
            y,
            radiusSquared,
            scaleArgument,
            scale,
            result,
            gaussianSpare_);
    }
    if (trace) {
        native_trace::setGaussianState(hasGaussianSpare_, gaussianSpareBits());
        native_trace::exit(
            0x598157,
            native_trace::GaussianRandom,
            std::bit_cast<std::uint64_t>(result));
    }
    return result;
#endif
}

} // namespace rmg
