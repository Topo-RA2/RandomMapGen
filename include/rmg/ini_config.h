#pragma once

#include "rmg/native_generator.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rmg {

class IniDocument {
public:
    static IniDocument loadFile(const std::filesystem::path& path);

    bool hasSection(const std::string& section) const;
    std::string getString(const std::string& section, const std::string& key, const std::string& fallback) const;
    int getInt(const std::string& section, const std::string& key, int fallback) const;
    std::uint32_t getUInt32(const std::string& section, const std::string& key, std::uint32_t fallback) const;
    double getDouble(const std::string& section, const std::string& key, double fallback) const;
    bool getBool(const std::string& section, const std::string& key, bool fallback) const;
    std::vector<std::pair<std::string, std::string>> sectionEntries(const std::string& section) const;

private:
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sections_;
};

void applyRandomMapSection(const IniDocument& ini, MapSeedOptions& options);
void applyRmgConfig(const IniDocument& ini, RmgConfig& config);

MapSeedOptions loadMapSeedOptions(const std::filesystem::path& path, MapSeedOptions fallback = {});
RmgConfig loadRmgConfig(const std::filesystem::path& path, RmgConfig fallback = {});

} // namespace rmg
