#pragma once

#include "rmg/map_types.h"
#include "rmg/native_generator.h"
#include "rmg/tile_catalog.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rmg {

struct LightingSettings {
    bool enabled = false;
    int levelIndex = 0;
    int ambient = 100;
    int red = 100;
    int green = 100;
    int blue = 100;
    int dominatorAmbient = 150;
    int dominatorRed = 85;
    int dominatorGreen = 20;
    int dominatorBlue = 30;
    int dominatorGround = 0;
    int dominatorLevel = 0;
    int dominatorAmbientChangeRate = 1;
};

struct BasicSceneSettings {
    int newIniFormat = 4;
    bool tiberiumGrowthEnabled = true;
    bool veinGrowthEnabled = true;
    bool iceGrowthEnabled = true;
    bool tiberiumDeathToVisceroid = true;
    bool freeRadar = false;
    int initTime = 10000;
};

struct MultiplayerSessionSettings {
    bool bridgeDestruction = true;
    bool crates = false;
};

struct MapDocument {
    std::string mode;
    std::uint32_t seed = 0;
    int players = 0;
    int theater = 0;
    int mapLevel = 4;
    Rect mapRect;
    Rect localSize;
    Rect headerRect;
    BasicSceneSettings basicScene;
    MultiplayerSessionSettings multiplayerSession;
    LightingSettings lighting;
    TileCatalog tileCatalog;
    RmgConfig rmgConfig;
    std::vector<Point> starts;
    std::vector<IsoTileOverride> isoTileOverrides;
    std::vector<NativeOverlayOverride> nativeOverlayOverrides;
    std::vector<NativeObjectOverride> nativeObjectOverrides;
    std::vector<NativeStructureOverride> nativeStructureOverrides;
    std::vector<Point> nativeWaypoints;
    std::vector<std::pair<std::string, std::string>> metrics;
    std::vector<std::string> notes;
};

std::string writeTextMap(const MapGrid& map, const MapDocument& document);
std::string writeYrIniMap(const MapGrid& map, const MapDocument& document);
std::string writeYrIniProductMap(const MapGrid& map, const MapDocument& document);
void writeTextMapFile(const std::filesystem::path& path, const MapGrid& map, const MapDocument& document);
void writeYrIniMapFile(const std::filesystem::path& path, const MapGrid& map, const MapDocument& document);
void writeYrIniProductMapFile(const std::filesystem::path& path, const MapGrid& map, const MapDocument& document);
MapDocument makeNativeDocument(const MapSeedOptions& options, const RmgConfig& config, const NativeGenerationReport& report, const MapGrid& map);

} // namespace rmg
