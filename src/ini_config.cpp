#include "rmg/ini_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace rmg {
namespace {

std::string trim(std::string value)
{
    auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) {
        return !isSpace(static_cast<unsigned char>(ch));
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) {
        return !isSpace(static_cast<unsigned char>(ch));
    }).base(), value.end());
    return value;
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::vector<int> parseIntList(const std::string& value)
{
    std::vector<int> result;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            result.push_back(std::stoi(item));
        }
    }
    return result;
}

std::vector<std::string> parseStringList(const std::string& value)
{
    std::vector<std::string> result;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            result.push_back(item);
        }
    }
    return result;
}

void applyList(const IniDocument& ini, const std::string& section, const std::string& key, std::vector<int>& target)
{
    const std::string raw = ini.getString(section, key, "");
    if (raw.empty()) {
        return;
    }
    auto values = parseIntList(raw);
    if (!values.empty()) {
        target = std::move(values);
    }
}

void applyStringList(const IniDocument& ini, const std::string& section, const std::string& key, std::vector<std::string>& target)
{
    const std::string raw = ini.getString(section, key, "");
    if (raw.empty()) {
        return;
    }
    auto values = parseStringList(raw);
    if (!values.empty()) {
        target = std::move(values);
    }
}

} // namespace

IniDocument IniDocument::loadFile(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("failed to open INI file: " + path.string());
    }

    IniDocument document;
    std::string section;
    std::string line;
    while (std::getline(file, line)) {
        const auto comment = line.find_first_of(";#");
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            document.sections_[section];
            continue;
        }

        const auto equals = line.find('=');
        if (equals == std::string::npos || section.empty()) {
            continue;
        }
        const std::string key = lower(trim(line.substr(0, equals)));
        const std::string value = trim(line.substr(equals + 1));
        document.sections_[section][key] = value;
    }
    return document;
}

bool IniDocument::hasSection(const std::string& section) const
{
    return sections_.find(lower(section)) != sections_.end();
}

std::string IniDocument::getString(const std::string& section, const std::string& key, const std::string& fallback) const
{
    const auto sectionIt = sections_.find(lower(section));
    if (sectionIt == sections_.end()) {
        return fallback;
    }
    const auto keyIt = sectionIt->second.find(lower(key));
    if (keyIt == sectionIt->second.end()) {
        return fallback;
    }
    return keyIt->second;
}

int IniDocument::getInt(const std::string& section, const std::string& key, int fallback) const
{
    const std::string value = getString(section, key, "");
    if (value.empty()) {
        return fallback;
    }
    return std::stoi(value);
}

std::uint32_t IniDocument::getUInt32(const std::string& section, const std::string& key, std::uint32_t fallback) const
{
    const std::string value = getString(section, key, "");
    if (value.empty()) {
        return fallback;
    }
    if (value.front() == '-' || value.front() == '+') {
        throw std::runtime_error(section + "." + key + " must be an unsigned 32-bit integer");
    }

    std::size_t parsedLength = 0;
    const unsigned long long parsed = std::stoull(value, &parsedLength, 10);
    if (parsedLength != value.size() || parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(section + "." + key + " must be between 0 and 4294967295");
    }
    return static_cast<std::uint32_t>(parsed);
}

double IniDocument::getDouble(const std::string& section, const std::string& key, double fallback) const
{
    const std::string value = getString(section, key, "");
    if (value.empty()) {
        return fallback;
    }
    return std::stod(value);
}

bool IniDocument::getBool(const std::string& section, const std::string& key, bool fallback) const
{
    const std::string value = lower(getString(section, key, ""));
    if (value.empty()) {
        return fallback;
    }
    if (value == "true" || value == "yes" || value == "1") {
        return true;
    }
    if (value == "false" || value == "no" || value == "0") {
        return false;
    }
    return fallback;
}

std::vector<std::pair<std::string, std::string>> IniDocument::sectionEntries(const std::string& section) const
{
    const auto sectionIt = sections_.find(lower(section));
    if (sectionIt == sections_.end()) {
        return {};
    }
    return std::vector<std::pair<std::string, std::string>>(
        sectionIt->second.begin(),
        sectionIt->second.end());
}

void applyRandomMapSection(const IniDocument& ini, MapSeedOptions& options)
{
    constexpr const char* section = "RandomMap";
    options.theater = ini.getInt(section, "Theater", options.theater);
    options.mapType = ini.getInt(section, "MapType", options.mapType);
    options.resources = ini.getInt(section, "Resources", options.resources);
    options.ruggedness = ini.getInt(section, "Ruggedness", options.ruggedness);
    options.time = ini.getInt(section, "Time", options.time);
    options.waterAmount = ini.getInt(section, "WaterAmount", options.waterAmount);
    options.numPlayers = ini.getInt(section, "NumPlayers", options.numPlayers);
    options.numPlayers = ini.getInt(section, "Players", options.numPlayers);
    options.tiberium = ini.getInt(section, "Tiberium", options.tiberium);
    options.tiberiumLayout = ini.getInt(section, "TiberiumLayout", options.tiberiumLayout);
    options.vegetation = ini.getInt(section, "Vegetation", options.vegetation);
    options.urbanPresence = ini.getInt(section, "UrbanPresence", options.urbanPresence);
    options.width = ini.getInt(section, "Width", options.width);
    options.height = ini.getInt(section, "Height", options.height);
    options.accessibility = ini.getInt(section, "Accessibility", options.accessibility);
    options.regionSize = ini.getInt(section, "RegionSize", options.regionSize);
    // Origin random-map generation keeps MapSeedClass' terrain base level at 4;
    // [RandomMap] Level is not a core terrain-generation input.
    options.terrainTypeOverride = ini.getInt(section, "TerrainType", options.terrainTypeOverride);
    options.seed = ini.getUInt32(section, "Seed", options.seed);
}

void applyRmgConfig(const IniDocument& ini, RmgConfig& config)
{
    for (const std::string section : {"General", "RandomMap", "RMG", "RMGMD"}) {
        config.minimumTiberium = ini.getInt(section, "RMGMinimumTiberium", config.minimumTiberium);
        config.maximumTiberium = ini.getInt(section, "RMGMaximumTiberium", config.maximumTiberium);
        config.maxTrees = ini.getInt(section, "MaxTrees", config.maxTrees);
        applyList(ini, section, "RMGLevelLightSettings", config.levelLightSettings);
        applyList(ini, section, "RMGVegetationMinimums", config.vegetationMinimums);
        applyList(ini, section, "RMGVegetationMaximums", config.vegetationMaximums);
        applyList(ini, section, "TemperateAmbientLight", config.temperateAmbientLight);
        applyList(ini, section, "SnowAmbientLight", config.snowAmbientLight);
        applyList(ini, section, "TemperateAmbientRed", config.temperateAmbientRed);
        applyList(ini, section, "TemperateAmbientGreen", config.temperateAmbientGreen);
        applyList(ini, section, "TemperateAmbientBlue", config.temperateAmbientBlue);
        applyList(ini, section, "SnowAmbientRed", config.snowAmbientRed);
        applyList(ini, section, "SnowAmbientGreen", config.snowAmbientGreen);
        applyList(ini, section, "SnowAmbientBlue", config.snowAmbientBlue);
        applyStringList(ini, section, "RMGTechBuildings", config.techBuildings);
        applyStringList(ini, section, "RMGCivilianBuildings", config.civilianBuildings);
        applyStringList(ini, section, "RMGNeutralUnits", config.neutralUnits);
        applyStringList(ini, section, "RMGTerrainChainObjects", config.terrainChainObjects);
        applyStringList(ini, section, "TemperateOrePatchLamps", config.temperateOrePatchLamps);
        applyStringList(ini, section, "SnowOrePatchLamps", config.snowOrePatchLamps);
    }
}

MapSeedOptions loadMapSeedOptions(const std::filesystem::path& path, MapSeedOptions fallback)
{
    const IniDocument ini = IniDocument::loadFile(path);
    applyRandomMapSection(ini, fallback);
    fallback.nativeSeedWidth = fallback.width;
    fallback.nativeSeedHeight = fallback.height;
    fallback.nativeWidthInterpolation = true;
    fallback.nativeHeightInterpolation = true;
    return fallback;
}

RmgConfig loadRmgConfig(const std::filesystem::path& path, RmgConfig fallback)
{
    const IniDocument ini = IniDocument::loadFile(path);
    applyRmgConfig(ini, fallback);
    return fallback;
}

} // namespace rmg
