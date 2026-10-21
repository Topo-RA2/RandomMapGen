#include "rmg/map_writer.h"
#include "rmg/cliff_pieces.h"
#include "pack_codec/pack_encoding.h"
#include "rmg/shore_pieces.h"

#include <algorithm>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace rmg {
namespace {

constexpr int kNativeInitialMapLevel = 4;
constexpr int kNativeHeightSlopeCount = 19;

void appendKeyValue(std::ostringstream& out, const std::string& key, const std::string& value)
{
    out << key << '=' << value << '\n';
}

void appendKeyValue(std::ostringstream& out, const std::string& key, int value)
{
    out << key << '=' << value << '\n';
}

void writeNativeProductEdge(std::ostringstream& out)
{
    static constexpr char nativeEdgeBytes[] = {
        '\x8B', '\x41', '\x04', '\x8B', '\x4C', '\x24', '\x04',
        '\x8B', '\x04', '\x88', '\xC2', '\x04',
    };
    out << "Edge=";
    out.write(nativeEdgeBytes, sizeof(nativeEdgeBytes));
    out << '\n';
}

std::string pointToString(Point p)
{
    std::ostringstream out;
    out << p.x << ',' << p.y;
    return out.str();
}

std::string joinInts(const std::vector<int>& values)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << values[i];
    }
    return out.str();
}

std::string joinStrings(const std::vector<std::string>& values)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << values[i];
    }
    return out.str();
}

int packedCell(Point p)
{
    return p.x + p.y * 1000;
}

bool isNativeHeightSlope(const Cell& cell) noexcept
{
    if (cell.landType != 3 || cell.slope <= 0 || cell.slope >= kNativeHeightSlopeCount) {
        return false;
    }
    switch (cell.terrain) {
    case Terrain::Land:
    case Terrain::Dirt:
    case Terrain::Rough:
    case Terrain::Pave:
    case Terrain::Green:
        return true;
    case Terrain::Water:
    case Terrain::Shore:
    case Terrain::Cliff:
        return false;
    }
    return false;
}

bool isNativeHeightSlope(const IsoTileOverride& override) noexcept
{
    return override.slopeIndex > 0 && override.slopeIndex < kNativeHeightSlopeCount;
}

int tileVariantForCell(const Cell& cell)
{
    if (isEncodedCliffPiece(cell.slope)) {
        return decodeCliffPiece(cell.slope).tile;
    }
    if (isEncodedShorePiece(cell.slope)) {
        return decodeShorePiece(cell.slope).tile;
    }
    if (cell.terrain == Terrain::Water) {
        return std::clamp(cell.slope, 0, 7);
    }
    if (cell.terrain == Terrain::Shore) {
        return decodeShorePiece(cell.slope).tile;
    }
    if (cell.terrain == Terrain::Cliff) {
        return std::clamp(cell.slope, 0, 15);
    }
    if (isNativeHeightSlope(cell)) {
        return cell.slope - 1;
    }
    return std::clamp(cell.slope, 0, 15);
}

bool canUseClearLat(Terrain terrain) noexcept
{
    return terrain == Terrain::Dirt
        || terrain == Terrain::Rough
        || terrain == Terrain::Pave
        || terrain == Terrain::Green;
}

int clearLatMaskForCell(const MapGrid& map, Point p)
{
    const Cell& cell = map.at(p);
    if (!canUseClearLat(cell.terrain)) {
        return 0;
    }

    int mask = 0;
    constexpr Point offsets[]{{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (int i = 0; i < 4; ++i) {
        const Point neighbor{p.x + offsets[i].x, p.y + offsets[i].y};
        if (!map.inBounds(neighbor)) {
            continue;
        }
        const Terrain adjacent = map.at(neighbor).terrain;
        if (adjacent == Terrain::Land) {
            mask |= 1 << i;
        }
    }
    return mask;
}

TileMapping exportTileMappingForCell(const TileCatalog& catalog, const MapGrid& map, Point p)
{
    const Cell& cell = map.at(p);
    if (isEncodedCliffPiece(cell.slope)) {
        return catalog.cliff;
    }
    if (isEncodedShorePiece(cell.slope)) {
        return catalog.shore;
    }
    if (isNativeHeightSlope(cell)) {
        return catalog.ramp;
    }
    if (clearLatMaskForCell(map, p) != 0) {
        return latTileMappingForTerrain(catalog, cell.terrain);
    }
    return tileMappingForTerrain(catalog, cell.terrain);
}

int exportTileVariantForCell(const MapGrid& map, Point p)
{
    const Cell& cell = map.at(p);
    if (isEncodedCliffPiece(cell.slope)) {
        return decodeCliffPiece(cell.slope).tile;
    }
    if (isEncodedShorePiece(cell.slope)) {
        return decodeShorePiece(cell.slope).tile;
    }
    if (isNativeHeightSlope(cell)) {
        return cell.slope - 1;
    }
    const int latMask = clearLatMaskForCell(map, p);
    if (latMask != 0) {
        return latMask - 1;
    }
    return tileVariantForCell(cell);
}

std::uint32_t terrainTileId(const TileCatalog& catalog, const MapGrid& map, Point p)
{
    return mappedTileNum(exportTileMappingForCell(catalog, map, p), exportTileVariantForCell(map, p));
}

std::uint8_t exportLevelForCell(const Cell& cell) noexcept
{
    return static_cast<std::uint8_t>(std::clamp(cell.level, 0, 255));
}

std::uint8_t exportIceGrowthForCell(const Cell& cell) noexcept
{
    return cell.iceGrowthAllowed ? 1 : 0;
}

int overlayIdForCell(const Cell& cell)
{
    switch (cell.overlay) {
    case Overlay::Ore:
        return 102;
    case Overlay::Gems:
        return 103;
    case Overlay::Grass:
        return std::clamp(cell.overlayValue, 168, 177);
    case Overlay::Bridge:
        return cell.overlayValue <= 8 ? 24 : 25;
    case Overlay::Tree:
    case Overlay::Tech:
    case Overlay::Start:
    case Overlay::NeutralBuilding:
    case Overlay::NeutralUnit:
    case Overlay::TerrainChain:
    case Overlay::None:
        return -1;
    }
    return -1;
}

int overlayDataForCell(const Cell& cell)
{
    if (cell.overlay == Overlay::Ore || cell.overlay == Overlay::Gems) {
        return std::clamp(cell.overlayValue, 1, 255);
    }
    if (cell.overlay == Overlay::Bridge) {
        return std::clamp(cell.overlayValue, 0, 17);
    }
    return 0;
}

std::string hexByte(int value)
{
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (value & 0xFF);
    return out.str();
}

std::string yesNo(bool value)
{
    return value ? "yes" : "no";
}

std::string trueFalse(bool value)
{
    return value ? "true" : "false";
}

std::string fixedRatio(int value, int divisor, int precision)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << static_cast<double>(value) / static_cast<double>(divisor);
    return out.str();
}

std::string pseudoPackLine(const Cell& cell)
{
    std::ostringstream out;
    out << hexByte(cell.x)
        << hexByte(cell.y)
        << hexByte(static_cast<int>(mappedTileNum(tileMappingForTerrain(TileCatalog{}, cell.terrain), tileVariantForCell(cell))))
        << hexByte(cell.level)
        << hexByte(cell.slope < 0 ? 0xFF : cell.slope);
    return out.str();
}

std::string overlayPackLine(const Cell& cell)
{
    std::ostringstream out;
    out << hexByte(cell.x)
        << hexByte(cell.y)
        << hexByte(overlayIdForCell(cell))
        << hexByte(overlayDataForCell(cell));
    return out.str();
}

std::uint8_t subTileForCell(const TileCatalog& catalog, const MapGrid& map, Point p, bool preserveSubTiles)
{
    const Cell& cell = map.at(p);
    if (isEncodedShorePiece(cell.slope)) {
        return static_cast<std::uint8_t>(decodeShorePiece(cell.slope).subTile);
    }
    if (!preserveSubTiles) {
        return 0;
    }

    const TileMapping mapping = exportTileMappingForCell(catalog, map, p);
    const int variant = exportTileVariantForCell(map, p);
    if (isNativeHeightSlope(cell) || cell.terrain == Terrain::Cliff || isEncodedCliffPiece(cell.slope)) {
        return 0;
    }
    if (cell.terrain == Terrain::Water) {
        return mappedSubTile(mapping, std::clamp(variant, 0, 7));
    }
    if (cell.terrain == Terrain::Shore) {
        const ShorePiece piece = decodeShorePiece(cell.slope);
        return piece.subTile > 0 ? static_cast<std::uint8_t>(piece.subTile) : mappedSubTile(mapping, std::clamp(variant, 0, 41));
    }
    if (cell.terrain == Terrain::Cliff) {
        return mappedSubTile(mapping, std::clamp(variant, 0, 15));
    }
    return mappedSubTile(mapping, std::clamp(variant, 0, 15));
}

Point displayToIso(int dx, int row, int width)
{
    const int dy = row * 2 + (dx & 1);
    const int rx = (dx + dy) / 2 + 1;
    const int ry = dy - rx + width + 1;
    return {ry, rx};
}

Point realToIso(Point p, int width)
{
    return displayToIso(p.x * 2, p.y, width);
}

void validateYrIniIsoBounds(const MapGrid& map)
{
    if (map.width() + map.height() > 512) {
        throw std::runtime_error("yrini export requires width + height <= 512 for RA2/YR iso coordinates");
    }
}

void writeEncodedSection(std::ostringstream& out, const std::string& sectionName, const std::string& encoded, std::size_t lineLength)
{
    out << '[' << sectionName << "]\n";
    const auto lines = ra2::pack::splitEncodedSection(encoded, lineLength);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out << (i + 1) << '=' << lines[i] << '\n';
    }
    out << '\n';
}

void appendU16(std::vector<std::uint8_t>& out, std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void appendI32(std::vector<std::uint8_t>& out, int value)
{
    const auto unsignedValue = static_cast<std::uint32_t>(value);
    out.push_back(static_cast<std::uint8_t>(unsignedValue & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((unsignedValue >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((unsignedValue >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((unsignedValue >> 24) & 0xFFu));
}

void appendU16LE(std::vector<std::uint8_t>& out, std::uint16_t value)
{
    appendU16(out, value);
}

void appendU32LE(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    appendI32(out, static_cast<int>(value));
}

std::string cyclicChoice(const std::vector<std::string>& values, int value, const std::string& fallback)
{
    if (values.empty()) {
        return fallback;
    }
    const int normalized = std::max(1, value) - 1;
    return values[static_cast<std::size_t>(normalized % static_cast<int>(values.size()))];
}

std::string structureTypeForTech(const RmgConfig& config, int value)
{
    return cyclicChoice(config.techBuildings, value, "CAOILD");
}

std::string structureTypeForCivilian(const RmgConfig& config, int value)
{
    return cyclicChoice(config.civilianBuildings, value, "CABHUT");
}

std::string unitTypeForNeutral(const RmgConfig& config, int value)
{
    return cyclicChoice(config.neutralUnits, value, "CIV1");
}

std::string terrainChainType(const RmgConfig& config, int value)
{
    return cyclicChoice(config.terrainChainObjects, value, "ROCK01");
}

std::string tiberiumTreeType(int value)
{
    const int normalized = std::clamp(value, 1, 3);
    return "TIBTRE0" + std::to_string(normalized);
}

std::string treeType(int value)
{
    const int normalized = std::clamp(value, 1, 25);
    std::ostringstream out;
    out << "TREE" << std::setw(2) << std::setfill('0') << normalized;
    return out.str();
}

std::string orePatchLampType(const MapDocument& document, int value)
{
    const auto& lamps = document.theater == 1 ? document.rmgConfig.snowOrePatchLamps : document.rmgConfig.temperateOrePatchLamps;
    return cyclicChoice(lamps, value, "INGALITE");
}

std::string nativeTerrainTypeName(int objectTypeIndex)
{
    if (objectTypeIndex >= 14 && objectTypeIndex <= 38) {
        return treeType(objectTypeIndex - 13);
    }
    if (objectTypeIndex >= 44 && objectTypeIndex <= 46) {
        return tiberiumTreeType(objectTypeIndex - 43);
    }
    return {};
}

std::string nativeTechStructureType(int nativeTechIndex)
{
    static const std::vector<std::string> techBuildings{
        "CAAIRP",
        "CATHOSP",
        "CAOILD",
        "CAOUTP",
        "CAMACH",
        "CAPOWR",
        "CASLAB",
    };
    if (nativeTechIndex < 0 || nativeTechIndex >= static_cast<int>(techBuildings.size())) {
        return "CAOILD";
    }
    return techBuildings[static_cast<std::size_t>(nativeTechIndex)];
}

void writeNativeStructureOverride(
    std::ostringstream& out,
    int& structureIndex,
    const NativeStructureOverride& structure)
{
    const int sellable = structure.nativeTechIndex == 1 ? 1 : 0;
    const int key = structure.nativeUniqueId >= 0 ? structure.nativeUniqueId : structureIndex;
    ++structureIndex;
    out << (structure.nativeSaveKey.empty() ? std::to_string(key) : structure.nativeSaveKey) << '=';
    if (!structure.nativeSaveValue.empty()) {
        out << structure.nativeSaveValue << '\n';
        return;
    }
    out << "Neutral," << nativeTechStructureType(structure.nativeTechIndex) << ",256,"
        << structure.iso.x << ',' << structure.iso.y << ",0,None," << sellable << ",0,1,0,0,None,None,None,0,0\n";
}

std::string theaterName(int theater)
{
    switch (theater) {
    case 0:
        return "TEMPERATE";
    case 1:
        return "SNOW";
    case 2:
        return "URBAN";
    case 3:
        return "DESERT";
    case 4:
        return "NEWURBAN";
    case 5:
        return "LUNAR";
    default:
        return "TEMPERATE";
    }
}

std::string multiplayerColor(int index)
{
    static const std::vector<std::string> colors{
        "Gold",
        "DarkRed",
        "DarkBlue",
        "DarkGreen",
        "Orange",
        "Purple",
        "LightBlue",
        "DarkSky",
    };
    return colors[static_cast<std::size_t>(std::max(0, index) % static_cast<int>(colors.size()))];
}

void writeHouseSection(std::ostringstream& out, const std::string& house, const std::string& color, const std::string& side)
{
    out << '[' << house << "]\n";
    out << "TechLevel=0\n";
    out << "IQ=0\n";
    out << "Edge=North\n";
    out << "Color=" << color << '\n';
    out << "Allies=" << house << '\n';
    out << "Country=" << side << "\n\n";
}

int indexedOrFallback(const std::vector<int>& values, int index, int fallback)
{
    if (values.empty()) {
        return fallback;
    }
    const int clampedIndex = std::clamp(index, 0, static_cast<int>(values.size()) - 1);
    return values[static_cast<std::size_t>(clampedIndex)];
}

LightingSettings makeLightingSettings(const MapSeedOptions& options, const RmgConfig& config)
{
    LightingSettings lighting;
    lighting.enabled = true;
    const int timeIndex = std::max(0, options.time);
    lighting.levelIndex = indexedOrFallback(config.levelLightSettings, timeIndex, kNativeInitialMapLevel);
    const bool snow = options.theater != 0;
    lighting.ambient = indexedOrFallback(snow ? config.snowAmbientLight : config.temperateAmbientLight, timeIndex, 100);
    lighting.red = indexedOrFallback(snow ? config.snowAmbientRed : config.temperateAmbientRed, timeIndex, 100);
    lighting.green = indexedOrFallback(snow ? config.snowAmbientGreen : config.temperateAmbientGreen, timeIndex, 100);
    lighting.blue = indexedOrFallback(snow ? config.snowAmbientBlue : config.temperateAmbientBlue, timeIndex, 100);
    return lighting;
}

void writeLightingSection(std::ostringstream& out, const LightingSettings& lighting)
{
    if (!lighting.enabled) {
        return;
    }

    out << "[Lighting]\n";
    out << "Ambient=" << fixedRatio(lighting.ambient, 100, 2) << '\n';
    out << "Red=" << fixedRatio(lighting.red, 100, 2) << '\n';
    out << "Green=" << fixedRatio(lighting.green, 100, 2) << '\n';
    out << "Blue=" << fixedRatio(lighting.blue, 100, 2) << '\n';
    out << "Ground=0.000\n";
    out << "Level=" << fixedRatio(lighting.levelIndex, 1000, 3) << '\n';
    out << "IonAmbient=" << fixedRatio(lighting.ambient, 100, 2) << '\n';
    out << "IonRed=0.30\n";
    out << "IonGreen=0.40\n";
    out << "IonBlue=0.75\n";
    out << "IonGround=0.000\n";
    out << "IonLevel=0.000\n";
    out << "DominatorAmbient=" << fixedRatio(lighting.dominatorAmbient, 100, 2) << '\n';
    out << "DominatorRed=" << fixedRatio(lighting.dominatorRed, 100, 2) << '\n';
    out << "DominatorGreen=" << fixedRatio(lighting.dominatorGreen, 100, 2) << '\n';
    out << "DominatorBlue=" << fixedRatio(lighting.dominatorBlue, 100, 2) << '\n';
    out << "DominatorGround=" << fixedRatio(lighting.dominatorGround, 1000, 3) << '\n';
    out << "DominatorLevel=" << fixedRatio(lighting.dominatorLevel, 1000, 3) << '\n';
    out << "DominatorAmbientChangeRate=" << fixedRatio(lighting.dominatorAmbientChangeRate, 1000, 3) << "\n\n";
}

struct ExportGeometry {
    Rect mapRect;
    Rect localRect;
    int sourceWidth = 0;
    int sourceHeight = 0;
};

bool hasExplicitRect(Rect rect) noexcept
{
    return rect.w > 0 && rect.h > 0;
}

ExportGeometry exportGeometryFor(const MapGrid& map, const MapDocument& document)
{
    ExportGeometry geometry;
    geometry.mapRect = hasExplicitRect(document.mapRect) ? document.mapRect : Rect{0, 0, map.width(), map.height()};
    geometry.localRect = hasExplicitRect(document.localSize) ? document.localSize : Rect{0, 0, map.width(), map.height()};
    geometry.sourceWidth = std::min(map.width(), std::max(0, geometry.localRect.w));
    geometry.sourceHeight = std::min(map.height(), std::max(0, geometry.localRect.h));
    return geometry;
}

Point mapPointToIso(const ExportGeometry& geometry, Point p)
{
    return displayToIso(p.x * 2, p.y, geometry.mapRect.w);
}

Point sourceCellToIso(const ExportGeometry& geometry, Point p)
{
    return mapPointToIso(geometry, {geometry.localRect.x + p.x, geometry.localRect.y + p.y});
}

Point nativeCellToMapPoint(Point cell, int mapWidth) noexcept
{
    const int xNumerator = cell.x - cell.y + mapWidth - 1;
    const int mapX = xNumerator >= 0 && (xNumerator & 1) != 0
        ? xNumerator / 2 + 1
        : xNumerator / 2;
    return {
        mapX,
        (cell.x + cell.y - mapWidth - 1) / 2,
    };
}

Point nativeCellToSourceCell(const ExportGeometry& geometry, Point nativeCell) noexcept
{
    const Point mapPoint = nativeCellToMapPoint(nativeCell, geometry.mapRect.w);
    return {mapPoint.x - geometry.localRect.x, mapPoint.y - geometry.localRect.y};
}

Point sourceCellToNativeCell(const ExportGeometry& geometry, Point source) noexcept
{
    const Point mapPoint{source.x + geometry.localRect.x, source.y + geometry.localRect.y};
    return {mapPoint.x + mapPoint.y + 1, geometry.mapRect.w - mapPoint.x + mapPoint.y};
}

bool sourceCellIsExported(const ExportGeometry& geometry, Point p) noexcept
{
    return p.x >= 0 && p.y >= 0 && p.x < geometry.sourceWidth && p.y < geometry.sourceHeight;
}

bool sourcePointForMapPoint(const MapGrid& map, const ExportGeometry& geometry, Point mapPoint, Point& sourcePoint)
{
    if (!contains(geometry.localRect, mapPoint)) {
        return false;
    }

    sourcePoint = {mapPoint.x - geometry.localRect.x, mapPoint.y - geometry.localRect.y};
    return sourceCellIsExported(geometry, sourcePoint) && map.inBounds(sourcePoint);
}

void validateYrIniIsoBounds(const ExportGeometry& geometry)
{
    if (geometry.mapRect.w + geometry.mapRect.h > 512) {
        throw std::runtime_error("yrini export requires map width + height <= 512 for RA2/YR iso coordinates");
    }
}

void writeTerrainChainEntries(
    std::ostringstream& out,
    const MapGrid& map,
    const MapDocument& document,
    const ExportGeometry& geometry,
    const std::unordered_set<int>& occupiedPackedCells)
{
    for (const auto& cell : map.cells()) {
        if (cell.overlay != Overlay::TerrainChain || !sourceCellIsExported(geometry, {cell.x, cell.y})) {
            continue;
        }

        const Point iso = sourceCellToIso(geometry, {cell.x, cell.y});
        const int packed = packedCell(iso);
        if (occupiedPackedCells.contains(packed)) {
            continue;
        }
        out << packed << '=' << terrainChainType(document.rmgConfig, cell.overlayValue) << '\n';
    }
}

struct IsoPayloadEntry {
    Point iso;
    std::uint32_t tile = 0;
    std::uint8_t subTile = 0;
    std::uint8_t level = 0;
    std::uint8_t iceGrowth = 0;
};

std::uint64_t isoOverrideKey(Point iso) noexcept
{
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(iso.x)) << 32)
        | static_cast<std::uint32_t>(iso.y);
}

std::unordered_map<std::uint64_t, const IsoTileOverride*> buildIsoOverrideMap(const MapDocument& document)
{
    std::unordered_map<std::uint64_t, const IsoTileOverride*> overrides;
    overrides.reserve(document.isoTileOverrides.size());
    for (const auto& override : document.isoTileOverrides) {
        overrides[isoOverrideKey(override.iso)] = &override;
    }
    return overrides;
}

void applyIsoOverride(IsoPayloadEntry& entry, const TileCatalog& catalog, const IsoTileOverride& override)
{
    if (override.nativeTileTypeIndex >= 0) {
        entry.tile = static_cast<std::uint32_t>(override.nativeTileTypeIndex);
        entry.subTile = static_cast<std::uint8_t>(std::clamp(override.subTile, 0, 255));
    } else if (isEncodedCliffPiece(override.slopeIndex)) {
        const CliffPiece piece = decodeCliffPiece(override.slopeIndex);
        entry.tile = mappedTileNum(catalog.cliff, piece.tile);
        entry.subTile = static_cast<std::uint8_t>(std::clamp(piece.subTile, 0, 255));
    } else if (isEncodedShorePiece(override.slopeIndex)) {
        const ShorePiece piece = decodeShorePiece(override.slopeIndex);
        entry.tile = mappedTileNum(catalog.shore, piece.tile);
        entry.subTile = static_cast<std::uint8_t>(std::clamp(piece.subTile, 0, 255));
    } else if (isNativeHeightSlope(override)) {
        entry.tile = mappedTileNum(catalog.ramp, override.slopeIndex - 1);
        entry.subTile = 0;
    } else if (canUseClearLat(override.terrain) && override.variant > 0) {
        entry.tile = mappedTileNum(latTileMappingForTerrain(catalog, override.terrain), override.variant - 1);
        entry.subTile = static_cast<std::uint8_t>(std::clamp(override.subTile, 0, 255));
    } else {
        entry.tile = mappedTileNum(tileMappingForTerrain(catalog, override.terrain), override.variant);
        entry.subTile = static_cast<std::uint8_t>(std::clamp(override.subTile, 0, 255));
    }
    entry.level = static_cast<std::uint8_t>(std::clamp(override.level, 0, 255));
    entry.iceGrowth = override.iceGrowthAllowed ? 1 : 0;
}

IsoPayloadEntry waterPayloadEntryForMapPoint(const TileCatalog& catalog, Point iso, int mapLevel)
{
    IsoPayloadEntry entry;
    entry.iso = iso;
    entry.tile = mappedTileNum(catalog.water, 0);
    entry.level = static_cast<std::uint8_t>(std::clamp(mapLevel, 0, 255));
    return entry;
}

std::vector<std::uint8_t> buildIsoMapPackPayload(const MapGrid& map, const MapDocument& document, const ExportGeometry& geometry, bool preserveSubTiles)
{
    const auto& catalog = document.tileCatalog;
    const int displayWidth = geometry.mapRect.w * 2 - 1;
    const auto isoOverrides = buildIsoOverrideMap(document);
    std::vector<IsoPayloadEntry> entries;
    entries.reserve(static_cast<std::size_t>(displayWidth * geometry.mapRect.h));

    for (int row = 0; row < geometry.mapRect.h; ++row) {
        for (int dx = 0; dx < displayWidth; ++dx) {
            const Point mapPoint{std::clamp(dx / 2, 0, geometry.mapRect.w - 1), row};
            Point sourcePoint;
            const bool hasSource = sourcePointForMapPoint(map, geometry, mapPoint, sourcePoint);
            const Point iso = displayToIso(dx, row, geometry.mapRect.w);
            IsoPayloadEntry entry = waterPayloadEntryForMapPoint(catalog, iso, document.mapLevel);
            const auto overrideIt = isoOverrides.find(isoOverrideKey(iso));
            const IsoTileOverride* isoOverride = overrideIt == isoOverrides.end() ? nullptr : overrideIt->second;
            if (isoOverride) {
                applyIsoOverride(entry, catalog, *isoOverride);
            } else if (hasSource) {
                const Cell& cell = map.at(sourcePoint);
                entry.tile = terrainTileId(catalog, map, sourcePoint);
                entry.subTile = subTileForCell(catalog, map, sourcePoint, preserveSubTiles);
                entry.level = exportLevelForCell(cell);
                entry.iceGrowth = exportIceGrowthForCell(cell);
            }
            entries.push_back(entry);
        }
    }

    std::sort(entries.begin(), entries.end(), [](const IsoPayloadEntry& left, const IsoPayloadEntry& right) {
        if (left.iso.y != right.iso.y) {
            return left.iso.y < right.iso.y;
        }
        return left.iso.x < right.iso.x;
    });

    std::vector<std::uint8_t> payload;
    payload.reserve(static_cast<std::size_t>(displayWidth * geometry.mapRect.h * 11 + 4));

    for (const auto& entry : entries) {
        appendU16LE(payload, static_cast<std::uint16_t>(std::clamp(entry.iso.x, 0, 0xFFFF)));
        appendU16LE(payload, static_cast<std::uint16_t>(std::clamp(entry.iso.y, 0, 0xFFFF)));
        appendU32LE(payload, entry.tile);
        payload.push_back(entry.subTile);
        payload.push_back(entry.level);
        payload.push_back(entry.iceGrowth);
    }
    payload.insert(payload.end(), 4, 0);
    return payload;
}

Point nativeProductHomePoint(const ExportGeometry& geometry)
{
    ExportGeometry homeGeometry = geometry;
    homeGeometry.mapRect.w &= ~1;
    return sourceCellToNativeCell(homeGeometry, {std::max(0, geometry.sourceWidth / 2), std::max(0, geometry.sourceHeight / 2 + 1)});
}

int nativeHeaderXFromCellDiff(int diff) noexcept
{
    // ScenarioClass::WriteMap converts cell-center iso coords through IsoCoord_CellToWorld,
    // then divides the positive world X coordinate by 60.
    return (15360 + 30 * diff) / 60;
}

int nativeHeaderYFromCellSum(int sum) noexcept
{
    // Cell centers add one half-cell on both iso axes before WriteMap divides world Y by 30.
    return (sum + 1) / 2;
}

Point nativeHeaderPointFromCell(Point cell) noexcept
{
    return {
        nativeHeaderXFromCellDiff(cell.x - cell.y),
        nativeHeaderYFromCellSum(cell.x + cell.y),
    };
}

Rect nativeHeaderRectFromUsableArea(Rect mapRect, Rect localRect) noexcept
{
    const int minDiff = 2 * localRect.x - mapRect.w + 1;
    const int maxDiff = 2 * (localRect.x + localRect.w) - mapRect.w - 1;
    const int minSum = mapRect.w + 2 * localRect.y + 1;
    const int maxSum = mapRect.w + 2 * (localRect.y + localRect.h) + 2;
    const int startX = nativeHeaderXFromCellDiff(minDiff);
    const int startY = nativeHeaderYFromCellSum(minSum);
    return {
        startX,
        startY,
        nativeHeaderXFromCellDiff(maxDiff) - startX,
        nativeHeaderYFromCellSum(maxSum) - startY,
    };
}

void writeNativeProductHeader(std::ostringstream& out, const MapDocument& document, const ExportGeometry& geometry, Rect headerRect)
{
    const std::size_t waypointCount = document.nativeWaypoints.empty() ? document.starts.size() : document.nativeWaypoints.size();
    out << "[Header]\n";
    out << "StartX=" << headerRect.x << '\n';
    out << "StartY=" << headerRect.y << '\n';
    out << "Width=" << headerRect.w << '\n';
    out << "Height=" << headerRect.h << '\n';
    out << "NumberStartingPoints=" << waypointCount << '\n';
    out << "NumCoopHumanStartSpots=4\n";
    for (std::size_t i = 0; i < 8; ++i) {
        if (!document.nativeWaypoints.empty() && i < document.nativeWaypoints.size()) {
            const Point headerWaypoint = nativeHeaderPointFromCell(document.nativeWaypoints[i]);
            out << "Waypoint" << (i + 1) << '=' << headerWaypoint.x << ',' << headerWaypoint.y << '\n';
        } else if (i < document.starts.size()) {
            const Point headerWaypoint{headerRect.x + headerRect.w - document.starts[i].x, headerRect.y + document.starts[i].y};
            out << "Waypoint" << (i + 1) << '=' << headerWaypoint.x << ',' << headerWaypoint.y << '\n';
        } else {
            out << "Waypoint" << (i + 1) << "=0,0\n";
        }
    }
    out << '\n';
}

void writeNativeProductSpecialFlags(std::ostringstream& out)
{
    out << "[SpecialFlags]\n";
    out << "TiberiumGrows=no\n";
    out << "TiberiumSpreads=yes\n";
    out << "TiberiumExplosive=no\n";
    out << "DestroyableBridges=yes\n";
    out << "MCVDeploy=no\n";
    out << "InitialVeteran=no\n";
    out << "FixedAlliance=no\n";
    out << "HarvesterImmune=no\n";
    out << "FogOfWar=no\n";
    out << "Inert=no\n";
    out << "IonStorms=no\n";
    out << "Meteorites=no\n";
    out << "Visceroids=no\n\n";
}

void writeNativeProductBasic(std::ostringstream& out)
{
    out << "[Basic]\n";
    out << "Name=906d,9047,6218,6a21,5f0f,\n";
    out << "NewINIFormat=4\n";
    out << "CarryOverCap=0\n";
    out << "EndOfGame=no\n";
    out << "SkipScore=no\n";
    out << "OneTimeOnly=no\n";
    out << "SkipMapSelect=no\n";
    out << "Official=yes\n";
    out << "IgnoreGlobalAITriggers=no\n";
    out << "TruckCrate=no\n";
    out << "TrainCrate=no\n";
    out << "Percent=0\n";
    out << "Theme=No theme\n";
    out << "Player=<human player>\n";
    out << "Intro=<none>\n";
    out << "Brief=<none>\n";
    out << "Win=<none>\n";
    out << "Lose=<none>\n";
    out << "Action=<none>\n";
    out << "PostScore=<none>\n";
    out << "PreMapSelect=<none>\n";
    out << "CarryOverMoney=0.000000\n";
    out << "TimerInherit=no\n";
    out << "FillSilos=no\n";
    out << "StartingDropships=0\n";
    out << "HomeCell=699\n";
    out << "AltHomeCell=699\n";
    out << "MultiplayerOnly=0\n";
    out << "TiberiumGrowthEnabled=yes\n";
    out << "VeinGrowthEnabled=yes\n";
    out << "IceGrowthEnabled=yes\n";
    out << "TiberiumDeathToVisceroid=yes\n";
    out << "FreeRadar=no\n";
    out << "InitTime=10000\n\n";
}

void writeNativeProductRanking(std::ostringstream& out)
{
    out << "[Ranking]\n";
    out << "ParTimeEasy=00:01:00\n";
    out << "ParTimeMedium=00:01:00\n";
    out << "ParTimeHard=00:01:00\n";
    out << "UnderParTitle=GUI:NoUnderParTitle\n";
    out << "UnderParMessage=GUI:NoUnderParMessage\n";
    out << "OverParTitle=GUI:NoOverParTitle\n";
    out << "OverParMessage=GUI:NoOverParMessage\n\n";
}

void writeNativeProductLighting(std::ostringstream& out, const LightingSettings& lighting)
{
    if (!lighting.enabled) {
        return;
    }

    out << "[Lighting]\n";
    out << "Ambient=" << fixedRatio(lighting.ambient, 100, 6) << '\n';
    out << "Red=" << fixedRatio(lighting.red, 100, 6) << '\n';
    out << "Green=" << fixedRatio(lighting.green, 100, 6) << '\n';
    out << "Blue=" << fixedRatio(lighting.blue, 100, 6) << '\n';
    out << "Ground=0.000000\n";
    out << "Level=" << fixedRatio(lighting.levelIndex, 1000, 6) << '\n';
    out << "IonAmbient=" << fixedRatio(lighting.ambient, 100, 6) << '\n';
    out << "IonRed=0.300000\n";
    out << "IonGreen=0.400000\n";
    out << "IonBlue=0.750000\n";
    out << "IonGround=0.000000\n";
    out << "IonLevel=0.000000\n";
    out << "DominatorAmbient=" << fixedRatio(lighting.dominatorAmbient, 100, 6) << '\n';
    out << "DominatorRed=" << fixedRatio(lighting.dominatorRed, 100, 6) << '\n';
    out << "DominatorGreen=" << fixedRatio(lighting.dominatorGreen, 100, 6) << '\n';
    out << "DominatorBlue=" << fixedRatio(lighting.dominatorBlue, 100, 6) << '\n';
    out << "DominatorGround=" << fixedRatio(lighting.dominatorGround, 1000, 6) << '\n';
    out << "DominatorLevel=" << fixedRatio(lighting.dominatorLevel, 1000, 6) << '\n';
    out << "DominatorAmbientChangeRate=" << fixedRatio(lighting.dominatorAmbientChangeRate, 1000, 6) << "\n\n";
}

void writeNativeProductHouseSection(
    std::ostringstream& out,
    const std::string& house,
    const std::string& country,
    int techLevel,
    int credits,
    const std::string& playerControl,
    const std::string& color)
{
    out << '[' << house << "]\n";
    out << "Country=" << country << '\n';
    out << "TechLevel=" << techLevel << '\n';
    out << "Credits=" << credits << '\n';
    out << "IQ=0\n";
    writeNativeProductEdge(out);
    out << "PlayerControl=" << playerControl << '\n';
    out << "Color=" << color << '\n';
    out << "Allies=" << house << '\n';
    out << "PercentBuilt=0\n";
    out << "NodeCount=0\n\n";
}

void writeNativeProductHouses(std::ostringstream& out)
{
    out << "[Houses]\n";
    out << "0=<human player>\n";
    out << "1=Computer\n";
    out << "2=Neutral\n";
    out << "3=Special\n\n";

    writeNativeProductHouseSection(out, "<human player>", "Americans", 10, 1000, "yes", "Gold");
    writeNativeProductHouseSection(out, "Computer", "Alliance", 10, 1000, "no", "DarkGreen");
    writeNativeProductHouseSection(out, "Neutral", "Neutral", 1, 0, "no", "LightGrey");
    writeNativeProductHouseSection(out, "Special", "Special", 1, 0, "no", "LightGrey");
}

void writeNativeProductWaypoints(std::ostringstream& out, const MapDocument& document, const ExportGeometry& geometry)
{
    out << "[Waypoints]\n";
    if (!document.nativeWaypoints.empty()) {
        for (std::size_t i = 0; i < document.nativeWaypoints.size(); ++i) {
            out << i << '=' << packedCell(document.nativeWaypoints[i]) << '\n';
        }
    } else {
        for (std::size_t i = 0; i < document.starts.size(); ++i) {
            const Point p = sourceCellToIso(geometry, document.starts[i]);
            out << i << '=' << packedCell(p) << '\n';
        }
    }
    const Point home = nativeProductHomePoint(geometry);
    out << "699=" << packedCell(home) << '\n';
    out << "700=" << packedCell(home) << "\n\n";
}

void writeNativeProductTerrain(std::ostringstream& out, const MapGrid& map, const MapDocument& document, const ExportGeometry& geometry)
{
    out << "[Terrain]\n";
    std::unordered_set<int> writtenPackedCells;
    if (!document.nativeObjectOverrides.empty()) {
        for (const NativeObjectOverride& object : document.nativeObjectOverrides) {
            if (object.objectAbstractType != 36) {
                continue;
            }
            const std::string typeName = nativeTerrainTypeName(object.objectTypeIndex);
            if (typeName.empty()) {
                continue;
            }
            const int packed = packedCell(object.iso);
            out << packed << '=' << typeName << '\n';
            writtenPackedCells.insert(packed);
        }
        writeTerrainChainEntries(out, map, document, geometry, writtenPackedCells);
        out << '\n';
        return;
    }

    for (const auto& cell : map.cells()) {
        if (!sourceCellIsExported(geometry, {cell.x, cell.y})) {
            continue;
        }
        if (cell.terrainObject == TerrainObject::TiberiumTree) {
            const Point iso = sourceCellToIso(geometry, {cell.x, cell.y});
            const int packed = packedCell(iso);
            out << packed << '=' << tiberiumTreeType(cell.terrainObjectValue) << '\n';
            writtenPackedCells.insert(packed);
        }
    }
    writeTerrainChainEntries(out, map, document, geometry, writtenPackedCells);
    out << '\n';
}

void writeNativeProductStructures(std::ostringstream& out, const MapGrid& map, const MapDocument& document, const ExportGeometry& geometry)
{
    std::ostringstream structures;
    int structureIndex = 0;
    if (!document.nativeStructureOverrides.empty()) {
        for (const NativeStructureOverride& structure : document.nativeStructureOverrides) {
            writeNativeStructureOverride(structures, structureIndex, structure);
        }
    }

    if (document.nativeStructureOverrides.empty() || document.mode == "native") {
        for (const auto& cell : map.cells()) {
            if (!sourceCellIsExported(geometry, {cell.x, cell.y})) {
                continue;
            }
            if (cell.overlay == Overlay::Tech && document.nativeStructureOverrides.empty()) {
                const Point iso = sourceCellToIso(geometry, {cell.x, cell.y});
                structures << structureIndex++ << "=Neutral," << structureTypeForTech(document.rmgConfig, cell.overlayValue) << ",256," << iso.x << ',' << iso.y << ",0,None,1,0,1,0,0,None,None,None,0,0\n";
            } else if (cell.overlay == Overlay::NeutralBuilding) {
                const Point iso = sourceCellToIso(geometry, {cell.x, cell.y});
                structures << structureIndex++ << "=Neutral," << structureTypeForCivilian(document.rmgConfig, cell.overlayValue) << ",256," << iso.x << ',' << iso.y << ",0,None,1,0,1,0,0,None,None,None,0,0\n";
            } else if (cell.terrainObject == TerrainObject::OrePatchLamp) {
                const Point iso = sourceCellToIso(geometry, {cell.x, cell.y});
                structures << structureIndex++ << "=Neutral," << orePatchLampType(document, cell.terrainObjectValue) << ",256," << iso.x << ',' << iso.y << ",0,None,1,0,1,0,0,None,None,None,0,0\n";
            }
        }
    }
    if (structureIndex > 0) {
        out << "[Structures]\n";
        out << structures.str();
        out << '\n';
    }
}

void buildOverlayPacks(const MapGrid& map, const MapDocument& document, const ExportGeometry& geometry, std::vector<std::uint8_t>& overlayPack, std::vector<std::uint8_t>& overlayDataPack)
{
    overlayPack.assign(1u << 18, 0xFF);
    overlayDataPack.assign(1u << 18, 0);
    if (!document.nativeOverlayOverrides.empty()) {
        for (const NativeOverlayOverride& overlay : document.nativeOverlayOverrides) {
            if (overlay.overlayTypeIndex < 0 || overlay.iso.x < 0 || overlay.iso.y < 0 || overlay.iso.x >= 512 || overlay.iso.y >= 512) {
                continue;
            }
            const auto index = static_cast<std::size_t>(overlay.iso.x + 512 * overlay.iso.y);
            overlayPack[index] = static_cast<std::uint8_t>(overlay.overlayTypeIndex);
            overlayDataPack[index] = static_cast<std::uint8_t>(std::clamp(overlay.overlayData, 0, 255));
        }
        return;
    }

    for (const auto& cell : map.cells()) {
        if (!sourceCellIsExported(geometry, {cell.x, cell.y})) {
            continue;
        }
        const int id = overlayIdForCell(cell);
        if (id < 0) {
            continue;
        }
        const Point iso = sourceCellToIso(geometry, {cell.x, cell.y});
        if (iso.x < 0 || iso.y < 0 || iso.x >= 512 || iso.y >= 512) {
            continue;
        }
        const auto index = static_cast<std::size_t>(iso.x + 512 * iso.y);
        overlayPack[index] = static_cast<std::uint8_t>(id);
        overlayDataPack[index] = static_cast<std::uint8_t>(overlayDataForCell(cell));
    }
}

void writeNativeProductAiTriggerTypesEnable(std::ostringstream& out)
{
    static const char* const triggerIds[] = {
        "0CAD0DCC-G",
        "0CAD0C7C-G",
        "0CAD0B2C-G",
        "0CA6621C-G",
        "0CDF8EDC-G",
        "0CDF8D8C-G",
        "0C0C7AEC-G",
        "0D62199C-G",
        "0CDF899C-G",
        "0D6216FC-G",
        "0D6215AC-G",
        "0D62145C-G",
        "0D0803CC-G",
        "0C32599C-G",
        "0CE23AAC-G",
        "0CA1F84C-G",
        "0CE26CAC-G",
        "0CE26B5C-G",
        "0CAD145C-G",
        "0CAD130C-G",
        "0CE2395C-G",
        "0D084B5C-G",
        "0D084A0C-G",
        "0D0848BC-G",
        "0CAD2AEC-G",
        "0D08461C-G",
        "0D0873DC-G",
        "0C8E2C3C-G",
        "0CAD25AC-G",
        "0D087D8C-G",
        "0C8B81BC-G",
        "0CADDEDC-G",
        "0C92C45C-G",
        "0C9565AC-G",
        "0C92C1BC-G",
        "0CADD99C-G",
        "0C9561BC-G",
        "0CE2EA2C-G",
        "0CE2E8DC-G",
        "0C955C3C-G",
        "0C955AEC-G",
        "0C8C71BC-G",
        "0C8C6EDC-G",
        "0D052AEC-G",
        "0CE44D0C-G",
        "0CA5945C-G",
        "0CAD2C3C-G",
        "0D535EDC-G",
        "0AA846FC-G",
        "0AA845AC-G",
        "0C8C645C-G",
        "0AD2BAEC-G",
        "0C96F5AC-G",
        "0C96F45C-G",
        "0CB2CEDC-G",
        "0C91D45C-G",
        "0CAD5AEC-G",
        "0D2C45DC-G",
        "0D2C448C-G",
        "0C8C56FC-G",
        "0D2C41EC-G",
        "0CA6AEDC-G",
        "0C0D251C-G",
        "0C8C51BC-G",
        "0CA24D8C-G",
        "0C8BC5AC-G",
        "0C8C4C3C-G",
        "0D53430C-G",
        "0ECCFCAC-G",
        "0ECCFA7C-G",
        "0C8EEEDC-G",
        "0C0D184C-G",
        "0ECCF68C-G",
        "0C8C430C-G",
        "0C87B99C-G",
        "0ECCF29C-G",
        "0ECCF14C-G",
        "0C8EEAEC-G",
        "0C87B45C-G",
        "0ECCBC3C-G",
        "0C8EE1BC-G",
        "0ECCB7FC-G",
        "0C32D84C-G",
        "0ECCB55C-G",
        "0ECCB40C-G",
        "0ECCB2BC-G",
        "0CB29EDC-G",
        "0C8C2C3C-G",
        "0C8C2AEC-G",
        "0A31295C-G",
        "0C87E5AC-G",
        "08A3EEDC-G",
        "0D022B1C-G",
        "0C1246FC-G",
        "0C1245AC-G",
        "0D0B3B9C-G",
        "0CA2A04C-G",
        "0CA2B06C-G",
        "0CA2FEDC-G",
        "0D2769BC-G",
        "0A452E5C-G",
        "0A452C6C-G",
        "0A44F2CC-G",
        "0A44F17C-G",
        "0D53099C-G",
        "0612B11C-G",
        "06C551AC-G",
        "0612CD8C-G",
        "0ABC484C-G",
        "05FFB18C-G",
        "05FFA7DC-G",
        "06002BFC-G",
        "05CB5B6C-G",
        "07C04EDC-G",
        "07C04C7C-G",
        "07C0B14C-G",
        "05FC1A8C-G",
        "05FC50EC-G",
        "05FC3C9C-G",
        "05FC37CC-G",
        "05FC333C-G",
        "05FC0C9C-G",
        "05FC088C-G",
        "05FC75CC-G",
        "05FC747C-G",
        "08B9154C-G",
        "08B911BC-G",
        "08B9213C-G",
        "08B9356C-G",
        "08B9305C-G",
        "08B9483C-G",
        "08B9423C-G",
        "08B940EC-G",
        "08B9586C-G",
        "0611A33C-G",
        "0611A1EC-G",
        "08B9641C-G",
        "08B97C6C-G",
        "08B9767C-G",
        "08B9708C-G",
        "05FC1C5C-G",
        "0612EAEC-G",
        "0612F9AC-G",
        "05FC13EC-G",
        "08B90B1C-G",
        "08B902EC-G",
        "0612F46C-G",
        "044082BC-G",
        "07C310BC-G",
        "07C329FC-G",
        "07C3266C-G",
        "07C33DDC-G",
        "0616004C-G",
        "07C330CC-G",
        "07C30A5C-G",
        "07C4853C-G",
        "043F521C-G",
        "043F874C-G",
        "043F40BC-G",
        "05CBC2EC-G",
        "05CBC19C-G",
        "05CBC04C-G",
        "078F4B2C-G",
        "05CB830C-G",
        "05CBAEDC-G",
    };

    out << "[AITriggerTypesEnable]\n";
    for (const char* triggerId : triggerIds) {
        out << triggerId << "=yes\n";
    }
    out << '\n';
}

std::string withCrLfLineEndings(const std::string& text)
{
    std::string result;
    result.reserve(text.size() + text.size() / 64);
    for (char ch : text) {
        if (ch == '\n') {
            result.push_back('\r');
        }
        result.push_back(ch);
    }
    return result;
}

std::string digestInputForNativeProductBody(std::string body)
{
    if (body.size() >= 2 && body[body.size() - 1] == '\n' && body[body.size() - 2] == '\n') {
        body.pop_back();
    }
    return withCrLfLineEndings(body);
}

void writeNativeProductDigest(std::ostringstream& out, const std::string& body)
{
    const std::string digestInput = digestInputForNativeProductBody(body);
    const auto digest = ra2::pack::sha1Digest(digestInput);
    const std::vector<std::uint8_t> digestBytes(digest.begin(), digest.end());
    out << "[Digest]\n";
    out << "1=" << ra2::pack::base64Encode(digestBytes) << '\n';
}

} // namespace

std::string writeTextMap(const MapGrid& map, const MapDocument& document)
{
    std::ostringstream out;
    out << "; RandomMapGen output\n";
    out << "; This is a deterministic debug/export format, not Westwood packed IsoMapPack5.\n\n";

    out << "[RMGMeta]\n";
    appendKeyValue(out, "Mode", document.mode);
    appendKeyValue(out, "Seed", static_cast<int>(document.seed));
    appendKeyValue(out, "Width", map.width());
    appendKeyValue(out, "Height", map.height());
    appendKeyValue(out, "Players", document.players);
    out << '\n';

    out << "[Metrics]\n";
    for (const auto& [key, value] : document.metrics) {
        appendKeyValue(out, key, value);
    }
    out << '\n';

    out << "[Starts]\n";
    for (std::size_t i = 0; i < document.starts.size(); ++i) {
        appendKeyValue(out, "Player" + std::to_string(i + 1), pointToString(document.starts[i]));
    }
    out << '\n';

    if (!document.notes.empty()) {
        out << "[Notes]\n";
        for (std::size_t i = 0; i < document.notes.size(); ++i) {
            appendKeyValue(out, "Note" + std::to_string(i + 1), document.notes[i]);
        }
        out << '\n';
    }

    out << "[MapAscii]\n";
    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) {
            out << displayChar(map.at({x, y}));
        }
        out << '\n';
    }
    out << '\n';

    out << "[Cells]\n";
    for (const auto& cell : map.cells()) {
        out << cell.x << ',' << cell.y << '=' << terrainName(cell.terrain);
        if (cell.overlay != Overlay::None) {
            out << ',' << overlayName(cell.overlay) << ',' << cell.overlayValue;
        }
        if (cell.terrainObject != TerrainObject::None) {
            out << ',' << terrainObjectName(cell.terrainObject) << ',' << cell.terrainObjectValue;
        }
        if (cell.height != 0 || cell.slope != -1) {
            out << ",h" << cell.height << ",s" << cell.slope;
        }
        if (cell.spawnExclusion) {
            out << ",spawnExcluded";
        }
        if (cell.cliffShoreExclusion) {
            out << ",cliffShoreExcluded";
        }
        if (cell.urbanRegionMark) {
            out << ",urbanRegion";
        }
        out << '\n';
    }

    return out.str();
}

std::string writeYrIniMap(const MapGrid& map, const MapDocument& document)
{
    const ExportGeometry geometry = exportGeometryFor(map, document);
    validateYrIniIsoBounds(geometry);
    const Rect headerRect = hasExplicitRect(document.headerRect) ? document.headerRect : Rect{0, 0, map.width(), map.height()};

    std::ostringstream out;
    out << "; RandomMapGen RA2/YR-style INI\n";
    out << "; OverlayPack/OverlayDataPack use real Format5+Format80 base64 encoding.\n";
    out << "; IsoMapPack5 uses real Format5 container with LZO1X chunks.\n\n";

    out << "[Basic]\n";
    out << "Name=RandomMapGen " << document.mode << " seed " << document.seed << '\n';
    out << "RandomMap=yes\n";
    out << "MultiplayerOnly=yes\n";
    out << "Player=Neutral\n";
    out << "NewINIFormat=" << document.basicScene.newIniFormat << '\n';
    out << "TiberiumGrowthEnabled=" << yesNo(document.basicScene.tiberiumGrowthEnabled) << '\n';
    out << "VeinGrowthEnabled=" << yesNo(document.basicScene.veinGrowthEnabled) << '\n';
    out << "IceGrowthEnabled=" << yesNo(document.basicScene.iceGrowthEnabled) << '\n';
    out << "TiberiumDeathToVisceroid=" << yesNo(document.basicScene.tiberiumDeathToVisceroid) << '\n';
    out << "FreeRadar=" << yesNo(document.basicScene.freeRadar) << '\n';
    out << "InitTime=" << document.basicScene.initTime << "\n\n";

    const bool useNativeWaypoints = document.mode == "native" && !document.nativeWaypoints.empty();
    const std::size_t waypointCount = useNativeWaypoints ? document.nativeWaypoints.size() : document.starts.size();

    out << "[Header]\n";
    out << "StartX=" << headerRect.x << '\n';
    out << "StartY=" << headerRect.y << '\n';
    out << "Width=" << headerRect.w << '\n';
    out << "Height=" << headerRect.h << '\n';
    out << "NumberStartingPoints=" << waypointCount << '\n';
    for (std::size_t i = 0; i < waypointCount; ++i) {
        if (useNativeWaypoints) {
            const Point headerWaypoint = nativeHeaderPointFromCell(document.nativeWaypoints[i]);
            out << "Waypoint" << (i + 1) << '=' << headerWaypoint.x << ',' << headerWaypoint.y << '\n';
        } else {
            out << "Waypoint" << (i + 1) << '=' << document.starts[i].x << ',' << document.starts[i].y << '\n';
        }
    }
    out << '\n';

    out << "[Houses]\n";
    out << "0=Neutral\n";
    out << "1=Special\n";
    for (int i = 0; i < document.players; ++i) {
        out << (i + 2) << "=Multi" << (i + 1) << '\n';
    }
    out << '\n';

    writeHouseSection(out, "Neutral", "Grey", "Neutral");
    writeHouseSection(out, "Special", "Grey", "Special");
    for (int i = 0; i < document.players; ++i) {
        writeHouseSection(out, "Multi" + std::to_string(i + 1), multiplayerColor(i), "Americans");
    }

    out << "[Map]\n";
    out << "Size=" << geometry.mapRect.x << ',' << geometry.mapRect.y << ',' << geometry.mapRect.w << ',' << geometry.mapRect.h << '\n';
    out << "Theater=" << theaterName(document.theater) << '\n';
    out << "Level=" << document.mapLevel << '\n';
    out << "LocalSize=" << geometry.localRect.x << ',' << geometry.localRect.y << ',' << geometry.localRect.w << ',' << geometry.localRect.h << "\n\n";

    writeLightingSection(out, document.lighting);

    out << "[Preview]\n";
    out << "Size=" << map.width() << ',' << map.height() << '\n';
    for (int y = 0; y < map.height(); ++y) {
        out << y << '=';
        for (int x = 0; x < map.width(); ++x) {
            out << displayChar(map.at({x, y}));
        }
        out << '\n';
    }
    out << '\n';

    out << "[Waypoints]\n";
    if (useNativeWaypoints) {
        for (std::size_t i = 0; i < document.nativeWaypoints.size(); ++i) {
            out << i << '=' << packedCell(document.nativeWaypoints[i]) << '\n';
        }
    } else {
        for (std::size_t i = 0; i < document.starts.size(); ++i) {
            const Point p = sourceCellToIso(geometry, document.starts[i]);
            out << i << '=' << packedCell(p) << '\n';
        }
    }
    out << '\n';

    out << "[Structures]\n";
    int structureIndex = 0;
    if (!document.nativeStructureOverrides.empty()) {
        for (const NativeStructureOverride& structure : document.nativeStructureOverrides) {
            writeNativeStructureOverride(out, structureIndex, structure);
        }
    }
    if (document.nativeStructureOverrides.empty() || document.mode == "native") {
        for (const auto& cell : map.cells()) {
            if (!sourceCellIsExported(geometry, {cell.x, cell.y})) {
                continue;
            }
            if (cell.overlay == Overlay::Tech && document.nativeStructureOverrides.empty()) {
                const Point iso = sourceCellToIso(geometry, {cell.x, cell.y});
                out << structureIndex++ << "=Neutral," << structureTypeForTech(document.rmgConfig, cell.overlayValue) << ",256," << iso.x << ',' << iso.y << ",0,None,1,0,1,0,0,None,None,None,0,0\n";
            } else if (cell.overlay == Overlay::NeutralBuilding) {
                const Point iso = sourceCellToIso(geometry, {cell.x, cell.y});
                out << structureIndex++ << "=Neutral," << structureTypeForCivilian(document.rmgConfig, cell.overlayValue) << ",256," << iso.x << ',' << iso.y << ",0,None,1,0,1,0,0,None,None,None,0,0\n";
            } else if (cell.terrainObject == TerrainObject::OrePatchLamp) {
                const Point iso = sourceCellToIso(geometry, {cell.x, cell.y});
                out << structureIndex++ << "=Neutral," << orePatchLampType(document, cell.terrainObjectValue) << ",256," << iso.x << ',' << iso.y << ",0,None,1,0,1,0,0,None,None,None,0,0\n";
            }
        }
    }
    out << '\n';

    out << "[Units]\n";
    int unitIndex = 0;
    for (const auto& cell : map.cells()) {
        if (!sourceCellIsExported(geometry, {cell.x, cell.y})) {
            continue;
        }
        if (cell.overlay == Overlay::NeutralUnit) {
            const Point iso = sourceCellToIso(geometry, {cell.x, cell.y});
            out << unitIndex++ << "=Neutral," << unitTypeForNeutral(document.rmgConfig, cell.overlayValue) << ",256," << iso.x << ',' << iso.y << ",0,Guard,None\n";
        }
    }
    out << '\n';

    if (!document.nativeObjectOverrides.empty()) {
        writeNativeProductTerrain(out, map, document, geometry);
    } else {
        out << "[Terrain]\n";
        for (const auto& cell : map.cells()) {
            if (!sourceCellIsExported(geometry, {cell.x, cell.y})) {
                continue;
            }
            if (cell.terrainObject == TerrainObject::TiberiumTree) {
                const Point iso = sourceCellToIso(geometry, {cell.x, cell.y});
                out << packedCell(iso) << '=' << tiberiumTreeType(cell.terrainObjectValue) << '\n';
            } else if (cell.overlay == Overlay::Tree) {
                const Point iso = sourceCellToIso(geometry, {cell.x, cell.y});
                out << packedCell(iso) << '=' << treeType(cell.overlayValue) << '\n';
            } else if (cell.overlay == Overlay::TerrainChain) {
                const Point iso = sourceCellToIso(geometry, {cell.x, cell.y});
                out << packedCell(iso) << '=' << terrainChainType(document.rmgConfig, cell.overlayValue) << '\n';
            }
        }
        out << '\n';
    }

    std::vector<std::uint8_t> overlayPack;
    std::vector<std::uint8_t> overlayDataPack;
    buildOverlayPacks(map, document, geometry, overlayPack, overlayDataPack);

    out << "[Overlay]\n";
    for (int y = 0; y < 512; ++y) {
        for (int x = 0; x < 512; ++x) {
            const auto index = static_cast<std::size_t>(x + 512 * y);
            if (overlayPack[index] == 0xFF) {
                continue;
            }
            out << packedCell({x, y}) << '=' << static_cast<int>(overlayPack[index]) << '\n';
        }
    }
    out << '\n';

    out << "[OverlayData]\n";
    for (int y = 0; y < 512; ++y) {
        for (int x = 0; x < 512; ++x) {
            const auto index = static_cast<std::size_t>(x + 512 * y);
            if (overlayPack[index] == 0xFF) {
                continue;
            }
            out << packedCell({x, y}) << '=' << static_cast<int>(overlayDataPack[index]) << '\n';
        }
    }
    out << '\n';

    out << "[RandomMapGenMetrics]\n";
    out << "Mode=" << document.mode << '\n';
    out << "Seed=" << document.seed << '\n';
    out << "Players=" << document.players << '\n';
    for (const auto& [key, value] : document.metrics) {
        out << key << '=' << value << '\n';
    }
    out << '\n';

    const bool preserveSubTiles = document.mode != "native";
    const auto isoPayload = buildIsoMapPackPayload(map, document, geometry, preserveSubTiles);
    writeEncodedSection(out, "IsoMapPack5", ra2::pack::base64Encode(ra2::pack::encodeFormat5Lzo(isoPayload)), 70);

    writeEncodedSection(out, "OverlayPack", ra2::pack::base64Encode(ra2::pack::encodeFormat5Format80(overlayPack)), 70);
    writeEncodedSection(out, "OverlayDataPack", ra2::pack::base64Encode(ra2::pack::encodeFormat5Format80(overlayDataPack)), 70);

    out << "[IsoMapPackPreview]\n";
    const auto isoOverrides = buildIsoOverrideMap(document);
    for (const auto& cell : map.cells()) {
        if (!sourceCellIsExported(geometry, {cell.x, cell.y})) {
            continue;
        }
        const Point iso = sourceCellToIso(geometry, {cell.x, cell.y});
        const Point nativeCell = sourceCellToNativeCell(geometry, {cell.x, cell.y});
        IsoPayloadEntry previewEntry;
        previewEntry.iso = iso;
        previewEntry.tile = terrainTileId(document.tileCatalog, map, {cell.x, cell.y});
        previewEntry.level = exportLevelForCell(cell);
        if (const auto overrideIt = isoOverrides.find(isoOverrideKey(nativeCell)); overrideIt != isoOverrides.end()) {
            applyIsoOverride(previewEntry, document.tileCatalog, *overrideIt->second);
        }
        out << iso.x << ',' << iso.y << '=' << previewEntry.tile << ',' << static_cast<int>(previewEntry.level) << ',' << cell.slope << ',' << pseudoPackLine(cell) << '\n';
    }
    out << '\n';

    out << "[OverlayPackPreview]\n";
    for (const auto& cell : map.cells()) {
        if (!sourceCellIsExported(geometry, {cell.x, cell.y})) {
            continue;
        }
        const int id = overlayIdForCell(cell);
        if (id >= 0) {
            out << cell.x << ',' << cell.y << '=' << id << ',' << overlayDataForCell(cell) << '\n';
        }
    }
    return out.str();
}

std::string writeYrIniProductMap(const MapGrid& map, const MapDocument& document)
{
    const ExportGeometry geometry = exportGeometryFor(map, document);
    validateYrIniIsoBounds(geometry);
    const Rect headerRect = hasExplicitRect(document.headerRect) ? document.headerRect : Rect{0, 0, map.width(), map.height()};

    std::ostringstream out;
    writeNativeProductHeader(out, document, geometry, headerRect);
    writeNativeProductSpecialFlags(out);
    writeNativeProductBasic(out);
    writeNativeProductRanking(out);
    writeNativeProductLighting(out, document.lighting);
    writeNativeProductHouses(out);

    out << "[Map]\n";
    out << "Theater=" << theaterName(document.theater) << '\n';
    out << "Size=" << geometry.mapRect.x << ',' << geometry.mapRect.y << ',' << geometry.mapRect.w << ',' << geometry.mapRect.h << '\n';
    out << "LocalSize=" << geometry.localRect.x << ',' << geometry.localRect.y << ',' << geometry.localRect.w << ',' << geometry.localRect.h << "\n\n";

    writeNativeProductWaypoints(out, document, geometry);

    const bool preserveSubTiles = document.mode != "native";
    const auto isoPayload = buildIsoMapPackPayload(map, document, geometry, preserveSubTiles);
    writeEncodedSection(out, "IsoMapPack5", ra2::pack::base64Encode(ra2::pack::encodeFormat5Lzo(isoPayload)), 70);

    writeNativeProductTerrain(out, map, document, geometry);
    writeNativeProductStructures(out, map, document, geometry);

    std::vector<std::uint8_t> overlayPack;
    std::vector<std::uint8_t> overlayDataPack;
    buildOverlayPacks(map, document, geometry, overlayPack, overlayDataPack);
    writeEncodedSection(out, "OverlayPack", ra2::pack::base64Encode(ra2::pack::encodeFormat5Format80(overlayPack)), 70);
    writeEncodedSection(out, "OverlayDataPack", ra2::pack::base64Encode(ra2::pack::encodeFormat5Format80(overlayDataPack)), 70);

    writeNativeProductAiTriggerTypesEnable(out);
    const std::string body = out.str();
    writeNativeProductDigest(out, body);
    return out.str();
}

void writeTextMapFile(const std::filesystem::path& path, const MapGrid& map, const MapDocument& document)
{
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open output file: " + path.string());
    }
    const std::string text = writeTextMap(map, document);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.flush();
    if (!file) {
        throw std::runtime_error("failed to write output file: " + path.string());
    }
}

void writeYrIniMapFile(const std::filesystem::path& path, const MapGrid& map, const MapDocument& document)
{
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open output file: " + path.string());
    }
    const std::string text = writeYrIniMap(map, document);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.flush();
    if (!file) {
        throw std::runtime_error("failed to write output file: " + path.string());
    }
}

void writeYrIniProductMapFile(const std::filesystem::path& path, const MapGrid& map, const MapDocument& document)
{
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open output file: " + path.string());
    }
    const std::string text = withCrLfLineEndings(writeYrIniProductMap(map, document));
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.flush();
    if (!file) {
        throw std::runtime_error("failed to write output file: " + path.string());
    }
}

MapDocument makeNativeDocument(const MapSeedOptions& options, const RmgConfig& config, const NativeGenerationReport& report, const MapGrid& map)
{
    MapDocument document;
    document.mode = "native";
    document.seed = options.seed;
    document.players = options.numPlayers;
    document.theater = options.theater;
    document.mapLevel = kNativeInitialMapLevel;
    if (map.hasNativeGeometry()) {
        document.mapRect = map.nativeMapRect();
        document.localSize = map.nativeLocalRect();
    } else {
        const int nativeLocalWidth = std::max(1, map.width());
        const int nativeLocalHeight = std::max(1, map.height());
        document.mapRect = {0, 0, nativeLocalWidth + 4, nativeLocalHeight + 12};
        document.localSize = {2, 5, nativeLocalWidth, nativeLocalHeight};
    }
    document.headerRect = nativeHeaderRectFromUsableArea(document.mapRect, document.localSize);
    document.lighting = makeLightingSettings(options, config);
    document.rmgConfig = config;
    document.isoTileOverrides = report.isoTileOverrides;
    document.nativeOverlayOverrides = report.nativeOverlayOverrides;
    document.nativeObjectOverrides = report.nativeObjectOverrides;
    document.nativeStructureOverrides = report.nativeStructureOverrides;
    document.nativeWaypoints = report.nativeWaypoints;
    document.notes = report.notes;
    document.metrics = {
        {"TerrainType", std::to_string(report.terrainType)},
        {"MapTheater", theaterName(document.theater)},
        {"MapLevel", std::to_string(document.mapLevel)},
        {"RandomMapPlayers", std::to_string(options.numPlayers)},
        {"RandomMapMapType", std::to_string(options.mapType)},
        {"RandomMapResources", std::to_string(options.resources)},
        {"RandomMapTime", std::to_string(options.time)},
        {"RandomMapWaterAmount", std::to_string(options.waterAmount)},
        {"RandomMapTiberium", std::to_string(options.tiberium)},
        {"RandomMapTiberiumLayout", std::to_string(options.tiberiumLayout)},
        {"RandomMapVegetation", std::to_string(options.vegetation)},
        {"RandomMapRuggedness", std::to_string(options.ruggedness)},
        {"RandomMapUrbanPresence", std::to_string(options.urbanPresence)},
        {"RandomMapAccessibility", std::to_string(options.accessibility)},
        {"RandomMapRegionSize", std::to_string(options.regionSize)},
        {"BasicNewINIFormat", std::to_string(document.basicScene.newIniFormat)},
        {"TerrainPatches", std::to_string(report.terrainPatches)},
        {"FloodFillAttempts", std::to_string(report.floodFillAttempts)},
        {"FloodFillFailures", std::to_string(report.floodFillFailures)},
        {"FloodFillMinimumClamps", std::to_string(report.floodFillMinimumClamps)},
        {"FloodFillFringeCells", std::to_string(report.floodFillFringeCells)},
        {"CliffLines", std::to_string(report.cliffLines)},
        {"CliffLineAttempts", std::to_string(report.cliffLineAttempts)},
        {"CliffLineSteps", std::to_string(report.cliffLineSteps)},
        {"CliffLineTouchedCells", std::to_string(report.cliffLineTouchedCells)},
        {"CliffLineAngleAdjustments", std::to_string(report.cliffLineAngleAdjustments)},
        {"CliffLineWidthAdjustments", std::to_string(report.cliffLineWidthAdjustments)},
        {"CliffLineBranchAttempts", std::to_string(report.cliffLineBranchAttempts)},
        {"CliffLineBranchSuccesses", std::to_string(report.cliffLineBranchSuccesses)},
        {"CliffLineRaisedCells", std::to_string(report.cliffLineRaisedCells)},
        {"CliffLineRollbacks", std::to_string(report.cliffLineRollbacks)},
        {"CliffPlateauAttempts", std::to_string(report.cliffPlateauAttempts)},
        {"CliffPlateauAccepted", std::to_string(report.cliffPlateauAccepted)},
        {"CliffPlateauRejectedBounds", std::to_string(report.cliffPlateauRejectedBounds)},
        {"CliffPlateauRejectedOccupied", std::to_string(report.cliffPlateauRejectedOccupied)},
        {"CliffPlateauRaisedCells", std::to_string(report.cliffPlateauRaisedCells)},
        {"CliffPlateauCliffCells", std::to_string(report.cliffPlateauCliffCells)},
        {"CliffPlateauWaterCells", std::to_string(report.cliffPlateauWaterCells)},
        {"CliffPlateauShoreCells", std::to_string(report.cliffPlateauShoreCells)},
        {"CliffPlateauOutwardCells", std::to_string(report.cliffPlateauOutwardCells)},
        {"CliffPlateauRollbacks", std::to_string(report.cliffPlateauRollbacks)},
        {"ZoneOutwardCalls", std::to_string(report.zoneOutwardCalls)},
        {"ZoneOutwardPasses", std::to_string(report.zoneOutwardPasses)},
        {"ZoneOutwardBorderCells", std::to_string(report.zoneOutwardBorderCells)},
        {"ZoneOutwardCandidateChecks", std::to_string(report.zoneOutwardCandidateChecks)},
        {"ZoneOutwardAcceptedCells", std::to_string(report.zoneOutwardAcceptedCells)},
        {"ZoneOutwardRejectedCells", std::to_string(report.zoneOutwardRejectedCells)},
        {"ZoneOutwardRaisedCells", std::to_string(report.zoneOutwardRaisedCells)},
        {"ZoneOutwardShoreCells", std::to_string(report.zoneOutwardShoreCells)},
        {"ZoneDirectionalCalls", std::to_string(report.zoneDirectionalCalls)},
        {"ZoneDirectionalBorderCells", std::to_string(report.zoneDirectionalBorderCells)},
        {"ZoneDirectionalHeapPushes", std::to_string(report.zoneDirectionalHeapPushes)},
        {"ZoneDirectionalHeapPops", std::to_string(report.zoneDirectionalHeapPops)},
        {"ZoneDirectionalCandidateChecks", std::to_string(report.zoneDirectionalCandidateChecks)},
        {"ZoneDirectionalAcceptedCells", std::to_string(report.zoneDirectionalAcceptedCells)},
        {"ZoneDirectionalRejectedCells", std::to_string(report.zoneDirectionalRejectedCells)},
        {"ZoneDirectionalFinalizedCells", std::to_string(report.zoneDirectionalFinalizedCells)},
        {"ZoneDirectionalAngleDrifts", std::to_string(report.zoneDirectionalAngleDrifts)},
        {"WaterChains", std::to_string(report.waterChains)},
        {"OreChainBudget", std::to_string(report.oreChainBudget)},
        {"OreChainTargetLength", std::to_string(report.oreChainTargetLength)},
        {"OreChainMaskBuilds", std::to_string(report.oreChainMaskBuilds)},
        {"OreChainMaskPasses", std::to_string(report.oreChainMaskPasses)},
        {"OreChainMaskAllowedCells", std::to_string(report.oreChainMaskAllowedCells)},
        {"OreChainMaskRejectedCells", std::to_string(report.oreChainMaskRejectedCells)},
        {"OreChainSeedSearches", std::to_string(report.oreChainSeedSearches)},
        {"OreChainHeapPushes", std::to_string(report.oreChainHeapPushes)},
        {"OreChainHeapPops", std::to_string(report.oreChainHeapPops)},
        {"OreChainCandidateChecks", std::to_string(report.oreChainCandidateChecks)},
        {"OreChainRejectedByMask", std::to_string(report.oreChainRejectedByMask)},
        {"OreChainRejectedCells", std::to_string(report.oreChainRejectedCells)},
        {"OreChainCommittedCells", std::to_string(report.oreChainCommittedCells)},
        {"OreChainShoreCells", std::to_string(report.oreChainShoreCells)},
        {"OreChainRollbacks", std::to_string(report.oreChainRollbacks)},
        {"RegionCandidates", std::to_string(report.regionCandidates)},
        {"RegionMinimumSize", std::to_string(report.regionMinimumSize)},
        {"RegionFilteredSmall", std::to_string(report.regionFilteredSmall)},
        {"Regions", std::to_string(report.regions)},
        {"ConnectedRegions", std::to_string(report.connectedRegions)},
        {"RegionBorderExpansionCalls", std::to_string(report.regionBorderExpansionCalls)},
        {"RegionBorderExpansionPasses", std::to_string(report.regionBorderExpansionPasses)},
        {"RegionBorderExpansionBoundaryCells", std::to_string(report.regionBorderExpansionBoundaryCells)},
        {"RegionBorderExpansionCandidateChecks", std::to_string(report.regionBorderExpansionCandidateChecks)},
        {"RegionBorderExpansionAcceptedCells", std::to_string(report.regionBorderExpansionAcceptedCells)},
        {"RegionBorderExpansionConflictFailures", std::to_string(report.regionBorderExpansionConflictFailures)},
        {"RegionBorderExpansionAborts", std::to_string(report.regionBorderExpansionAborts)},
        {"RegionBorderExpansionRejectedWater", std::to_string(report.regionBorderExpansionRejectedWater)},
        {"RegionBorderExpansionRejectedTerrain", std::to_string(report.regionBorderExpansionRejectedTerrain)},
        {"RegionBorderExpansionLevelAdjustments", std::to_string(report.regionBorderExpansionLevelAdjustments)},
        {"RegionProcessCalls", std::to_string(report.regionProcessCalls)},
        {"RegionProcessMinimumSize", std::to_string(report.regionProcessMinimumSize)},
        {"RegionProcessSkippedSmall", std::to_string(report.regionProcessSkippedSmall)},
        {"RegionProcessInputCells", std::to_string(report.regionProcessInputCells)},
        {"RegionProcessSubregions", std::to_string(report.regionProcessSubregions)},
        {"RegionProcessOutputRegions", std::to_string(report.regionProcessOutputRegions)},
        {"RegionProcessOutputCells", std::to_string(report.regionProcessOutputCells)},
        {"RegionProcessBorderCells", std::to_string(report.regionProcessBorderCells)},
        {"RegionProcessNeighborLinks", std::to_string(report.regionProcessNeighborLinks)},
        {"RegionProcessCandidateLevels", std::to_string(report.regionProcessCandidateLevels)},
        {"RegionProcessSmallMerges", std::to_string(report.regionProcessSmallMerges)},
        {"RegionProcessLevelAdjustments", std::to_string(report.regionProcessLevelAdjustments)},
        {"RegionNeighborBuildCalls", std::to_string(report.regionNeighborBuildCalls)},
        {"RegionNeighborBoundaryCells", std::to_string(report.regionNeighborBoundaryCells)},
        {"RegionNeighborLinks", std::to_string(report.regionNeighborLinks)},
        {"RegionBridgeCliffCalls", std::to_string(report.regionBridgeCliffCalls)},
        {"RegionBridgePairsChecked", std::to_string(report.regionBridgePairsChecked)},
        {"RegionBridgeAttempts", std::to_string(report.regionBridgeAttempts)},
        {"RegionBridgesPlaced", std::to_string(report.regionBridgesPlaced)},
        {"RegionCliffPairsChecked", std::to_string(report.regionCliffPairsChecked)},
        {"RegionCliffAttempts", std::to_string(report.regionCliffAttempts)},
        {"RegionCliffsPlaced", std::to_string(report.regionCliffsPlaced)},
        {"RegionCliffMaskChecks", std::to_string(report.regionCliffMaskChecks)},
        {"RegionCliffMaskFailures", std::to_string(report.regionCliffMaskFailures)},
        {"RegionCliffAttemptLimitHits", std::to_string(report.regionCliffAttemptLimitHits)},
        {"StartsPlaced", std::to_string(report.startsPlaced)},
        {"SpawnExclusionCells", std::to_string(report.spawnExclusionCells)},
        {"SpawnExclusionStarts", std::to_string(report.spawnExclusionStarts)},
        {"SpawnExclusionHeapPushes", std::to_string(report.spawnExclusionHeapPushes)},
        {"SpawnExclusionHeapPops", std::to_string(report.spawnExclusionHeapPops)},
        {"SpawnExclusionCandidateChecks", std::to_string(report.spawnExclusionCandidateChecks)},
        {"SpawnExclusionRejectedNonBlank", std::to_string(report.spawnExclusionRejectedNonBlank)},
        {"SpawnExclusionLimitHits", std::to_string(report.spawnExclusionLimitHits)},
        {"CliffShoreExclusionCells", std::to_string(report.cliffShoreExclusionCells)},
        {"TechBuildingPlacementMode", std::to_string(report.techBuildingPlacementMode)},
        {"TechBuildingRounds", std::to_string(report.techBuildingRounds)},
        {"TechBuildingEligibleRegions", std::to_string(report.techBuildingEligibleRegions)},
        {"TechBuildingTargets", std::to_string(report.techBuildingTargets)},
        {"TechBuildingRegionCalls", std::to_string(report.techBuildingRegionCalls)},
        {"TechBuildingCandidateSamples", std::to_string(report.techBuildingCandidateSamples)},
        {"TechBuildingBlankSearchRejects", std::to_string(report.techBuildingBlankSearchRejects)},
        {"TechBuildingGlobalFallbacks", std::to_string(report.techBuildingGlobalFallbacks)},
        {"TechBuildingFootprintChecks", std::to_string(report.techBuildingFootprintChecks)},
        {"TechBuildingFootprintFailures", std::to_string(report.techBuildingFootprintFailures)},
        {"TechBuildingRejectedObjects", std::to_string(report.techBuildingRejectedObjects)},
        {"TechBuildingRejectedNonBlank", std::to_string(report.techBuildingRejectedNonBlank)},
        {"TechBuildingRejectedLevel", std::to_string(report.techBuildingRejectedLevel)},
        {"TechBuildingRejectedRock", std::to_string(report.techBuildingRejectedRock)},
        {"TechBuildingRejectedUsableArea", std::to_string(report.techBuildingRejectedUsableArea)},
        {"TechBuildingRejectedExclusion", std::to_string(report.techBuildingRejectedExclusion)},
        {"TechBuildingUnlimboSuccesses", std::to_string(report.techBuildingUnlimboSuccesses)},
        {"TechBuildings", std::to_string(report.techBuildings)},
        {"TiberiumPatches", std::to_string(report.tiberiumPatches)},
        {"RMGMinimumTiberium", std::to_string(report.rmgMinimumTiberium)},
        {"RMGMaximumTiberium", std::to_string(report.rmgMaximumTiberium)},
        {"TiberiumBudget", std::to_string(report.tiberiumBudget)},
        {"TiberiumBudgetCells", std::to_string(report.tiberiumBudgetCells)},
        {"RegionWaypointQuotaTotal", std::to_string(report.regionWaypointQuotaTotal)},
        {"RegionStartQuotaTotal", std::to_string(report.regionStartQuotaTotal)},
        {"NativeWaypointsSelected", std::to_string(report.nativeWaypointsSelected)},
        {"NativeSpawnCandidateCells", std::to_string(report.nativeSpawnCandidateCells)},
        {"NativeSpawnPoolCells", std::to_string(report.nativeSpawnPoolCells)},
        {"NativeSpawnExtraCells", std::to_string(report.nativeSpawnExtraCells)},
        {"TiberiumRegionSeeds", std::to_string(report.tiberiumRegionSeeds)},
        {"TiberiumWaypointSeeds", std::to_string(report.tiberiumWaypointSeeds)},
        {"TiberiumSpecialPatchSeeds", std::to_string(report.tiberiumSpecialPatchSeeds)},
        {"RichTiberiumPatches", std::to_string(report.richTiberiumPatches)},
        {"TiberiumPatchHeapPushes", std::to_string(report.tiberiumPatchHeapPushes)},
        {"TiberiumPatchHeapPops", std::to_string(report.tiberiumPatchHeapPops)},
        {"TiberiumPatchCandidateChecks", std::to_string(report.tiberiumPatchCandidateChecks)},
        {"TiberiumPatchRejectedCells", std::to_string(report.tiberiumPatchRejectedCells)},
        {"TiberiumPatchExistingOverlayIncrements", std::to_string(report.tiberiumPatchExistingOverlayIncrements)},
        {"TiberiumPatchFirstCellResets", std::to_string(report.tiberiumPatchFirstCellResets)},
        {"TiberiumPatchInitialRetries", std::to_string(report.tiberiumPatchInitialRetries)},
        {"TiberiumPatchUnfilled", std::to_string(report.tiberiumPatchUnfilled)},
        {"LandCells", std::to_string(report.landCells)},
        {"DirtCells", std::to_string(report.dirtCells)},
        {"RoughCells", std::to_string(report.roughCells)},
        {"GreenCells", std::to_string(report.greenCells)},
        {"WaterCells", std::to_string(report.waterCells)},
        {"ShoreCells", std::to_string(report.shoreCells)},
        {"TerrainPostWaterChecks", std::to_string(report.terrainPostWaterChecks)},
        {"TerrainPostWaterCleared", std::to_string(report.terrainPostWaterCleared)},
        {"TerrainPostShoreAnchors", std::to_string(report.terrainPostShoreAnchors)},
        {"TerrainPostShoreCandidates", std::to_string(report.terrainPostShoreCandidates)},
        {"TerrainPostShoreTiles", std::to_string(report.terrainPostShoreTiles)},
        {"ShoreInitialCandidates", std::to_string(report.shoreInitialCandidates)},
        {"ShoreTargetTiles", std::to_string(report.shoreTargetTiles)},
        {"ShoreTilesPlaced", std::to_string(report.shoreTilesPlaced)},
        {"ShoreExpansionCandidates", std::to_string(report.shoreExpansionCandidates)},
        {"Pond2x2Tiles", std::to_string(report.pond2x2Tiles)},
        {"PondEdgeTiles", std::to_string(report.pondEdgeTiles)},
        {"PondTemplateCells", std::to_string(report.pondTemplateCells)},
        {"ShoreSlopeVariants", std::to_string(report.shoreSlopeVariants)},
        {"CliffSlopeVariants", std::to_string(report.cliffSlopeVariants)},
        {"HeightNoiseCells", std::to_string(report.heightNoiseCells)},
        {"HeightNoiseSkippedLowRuggedness", std::to_string(report.heightNoiseSkippedLowRuggedness)},
        {"HeightNoiseGaussianSamples", std::to_string(report.heightNoiseGaussianSamples)},
        {"HeightNoiseExclusionClamps", std::to_string(report.heightNoiseExclusionClamps)},
        {"HeightNoiseRaisedCells", std::to_string(report.heightNoiseRaisedCells)},
        {"HeightNoiseLoweredCells", std::to_string(report.heightNoiseLoweredCells)},
        {"HeightNoiseTotalRaiseSteps", std::to_string(report.heightNoiseTotalRaiseSteps)},
        {"HeightNoiseTotalLowerSteps", std::to_string(report.heightNoiseTotalLowerSteps)},
        {"SlopePatternChecks", std::to_string(report.slopePatternChecks)},
        {"SlopePatternMatches", std::to_string(report.slopePatternMatches)},
        {"SlopePatternClearedCells", std::to_string(report.slopePatternClearedCells)},
        {"SlopePatternRaisedLevels", std::to_string(report.slopePatternRaisedLevels)},
        {"OreCells", std::to_string(report.oreCells)},
        {"GemCells", std::to_string(report.gemCells)},
        {"OreValue", std::to_string(report.oreValue)},
        {"SurfaceBlobs", std::to_string(report.surfaceBlobs)},
        {"SurfaceCells", std::to_string(report.surfaceCells)},
        {"SurfaceMode", std::to_string(report.surfaceMode)},
        {"SurfaceSimpleDensityCells", std::to_string(report.surfaceSimpleDensityCells)},
        {"SurfaceSimpleRolls", std::to_string(report.surfaceSimpleRolls)},
        {"SurfaceSimpleHits", std::to_string(report.surfaceSimpleHits)},
        {"SurfaceSimpleSizeSamples", std::to_string(report.surfaceSimpleSizeSamples)},
        {"SurfaceSimpleTreeTarget", std::to_string(report.surfaceSimpleTreeTarget)},
        {"VegetationDensityCells", std::to_string(report.vegetationDensityCells)},
        {"VegetationDensityBoosts", std::to_string(report.vegetationDensityBoosts)},
        {"VegetationDensityRolls", std::to_string(report.vegetationDensityRolls)},
        {"VegetationDensityHits", std::to_string(report.vegetationDensityHits)},
        {"GrassTarget", std::to_string(report.grassTarget)},
        {"GrassAttemptBudget", std::to_string(report.grassAttemptBudget)},
        {"GrassAttempts", std::to_string(report.grassAttempts)},
        {"GrassCandidateSamples", std::to_string(report.grassCandidateSamples)},
        {"GrassRejectedOccupied", std::to_string(report.grassRejectedOccupied)},
        {"GrassRejectedTerrain", std::to_string(report.grassRejectedTerrain)},
        {"GrassPlaced", std::to_string(report.grassPlaced)},
        {"HeapSurfaceBlobs", std::to_string(report.heapSurfaceBlobs)},
        {"HeapSurfaceCells", std::to_string(report.heapSurfaceCells)},
        {"GroundBlobHeapPushes", std::to_string(report.groundBlobHeapPushes)},
        {"GroundBlobHeapPops", std::to_string(report.groundBlobHeapPops)},
        {"GroundBlobCandidateChecks", std::to_string(report.groundBlobCandidateChecks)},
        {"GroundBlobRejectedCells", std::to_string(report.groundBlobRejectedCells)},
        {"GroundBlobTaggedCells", std::to_string(report.groundBlobTaggedCells)},
        {"DecorationCentersTried", std::to_string(report.decorationCentersTried)},
        {"DecorationCentersAccepted", std::to_string(report.decorationCentersAccepted)},
        {"DecorationTargetEvents", std::to_string(report.decorationTargetEvents)},
        {"DecorationEvents", std::to_string(report.decorationEvents)},
        {"DecorationUrbanAttempts", std::to_string(report.decorationUrbanAttempts)},
        {"DecorationUrbanSkips", std::to_string(report.decorationUrbanSkips)},
        {"UrbanPatchRects", std::to_string(report.urbanPatchRects)},
        {"UrbanPatchRectCells", std::to_string(report.urbanPatchRectCells)},
        {"DecorationFootprintChecks", std::to_string(report.decorationFootprintChecks)},
        {"DecorationFootprintFailures", std::to_string(report.decorationFootprintFailures)},
        {"DecorationFootprintRejectedBounds", std::to_string(report.decorationFootprintRejectedBounds)},
        {"DecorationFootprintRejectedTerrain", std::to_string(report.decorationFootprintRejectedTerrain)},
        {"DecorationFootprintRejectedOccupied", std::to_string(report.decorationFootprintRejectedOccupied)},
        {"DecorationFootprintRejectedLevel", std::to_string(report.decorationFootprintRejectedLevel)},
        {"DecorationFootprintRejectedRock", std::to_string(report.decorationFootprintRejectedRock)},
        {"DecorationFootprintRejectedExclusion", std::to_string(report.decorationFootprintRejectedExclusion)},
        {"UrbanRegionCells", std::to_string(report.urbanRegionCells)},
        {"UrbanRegionBoundaryCells", std::to_string(report.urbanRegionBoundaryCells)},
        {"UrbanRegionRejected", std::to_string(report.urbanRegionRejected)},
        {"UrbanRegionRejectedSmall", std::to_string(report.urbanRegionRejectedSmall)},
        {"UrbanRegionRejectedSparse", std::to_string(report.urbanRegionRejectedSparse)},
        {"UrbanRegionHeapPushes", std::to_string(report.urbanRegionHeapPushes)},
        {"UrbanRegionHeapPops", std::to_string(report.urbanRegionHeapPops)},
        {"UrbanRegionMarkedCells", std::to_string(report.urbanRegionMarkedCells)},
        {"UrbanRegionAnchorPavedCells", std::to_string(report.urbanRegionAnchorPavedCells)},
        {"UrbanRegionInitialPaveAttempts", std::to_string(report.urbanRegionInitialPaveAttempts)},
        {"UrbanRegionInitialPavedCells", std::to_string(report.urbanRegionInitialPavedCells)},
        {"UrbanRegionInitialPave2x2", std::to_string(report.urbanRegionInitialPave2x2)},
        {"UrbanCliffEdgeCandidates", std::to_string(report.urbanCliffEdgeCandidates)},
        {"UrbanCliffEdgeSegments", std::to_string(report.urbanCliffEdgeSegments)},
        {"UrbanCliffEdgeCells", std::to_string(report.urbanCliffEdgeCells)},
        {"UrbanCliffEdgeEndpointVariants", std::to_string(report.urbanCliffEdgeEndpointVariants)},
        {"UrbanCliffEdgeRandomShortens", std::to_string(report.urbanCliffEdgeRandomShortens)},
        {"UrbanCliffEdgeAreaFailures", std::to_string(report.urbanCliffEdgeAreaFailures)},
        {"UrbanCliffEdgeDecorTiles", std::to_string(report.urbanCliffEdgeDecorTiles)},
        {"UrbanRegions", std::to_string(report.urbanRegions)},
        {"UrbanPavedCells", std::to_string(report.urbanPavedCells)},
        {"CivilianBuildingTargets", std::to_string(report.civilianBuildingTargets)},
        {"CivilianBuildingAttempts", std::to_string(report.civilianBuildingAttempts)},
        {"CivilianBuildingFootprintFailures", std::to_string(report.civilianBuildingFootprintFailures)},
        {"CivilianBuildings", std::to_string(report.civilianBuildings)},
        {"CivilianUnitTargets", std::to_string(report.civilianUnitTargets)},
        {"CivilianUnitAttempts", std::to_string(report.civilianUnitAttempts)},
        {"CivilianUnitRejectedBounds", std::to_string(report.civilianUnitRejectedBounds)},
        {"CivilianUnitRejectedOccupied", std::to_string(report.civilianUnitRejectedOccupied)},
        {"CivilianUnitRejectedExclusion", std::to_string(report.civilianUnitRejectedExclusion)},
        {"CivilianInfantry", std::to_string(report.civilianInfantry)},
        {"CivilianVehicleUnits", std::to_string(report.civilianVehicleUnits)},
        {"CivilianUnits", std::to_string(report.civilianUnits)},
        {"TerrainChains", std::to_string(report.terrainChains)},
        {"TerrainChainCells", std::to_string(report.terrainChainCells)},
        {"TerrainChainFrontierSteps", std::to_string(report.terrainChainFrontierSteps)},
        {"TerrainChainBranchSteps", std::to_string(report.terrainChainBranchSteps)},
        {"TerrainChainEntriesQueued", std::to_string(report.terrainChainEntriesQueued)},
        {"TerrainChainEntriesProcessed", std::to_string(report.terrainChainEntriesProcessed)},
        {"TerrainChainQueueMax", std::to_string(report.terrainChainQueueMax)},
        {"TerrainChainRejectedPlacements", std::to_string(report.terrainChainRejectedPlacements)},
        {"TerrainChainTerrainSelections", std::to_string(report.terrainChainTerrainSelections)},
        {"TerrainChainSelectionFailures", std::to_string(report.terrainChainSelectionFailures)},
        {"TerrainChainGaussianSizeSamples", std::to_string(report.terrainChainGaussianSizeSamples)},
        {"TerrainChainSizeClassFallbacks", std::to_string(report.terrainChainSizeClassFallbacks)},
        {"TerrainChainTileAttempts", std::to_string(report.terrainChainTileAttempts)},
        {"TerrainChainConnectionSkips", std::to_string(report.terrainChainConnectionSkips)},
        {"TerrainChainQueueSaturationRejects", std::to_string(report.terrainChainQueueSaturationRejects)},
        {"TerrainChainTailRestores", std::to_string(report.terrainChainTailRestores)},
        {"TerrainChainSize2Skips", std::to_string(report.terrainChainSize2Skips)},
        {"TerrainChainFoundationCellsChecked", std::to_string(report.terrainChainFoundationCellsChecked)},
        {"TerrainChainRejectedBounds", std::to_string(report.terrainChainRejectedBounds)},
        {"TerrainChainRejectedTerrain", std::to_string(report.terrainChainRejectedTerrain)},
        {"TerrainChainRejectedOccupied", std::to_string(report.terrainChainRejectedOccupied)},
        {"TerrainChainRejectedSlope", std::to_string(report.terrainChainRejectedSlope)},
        {"TerrainChainRejectedMarkedFootprints", std::to_string(report.terrainChainRejectedMarkedFootprints)},
        {"TerrainChainFootprintsMarked", std::to_string(report.terrainChainFootprintsMarked)},
        {"TerrainChainFootprintsCleared", std::to_string(report.terrainChainFootprintsCleared)},
        {"TerrainChainFootprintCellsMarked", std::to_string(report.terrainChainFootprintCellsMarked)},
        {"TerrainChainFootprintCellsCleared", std::to_string(report.terrainChainFootprintCellsCleared)},
        {"TerrainChainTilesCleared", std::to_string(report.terrainChainTilesCleared)},
        {"TerrainChainTerminalFallbacks", std::to_string(report.terrainChainTerminalFallbacks)},
        {"NeutralDecorationBuildingTargets", std::to_string(report.neutralDecorationBuildingTargets)},
        {"NeutralDecorationBuildingAttempts", std::to_string(report.neutralDecorationBuildingAttempts)},
        {"NeutralDecorationBuildingFootprintFailures", std::to_string(report.neutralDecorationBuildingFootprintFailures)},
        {"NeutralDecorationBuildings", std::to_string(report.neutralDecorationBuildings)},
        {"NeutralDecorationUnitTargets", std::to_string(report.neutralDecorationUnitTargets)},
        {"NeutralDecorationUnitAttempts", std::to_string(report.neutralDecorationUnitAttempts)},
        {"NeutralDecorationUnitRejectedBounds", std::to_string(report.neutralDecorationUnitRejectedBounds)},
        {"NeutralDecorationUnitRejectedOccupied", std::to_string(report.neutralDecorationUnitRejectedOccupied)},
        {"NeutralDecorationUnitRejectedExclusion", std::to_string(report.neutralDecorationUnitRejectedExclusion)},
        {"NeutralDecorationInfantry", std::to_string(report.neutralDecorationInfantry)},
        {"NeutralDecorationVehicleUnits", std::to_string(report.neutralDecorationVehicleUnits)},
        {"NeutralDecorationUnits", std::to_string(report.neutralDecorationUnits)},
        {"MaxTrees", std::to_string(report.maxTrees)},
        {"VegetationMinimum", std::to_string(report.vegetationMinimum)},
        {"VegetationMaximum", std::to_string(report.vegetationMaximum)},
        {"TreePlacementPhase", std::to_string(report.treePlacementPhase)},
        {"TreeClusterTarget", std::to_string(report.treeClusterTarget)},
        {"TreeClusters", std::to_string(report.treeClusters)},
        {"TreeCells", std::to_string(report.treeCells)},
        {"HeapTreeClusters", std::to_string(report.heapTreeClusters)},
        {"HeapTreeCells", std::to_string(report.heapTreeCells)},
        {"TreePatchHeapPushes", std::to_string(report.treePatchHeapPushes)},
        {"TreePatchHeapPops", std::to_string(report.treePatchHeapPops)},
        {"TreePatchCandidateChecks", std::to_string(report.treePatchCandidateChecks)},
        {"TreePatchRejectedCells", std::to_string(report.treePatchRejectedCells)},
        {"TreePatchTaggedCells", std::to_string(report.treePatchTaggedCells)},
        {"TreePatchPlacementChecks", std::to_string(report.treePatchPlacementChecks)},
        {"TreePatchRejectedPlacements", std::to_string(report.treePatchRejectedPlacements)},
        {"TiberiumTrees", std::to_string(report.tiberiumTrees)},
        {"OrePatchLamps", std::to_string(report.orePatchLamps)},
        {"MinStartResourceValue", std::to_string(report.minStartResourceValue)},
        {"MaxStartResourceValue", std::to_string(report.maxStartResourceValue)},
        {"StartResourceValueSpread", std::to_string(report.startResourceValueSpread)},
        {"MinStartResourceDistance", std::to_string(report.minStartResourceDistance)},
        {"MaxStartResourceDistance", std::to_string(report.maxStartResourceDistance)},
        {"StartResourceDistanceSpread", std::to_string(report.startResourceDistanceSpread)},
        {"MinBuildableNearStart", std::to_string(report.minBuildableNearStart)},
        {"MaxBuildableNearStart", std::to_string(report.maxBuildableNearStart)},
        {"BuildableNearStartSpread", std::to_string(report.buildableNearStartSpread)},
        {"BasicTiberiumGrowthEnabled", trueFalse(document.basicScene.tiberiumGrowthEnabled)},
        {"BasicVeinGrowthEnabled", trueFalse(document.basicScene.veinGrowthEnabled)},
        {"BasicIceGrowthEnabled", trueFalse(document.basicScene.iceGrowthEnabled)},
        {"BasicTiberiumDeathToVisceroid", trueFalse(document.basicScene.tiberiumDeathToVisceroid)},
        {"BasicFreeRadar", trueFalse(document.basicScene.freeRadar)},
        {"BasicInitTime", std::to_string(document.basicScene.initTime)},
        {"SessionBridgeDestruction", trueFalse(document.multiplayerSession.bridgeDestruction)},
        {"SessionCrates", trueFalse(document.multiplayerSession.crates)},
        {"LightingLevelIndex", std::to_string(document.lighting.levelIndex)},
        {"LightingAmbient", std::to_string(document.lighting.ambient)},
        {"LightingRed", std::to_string(document.lighting.red)},
        {"LightingGreen", std::to_string(document.lighting.green)},
        {"LightingBlue", std::to_string(document.lighting.blue)},
        {"DominatorLightingAmbient", std::to_string(document.lighting.dominatorAmbient)},
        {"DominatorLightingRed", std::to_string(document.lighting.dominatorRed)},
        {"DominatorLightingGreen", std::to_string(document.lighting.dominatorGreen)},
        {"DominatorLightingBlue", std::to_string(document.lighting.dominatorBlue)},
        {"DominatorLightingGround", std::to_string(document.lighting.dominatorGround)},
        {"DominatorLightingLevel", std::to_string(document.lighting.dominatorLevel)},
        {"DominatorAmbientChangeRate", std::to_string(document.lighting.dominatorAmbientChangeRate)},
        {"RMGLevelLightSettings", joinInts(config.levelLightSettings)},
        {"TemperateAmbientLight", joinInts(config.temperateAmbientLight)},
        {"SnowAmbientLight", joinInts(config.snowAmbientLight)},
        {"TemperateAmbientRed", joinInts(config.temperateAmbientRed)},
        {"TemperateAmbientGreen", joinInts(config.temperateAmbientGreen)},
        {"TemperateAmbientBlue", joinInts(config.temperateAmbientBlue)},
        {"SnowAmbientRed", joinInts(config.snowAmbientRed)},
        {"SnowAmbientGreen", joinInts(config.snowAmbientGreen)},
        {"SnowAmbientBlue", joinInts(config.snowAmbientBlue)},
        {"TemperateOrePatchLamps", joinStrings(config.temperateOrePatchLamps)},
        {"SnowOrePatchLamps", joinStrings(config.snowOrePatchLamps)},
        {"RMGTechBuildings", joinStrings(config.techBuildings)},
        {"RMGCivilianBuildings", joinStrings(config.civilianBuildings)},
        {"RMGNeutralUnits", joinStrings(config.neutralUnits)},
        {"RMGTerrainChainObjects", joinStrings(config.terrainChainObjects)},
        {"MapEvaluatorVersion", report.evaluation.evaluatorVersion},
        {"MapPlayability", std::to_string(report.evaluation.playability)},
        {"MapFairness", std::to_string(report.evaluation.fairness)},
        {"MapEvaluationConfidence", std::to_string(report.evaluation.confidence)},
        {"MapTopology", report.evaluation.topology},
        {"MapPace", report.evaluation.pace},
        {"MapGroundContactRatio", std::to_string(report.evaluation.groundContactRatio)},
        {"MapResourceCentralization", std::to_string(report.evaluation.resourceCentralization)},
        {"MapUtilization", std::to_string(report.evaluation.mapUtilization)},
    };

    for (std::size_t i = 0; i < report.startResourceValues.size(); ++i) {
        document.metrics.push_back({"Player" + std::to_string(i + 1) + "StartResourceValue", std::to_string(report.startResourceValues[i])});
    }
    for (std::size_t i = 0; i < report.startResourceDistances.size(); ++i) {
        document.metrics.push_back({"Player" + std::to_string(i + 1) + "StartResourceDistance", std::to_string(report.startResourceDistances[i])});
    }
    for (std::size_t i = 0; i < report.buildableNearStarts.size(); ++i) {
        document.metrics.push_back({"Player" + std::to_string(i + 1) + "BuildableNearStart", std::to_string(report.buildableNearStarts[i])});
    }

    for (const auto& cell : map.cells()) {
        if (cell.overlay == Overlay::Start) {
            document.starts.push_back({cell.x, cell.y});
        }
    }
    return document;
}

} // namespace rmg
