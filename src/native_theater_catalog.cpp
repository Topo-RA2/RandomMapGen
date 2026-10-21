#include "rmg/native_theater_catalog.h"

#include "rmg/native_resource.h"
#include "ra2formats/ini.h"
#include "ra2formats/tmp.h"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rmg {
namespace {

struct TheaterTile {
    int setNumber = 0;
    std::vector<std::string> variants;
};

std::string tileSetSectionName(int index)
{
    std::ostringstream name;
    name << "TileSet" << std::setfill('0') << std::setw(4) << index;
    return name.str();
}

std::string tileNumberSuffix(int number)
{
    std::ostringstream suffix;
    suffix << std::setfill('0') << std::setw(2) << number;
    return suffix.str();
}

std::string findTileFile(
    const native_assets::ResourceFileSystem& resources,
    const native_assets::TheaterDefinition& theater,
    const std::string& base)
{
    const std::string primary = base + theater.extension;
    if (resources.contains(primary)) {
        return primary;
    }
    if (theater.name == "NEWURBAN") {
        const std::string fallback = base + ".tem";
        if (resources.contains(fallback)) {
            return fallback;
        }
    }
    return {};
}

int firstTileForSet(const std::vector<TheaterTile>& tiles, int setNumber) noexcept
{
    for (std::size_t index = 0; index < tiles.size(); ++index) {
        if (tiles[index].setNumber == setNumber) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int tileCountForSet(const std::vector<TheaterTile>& tiles, int setNumber) noexcept
{
    return static_cast<int>(std::count_if(
        tiles.begin(),
        tiles.end(),
        [setNumber](const TheaterTile& tile) { return tile.setNumber == setNumber; }));
}

int tileBaseForGeneralKey(
    const std::vector<TheaterTile>& tiles,
    const ra2formats::IniSection& general,
    std::string_view key) noexcept
{
    return firstTileForSet(tiles, general.getInt(key, -1));
}

void updateMapping(
    TileMapping& mapping,
    const std::vector<TheaterTile>& tiles,
    const ra2formats::IniSection& general,
    std::string_view key)
{
    const int setNumber = general.getInt(key, -1);
    const int tileBase = firstTileForSet(tiles, setNumber);
    const int tileCount = tileCountForSet(tiles, setNumber);
    if (tileBase < 0 || tileCount <= 0) {
        return;
    }
    mapping.tileNum = static_cast<std::uint32_t>(tileBase);
    mapping.tileCount = static_cast<std::uint16_t>(tileCount);
    mapping.subTileBase = 0;
    mapping.subTileCount = 1;
    mapping.variantRemap.clear();
}

void updateExportCatalog(
    NativeTheaterCatalog& result,
    const std::vector<TheaterTile>& tiles,
    const ra2formats::IniSection& general)
{
    updateMapping(result.exportCatalog.land, tiles, general, "ClearTile");
    updateMapping(result.exportCatalog.ramp, tiles, general, "RampBase");
    updateMapping(result.exportCatalog.water, tiles, general, "WaterSet");
    updateMapping(result.exportCatalog.shore, tiles, general, "ShorePieces");
    updateMapping(result.exportCatalog.dirt, tiles, general, "SandTile");
    updateMapping(result.exportCatalog.rough, tiles, general, "RoughTile");
    updateMapping(result.exportCatalog.pave, tiles, general, "PaveTile");
    updateMapping(result.exportCatalog.green, tiles, general, "GreenTile");
    updateMapping(result.exportCatalog.cliff, tiles, general, "CliffSet");
    updateMapping(result.exportCatalog.dirtLat, tiles, general, "ClearToSandLat");
    updateMapping(result.exportCatalog.roughLat, tiles, general, "ClearToRoughLat");
    updateMapping(result.exportCatalog.paveLat, tiles, general, "ClearToPaveLat");
    updateMapping(result.exportCatalog.greenLat, tiles, general, "ClearToGreenLat");
}

} // namespace

const NativeIsoTileTemplate* NativeTheaterCatalog::tileType(int tileTypeIndex) const noexcept
{
    return tileTypeIndex < 0 || static_cast<std::size_t>(tileTypeIndex) >= tileTypes.size()
        ? nullptr
        : &tileTypes[static_cast<std::size_t>(tileTypeIndex)];
}

NativeTheaterCatalog loadNativeTheaterCatalog(
    const std::filesystem::path& gameResourceDirectory,
    int theater)
{
    const native_assets::TheaterDefinition& definition =
        native_assets::theaterDefinition(theater);

    native_assets::ResourceFileSystem resources;
    resources.addDirectory(gameResourceDirectory);
    resources.loadYuriMixes();
    resources.loadTheater(definition);

    const ra2formats::IniDocument theaterIni =
        ra2formats::decodeIni(resources.open(definition.iniFile));
    const ra2formats::IniSection* general = theaterIni.find("General");
    if (general == nullptr) {
        throw std::runtime_error(definition.name + " Theater INI 缺少 [General]");
    }

    const int bridgeSet = general->getInt("BridgeSet", -1);
    const int trainBridgeSet = general->getInt("TrainBridgeSet", -1);
    const int woodBridgeSet = general->getInt("WoodBridgeSet", -1);

    std::vector<TheaterTile> tiles;
    for (int setNumber = 0;; ++setNumber) {
        const ra2formats::IniSection* section = theaterIni.find(tileSetSectionName(setNumber));
        if (section == nullptr) {
            break;
        }
        const std::string fileName(section->get("FileName"));
        const int tileCount = section->getInt("TilesInSet", 0);
        if (fileName.empty() || tileCount < 0 || tileCount > 10000) {
            throw std::runtime_error(
                definition.name + " TileSet 定义无效: " + tileSetSectionName(setNumber));
        }

        const bool bridge =
            setNumber == bridgeSet || setNumber == trainBridgeSet || setNumber == woodBridgeSet;
        for (int tile = 1; tile <= tileCount; ++tile) {
            TheaterTile entry;
            entry.setNumber = setNumber;
            const std::string base = fileName + tileNumberSuffix(tile);
            const std::string primary = findTileFile(resources, definition, base);
            if (primary.empty()) {
                tiles.push_back(std::move(entry));
                continue;
            }
            entry.variants.push_back(primary);
            if (!bridge) {
                for (char variant = 'a'; variant <= 'z'; ++variant) {
                    const std::string candidate = findTileFile(
                        resources,
                        definition,
                        base + variant);
                    if (candidate.empty()) {
                        break;
                    }
                    entry.variants.push_back(candidate);
                }
            }
            tiles.push_back(std::move(entry));
        }
    }
    if (tiles.empty()) {
        throw std::runtime_error(definition.name + " Theater 没有 TileSet");
    }

    NativeTheaterCatalog result;
    result.theater = theater;
    result.theaterName = definition.name;
    result.exportCatalog = loadTileCatalogPreset("yr", theater);
    result.rampBase = tileBaseForGeneralKey(tiles, *general, "RampBase");
    result.rampSmoothBase = tileBaseForGeneralKey(tiles, *general, "RampSmooth");
    result.bridgeSetBase = tileBaseForGeneralKey(tiles, *general, "BridgeSet");
    result.woodBridgeSetBase = tileBaseForGeneralKey(tiles, *general, "WoodBridgeSet");
    result.cliffSetBase = tileBaseForGeneralKey(tiles, *general, "CliffSet");
    result.shorePiecesBase = tileBaseForGeneralKey(tiles, *general, "ShorePieces");
    result.waterBase = tileBaseForGeneralKey(tiles, *general, "WaterSet");
    result.paveBase = tileBaseForGeneralKey(tiles, *general, "PaveTile");
    result.miscPaveBase = tileBaseForGeneralKey(tiles, *general, "MiscPaveTile");
    result.waterfallEastBase = tileBaseForGeneralKey(tiles, *general, "WaterfallEast");
    result.waterfallWestBase = tileBaseForGeneralKey(tiles, *general, "WaterfallWest");
    result.waterfallNorthBase = tileBaseForGeneralKey(tiles, *general, "WaterfallNorth");
    result.waterfallSouthBase = tileBaseForGeneralKey(tiles, *general, "WaterfallSouth");
    result.cliffRampsBase = tileBaseForGeneralKey(tiles, *general, "CliffRamps");
    result.waterCavesBase = tileBaseForGeneralKey(tiles, *general, "WaterCaves");
    result.waterCliffsBase = tileBaseForGeneralKey(tiles, *general, "WaterCliffs");
    result.destroyableCliffsBase = tileBaseForGeneralKey(tiles, *general, "DestroyableCliffs");
    result.pavedRoadsBase = tileBaseForGeneralKey(tiles, *general, "PavedRoads");
    result.pavedRoadEndsBase = tileBaseForGeneralKey(tiles, *general, "PavedRoadEnds");
    result.slopeSetPiecesBase = tileBaseForGeneralKey(tiles, *general, "SlopeSetPieces");
    result.slopeSetPieces2Base = tileBaseForGeneralKey(tiles, *general, "SlopeSetPieces2");
    if (result.slopeSetPiecesBase < 0) {
        throw std::runtime_error(definition.name + " Theater INI 的 SlopeSetPieces 无效");
    }
    if (result.slopeSetPieces2Base < 0) {
        throw std::runtime_error(definition.name + " Theater INI 的 SlopeSetPieces2 无效");
    }
    updateExportCatalog(result, tiles, *general);

    result.tileTypes.reserve(tiles.size());
    for (std::size_t index = 0; index < tiles.size(); ++index) {
        const TheaterTile& source = tiles[index];
        NativeIsoTileTemplate tile;
        tile.tileTypeIndex = static_cast<int>(index);
        if (const ra2formats::IniSection* tileSet =
                theaterIni.find(tileSetSectionName(source.setNumber))) {
            tile.morphable = tileSet->getBool("Morphable", false);
        }
        if (!source.variants.empty()) {
            tile.fileName = source.variants.front();
            const ra2formats::TmpFile decoded =
                ra2formats::decodeTmp(resources.open(tile.fileName));
            tile.width = decoded.width;
            tile.height = decoded.height;
            tile.slots.reserve(decoded.tiles.size());
            for (const std::optional<ra2formats::TmpTile>& sourceSlot : decoded.tiles) {
                NativeIsoTileSlot slot;
                if (sourceSlot.has_value()) {
                    slot.present = true;
                    slot.heightOffset = sourceSlot->height;
                    slot.terrainType = sourceSlot->terrainType;
                    slot.slope = sourceSlot->rampType;
                }
                tile.slots.push_back(slot);
            }
        }
        result.tileTypes.push_back(std::move(tile));
    }

    return result;
}

} // namespace rmg
