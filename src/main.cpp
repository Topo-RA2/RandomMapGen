#include "rmg/generation_error.h"
#include "rmg/ini_config.h"
#include "rmg/map_evaluation.h"
#include "rmg/map_writer.h"
#include "rmg/native_game_options.h"
#include "rmg/native_generator.h"
#include "rmg/native_theater_catalog.h"
#include "rmg/tile_catalog.h"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef RMG_GENERATOR_VERSION
#define RMG_GENERATOR_VERSION "1.1.0-native"
#endif

namespace {

using namespace rmg;

struct CliOptions {
    std::string mode = "native";
    std::string format = "yrini";
    std::string tilePreset = "yr";
    std::filesystem::path sedPath;
    std::filesystem::path rmgConfigPath;
    std::filesystem::path gameResourceDir;
    std::filesystem::path theaterIniPath;
    std::filesystem::path tileCatalogPath;
    std::filesystem::path output;
    std::filesystem::path productOutput;
    std::filesystem::path evaluationReportOutput;
    std::filesystem::path nativeOptionsReportOutput;

    std::optional<std::uint32_t> seed;
    std::optional<std::uint32_t> randomizationSeed;
    std::optional<int> width;
    std::optional<int> height;
    std::optional<int> players;
    std::optional<int> theater;
    std::optional<int> mapType;
    std::optional<int> resources;
    std::optional<int> time;
    std::optional<int> waterAmount;
    std::optional<int> tiberium;
    std::optional<int> tiberiumLayout;
    std::optional<int> vegetation;
    std::optional<int> ruggedness;
    std::optional<int> urbanPresence;
    std::optional<int> accessibility;
    std::optional<int> regionSize;
    std::optional<int> terrainType;
    std::optional<int> nativeScenarioUniqueId;

    std::optional<int> gameTerrainType;
    std::optional<int> timeOfDay;
    std::optional<int> climate;
    std::optional<int> mapSize;
};

void printUsage()
{
    std::cout << R"(RandomMapGen native-only

用法:
  rmg --mode native [原生参数] --out FILE [--format debug|yrini|yrini-product]
  rmg --mode game-native [游戏界面参数] --out FILE [--format ...]

通用参数:
  --mode native|game-native       生成模式，默认 native
  --out FILE                      主输出文件
  --product-out FILE              native/yrini 同时输出原版产品地图
  --format debug|yrini|yrini-product
  --rmg-config FILE               RMG/INI 调优参数
  --game-resource-dir DIR         用户自备的 RA2/YR 游戏资源目录
  --tile-preset yr                内置 TileCatalog，默认 yr
  --theater-ini FILE              从 Theater INI 加载 TileCatalog
  --tile-catalog FILE             覆盖 TileCatalog
  --evaluation-report-out FILE    写出地图评估 JSON
  --native-options-report-out FILE
  --version
  --help

native 参数:
  --seed N --width N --height N --players N --theater N
  --map-type N --resources N --time N --water-amount N
  --tiberium N --tiberium-layout N --vegetation N
  --ruggedness N --urban-presence N --accessibility N
  --region-size N --terrain-type N --sed FILE
  --native-scenario-unique-id N

game-native 参数:
  --randomization-seed N --game-terrain-type 1..4 --time-of-day 0..3
  --climate 0..1 --map-size 0..3 --resources 0..3 --players 2..8
)";
}

int parseInt(std::string_view text, std::string_view name)
{
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument(std::string(name) + " 必须是整数");
    }
    return value;
}

std::uint32_t parseUInt32(std::string_view text, std::string_view name)
{
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument(std::string(name) + " 必须是 0..4294967295 的整数");
    }
    return value;
}

std::pair<std::string_view, std::optional<std::string_view>> splitArgument(std::string_view arg)
{
    if (!arg.starts_with("--")) {
        throw std::invalid_argument("未知位置参数: " + std::string(arg));
    }
    const std::size_t equals = arg.find('=');
    if (equals == std::string_view::npos) {
        return {arg, std::nullopt};
    }
    return {arg.substr(0, equals), arg.substr(equals + 1)};
}

std::string_view requireValue(
    std::string_view name,
    std::optional<std::string_view> inlineValue,
    int& index,
    int argc,
    char** argv)
{
    if (inlineValue.has_value()) {
        return *inlineValue;
    }
    if (index + 1 >= argc) {
        throw std::invalid_argument(std::string(name) + " 缺少参数值");
    }
    return argv[++index];
}

CliOptions parseArguments(int argc, char** argv)
{
    CliOptions result;
    for (int index = 1; index < argc; ++index) {
        const auto [name, inlineValue] = splitArgument(argv[index]);
        auto value = [&]() {
            return requireValue(name, inlineValue, index, argc, argv);
        };
        if (name == "--help" || name == "-h") {
            printUsage();
            std::exit(0);
        }
        if (name == "--version") {
            std::cout << "RandomMapGen " << RMG_GENERATOR_VERSION << '\n';
            std::exit(0);
        }
        if (name == "--mode") {
            result.mode = std::string(value());
        } else if (name == "--format") {
            result.format = std::string(value());
        } else if (name == "--out") {
            result.output = std::string(value());
        } else if (name == "--product-out") {
            result.productOutput = std::string(value());
        } else if (name == "--sed") {
            result.sedPath = std::string(value());
        } else if (name == "--rmg-config") {
            result.rmgConfigPath = std::string(value());
        } else if (name == "--game-resource-dir") {
            result.gameResourceDir = std::string(value());
        } else if (name == "--theater-ini") {
            result.theaterIniPath = std::string(value());
        } else if (name == "--tile-catalog") {
            result.tileCatalogPath = std::string(value());
        } else if (name == "--tile-preset") {
            result.tilePreset = std::string(value());
        } else if (name == "--evaluation-report-out") {
            result.evaluationReportOutput = std::string(value());
        } else if (name == "--native-options-report-out") {
            result.nativeOptionsReportOutput = std::string(value());
        } else if (name == "--seed") {
            result.seed = parseUInt32(value(), name);
        } else if (name == "--randomization-seed") {
            result.randomizationSeed = parseUInt32(value(), name);
        } else if (name == "--width") {
            result.width = parseInt(value(), name);
        } else if (name == "--height") {
            result.height = parseInt(value(), name);
        } else if (name == "--players") {
            result.players = parseInt(value(), name);
        } else if (name == "--theater") {
            result.theater = parseInt(value(), name);
        } else if (name == "--map-type") {
            result.mapType = parseInt(value(), name);
        } else if (name == "--resources") {
            result.resources = parseInt(value(), name);
        } else if (name == "--time") {
            result.time = parseInt(value(), name);
        } else if (name == "--water-amount") {
            result.waterAmount = parseInt(value(), name);
        } else if (name == "--tiberium") {
            result.tiberium = parseInt(value(), name);
        } else if (name == "--tiberium-layout") {
            result.tiberiumLayout = parseInt(value(), name);
        } else if (name == "--vegetation") {
            result.vegetation = parseInt(value(), name);
        } else if (name == "--ruggedness") {
            result.ruggedness = parseInt(value(), name);
        } else if (name == "--urban-presence") {
            result.urbanPresence = parseInt(value(), name);
        } else if (name == "--accessibility") {
            result.accessibility = parseInt(value(), name);
        } else if (name == "--region-size") {
            result.regionSize = parseInt(value(), name);
        } else if (name == "--terrain-type") {
            result.terrainType = parseInt(value(), name);
        } else if (name == "--native-scenario-unique-id") {
            result.nativeScenarioUniqueId = parseInt(value(), name);
        } else if (name == "--game-terrain-type") {
            result.gameTerrainType = parseInt(value(), name);
        } else if (name == "--time-of-day") {
            result.timeOfDay = parseInt(value(), name);
        } else if (name == "--climate") {
            result.climate = parseInt(value(), name);
        } else if (name == "--map-size") {
            result.mapSize = parseInt(value(), name);
        } else {
            throw std::invalid_argument("未知参数: " + std::string(name));
        }
    }
    if (result.output.empty()) {
        throw std::invalid_argument("必须提供 --out");
    }
    if (result.mode != "native" && result.mode != "game-native") {
        throw std::invalid_argument("--mode 仅支持 native 或 game-native");
    }
    if (result.format != "debug" && result.format != "yrini" && result.format != "yrini-product") {
        throw std::invalid_argument("--format 仅支持 debug、yrini 或 yrini-product");
    }
    if (!result.productOutput.empty() && result.format != "yrini") {
        throw std::invalid_argument("--product-out 仅可与 --format yrini 同时使用");
    }
    if (result.mode == "native") {
        if (result.randomizationSeed.has_value()
            || result.gameTerrainType.has_value()
            || result.timeOfDay.has_value()
            || result.climate.has_value()
            || result.mapSize.has_value()) {
            throw std::invalid_argument("game-native 参数不能用于 native 模式");
        }
        if (!result.nativeOptionsReportOutput.empty()) {
            throw std::invalid_argument("--native-options-report-out 仅用于 game-native");
        }
    }
    if (!result.tilePreset.empty() && !isKnownTileCatalogPreset(result.tilePreset)) {
        throw std::invalid_argument("未知 TileCatalog preset: " + result.tilePreset);
    }
    return result;
}

void requireRange(const char* name, int value, int minimum, int maximum)
{
    if (value < minimum || value > maximum) {
        throw std::invalid_argument(
            std::string(name) + " 必须在 " + std::to_string(minimum)
            + ".." + std::to_string(maximum) + " 之间");
    }
}

void validateNativeOptions(const MapSeedOptions& options, bool gameNative)
{
    requireRange("width", options.width, gameNative ? 0 : 1, 512);
    requireRange("height", options.height, gameNative ? 0 : 1, 512);
    requireRange("players", options.numPlayers, 2, 8);
    requireRange("theater", options.theater, 0, 5);
    requireRange("map-type", options.mapType, 0, 4);
    requireRange("resources", options.resources, 0, 4);
    requireRange("time", options.time, 0, 4);
    requireRange("water-amount", options.waterAmount, 0, 100);
    requireRange("tiberium", options.tiberium, 0, 100);
    requireRange("tiberium-layout", options.tiberiumLayout, 0, 100);
    requireRange("vegetation", options.vegetation, 0, 100);
    requireRange("ruggedness", options.ruggedness, 0, 100);
    requireRange("urban-presence", options.urbanPresence, 0, 100);
    requireRange("accessibility", options.accessibility, 0, 100);
    requireRange("region-size", options.regionSize, 0, 100);
    requireRange("terrain-type", options.terrainTypeOverride, -1, 2);
    if (options.nativeScenarioUniqueId < -1) {
        throw std::invalid_argument("native-scenario-unique-id 必须为 -1 或非负整数");
    }
}

void rejectNativeOverridesForGameMode(const CliOptions& cli)
{
    if (cli.seed.has_value()
        || cli.width.has_value()
        || cli.height.has_value()
        || cli.theater.has_value()
        || cli.mapType.has_value()
        || cli.time.has_value()
        || cli.waterAmount.has_value()
        || cli.tiberium.has_value()
        || cli.tiberiumLayout.has_value()
        || cli.vegetation.has_value()
        || cli.ruggedness.has_value()
        || cli.urbanPresence.has_value()
        || cli.accessibility.has_value()
        || cli.regionSize.has_value()
        || cli.terrainType.has_value()
        || !cli.sedPath.empty()) {
        throw std::invalid_argument(
            "game-native 不接受原生底层覆盖参数；请使用游戏界面参数");
    }
}

MapSeedOptions makeNativeOptions(const CliOptions& cli)
{
    MapSeedOptions options;
    if (!cli.sedPath.empty()) {
        options = loadMapSeedOptions(cli.sedPath, options);
    }
    if (cli.seed.has_value()) {
        options.seed = *cli.seed;
    }
    if (cli.width.has_value()) {
        if (options.nativeSeedWidth < 0) {
            options.nativeSeedWidth = *cli.width;
        }
        options.width = *cli.width;
        options.nativeWidthInterpolation = false;
    }
    if (cli.height.has_value()) {
        if (options.nativeSeedHeight < 0) {
            options.nativeSeedHeight = *cli.height;
        }
        options.height = *cli.height;
        options.nativeHeightInterpolation = false;
    }
    if (cli.players.has_value()) {
        options.numPlayers = *cli.players;
    }
    if (cli.theater.has_value()) {
        options.theater = *cli.theater;
    }
    if (cli.mapType.has_value()) {
        options.mapType = *cli.mapType;
    }
    if (cli.resources.has_value()) {
        options.resources = *cli.resources;
    }
    if (cli.time.has_value()) {
        options.time = *cli.time;
    }
    if (cli.waterAmount.has_value()) {
        options.waterAmount = *cli.waterAmount;
    }
    if (cli.tiberium.has_value()) {
        options.tiberium = *cli.tiberium;
    }
    if (cli.tiberiumLayout.has_value()) {
        options.tiberiumLayout = *cli.tiberiumLayout;
    }
    if (cli.vegetation.has_value()) {
        options.vegetation = *cli.vegetation;
    }
    if (cli.ruggedness.has_value()) {
        options.ruggedness = *cli.ruggedness;
    }
    if (cli.urbanPresence.has_value()) {
        options.urbanPresence = *cli.urbanPresence;
    }
    if (cli.accessibility.has_value()) {
        options.accessibility = *cli.accessibility;
    }
    if (cli.regionSize.has_value()) {
        options.regionSize = *cli.regionSize;
    }
    if (cli.terrainType.has_value()) {
        options.terrainTypeOverride = *cli.terrainType;
    }
    if (cli.nativeScenarioUniqueId.has_value()) {
        options.nativeScenarioUniqueId = *cli.nativeScenarioUniqueId;
    }
    validateNativeOptions(options, false);
    return options;
}

MapSeedOptions makeGameNativeOptions(
    const CliOptions& cli,
    const RmgConfig& config,
    NativeGameOptions& gameOptions,
    NativeGameOptionsResolution& resolution)
{
    rejectNativeOverridesForGameMode(cli);
    gameOptions = {
        cli.gameTerrainType.value_or(1),
        cli.timeOfDay.value_or(0),
        cli.climate.value_or(0),
        cli.mapSize.value_or(0),
        cli.resources.value_or(0),
        cli.players.value_or(2),
        cli.randomizationSeed.value_or(0),
    };
    resolution = deriveNativeGameOptions(gameOptions, config);
    resolution.mapSeedOptions.nativeScenarioUniqueId =
        cli.nativeScenarioUniqueId.value_or(-1);
    validateNativeOptions(resolution.mapSeedOptions, true);
    return resolution.mapSeedOptions;
}

TileCatalog loadTileCatalogForCli(const CliOptions& cli, int theater)
{
    TileCatalog catalog;
    if (!cli.tilePreset.empty()) {
        catalog = loadTileCatalogPreset(cli.tilePreset, theater);
    }
    if (!cli.theaterIniPath.empty()) {
        catalog = loadTileCatalogFromTheaterIni(cli.theaterIniPath, catalog);
    }
    if (!cli.tileCatalogPath.empty()) {
        catalog = loadTileCatalog(cli.tileCatalogPath, catalog);
    }
    return catalog;
}

std::string withCrLf(std::string text)
{
    std::string converted;
    converted.reserve(text.size() + text.size() / 64);
    for (const char character : text) {
        if (character == '\n' && (converted.empty() || converted.back() != '\r')) {
            converted.push_back('\r');
        }
        converted.push_back(character);
    }
    return converted;
}

std::string serializeOutput(
    const CliOptions& cli,
    const MapGrid& map,
    const MapDocument& document)
{
    if (cli.format == "debug") {
        return writeTextMap(map, document);
    }
    if (cli.format == "yrini-product") {
        return withCrLf(writeYrIniProductMap(map, document));
    }
    return writeYrIniMap(map, document);
}

struct PendingFile {
    std::filesystem::path finalPath;
    std::filesystem::path temporaryPath;
};

void preflightOutput(const std::filesystem::path& path)
{
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        throw std::runtime_error("输出文件已存在，拒绝覆盖: " + path.string());
    }
    if (error) {
        throw std::runtime_error("无法检查输出路径: " + path.string());
    }
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            throw std::runtime_error("无法创建输出目录: " + path.parent_path().string());
        }
    }
}

PendingFile writeTemporaryFile(const std::filesystem::path& path, std::string_view contents)
{
    preflightOutput(path);
    PendingFile pending{path, path};
    pending.temporaryPath += ".tmp";
    std::error_code error;
    if (std::filesystem::exists(pending.temporaryPath, error)) {
        throw std::runtime_error("临时输出文件已存在: " + pending.temporaryPath.string());
    }
    std::ofstream file(pending.temporaryPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("无法创建临时输出: " + pending.temporaryPath.string());
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    file.flush();
    if (!file) {
        std::filesystem::remove(pending.temporaryPath, error);
        throw std::runtime_error("写入临时输出失败: " + pending.temporaryPath.string());
    }
    return pending;
}

void publishFiles(const std::vector<std::pair<std::filesystem::path, std::string>>& outputs)
{
    for (std::size_t left = 0; left < outputs.size(); ++left) {
        for (std::size_t right = left + 1; right < outputs.size(); ++right) {
            if (outputs[left].first == outputs[right].first) {
                throw std::invalid_argument("输出路径重复: " + outputs[left].first.string());
            }
        }
    }
    std::vector<PendingFile> pending;
    std::size_t publishedCount = 0;
    pending.reserve(outputs.size());
    try {
        for (const auto& [path, contents] : outputs) {
            pending.push_back(writeTemporaryFile(path, contents));
        }
        for (const PendingFile& file : pending) {
            std::error_code error;
            std::filesystem::rename(file.temporaryPath, file.finalPath, error);
            if (error) {
                throw std::runtime_error("发布输出失败: " + file.finalPath.string());
            }
            ++publishedCount;
        }
    } catch (...) {
        std::error_code error;
        for (const PendingFile& file : pending) {
            std::filesystem::remove(file.temporaryPath, error);
        }
        for (std::size_t index = 0; index < publishedCount; ++index) {
            std::filesystem::remove(pending[index].finalPath, error);
        }
        throw;
    }
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const CliOptions cli = parseArguments(argc, argv);
        RmgConfig config;
        if (!cli.rmgConfigPath.empty()) {
            config = loadRmgConfig(cli.rmgConfigPath, config);
        }

        NativeGameOptions gameOptions;
        NativeGameOptionsResolution gameResolution;
        const MapSeedOptions options = cli.mode == "game-native"
            ? makeGameNativeOptions(cli, config, gameOptions, gameResolution)
            : makeNativeOptions(cli);

        std::optional<NativeTheaterCatalog> theaterCatalog;
        if (!cli.gameResourceDir.empty()) {
            theaterCatalog.emplace(loadNativeTheaterCatalog(
                cli.gameResourceDir,
                options.theater));
        }

        NativeMapGenerator generator(
            theaterCatalog.has_value() ? &*theaterCatalog : nullptr);
        MapGrid map = generator.generate(options, config);
        if (generator.report().startsPlaced != options.numPlayers) {
            throw GenerationError(
                GenerationFailureCode::StartsInsufficient,
                "地图仅放置了 " + std::to_string(generator.report().startsPlaced)
                    + "/" + std::to_string(options.numPlayers) + " 个出生点");
        }

        MapDocument document = makeNativeDocument(
            options,
            config,
            generator.report(),
            map);
        document.mode = cli.mode;
        document.tileCatalog = theaterCatalog.has_value()
            ? theaterCatalog->exportCatalog
            : loadTileCatalogForCli(cli, options.theater);

        const std::string primary = serializeOutput(cli, map, document);
        std::vector<std::pair<std::filesystem::path, std::string>> outputs;
        outputs.emplace_back(cli.output, primary);
        if (!cli.productOutput.empty()) {
            outputs.emplace_back(
                cli.productOutput,
                withCrLf(writeYrIniProductMap(map, document)));
        }
        if (!cli.evaluationReportOutput.empty()) {
            outputs.emplace_back(
                cli.evaluationReportOutput,
                serializeMapEvaluation(generator.report().evaluation));
        }
        if (!cli.nativeOptionsReportOutput.empty()
            && cli.mode == "game-native") {
            outputs.emplace_back(
                cli.nativeOptionsReportOutput,
                serializeNativeGameOptionsReport(gameOptions, gameResolution));
        }
        publishFiles(outputs);

        std::cout << "generated " << cli.mode << " map: " << cli.output.string() << '\n';
        return 0;
    } catch (const GenerationError& error) {
        std::cerr << "rmg: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "rmg: " << error.what() << "\n\n";
        printUsage();
        return 1;
    }
}
