#include "rmg/tile_catalog.h"

#include "rmg/ini_config.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

namespace rmg {
namespace {

std::vector<int> parseIntList(const std::string& value)
{
    std::vector<int> result;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) {
            result.push_back(std::stoi(item));
        }
    }
    return result;
}

TileMapping readMapping(const IniDocument& ini, const std::string& key, TileMapping fallback)
{
    const std::string raw = ini.getString("Tiles", key, "");
    if (raw.empty()) {
        return fallback;
    }

    const auto values = parseIntList(raw);
    if (values.empty()) {
        return fallback;
    }

    TileMapping mapping = fallback;
    mapping.tileNum = static_cast<std::uint32_t>(std::max(0, values[0]));
    if (values.size() > 1) {
        mapping.subTileBase = static_cast<std::uint8_t>(std::clamp(values[1], 0, 255));
    }
    if (values.size() > 2) {
        mapping.subTileCount = static_cast<std::uint8_t>(std::clamp(values[2], 1, 255));
    }
    if (values.size() > 3) {
        mapping.variantRemap.assign(values.begin() + 3, values.end());
    }
    return mapping;
}

std::string tileSetSectionName(int index)
{
    std::ostringstream out;
    out << "TileSet" << std::setw(4) << std::setfill('0') << index;
    return out.str();
}

std::vector<int> buildFirstTileBySet(const IniDocument& ini)
{
    std::vector<int> firstTileBySet;
    int firstTile = 0;
    for (int index = 0;; ++index) {
        const std::string section = tileSetSectionName(index);
        if (!ini.hasSection(section)) {
            break;
        }
        firstTileBySet.push_back(firstTile);
        firstTile += std::max(0, ini.getInt(section, "TilesInSet", 0));
    }
    return firstTileBySet;
}

TileMapping mappingFromSet(const IniDocument& ini, const std::vector<int>& firstTileBySet, const std::string& key, TileMapping fallback)
{
    const int setNum = ini.getInt("General", key, -1);
    if (setNum < 0 || setNum >= static_cast<int>(firstTileBySet.size())) {
        return fallback;
    }

    const std::string section = tileSetSectionName(setNum);
    TileMapping mapping = fallback;
    mapping.tileNum = static_cast<std::uint32_t>(firstTileBySet[static_cast<std::size_t>(setNum)]);
    mapping.tileCount = static_cast<std::uint16_t>(std::clamp(ini.getInt(section, "TilesInSet", 1), 1, 0xFFFF));
    mapping.subTileBase = 0;
    mapping.subTileCount = 1;
    mapping.variantRemap.clear();
    return mapping;
}

TileMapping mapping(std::uint32_t tileNum, std::uint16_t tileCount, std::uint8_t subTileBase = 0, std::uint8_t subTileCount = 1)
{
    TileMapping result;
    result.tileNum = tileNum;
    result.tileCount = tileCount;
    result.subTileBase = subTileBase;
    result.subTileCount = subTileCount;
    return result;
}

TileCatalog yrTemperateLikeCatalog()
{
    TileCatalog catalog;
    catalog.land = mapping(0, 1);
    catalog.ramp = mapping(1, 18);
    catalog.water = mapping(314, 14);
    catalog.shore = mapping(89, 42);
    catalog.dirt = mapping(418, 1);
    catalog.rough = mapping(131, 1);
    catalog.pave = mapping(534, 1);
    catalog.green = mapping(493, 1);
    catalog.cliff = mapping(49, 40);
    catalog.dirtLat = mapping(419, 16);
    catalog.roughLat = mapping(132, 16);
    catalog.paveLat = mapping(463, 16);
    catalog.greenLat = mapping(494, 16);
    return catalog;
}

TileCatalog yrSnowCatalog()
{
    TileCatalog catalog;
    catalog.land = mapping(0, 1);
    catalog.ramp = mapping(1, 18);
    catalog.water = mapping(314, 14);
    catalog.shore = mapping(89, 42);
    catalog.dirt = mapping(696, 1);
    catalog.rough = mapping(131, 1);
    catalog.pave = mapping(734, 1);
    catalog.green = mapping(713, 1);
    catalog.cliff = mapping(49, 40);
    catalog.dirtLat = mapping(697, 16);
    catalog.roughLat = mapping(132, 16);
    catalog.paveLat = mapping(749, 16);
    catalog.greenLat = mapping(714, 16);
    return catalog;
}

TileCatalog yrDesertCatalog()
{
    TileCatalog catalog;
    catalog.land = mapping(461, 1);
    catalog.ramp = mapping(1, 18);
    catalog.water = mapping(286, 14);
    catalog.shore = mapping(89, 42);
    catalog.dirt = mapping(386, 1);
    catalog.rough = mapping(131, 1);
    catalog.pave = mapping(502, 1);
    catalog.green = mapping(461, 1);
    catalog.cliff = mapping(49, 40);
    catalog.dirtLat = mapping(387, 16);
    catalog.roughLat = mapping(132, 16);
    catalog.paveLat = mapping(431, 16);
    catalog.greenLat = mapping(462, 16);
    return catalog;
}

TileCatalog yrLunarCatalog()
{
    TileCatalog catalog;
    catalog.land = mapping(109, 1);
    catalog.ramp = mapping(1, 18);
    catalog.water = mapping(109, 1);
    catalog.shore = mapping(109, 1);
    catalog.dirt = mapping(76, 1);
    catalog.rough = mapping(49, 1);
    catalog.pave = mapping(150, 1);
    catalog.green = mapping(109, 1);
    catalog.cliff = mapping(49, 1);
    catalog.dirtLat = mapping(77, 16);
    catalog.roughLat = mapping(50, 16);
    catalog.paveLat = mapping(93, 16);
    catalog.greenLat = mapping(110, 16);
    return catalog;
}

std::string normalizePresetName(const std::string& preset)
{
    std::string normalized;
    normalized.reserve(preset.size());
    for (const unsigned char ch : preset) {
        if (ch != '-' && ch != '_') {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return normalized;
}

} // namespace

TileCatalog loadTileCatalog(const std::filesystem::path& path, TileCatalog fallback)
{
    const IniDocument ini = IniDocument::loadFile(path);
    fallback.land = readMapping(ini, "Land", fallback.land);
    fallback.ramp = readMapping(ini, "Ramp", fallback.ramp);
    fallback.water = readMapping(ini, "Water", fallback.water);
    fallback.shore = readMapping(ini, "Shore", fallback.shore);
    fallback.dirt = readMapping(ini, "Dirt", fallback.dirt);
    fallback.rough = readMapping(ini, "Rough", fallback.rough);
    fallback.pave = readMapping(ini, "Pave", fallback.pave);
    fallback.green = readMapping(ini, "Green", fallback.green);
    fallback.cliff = readMapping(ini, "Cliff", fallback.cliff);
    fallback.dirtLat = readMapping(ini, "DirtLat", fallback.dirtLat);
    fallback.roughLat = readMapping(ini, "RoughLat", fallback.roughLat);
    fallback.paveLat = readMapping(ini, "PaveLat", fallback.paveLat);
    fallback.greenLat = readMapping(ini, "GreenLat", fallback.greenLat);
    return fallback;
}

TileCatalog loadTileCatalogFromTheaterIni(const std::filesystem::path& path, TileCatalog fallback)
{
    const IniDocument ini = IniDocument::loadFile(path);
    const auto firstTileBySet = buildFirstTileBySet(ini);
    if (firstTileBySet.empty()) {
        return fallback;
    }

    fallback.land = mappingFromSet(ini, firstTileBySet, "ClearTile", fallback.land);
    fallback.ramp = mappingFromSet(ini, firstTileBySet, "RampBase", fallback.ramp);
    fallback.ramp.tileCount = 18;
    fallback.water = mappingFromSet(ini, firstTileBySet, "WaterSet", fallback.water);
    fallback.shore = mappingFromSet(ini, firstTileBySet, "ShorePieces", fallback.shore);
    fallback.dirt = mappingFromSet(ini, firstTileBySet, "SandTile", fallback.dirt);
    fallback.rough = mappingFromSet(ini, firstTileBySet, "RoughTile", fallback.rough);
    if (fallback.rough.tileNum == TileCatalog{}.rough.tileNum) {
        fallback.rough = mappingFromSet(ini, firstTileBySet, "RoughGround", fallback.rough);
    }
    fallback.pave = mappingFromSet(ini, firstTileBySet, "PaveTile", fallback.pave);
    if (fallback.pave.tileNum == TileCatalog{}.pave.tileNum) {
        fallback.pave = mappingFromSet(ini, firstTileBySet, "MiscPaveTile", fallback.pave);
    }
    fallback.green = mappingFromSet(ini, firstTileBySet, "GreenTile", fallback.green);
    fallback.cliff = mappingFromSet(ini, firstTileBySet, "CliffSet", fallback.cliff);
    fallback.dirtLat = mappingFromSet(ini, firstTileBySet, "ClearToSandLat", fallback.dirt);
    fallback.roughLat = mappingFromSet(ini, firstTileBySet, "ClearToRoughLat", fallback.rough);
    fallback.paveLat = mappingFromSet(ini, firstTileBySet, "ClearToPaveLat", fallback.pave);
    fallback.greenLat = mappingFromSet(ini, firstTileBySet, "ClearToGreenLat", fallback.green);
    return fallback;
}

bool isKnownTileCatalogPreset(const std::string& preset)
{
    const std::string normalized = normalizePresetName(preset);
    return normalized == "yr" || normalized == "yurisrevenge";
}

TileCatalog loadTileCatalogPreset(const std::string& preset, int theater)
{
    if (!isKnownTileCatalogPreset(preset)) {
        throw std::runtime_error("unknown tile catalog preset: " + preset);
    }
    switch (theater) {
    case 1:
        return yrSnowCatalog();
    case 3:
        return yrDesertCatalog();
    case 5:
        return yrLunarCatalog();
    case 0:
    case 2:
    case 4:
    default:
        return yrTemperateLikeCatalog();
    }
}

TileMapping tileMappingForTerrain(const TileCatalog& catalog, Terrain terrain)
{
    switch (terrain) {
    case Terrain::Water:
        return catalog.water;
    case Terrain::Shore:
        return catalog.shore;
    case Terrain::Dirt:
        return catalog.dirt;
    case Terrain::Rough:
        return catalog.rough;
    case Terrain::Pave:
        return catalog.pave;
    case Terrain::Green:
        return catalog.green;
    case Terrain::Cliff:
        return catalog.cliff;
    case Terrain::Land:
        return catalog.land;
    }
    return catalog.land;
}

TileMapping latTileMappingForTerrain(const TileCatalog& catalog, Terrain terrain)
{
    switch (terrain) {
    case Terrain::Dirt:
        return catalog.dirtLat;
    case Terrain::Rough:
        return catalog.roughLat;
    case Terrain::Pave:
        return catalog.paveLat;
    case Terrain::Green:
        return catalog.greenLat;
    case Terrain::Water:
    case Terrain::Shore:
    case Terrain::Cliff:
    case Terrain::Land:
        break;
    }
    return tileMappingForTerrain(catalog, terrain);
}

std::uint32_t mappedTileNum(const TileMapping& mapping, int variant)
{
    const int count = std::max(1, static_cast<int>(mapping.tileCount));
    int normalized = ((variant % count) + count) % count;
    if (!mapping.variantRemap.empty()) {
        const int remapIndex = ((variant % static_cast<int>(mapping.variantRemap.size())) + static_cast<int>(mapping.variantRemap.size())) % static_cast<int>(mapping.variantRemap.size());
        normalized = ((mapping.variantRemap[static_cast<std::size_t>(remapIndex)] % count) + count) % count;
    }
    return mapping.tileNum + static_cast<std::uint32_t>(normalized);
}

std::uint8_t mappedSubTile(const TileMapping& mapping, int variant)
{
    const int count = std::max(1, static_cast<int>(mapping.subTileCount));
    const int normalized = ((variant % count) + count) % count;
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(mapping.subTileBase) + normalized, 0, 255));
}

} // namespace rmg
