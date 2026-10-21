#include "rmg/native_game_options.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace rmg {
namespace {

struct Range {
    int minimum;
    int maximum;
};

struct TerrainRanges {
    Range water;
    Range ruggedness;
    Range urbanPresence;
    Range accessibility;
    Range regionSize;
};

constexpr std::array<TerrainRanges, 4> kTerrainRanges{{
    {{0, 25}, {20, 100}, {0, 100}, {0, 100}, {0, 100}},
    {{50, 100}, {20, 100}, {0, 100}, {0, 100}, {35, 100}},
    {{0, 100}, {20, 100}, {0, 100}, {0, 100}, {0, 100}},
    {{0, 100}, {20, 100}, {0, 0}, {0, 20}, {0, 50}},
}};

void requireRange(const char* name, int value, int minimum, int maximum)
{
    if (value < minimum || value > maximum) {
        throw std::invalid_argument(
            std::string(name) + " must be between " + std::to_string(minimum)
            + " and " + std::to_string(maximum));
    }
}

Range vegetationRange(const RmgConfig& config, int terrainType)
{
    const std::size_t index = static_cast<std::size_t>(terrainType);
    if (config.vegetationMinimums.size() <= index) {
        throw std::invalid_argument(
            "vegetationMinimums must contain an entry for terrainType "
            + std::to_string(terrainType));
    }
    if (config.vegetationMaximums.size() <= index) {
        throw std::invalid_argument(
            "vegetationMaximums must contain an entry for terrainType "
            + std::to_string(terrainType));
    }

    Range range{
        std::clamp(config.vegetationMinimums[index], 0, 100),
        std::clamp(config.vegetationMaximums[index], 0, 100),
    };
    if (range.minimum > range.maximum) {
        range.minimum = range.maximum;
    }
    return range;
}

int drawRange(NativeRandomizer& randomizer, Range range)
{
    return randomizer.randomRanged(range.minimum, range.maximum);
}

bool sameMapSeedOptions(const MapSeedOptions& left, const MapSeedOptions& right)
{
    return left.theater == right.theater
        && left.mapType == right.mapType
        && left.resources == right.resources
        && left.ruggedness == right.ruggedness
        && left.time == right.time
        && left.waterAmount == right.waterAmount
        && left.numPlayers == right.numPlayers
        && left.tiberium == right.tiberium
        && left.tiberiumLayout == right.tiberiumLayout
        && left.vegetation == right.vegetation
        && left.urbanPresence == right.urbanPresence
        && left.width == right.width
        && left.height == right.height
        && left.accessibility == right.accessibility
        && left.regionSize == right.regionSize
        && left.seed == right.seed
        && left.terrainTypeOverride == right.terrainTypeOverride
        && left.nativeWidthInterpolation == right.nativeWidthInterpolation
        && left.nativeHeightInterpolation == right.nativeHeightInterpolation
        && left.nativeSeedWidth == right.nativeSeedWidth
        && left.nativeSeedHeight == right.nativeSeedHeight;
}

void requireVerification(bool condition, const std::string& detail)
{
    if (!condition) {
        throw std::runtime_error("native game options self-test failed: " + detail);
    }
}

std::optional<std::uint64_t> nonNegativeInteger(const nlohmann::json& value)
{
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    if (value.is_number_integer()) {
        const std::int64_t integer = value.get<std::int64_t>();
        if (integer >= 0) {
            return static_cast<std::uint64_t>(integer);
        }
    }
    return std::nullopt;
}

bool exactlyMatchesJson(const nlohmann::json& actual, const nlohmann::json& expected)
{
    if (expected.is_object()) {
        if (!actual.is_object() || actual.size() != expected.size()) {
            return false;
        }
        for (auto it = expected.begin(); it != expected.end(); ++it) {
            const auto actualIt = actual.find(it.key());
            if (actualIt == actual.end() || !exactlyMatchesJson(*actualIt, it.value())) {
                return false;
            }
        }
        return true;
    }
    if (expected.is_number_integer() || expected.is_number_unsigned()) {
        const auto actualInteger = nonNegativeInteger(actual);
        const auto expectedInteger = nonNegativeInteger(expected);
        return actualInteger.has_value()
            && expectedInteger.has_value()
            && *actualInteger == *expectedInteger;
    }
    return actual.type() == expected.type() && actual == expected;
}

template <typename Mutator>
void requireRejected(const NativeGameOptions& base, Mutator mutator, const char* label)
{
    NativeGameOptions invalid = base;
    mutator(invalid);
    bool rejected = false;
    try {
        static_cast<void>(deriveNativeGameOptions(invalid));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    requireVerification(rejected, std::string("accepted invalid ") + label);
}

} // namespace

NativeGameOptionsResolution deriveNativeGameOptions(
    const NativeGameOptions& options,
    const RmgConfig& config)
{
    requireRange("terrainType", options.terrainType, 1, 4);
    requireRange("timeOfDay", options.timeOfDay, 0, 3);
    requireRange("climate", options.climate, 0, 1);
    requireRange("mapSize", options.mapSize, 0, 3);
    requireRange("resources", options.resources, 0, 3);
    requireRange("players", options.players, 2, 8);

    const TerrainRanges& ranges = kTerrainRanges[static_cast<std::size_t>(options.terrainType - 1)];
    const Range vegetation = vegetationRange(config, options.terrainType);
    NativeRandomizer randomizer(options.randomizationSeed);

    MapSeedOptions derived;
    derived.theater = options.climate;
    derived.mapType = options.terrainType;
    derived.resources = options.resources;
    derived.time = options.timeOfDay;
    derived.numPlayers = options.players;
    derived.width = options.mapSize;
    derived.height = options.mapSize;
    derived.nativeSeedWidth = options.mapSize;
    derived.nativeSeedHeight = options.mapSize;
    derived.nativeWidthInterpolation = true;
    derived.nativeHeightInterpolation = true;
    derived.terrainTypeOverride = -1;

    derived.waterAmount = drawRange(randomizer, ranges.water);
    derived.ruggedness = drawRange(randomizer, ranges.ruggedness);
    derived.urbanPresence = drawRange(randomizer, ranges.urbanPresence);
    derived.accessibility = drawRange(randomizer, ranges.accessibility);
    derived.regionSize = drawRange(randomizer, ranges.regionSize);
    derived.tiberium = std::max(1, options.resources * 20);
    derived.tiberiumLayout = randomizer.randomRanged(0, 100);
    derived.vegetation = drawRange(randomizer, vegetation);
    derived.seed = static_cast<std::uint32_t>(randomizer.randomRanged(0, 65535));

    NativeGameOptionsResolution resolution;
    resolution.mapSeedOptions = derived;
    resolution.playableWidth = nativeInterpolatedPlayableSize(options.mapSize, options.players);
    resolution.playableHeight = resolution.playableWidth;
    resolution.randomDrawCount = randomizer.drawCount();
    return resolution;
}

std::string serializeNativeGameOptionsReport(
    const NativeGameOptions& options,
    const NativeGameOptionsResolution& resolution)
{
    const MapSeedOptions& derived = resolution.mapSeedOptions;
    std::ostringstream out;
    out << "{\n"
        << "  \"schemaVersion\": 1,\n"
        << "  \"input\": {\n"
        << "    \"mode\": \"game-native\",\n"
        << "    \"terrainType\": " << options.terrainType << ",\n"
        << "    \"timeOfDay\": " << options.timeOfDay << ",\n"
        << "    \"climate\": " << options.climate << ",\n"
        << "    \"mapSize\": " << options.mapSize << ",\n"
        << "    \"resources\": " << options.resources << ",\n"
        << "    \"players\": " << options.players << ",\n"
        << "    \"randomizationSeed\": " << options.randomizationSeed << "\n"
        << "  },\n"
        << "  \"derived\": {\n"
        << "    \"theater\": " << derived.theater << ",\n"
        << "    \"mapType\": " << derived.mapType << ",\n"
        << "    \"resources\": " << derived.resources << ",\n"
        << "    \"ruggedness\": " << derived.ruggedness << ",\n"
        << "    \"time\": " << derived.time << ",\n"
        << "    \"waterAmount\": " << derived.waterAmount << ",\n"
        << "    \"numPlayers\": " << derived.numPlayers << ",\n"
        << "    \"tiberium\": " << derived.tiberium << ",\n"
        << "    \"tiberiumLayout\": " << derived.tiberiumLayout << ",\n"
        << "    \"vegetation\": " << derived.vegetation << ",\n"
        << "    \"urbanPresence\": " << derived.urbanPresence << ",\n"
        << "    \"width\": " << derived.width << ",\n"
        << "    \"height\": " << derived.height << ",\n"
        << "    \"accessibility\": " << derived.accessibility << ",\n"
        << "    \"regionSize\": " << derived.regionSize << ",\n"
        << "    \"seed\": " << derived.seed << "\n"
        << "  },\n"
        << "  \"finalMapSeed\": " << derived.seed << ",\n"
        << "  \"playableWidth\": " << resolution.playableWidth << ",\n"
        << "  \"playableHeight\": " << resolution.playableHeight << ",\n"
        << "  \"rngDrawCount\": " << resolution.randomDrawCount << "\n"
        << "}\n";
    return out.str();
}

NativeGameOptionsResolution validateNativeGameOptionsReport(
    std::string_view report,
    const NativeGameOptions& expectedOptions,
    const RmgConfig& config)
{
    if (report.empty() || report.size() > kMaximumNativeGameOptionsReportBytes) {
        throw std::runtime_error("native options report must be between 1 and 8192 bytes");
    }

    const NativeGameOptionsResolution expectedResolution = deriveNativeGameOptions(expectedOptions, config);
    const nlohmann::json expected = nlohmann::json::parse(
        serializeNativeGameOptionsReport(expectedOptions, expectedResolution),
        nullptr,
        false);
    const nlohmann::json actual = nlohmann::json::parse(std::string(report), nullptr, false);
    if (actual.is_discarded() || expected.is_discarded() || !exactlyMatchesJson(actual, expected)) {
        throw std::runtime_error("native options report does not match recomputed values");
    }
    return expectedResolution;
}

NativeGameOptionsResolution validateNativeGameOptionsReportFile(
    const std::filesystem::path& path,
    const NativeGameOptions& expectedOptions,
    const RmgConfig& config)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > kMaximumNativeGameOptionsReportBytes) {
        throw std::runtime_error("native options report is missing or too large: " + path.string());
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open native options report: " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string report = buffer.str();
    if ((!input.good() && !input.eof()) || report.size() != size) {
        throw std::runtime_error("failed to read native options report: " + path.string());
    }
    return validateNativeGameOptionsReport(report, expectedOptions, config);
}

void verifyNativeGameOptions()
{
    constexpr std::array<std::array<int, 4>, 7> expectedSizes{{
        {{70, 73, 76, 80}},
        {{70, 73, 76, 80}},
        {{70, 73, 76, 80}},
        {{80, 83, 86, 90}},
        {{90, 93, 96, 100}},
        {{100, 103, 106, 110}},
        {{100, 106, 113, 120}},
    }};
    for (int players = 2; players <= 8; ++players) {
        for (int mapSize = 0; mapSize <= 3; ++mapSize) {
            requireVerification(
                nativeInterpolatedPlayableSize(mapSize, players)
                    == expectedSizes[static_cast<std::size_t>(players - 2)][static_cast<std::size_t>(mapSize)],
                "playable size golden mismatch for players=" + std::to_string(players)
                    + " mapSize=" + std::to_string(mapSize));
        }
    }

    std::size_t selectorCaseCount = 0;
    std::array<int, 7> playerCaseCounts{};
    const RmgConfig defaultConfig;
    for (int terrainType = 1; terrainType <= 4; ++terrainType) {
        for (int timeOfDay = 0; timeOfDay <= 3; ++timeOfDay) {
            for (int climate = 0; climate <= 1; ++climate) {
                for (int mapSize = 0; mapSize <= 3; ++mapSize) {
                    for (int resources = 0; resources <= 3; ++resources) {
                        for (int players = 2; players <= 8; ++players) {
                            NativeGameOptions input;
                            input.terrainType = terrainType;
                            input.timeOfDay = timeOfDay;
                            input.climate = climate;
                            input.mapSize = mapSize;
                            input.resources = resources;
                            input.players = players;
                            input.randomizationSeed = 0x9E3779B9u
                                * static_cast<std::uint32_t>(selectorCaseCount + 1);

                            const NativeGameOptionsResolution result = deriveNativeGameOptions(input, defaultConfig);
                            const NativeGameOptionsResolution repeated = deriveNativeGameOptions(input, defaultConfig);
                            const NativeGameOptionsResolution validated = validateNativeGameOptionsReport(
                                serializeNativeGameOptionsReport(input, result),
                                input,
                                defaultConfig);
                            const MapSeedOptions& derived = result.mapSeedOptions;
                            const TerrainRanges& ranges = kTerrainRanges[static_cast<std::size_t>(terrainType - 1)];
                            const Range vegetation = vegetationRange(defaultConfig, terrainType);
                            const int expectedPlayableSize = expectedSizes[static_cast<std::size_t>(input.players - 2)]
                                [static_cast<std::size_t>(mapSize)];
                            requireVerification(
                                sameMapSeedOptions(derived, repeated.mapSeedOptions)
                                    && sameMapSeedOptions(derived, validated.mapSeedOptions)
                                    && derived.theater == climate
                                    && derived.mapType == terrainType
                                    && derived.resources == resources
                                    && derived.time == timeOfDay
                                    && derived.numPlayers == input.players
                                    && derived.width == mapSize
                                    && derived.height == mapSize
                                    && derived.nativeSeedWidth == mapSize
                                    && derived.nativeSeedHeight == mapSize
                                    && derived.nativeWidthInterpolation
                                    && derived.nativeHeightInterpolation
                                    && derived.terrainTypeOverride == -1
                                    && derived.tiberium == std::max(1, resources * 20)
                                    && derived.waterAmount >= ranges.water.minimum
                                    && derived.waterAmount <= ranges.water.maximum
                                    && derived.ruggedness >= ranges.ruggedness.minimum
                                    && derived.ruggedness <= ranges.ruggedness.maximum
                                    && derived.urbanPresence >= ranges.urbanPresence.minimum
                                    && derived.urbanPresence <= ranges.urbanPresence.maximum
                                    && derived.accessibility >= ranges.accessibility.minimum
                                    && derived.accessibility <= ranges.accessibility.maximum
                                    && derived.regionSize >= ranges.regionSize.minimum
                                    && derived.regionSize <= ranges.regionSize.maximum
                                    && derived.tiberiumLayout >= 0
                                    && derived.tiberiumLayout <= 100
                                    && derived.vegetation >= vegetation.minimum
                                    && derived.vegetation <= vegetation.maximum
                                    && result.playableWidth == expectedPlayableSize
                                    && result.playableHeight == expectedPlayableSize
                                    && result.randomDrawCount > 0
                                    && repeated.randomDrawCount == result.randomDrawCount
                                    && validated.randomDrawCount == result.randomDrawCount,
                                "selector matrix mismatch at case=" + std::to_string(selectorCaseCount));
                            ++playerCaseCounts[static_cast<std::size_t>(input.players - 2)];
                            ++selectorCaseCount;
                        }
                    }
                }
            }
        }
    }
    requireVerification(selectorCaseCount == 3584, "selector matrix did not cover 3584 cases");
    requireVerification(
        std::all_of(playerCaseCounts.begin(), playerCaseCounts.end(), [](int count) { return count > 0; }),
        "selector matrix did not exercise every player count");

    struct Golden {
        int water;
        int ruggedness;
        int urbanPresence;
        int accessibility;
        int regionSize;
        int tiberiumLayout;
        int vegetation;
        std::uint32_t mapSeed;
        std::uint64_t draws;
    };
    constexpr std::array<Golden, 4> goldens{{
        {25, 97, 76, 97, 66, 6, 77, 58275u, 10},
        {63, 96, 97, 66, 41, 35, 92, 50959u, 12},
        {57, 97, 76, 97, 66, 6, 100, 17529u, 9},
        {57, 97, 0, 12, 33, 66, 100, 30982u, 8},
    }};
    for (int terrainType = 1; terrainType <= 4; ++terrainType) {
        NativeGameOptions input;
        input.terrainType = terrainType;
        input.timeOfDay = terrainType - 1;
        input.climate = terrainType % 2;
        input.mapSize = terrainType - 1;
        input.resources = terrainType - 1;
        input.players = terrainType + 1;
        input.randomizationSeed = 0x12345678u;
        const NativeGameOptionsResolution result = deriveNativeGameOptions(input);
        const MapSeedOptions& derived = result.mapSeedOptions;
        const Golden& expected = goldens[static_cast<std::size_t>(terrainType - 1)];
        requireVerification(
            derived.theater == input.climate
                && derived.mapType == input.terrainType
                && derived.resources == input.resources
                && derived.ruggedness == expected.ruggedness
                && derived.time == input.timeOfDay
                && derived.waterAmount == expected.water
                && derived.numPlayers == input.players
                && derived.tiberium == std::max(1, input.resources * 20)
                && derived.tiberiumLayout == expected.tiberiumLayout
                && derived.vegetation == expected.vegetation
                && derived.urbanPresence == expected.urbanPresence
                && derived.width == input.mapSize
                && derived.height == input.mapSize
                && derived.accessibility == expected.accessibility
                && derived.regionSize == expected.regionSize
                && derived.seed == expected.mapSeed
                && derived.terrainTypeOverride == -1
                && derived.nativeWidthInterpolation
                && derived.nativeHeightInterpolation
                && derived.nativeSeedWidth == input.mapSize
                && derived.nativeSeedHeight == input.mapSize
                && result.randomDrawCount == expected.draws,
            "terrain golden mismatch for terrainType=" + std::to_string(terrainType));
    }

    for (int resources = 0; resources <= 3; ++resources) {
        NativeGameOptions input;
        input.resources = resources;
        const int expected = resources == 0 ? 1 : resources * 20;
        requireVerification(
            deriveNativeGameOptions(input).mapSeedOptions.tiberium == expected,
            "resource mapping mismatch for resources=" + std::to_string(resources));
    }

    NativeGameOptions base;
    base.randomizationSeed = std::numeric_limits<std::uint32_t>::max();
    const NativeGameOptionsResolution first = deriveNativeGameOptions(base);
    const NativeGameOptionsResolution second = deriveNativeGameOptions(base);
    requireVerification(
        sameMapSeedOptions(first.mapSeedOptions, second.mapSeedOptions)
            && first.playableWidth == second.playableWidth
            && first.playableHeight == second.playableHeight
            && first.randomDrawCount == second.randomDrawCount,
        "same input was not deterministic");

    RmgConfig lowClampedConfig;
    lowClampedConfig.vegetationMinimums[1] = -10;
    lowClampedConfig.vegetationMaximums[1] = -5;
    requireVerification(
        deriveNativeGameOptions(base, lowClampedConfig).mapSeedOptions.vegetation == 0,
        "negative vegetation range did not clamp to zero");

    RmgConfig highClampedConfig;
    highClampedConfig.vegetationMinimums[1] = 110;
    highClampedConfig.vegetationMaximums[1] = 120;
    requireVerification(
        deriveNativeGameOptions(base, highClampedConfig).mapSeedOptions.vegetation == 100,
        "oversized vegetation range did not clamp to 100");

    RmgConfig reversedConfig;
    reversedConfig.vegetationMinimums[1] = 90;
    reversedConfig.vegetationMaximums[1] = 10;
    requireVerification(
        deriveNativeGameOptions(base, reversedConfig).mapSeedOptions.vegetation == 10,
        "reversed vegetation range did not collapse to maximum");

    for (bool truncateMinimums : {false, true}) {
        RmgConfig shortConfig;
        if (truncateMinimums) {
            shortConfig.vegetationMinimums.resize(1);
        } else {
            shortConfig.vegetationMaximums.resize(1);
        }
        bool rejected = false;
        try {
            static_cast<void>(deriveNativeGameOptions(base, shortConfig));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        requireVerification(rejected, "accepted an undersized vegetation list");
    }

    for (const NativeGameOptions valid : {
            NativeGameOptions{1, 0, 0, 0, 0, 2, 0u},
            NativeGameOptions{4, 3, 1, 3, 3, 8, std::numeric_limits<std::uint32_t>::max()},
        }) {
        static_cast<void>(deriveNativeGameOptions(valid));
    }
    requireRejected(base, [](NativeGameOptions& value) { value.terrainType = 0; }, "terrainType");
    requireRejected(base, [](NativeGameOptions& value) { value.terrainType = 5; }, "terrainType");
    requireRejected(base, [](NativeGameOptions& value) { value.timeOfDay = -1; }, "timeOfDay");
    requireRejected(base, [](NativeGameOptions& value) { value.timeOfDay = 4; }, "timeOfDay");
    requireRejected(base, [](NativeGameOptions& value) { value.climate = -1; }, "climate");
    requireRejected(base, [](NativeGameOptions& value) { value.climate = 2; }, "climate");
    requireRejected(base, [](NativeGameOptions& value) { value.mapSize = -1; }, "mapSize");
    requireRejected(base, [](NativeGameOptions& value) { value.mapSize = 4; }, "mapSize");
    requireRejected(base, [](NativeGameOptions& value) { value.resources = -1; }, "resources");
    requireRejected(base, [](NativeGameOptions& value) { value.resources = 4; }, "resources");
    requireRejected(base, [](NativeGameOptions& value) { value.players = 1; }, "players");
    requireRejected(base, [](NativeGameOptions& value) { value.players = 9; }, "players");

    const std::string report = serializeNativeGameOptionsReport(base, first);
    requireVerification(
        report.find("\"mode\": \"game-native\"") != std::string::npos
            && report.find("\"randomizationSeed\": 4294967295") != std::string::npos
            && report.find("\"finalMapSeed\": ") != std::string::npos
            && report.find("\"rngDrawCount\": ") != std::string::npos,
        "schema 1 report omitted native seed provenance");

    const NativeGameOptionsResolution validated = validateNativeGameOptionsReport(report, base);
    requireVerification(
        sameMapSeedOptions(validated.mapSeedOptions, first.mapSeedOptions)
            && validated.randomDrawCount == first.randomDrawCount,
        "schema 1 report did not survive strict recomputation");

    auto requireReportRejected = [&](std::string invalidReport, const char* label) {
        bool rejected = false;
        try {
            static_cast<void>(validateNativeGameOptionsReport(invalidReport, base));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        requireVerification(rejected, std::string("accepted invalid report ") + label);
    };
    std::string floatSchema = report;
    floatSchema.replace(floatSchema.find("\"schemaVersion\": 1"), 18, "\"schemaVersion\": 1.0");
    requireReportRejected(floatSchema, "with a non-integer schema version");
    std::string extraField = report;
    extraField.insert(extraField.find("\n") + 1, "  \"unexpected\": 1,\n");
    requireReportRejected(extraField, "with an extra root field");
}

} // namespace rmg
