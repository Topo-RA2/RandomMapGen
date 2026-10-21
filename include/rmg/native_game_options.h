#pragma once

#include "rmg/native_generator.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace rmg {

inline constexpr std::size_t kMaximumNativeGameOptionsReportBytes = 8U * 1024U;

struct NativeGameOptions {
    int terrainType = 1;
    int timeOfDay = 0;
    int climate = 0;
    int mapSize = 0;
    int resources = 0;
    int players = 2;
    std::uint32_t randomizationSeed = 0;
};

struct NativeGameOptionsResolution {
    MapSeedOptions mapSeedOptions;
    int playableWidth = 0;
    int playableHeight = 0;
    std::uint64_t randomDrawCount = 0;
};

NativeGameOptionsResolution deriveNativeGameOptions(
    const NativeGameOptions& options,
    const RmgConfig& config = {});

std::string serializeNativeGameOptionsReport(
    const NativeGameOptions& options,
    const NativeGameOptionsResolution& resolution);

NativeGameOptionsResolution validateNativeGameOptionsReport(
    std::string_view report,
    const NativeGameOptions& expectedOptions,
    const RmgConfig& config = {});

NativeGameOptionsResolution validateNativeGameOptionsReportFile(
    const std::filesystem::path& path,
    const NativeGameOptions& expectedOptions,
    const RmgConfig& config = {});

void verifyNativeGameOptions();

} // namespace rmg
