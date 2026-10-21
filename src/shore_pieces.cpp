#include "rmg/shore_pieces.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace rmg {
namespace {

// Native MapClass_GetCliffSlopeFlags_57B210 bit order. The names are map-grid
// directions around the current land/shore candidate cell.
constexpr int kNorthWest = 0x40;
constexpr int kNorth = 0x80;
constexpr int kNorthEast = 0x01;
constexpr int kWest = 0x20;
constexpr int kEast = 0x02;
constexpr int kSouthWest = 0x10;
constexpr int kSouth = 0x08;
constexpr int kSouthEast = 0x04;

struct ShoreTemplateInfo {
    int width = 1;
    int height = 1;
    // Row-major TMP image slots from the real Temperate shoreNN.tem files.
    // W=water side, L=land/beach side, .=empty foundation slot.
    std::string_view terrain;
};

struct ShorePlacement {
    int piece = 0;
    Point anchor{};
    int score = 0;
};

struct ShoreSubTileCandidate {
    ShorePiece piece{};
    int requiredFlags = 0;
    int forbiddenFlags = 0;
    int priority = 0;
};

struct PieceList {
    std::array<int, 12> values{};
    int count = 0;
};

constexpr int kFirstShorePiece = 1;
constexpr int kLastShorePiece = 42;

constexpr std::array<ShoreTemplateInfo, 42> kShoreTemplates{{
    {2, 2, "LLWW"}, {2, 2, "LLWW"}, {2, 2, "LLWW"}, {1, 2, "LW"},
    {2, 3, "LLWLWW"}, {2, 3, "LLLWWW"}, {2, 2, "LWWW"}, {2, 2, "LWWW"},
    {2, 2, "LWLW"}, {2, 2, "LWLW"}, {2, 2, "LWLW"}, {2, 1, "LW"},
    {3, 2, "LLWLWW"}, {3, 2, "LWWLLW"}, {2, 2, "WWLW"}, {2, 2, "WWLW"},
    {2, 2, "WWLL"}, {2, 2, "WWLL"}, {2, 2, "WWLL"}, {1, 2, "WL"},
    {2, 3, "WWWLLL"}, {2, 3, "WWLWLL"}, {2, 2, "WWWL"}, {2, 2, "WWWL"},
    {2, 2, "WLWL"}, {2, 2, "WLWL"}, {2, 2, "WLWL"}, {2, 1, "WL"},
    {3, 2, "WLLWWL"}, {3, 2, "WWLWLL"}, {2, 2, "WLWW"}, {2, 2, "WLWW"},
    {2, 2, "LLLW"}, {2, 2, "LLLW"}, {2, 2, "LWLL"}, {2, 2, "LWLL"},
    {2, 2, "WLLL"}, {2, 2, "WLLL"}, {2, 2, "LLWL"}, {2, 2, "LLWL"},
    {6, 4, ".LLL..LWWWWWWWWWWWWWWWWW"}, {9, 5, "..WW.......WWL....LWWWWWWWW.WWWWWWWW.WWWWWWWW"},
}};

static_assert(kShoreTemplates.size() == 42);

bool isLargeShorePiece(int piece) noexcept
{
    return piece == 41 || piece == 42;
}

bool enableLargeVisualShorePieces() noexcept
{
    return false;
}

bool enableMultiCellShoreFootprints() noexcept
{
    if (const char* value = std::getenv("RMG_NATIVE_SHORE_MULTI_CELL"); value != nullptr && value[0] == '0') {
        return false;
    }
    return true;
}

constexpr bool has(int mask, int bit) noexcept
{
    return (mask & bit) != 0;
}

bool isWaterCell(const MapGrid& map, Point point) noexcept
{
    return map.inBounds(point) && map.at(point).terrain == Terrain::Water;
}

bool isShoreCell(const MapGrid& map, Point point) noexcept
{
    return map.inBounds(point) && map.at(point).terrain == Terrain::Shore;
}

bool touchesShoreCell(const MapGrid& map, Point point) noexcept
{
    if (!map.inBounds(point)) {
        return false;
    }
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            if (isShoreCell(map, {point.x + dx, point.y + dy})) {
                return true;
            }
        }
    }
    return false;
}

bool canWriteTemplateSlot(const MapGrid& map, Point point, char slot) noexcept
{
    return slot != 'W' || touchesShoreCell(map, point);
}

int shoreNeighbors4(const MapGrid& map, Point point) noexcept
{
    int count = 0;
    if (isShoreCell(map, {point.x, point.y - 1})) {
        ++count;
    }
    if (isShoreCell(map, {point.x + 1, point.y})) {
        ++count;
    }
    if (isShoreCell(map, {point.x, point.y + 1})) {
        ++count;
    }
    if (isShoreCell(map, {point.x - 1, point.y})) {
        ++count;
    }
    return count;
}

bool canWriteWaterSideShoreSlot(const MapGrid& map, Point point) noexcept
{
    return touchesShoreCell(map, point);
}

bool canOverwriteShoreCell(const MapGrid& map, Point point) noexcept
{
    if (!isShoreCell(map, point)) {
        return false;
    }
    const Cell& cell = map.at(point);
    return cell.overlay == Overlay::None && cell.terrainObject == TerrainObject::None;
}

char templateSlot(const ShoreTemplateInfo& info, int x, int y) noexcept
{
    const int subTile = y * info.width + x;
    if (x < 0 || y < 0 || x >= info.width || y >= info.height || subTile < 0 || static_cast<std::size_t>(subTile) >= info.terrain.size()) {
        return '.';
    }
    const char slot = info.terrain[static_cast<std::size_t>(subTile)];
    return slot == ' ' ? '.' : slot;
}

bool isValidTemplateSlot(const ShoreTemplateInfo& info, int x, int y) noexcept
{
    return templateSlot(info, x, y) != '.';
}

int validTemplateSlotCount(const ShoreTemplateInfo& info) noexcept
{
    int count = 0;
    for (int y = 0; y < info.height; ++y) {
        for (int x = 0; x < info.width; ++x) {
            if (isValidTemplateSlot(info, x, y)) {
                ++count;
            }
        }
    }
    return count;
}

bool canUseCellForTemplateSlot(const MapGrid& map, Point point, char slot, bool allowLandSlotOnWater = false) noexcept
{
    if (!map.inBounds(point)) {
        return false;
    }

    const Cell& cell = map.at(point);
    if (cell.overlay != Overlay::None || cell.terrainObject != TerrainObject::None || cell.terrain == Terrain::Cliff) {
        return false;
    }
    if (slot == 'W') {
        return cell.terrain == Terrain::Water;
    }
    if (allowLandSlotOnWater && cell.terrain == Terrain::Water) {
        return true;
    }
    return cell.terrain == Terrain::Shore || isSolidLand(cell);
}

std::uint32_t shoreNoise(Point point, int salt) noexcept
{
    std::uint32_t value = static_cast<std::uint32_t>(point.x) * 0x45D9F3Bu
        ^ static_cast<std::uint32_t>(point.y) * 0x119DE1F3u
        ^ static_cast<std::uint32_t>(salt) * 0x27D4EB2Du;
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

int waterFlagsForCell(const MapGrid& map, Point point) noexcept
{
    int flags = 0;
    if (isWaterCell(map, {point.x - 1, point.y - 1})) {
        flags |= kNorthWest;
    }
    if (isWaterCell(map, {point.x, point.y - 1})) {
        flags |= kNorth;
    }
    if (isWaterCell(map, {point.x + 1, point.y - 1})) {
        flags |= kNorthEast;
    }
    if (isWaterCell(map, {point.x - 1, point.y})) {
        flags |= kWest;
    }
    if (isWaterCell(map, {point.x + 1, point.y})) {
        flags |= kEast;
    }
    if (isWaterCell(map, {point.x - 1, point.y + 1})) {
        flags |= kSouthWest;
    }
    if (isWaterCell(map, {point.x, point.y + 1})) {
        flags |= kSouth;
    }
    if (isWaterCell(map, {point.x + 1, point.y + 1})) {
        flags |= kSouthEast;
    }
    return flags;
}

int cardinalFlags(int flags) noexcept
{
    return flags & (kNorth | kEast | kSouth | kWest);
}

int countCardinalWater(int flags) noexcept
{
    const int cardinal = cardinalFlags(flags);
    return (has(cardinal, kNorth) ? 1 : 0)
        + (has(cardinal, kEast) ? 1 : 0)
        + (has(cardinal, kSouth) ? 1 : 0)
        + (has(cardinal, kWest) ? 1 : 0);
}

template <std::size_t N>
int choosePiece(Point point, const std::array<int, N>& pieces, int salt) noexcept
{
    return pieces[static_cast<std::size_t>(shoreNoise(point, salt) % pieces.size())];
}

int straightRunLength(const MapGrid& map, Point point, int direction) noexcept
{
    int run = 1;
    Point land;
    Point water;
    Point step;
    switch (direction) {
    case kSouth:
        land = {point.x + 1, point.y};
        water = {point.x + 1, point.y + 1};
        step = {1, 0};
        break;
    case kEast:
        land = {point.x, point.y + 1};
        water = {point.x + 1, point.y + 1};
        step = {0, 1};
        break;
    case kWest:
        land = {point.x, point.y + 1};
        water = {point.x - 1, point.y + 1};
        step = {0, 1};
        break;
    case kNorth:
        land = {point.x + 1, point.y};
        water = {point.x + 1, point.y - 1};
        step = {1, 0};
        break;
    default:
        return run;
    }

    while (map.inBounds(land) && map.inBounds(water) && !isWaterCell(map, land) && isWaterCell(map, water)) {
        ++run;
        land = {land.x + step.x, land.y + step.y};
        water = {water.x + step.x, water.y + step.y};
    }
    return run;
}

int fallbackStraightPiece(int direction) noexcept
{
    switch (direction) {
    case kSouth:
        return 4;
    case kEast:
        return 12;
    case kWest:
        return 28;
    case kNorth:
        return 20;
    default:
        return 17;
    }
}

int straightPiece(const MapGrid& map, Point point, int direction, int flags) noexcept
{
    const int run = straightRunLength(map, point, direction);
    const std::uint32_t roll = shoreNoise(point, 1000 + direction);
    switch (direction) {
    case kSouth:
        if ((run & 1) != 0 || (flags & kSouthEast) == 0 || (flags & (kNorthEast | kEast)) != 0) {
            return 4;
        }
        return 1 + static_cast<int>(roll % 3u);
    case kEast:
        if (((run & 1) != 0 && (flags & kNorth) == 0) || (flags & kSouthEast) == 0 || (flags & (kSouthWest | kSouth)) != 0) {
            return 12;
        }
        return 9 + static_cast<int>(roll % 3u);
    case kWest:
        if (((run & 1) != 0 && (flags & kNorth) == 0) || (flags & kSouthWest) == 0 || (flags & (kSouth | kSouthEast)) != 0) {
            return 28;
        }
        return 25 + static_cast<int>(roll % 3u);
    case kNorth:
        if ((run & 1) != 0 || (flags & kNorthEast) == 0 || (flags & (kEast | kSouthEast)) != 0) {
            return 20;
        }
        return 17 + static_cast<int>(roll % 3u);
    default:
        return 17;
    }
}

int cornerPiece(const MapGrid& map, Point point, int cardinal) noexcept
{
    switch (cardinal) {
    case kNorth | kEast:
        return 22;
    case kEast | kSouth:
        return 6;
    case kSouth | kWest:
        return 29;
    case kWest | kNorth:
        return 30;
    default:
        return straightPiece(map, point, cardinal & -cardinal, cardinal);
    }
}

int innerCornerPiece(Point point, int flags) noexcept
{
    if (has(flags, kNorthEast)) {
        return choosePiece(point, std::array<int, 2>{{35, 36}}, 401);
    }
    if (has(flags, kSouthEast)) {
        return choosePiece(point, std::array<int, 2>{{33, 34}}, 403);
    }
    if (has(flags, kSouthWest)) {
        return choosePiece(point, std::array<int, 2>{{39, 40}}, 407);
    }
    return choosePiece(point, std::array<int, 2>{{37, 38}}, 409);
}

int pieceForNativeFlags(const MapGrid& map, Point point, int flags) noexcept
{
    const int cardinal = cardinalFlags(flags);
    const int cardinalCount = countCardinalWater(flags);

    if ((flags & (kNorth | kWest)) == (kNorth | kWest)) {
        if ((flags & (kNorthEast | kSouthWest)) == (kNorthEast | kSouthWest)) {
            return choosePiece(point, std::array<int, 2>{{23, 24}}, 223);
        }
        return has(flags, kNorthEast) ? 21 : 30;
    }

    if ((flags & (kNorth | kEast)) == (kNorth | kEast)) {
        if ((flags & (kSouthEast | kNorthWest)) == (kSouthEast | kNorthWest)) {
            return choosePiece(point, std::array<int, 2>{{15, 16}}, 215);
        }
        return has(flags, kSouthEast) ? 14 : 22;
    }

    if ((flags & (kSouth | kEast)) == (kSouth | kEast)) {
        if ((flags & (kNorthEast | kSouthWest)) == (kNorthEast | kSouthWest)) {
            return choosePiece(point, std::array<int, 2>{{7, 8}}, 207);
        }
        return has(flags, kNorthEast) ? 13 : 6;
    }

    if ((flags & (kSouth | kWest)) == (kSouth | kWest)) {
        if ((flags & (kSouthEast | kNorthWest)) == (kSouthEast | kNorthWest)) {
            return choosePiece(point, std::array<int, 2>{{31, 32}}, 231);
        }
        return has(flags, kSouthEast) ? 5 : 29;
    }

    if (cardinalCount == 1) {
        return straightPiece(map, point, cardinal, flags);
    }

    if (cardinalCount == 2) {
        if (cardinal == (kNorth | kSouth)) {
            return choosePiece(point, std::array<int, 2>{{17, 25}}, 503);
        }
        if (cardinal == (kEast | kWest)) {
            return choosePiece(point, std::array<int, 2>{{1, 9}}, 509);
        }
        return cornerPiece(map, point, cardinal);
    }

    if (cardinalCount == 3) {
        return cornerPiece(map, point, cardinal & (kNorth | kEast | kSouth | kWest));
    }

    if (cardinalCount == 4) {
        return 30;
    }

    if ((flags & (kNorthEast | kSouthEast | kSouthWest | kNorthWest)) != 0) {
        return innerCornerPiece(point, flags);
    }

    return 17;
}

void addCandidatePiece(PieceList& pieces, int piece) noexcept
{
    if (piece < kFirstShorePiece || piece > 40) {
        return;
    }
    for (int i = 0; i < pieces.count; ++i) {
        if (pieces.values[static_cast<std::size_t>(i)] == piece) {
            return;
        }
    }
    if (pieces.count < static_cast<int>(pieces.values.size())) {
        pieces.values[static_cast<std::size_t>(pieces.count++)] = piece;
    }
}

void addCandidateRange(PieceList& pieces, int first, int last) noexcept
{
    for (int piece = first; piece <= last; ++piece) {
        addCandidatePiece(pieces, piece);
    }
}

PieceList candidatePiecesForFlags(const MapGrid& map, Point point, int flags) noexcept
{
    PieceList pieces;
    const int nativePiece = pieceForNativeFlags(map, point, flags);
    addCandidatePiece(pieces, nativePiece);

    const int cardinal = cardinalFlags(flags);
    const int cardinalCount = countCardinalWater(flags);
    if (cardinalCount == 1) {
        switch (cardinal) {
        case kSouth:
            addCandidateRange(pieces, 1, 4);
            break;
        case kEast:
            addCandidateRange(pieces, 9, 12);
            break;
        case kNorth:
            addCandidateRange(pieces, 17, 20);
            break;
        case kWest:
            addCandidateRange(pieces, 25, 28);
            break;
        default:
            break;
        }
        return pieces;
    }

    if ((flags & (kSouth | kEast)) == (kSouth | kEast)) {
        addCandidatePiece(pieces, 6);
        if ((flags & (kNorthEast | kSouthWest)) == (kNorthEast | kSouthWest)) {
            addCandidateRange(pieces, 7, 8);
        } else if (has(flags, kNorthEast)) {
            addCandidatePiece(pieces, 13);
        }
    }
    if ((flags & (kNorth | kEast)) == (kNorth | kEast)) {
        addCandidatePiece(pieces, 22);
        if ((flags & (kSouthEast | kNorthWest)) == (kSouthEast | kNorthWest)) {
            addCandidateRange(pieces, 15, 16);
        } else if (has(flags, kSouthEast)) {
            addCandidatePiece(pieces, 14);
        }
    }
    if ((flags & (kNorth | kWest)) == (kNorth | kWest)) {
        addCandidatePiece(pieces, 30);
        if ((flags & (kNorthEast | kSouthWest)) == (kNorthEast | kSouthWest)) {
            addCandidateRange(pieces, 23, 24);
        } else if (has(flags, kNorthEast)) {
            addCandidatePiece(pieces, 21);
        }
    }
    if ((flags & (kSouth | kWest)) == (kSouth | kWest)) {
        addCandidatePiece(pieces, 29);
        if ((flags & (kSouthEast | kNorthWest)) == (kSouthEast | kNorthWest)) {
            addCandidateRange(pieces, 31, 32);
        } else if (has(flags, kSouthEast)) {
            addCandidatePiece(pieces, 5);
        }
    }

    if (cardinalCount == 0) {
        if (has(flags, kSouthEast)) {
            addCandidateRange(pieces, 33, 34);
        }
        if (has(flags, kNorthEast)) {
            addCandidateRange(pieces, 35, 36);
        }
        if (has(flags, kNorthWest)) {
            addCandidateRange(pieces, 37, 38);
        }
        if (has(flags, kSouthWest)) {
            addCandidateRange(pieces, 39, 40);
        }
    }

    if (pieces.count == 0) {
        addCandidatePiece(pieces, nativePiece);
    }
    return pieces;
}

int oppositeFlags(int flags) noexcept
{
    int result = 0;
    if (has(flags, kNorth)) {
        result |= kSouth;
    }
    if (has(flags, kSouth)) {
        result |= kNorth;
    }
    if (has(flags, kEast)) {
        result |= kWest;
    }
    if (has(flags, kWest)) {
        result |= kEast;
    }
    if (has(flags, kNorthEast)) {
        result |= kSouthWest;
    }
    if (has(flags, kSouthWest)) {
        result |= kNorthEast;
    }
    if (has(flags, kSouthEast)) {
        result |= kNorthWest;
    }
    if (has(flags, kNorthWest)) {
        result |= kSouthEast;
    }
    return result;
}

int templateInteriorFlags(int x, int y, const ShoreTemplateInfo& info) noexcept
{
    int flags = 0;
    if (isValidTemplateSlot(info, x, y - 1)) {
        flags |= kNorth;
    }
    if (isValidTemplateSlot(info, x, y + 1)) {
        flags |= kSouth;
    }
    if (isValidTemplateSlot(info, x - 1, y)) {
        flags |= kWest;
    }
    if (isValidTemplateSlot(info, x + 1, y)) {
        flags |= kEast;
    }
    if (isValidTemplateSlot(info, x + 1, y - 1)) {
        flags |= kNorthEast;
    }
    if (isValidTemplateSlot(info, x + 1, y + 1)) {
        flags |= kSouthEast;
    }
    if (isValidTemplateSlot(info, x - 1, y + 1)) {
        flags |= kSouthWest;
    }
    if (isValidTemplateSlot(info, x - 1, y - 1)) {
        flags |= kNorthWest;
    }
    return flags;
}

int templateWaterNeighborFlags(int x, int y, const ShoreTemplateInfo& info) noexcept
{
    int flags = 0;
    if (templateSlot(info, x - 1, y - 1) == 'W') {
        flags |= kNorthWest;
    }
    if (templateSlot(info, x, y - 1) == 'W') {
        flags |= kNorth;
    }
    if (templateSlot(info, x + 1, y - 1) == 'W') {
        flags |= kNorthEast;
    }
    if (templateSlot(info, x - 1, y) == 'W') {
        flags |= kWest;
    }
    if (templateSlot(info, x + 1, y) == 'W') {
        flags |= kEast;
    }
    if (templateSlot(info, x - 1, y + 1) == 'W') {
        flags |= kSouthWest;
    }
    if (templateSlot(info, x, y + 1) == 'W') {
        flags |= kSouth;
    }
    if (templateSlot(info, x + 1, y + 1) == 'W') {
        flags |= kSouthEast;
    }
    return flags;
}

int templateExteriorFlags(int x, int y, const ShoreTemplateInfo& info) noexcept
{
    return (kNorth | kEast | kSouth | kWest | kNorthEast | kSouthEast | kSouthWest | kNorthWest)
        & ~templateInteriorFlags(x, y, info);
}

int bitCount8(int flags) noexcept
{
    int count = 0;
    while (flags != 0) {
        flags &= flags - 1;
        ++count;
    }
    return count;
}

int candidateScore(const ShoreSubTileCandidate& candidate, int flags) noexcept
{
    const int requiredMisses = bitCount8(candidate.requiredFlags & ~flags);
    const int forbiddenHits = bitCount8(candidate.forbiddenFlags & flags);
    const int extraCardinal = bitCount8(cardinalFlags(flags & ~candidate.requiredFlags));
    const int diagonalMatches = bitCount8((flags & candidate.requiredFlags) & (kNorthEast | kSouthEast | kSouthWest | kNorthWest));
    return candidate.priority + bitCount8(candidate.requiredFlags & flags) * 24 + diagonalMatches * 6 - requiredMisses * 80 - forbiddenHits * 36 - extraCardinal * 10;
}

std::vector<ShoreSubTileCandidate> buildSubTileCandidates() noexcept
{
    std::vector<ShoreSubTileCandidate> candidates;
    candidates.reserve(128);

    const auto addTemplateCandidates = [&](int firstPiece, int lastPiece, int exteriorFlags, int priority) {
        for (int piece = firstPiece; piece <= lastPiece; ++piece) {
            const ShoreTemplateInfo& info = kShoreTemplates[static_cast<std::size_t>(piece - 1)];
            for (int y = 0; y < info.height; ++y) {
                for (int x = 0; x < info.width; ++x) {
                    if (!isValidTemplateSlot(info, x, y) || templateSlot(info, x, y) == 'W') {
                        continue;
                    }
                    const int exterior = templateExteriorFlags(x, y, info);
                    const int required = templateWaterNeighborFlags(x, y, info) | (exteriorFlags & exterior);
                    const int forbidden = oppositeFlags(exteriorFlags) & exterior;
                    if (required == 0) {
                        continue;
                    }
                    candidates.push_back({{piece - 1, y * info.width + x}, required, forbidden, priority});
                }
            }
        }
    };

    addTemplateCandidates(1, 4, kSouth, 40);
    addTemplateCandidates(9, 12, kEast, 40);
    addTemplateCandidates(17, 20, kNorth, 40);
    addTemplateCandidates(25, 28, kWest, 40);
    addTemplateCandidates(5, 5, kSouth | kWest, 55);
    addTemplateCandidates(6, 6, kSouth | kEast, 55);
    addTemplateCandidates(7, 8, kSouth | kEast | kNorthEast | kSouthWest, 58);
    addTemplateCandidates(13, 13, kSouth | kEast | kNorthEast, 56);
    addTemplateCandidates(14, 14, kNorth | kEast | kSouthEast, 56);
    addTemplateCandidates(15, 16, kNorth | kEast | kSouthEast | kNorthWest, 58);
    addTemplateCandidates(21, 21, kNorth | kWest | kNorthEast, 56);
    addTemplateCandidates(22, 22, kNorth | kEast, 55);
    addTemplateCandidates(23, 24, kNorth | kWest | kNorthEast | kSouthWest, 58);
    addTemplateCandidates(29, 29, kSouth | kWest, 55);
    addTemplateCandidates(30, 30, kNorth | kWest, 55);
    addTemplateCandidates(31, 32, kSouth | kWest | kSouthEast | kNorthWest, 58);
    addTemplateCandidates(33, 36, kSouthEast, 45);
    addTemplateCandidates(37, 38, kNorthWest, 45);
    addTemplateCandidates(39, 40, kSouthWest, 45);
    return candidates;
}

const std::vector<ShoreSubTileCandidate>& subTileCandidates() noexcept
{
    static const std::vector<ShoreSubTileCandidate> candidates = buildSubTileCandidates();
    return candidates;
}

Point defaultAnchorForPiece(Point point, int piece) noexcept
{
    const ShoreTemplateInfo& info = kShoreTemplates[static_cast<std::size_t>(std::clamp(piece, 1, 42) - 1)];
    return {point.x - info.width / 2, point.y - info.height / 2};
}

Point anchorFromLocalSlot(Point point, int localX, int localY) noexcept
{
    return {point.x - localX, point.y - localY};
}

ShorePlacement placementForNativeFlags(const MapGrid& map, Point point, int flags) noexcept
{
    const int cardinal = cardinalFlags(flags);
    const int cardinalCount = countCardinalWater(flags);
    const int piece = pieceForNativeFlags(map, point, flags);

    Point anchor = defaultAnchorForPiece(point, piece);
    const ShoreTemplateInfo& info = kShoreTemplates[static_cast<std::size_t>(std::clamp(piece, 1, 42) - 1)];
    if (cardinalCount == 1) {
        const int localX = info.width <= 1 ? 0 : static_cast<int>(shoreNoise(point, piece * 17) % static_cast<std::uint32_t>(info.width));
        const int localY = info.height <= 1 ? 0 : static_cast<int>(shoreNoise(point, piece * 19) % static_cast<std::uint32_t>(info.height));
        switch (cardinal) {
        case kEast:
            anchor = anchorFromLocalSlot(point, 0, localY);
            break;
        case kSouth:
            anchor = anchorFromLocalSlot(point, localX, 0);
            break;
        case kWest:
            anchor = anchorFromLocalSlot(point, info.width - 1, localY);
            break;
        case kNorth:
            anchor = anchorFromLocalSlot(point, localX, info.height - 1);
            break;
        default:
            break;
        }
    } else if (cardinalCount == 2) {
        if (cardinal == (kNorth | kEast)) {
            anchor = anchorFromLocalSlot(point, 0, info.height - 1);
        } else if (cardinal == (kEast | kSouth)) {
            anchor = anchorFromLocalSlot(point, 0, 0);
        } else if (cardinal == (kSouth | kWest)) {
            anchor = anchorFromLocalSlot(point, info.width - 1, 0);
        } else if (cardinal == (kWest | kNorth)) {
            anchor = anchorFromLocalSlot(point, info.width - 1, info.height - 1);
        }
    }

    return {piece, anchor};
}

bool canPlaceTemplate(const MapGrid& map, const std::vector<bool>& placed, ShorePlacement placement) noexcept
{
    if (placement.piece < kFirstShorePiece || placement.piece > kLastShorePiece) {
        return false;
    }
    const ShoreTemplateInfo& info = kShoreTemplates[static_cast<std::size_t>(placement.piece - 1)];
    int existingShoreCells = 0;
    int landSlotsOnWater = 0;
    int landSlots = 0;
    int writableWaterSlots = 0;
    int validSlots = 0;
    const bool largePiece = isLargeShorePiece(placement.piece);
    for (int y = 0; y < info.height; ++y) {
        for (int x = 0; x < info.width; ++x) {
            const char slot = templateSlot(info, x, y);
            if (slot == '.') {
                continue;
            }
            ++validSlots;
            if (slot != 'W') {
                ++landSlots;
            }
            const Point p{placement.anchor.x + x, placement.anchor.y + y};
            if (!map.inBounds(p)) {
                return false;
            }
            const auto index = static_cast<std::size_t>(p.y * map.width() + p.x);
            if (!canUseCellForTemplateSlot(map, p, slot, largePiece)) {
                return false;
            }
            const bool writableSlot = canWriteTemplateSlot(map, p, slot);
            if (writableSlot && placed[index] && !isLargeShorePiece(placement.piece)) {
                return false;
            }
            if (slot == 'W' && writableSlot) {
                ++writableWaterSlots;
            }
            if (isShoreCell(map, p)) {
                ++existingShoreCells;
            } else if (largePiece && slot == 'L' && map.at(p).terrain == Terrain::Water) {
                ++landSlotsOnWater;
            }
        }
    }
    if (largePiece) {
        return landSlots > 0
            && writableWaterSlots >= 8
            && existingShoreCells > 0
            && existingShoreCells + landSlotsOnWater == landSlots
            && landSlotsOnWater <= 1;
    }
    return validSlots > 0 && existingShoreCells > 0 && writableWaterSlots > 0;
}

bool templateFitsCurrentWaterline(const MapGrid& map, const ShoreTemplateInfo& info, Point anchor) noexcept
{
    int shoreSlots = 0;
    int waterSlots = 0;
    for (int y = 0; y < info.height; ++y) {
        for (int x = 0; x < info.width; ++x) {
            const char slot = templateSlot(info, x, y);
            if (slot == '.') {
                continue;
            }

            const Point p{anchor.x + x, anchor.y + y};
            if (!map.inBounds(p)) {
                return false;
            }

            const Cell& cell = map.at(p);
            if (cell.overlay != Overlay::None || cell.terrainObject != TerrainObject::None || cell.terrain == Terrain::Cliff) {
                return false;
            }
            if (slot == 'W') {
                if (cell.terrain != Terrain::Water) {
                    return false;
                }
                ++waterSlots;
                continue;
            }
            if (cell.terrain != Terrain::Shore && !isSolidLand(cell)) {
                return false;
            }
            if (cell.terrain == Terrain::Shore) {
                ++shoreSlots;
            }
        }
    }

    return shoreSlots > 0 && waterSlots > 0;
}

bool canPlaceLargeVisualTemplate(const MapGrid& map, ShorePlacement placement) noexcept
{
    if (!isLargeShorePiece(placement.piece)) {
        return false;
    }

    const ShoreTemplateInfo& info = kShoreTemplates[static_cast<std::size_t>(placement.piece - 1)];
    int shoreSlots = 0;
    int landSlotsOnWater = 0;
    int waterSlots = 0;
    for (int y = 0; y < info.height; ++y) {
        for (int x = 0; x < info.width; ++x) {
            const char slot = templateSlot(info, x, y);
            if (slot == '.') {
                continue;
            }
            const Point p{placement.anchor.x + x, placement.anchor.y + y};
            if (!map.inBounds(p)) {
                return false;
            }
            const Cell& cell = map.at(p);
            if (cell.overlay != Overlay::None || cell.terrainObject != TerrainObject::None || cell.terrain == Terrain::Cliff) {
                return false;
            }
            if (slot == 'W') {
                if (cell.terrain != Terrain::Water) {
                    return false;
                }
                ++waterSlots;
                continue;
            }

            if (cell.terrain == Terrain::Shore) {
                ++shoreSlots;
            } else if (cell.terrain == Terrain::Water) {
                ++landSlotsOnWater;
            } else {
                return false;
            }
        }
    }

    if (placement.piece == 41) {
        return waterSlots >= 16 && shoreSlots >= 3 && landSlotsOnWater <= 1;
    }
    return waterSlots >= 26 && shoreSlots >= 1 && landSlotsOnWater <= 1;
}

void applyLargeVisualTemplate(MapGrid& map, ShorePlacement placement) noexcept
{
    const ShoreTemplateInfo& info = kShoreTemplates[static_cast<std::size_t>(placement.piece - 1)];
    for (int y = 0; y < info.height; ++y) {
        for (int x = 0; x < info.width; ++x) {
            const char slot = templateSlot(info, x, y);
            if (slot == '.') {
                continue;
            }
            const Point p{placement.anchor.x + x, placement.anchor.y + y};
            if (!map.inBounds(p)) {
                continue;
            }
            Cell& cell = map.at(p);
            if (cell.terrain == Terrain::Water || cell.terrain == Terrain::Shore) {
                cell.slope = encodeShorePiece({placement.piece - 1, y * info.width + x});
            }
        }
    }
}

void applyLargeVisualShoreTemplates(MapGrid& map) noexcept
{
    if (!enableLargeVisualShorePieces()) {
        return;
    }

    std::vector<ShorePlacement> candidates;
    candidates.reserve(map.cells().size());

    for (const Cell& cell : map.cells()) {
        if (cell.terrain != Terrain::Shore || cell.overlay != Overlay::None || cell.terrainObject != TerrainObject::None) {
            continue;
        }
        const Point point{cell.x, cell.y};
        for (int piece : {41, 42}) {
            const ShoreTemplateInfo& info = kShoreTemplates[static_cast<std::size_t>(piece - 1)];
            for (int y = 0; y < info.height; ++y) {
                for (int x = 0; x < info.width; ++x) {
                    if (templateSlot(info, x, y) != 'L') {
                        continue;
                    }
                    ShorePlacement placement{piece, {point.x - x, point.y - y}, 0};
                    if (canPlaceLargeVisualTemplate(map, placement)) {
                        const int validSlots = validTemplateSlotCount(info);
                        placement.score = validSlots * 10 + piece;
                        candidates.push_back(placement);
                    }
                }
            }
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](ShorePlacement lhs, ShorePlacement rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        if (lhs.anchor.y != rhs.anchor.y) {
            return lhs.anchor.y < rhs.anchor.y;
        }
        if (lhs.anchor.x != rhs.anchor.x) {
            return lhs.anchor.x < rhs.anchor.x;
        }
        return lhs.piece < rhs.piece;
    });

    for (ShorePlacement placement : candidates) {
        if (canPlaceLargeVisualTemplate(map, placement)) {
            applyLargeVisualTemplate(map, placement);
        }
    }
}

int templatePlacementScore(const MapGrid& map, ShorePlacement placement) noexcept
{
    if (placement.piece < kFirstShorePiece || placement.piece > kLastShorePiece) {
        return -100000;
    }

    const ShoreTemplateInfo& info = kShoreTemplates[static_cast<std::size_t>(placement.piece - 1)];
    int score = validTemplateSlotCount(info) * 6;
    int existingShoreCells = 0;
    int landSlotsOnWater = 0;
    int landSlots = 0;
    int writableWaterSlots = 0;
    int deepWaterSlots = 0;
    int waterFlagMatches = 0;
    int waterFlagMisses = 0;
    int extraWaterFlags = 0;

    const bool largePiece = isLargeShorePiece(placement.piece);
    for (int y = 0; y < info.height; ++y) {
        for (int x = 0; x < info.width; ++x) {
            const char slot = templateSlot(info, x, y);
            if (slot == '.') {
                continue;
            }

            const Point p{placement.anchor.x + x, placement.anchor.y + y};
            if (!canUseCellForTemplateSlot(map, p, slot, largePiece)) {
                return -100000;
            }

            const Cell& cell = map.at(p);
            if (slot == 'W') {
                if (canWriteTemplateSlot(map, p, slot)) {
                    ++writableWaterSlots;
                    score += 12;
                } else {
                    ++deepWaterSlots;
                    score -= 34;
                }
                continue;
            }

            ++landSlots;
            const int actualWater = waterFlagsForCell(map, p);
            const int expectedWater = templateWaterNeighborFlags(x, y, info);
            waterFlagMatches += bitCount8(actualWater & expectedWater);
            waterFlagMisses += bitCount8(expectedWater & ~actualWater);
            extraWaterFlags += bitCount8(cardinalFlags(actualWater & ~expectedWater));

            if (cell.terrain == Terrain::Shore) {
                ++existingShoreCells;
                score += 32;
            } else if (largePiece && cell.terrain == Terrain::Water) {
                ++landSlotsOnWater;
                score -= 80;
            } else if (isSolidLand(cell)) {
                score += 8;
            }
        }
    }

    if (existingShoreCells == 0 || writableWaterSlots == 0) {
        return -100000;
    }
    if (largePiece) {
        if (landSlots == 0
            || writableWaterSlots < 8
            || existingShoreCells <= 0
            || existingShoreCells + landSlotsOnWater != landSlots
            || landSlotsOnWater > 1) {
            return -100000;
        }
        score -= 70;
        score += existingShoreCells * 18;
    }

    score += existingShoreCells * 18;
    score += waterFlagMatches * 16;
    score -= waterFlagMisses * 46;
    score -= extraWaterFlags * 14;
    score -= deepWaterSlots * 18;
    return score;
}

void applyTemplate(MapGrid& map, std::vector<bool>& placed, ShorePlacement placement) noexcept
{
    const ShoreTemplateInfo& info = kShoreTemplates[static_cast<std::size_t>(placement.piece - 1)];
    for (int y = 0; y < info.height; ++y) {
        for (int x = 0; x < info.width; ++x) {
            const char slot = templateSlot(info, x, y);
            if (slot == '.') {
                continue;
            }
            const Point p{placement.anchor.x + x, placement.anchor.y + y};
            if (!canUseCellForTemplateSlot(map, p, slot, isLargeShorePiece(placement.piece))) {
                continue;
            }
            if (!canWriteTemplateSlot(map, p, slot)) {
                continue;
            }
            if (slot == 'W' && !canWriteWaterSideShoreSlot(map, p)) {
                continue;
            }
            if (slot != 'W' && !isShoreCell(map, p)) {
                continue;
            }
            const int subTile = y * info.width + x;
            Cell& cell = map.at(p);
            cell.slope = encodeShorePiece({placement.piece - 1, subTile});
            placed[static_cast<std::size_t>(p.y * map.width() + p.x)] = true;
        }
    }
}

bool isBetterWaterSideCandidate(Point point, ShorePiece candidate, int score, ShorePiece current, int currentScore) noexcept
{
    if (score != currentScore) {
        return score > currentScore;
    }
    const std::uint32_t candidateTie = shoreNoise(point, candidate.tile * 257 + candidate.subTile);
    const std::uint32_t currentTie = shoreNoise(point, current.tile * 257 + current.subTile);
    return candidateTie < currentTie;
}

void applySimpleWaterSideShorePieces(
    MapGrid& map,
    const std::vector<bool>* primaryShoreSlots = nullptr,
    bool preservePrimarySlots = false,
    const std::vector<bool>* protectedCells = nullptr) noexcept
{
    std::vector<int> bestScore(map.cells().size(), std::numeric_limits<int>::min());
    std::vector<ShorePiece> bestPiece(map.cells().size());

    for (const Cell& cell : map.cells()) {
        if (cell.terrain != Terrain::Shore || !isEncodedShorePiece(cell.slope)) {
            continue;
        }

        const Point shorePoint{cell.x, cell.y};
        const ShorePiece shorePiece = decodeShorePiece(cell.slope);
        if (shorePiece.tile < 0 || shorePiece.tile >= 40) {
            continue;
        }

        const ShoreTemplateInfo& info = kShoreTemplates[static_cast<std::size_t>(shorePiece.tile)];
        const int localX = shorePiece.subTile % info.width;
        const int localY = shorePiece.subTile / info.width;
        if (templateSlot(info, localX, localY) != 'L') {
            continue;
        }

        const Point anchor{shorePoint.x - localX, shorePoint.y - localY};
        if (!templateFitsCurrentWaterline(map, info, anchor)) {
            continue;
        }

        const int shoreFlags = waterFlagsForCell(map, shorePoint);
        if (primaryShoreSlots != nullptr) {
            const auto shoreIndex = static_cast<std::size_t>(shorePoint.y * map.width() + shorePoint.x);
            if (!(*primaryShoreSlots)[shoreIndex] && countCardinalWater(shoreFlags) < 2) {
                continue;
            }
        }
        const int expectedFlags = templateWaterNeighborFlags(localX, localY, info);
        for (int y = 0; y < info.height; ++y) {
            for (int x = 0; x < info.width; ++x) {
                if (templateSlot(info, x, y) != 'W') {
                    continue;
                }

                const Point waterPoint{anchor.x + x, anchor.y + y};
                if (!map.inBounds(waterPoint) || !canWriteWaterSideShoreSlot(map, waterPoint)) {
                    continue;
                }

                const Cell& waterCell = map.at(waterPoint);
                if (waterCell.terrain != Terrain::Water || waterCell.overlay != Overlay::None || waterCell.terrainObject != TerrainObject::None) {
                    continue;
                }

                const int waterFlags = waterFlagsForCell(map, waterPoint);
                const int dx = std::abs(x - localX);
                const int dy = std::abs(y - localY);
                if (std::max(dx, dy) > 1) {
                    continue;
                }
                if (primaryShoreSlots != nullptr && !(*primaryShoreSlots)[static_cast<std::size_t>(shorePoint.y * map.width() + shorePoint.x)] && dx + dy != 1) {
                    continue;
                }

                int score = 100;
                score += bitCount8(waterFlags & expectedFlags) * 8;
                score += (dx + dy == 1) ? 24 : 12;
                score += countCardinalWater(waterFlags) * 3;
                score -= bitCount8(cardinalFlags(waterFlags & ~expectedFlags)) * 5;

                const auto index = static_cast<std::size_t>(waterPoint.y * map.width() + waterPoint.x);
                if ((preservePrimarySlots
                        && primaryShoreSlots != nullptr
                        && (*primaryShoreSlots)[index])
                    || (protectedCells != nullptr && (*protectedCells)[index])) {
                    continue;
                }
                const ShorePiece candidate{shorePiece.tile, y * info.width + x};
                if (isBetterWaterSideCandidate(waterPoint, candidate, score, bestPiece[index], bestScore[index])) {
                    bestScore[index] = score;
                    bestPiece[index] = candidate;
                }
            }
        }
    }

    for (Cell& cell : map.cells()) {
        const auto index = static_cast<std::size_t>(cell.y * map.width() + cell.x);
        if (cell.terrain == Terrain::Water && bestScore[index] != std::numeric_limits<int>::min()) {
            cell.slope = encodeShorePiece(bestPiece[index]);
        }
    }
}

} // namespace

int encodeShorePiece(ShorePiece piece) noexcept
{
    const int tile = std::clamp(piece.tile, 0, 41);
    const int subTile = std::clamp(piece.subTile, 0, 255);
    return (tile + 1) * kShorePieceEncodingBase + subTile;
}

ShorePiece decodeShorePiece(int encoded) noexcept
{
    if (!isEncodedShorePiece(encoded)) {
        return {std::clamp(encoded, 0, 41), 0};
    }
    return {
        std::clamp(encoded / kShorePieceEncodingBase - 1, 0, 41),
        std::clamp(encoded % kShorePieceEncodingBase, 0, 255),
    };
}

bool isEncodedShorePiece(int encoded) noexcept
{
    return encoded >= kShorePieceEncodingBase && encoded < kShorePieceEncodingLimit;
}

int shoreTemplateWidth(int piece) noexcept
{
    const ShoreTemplateInfo& info = kShoreTemplates[static_cast<std::size_t>(std::clamp(piece, 1, 42) - 1)];
    return info.width;
}

int shoreTemplateHeight(int piece) noexcept
{
    const ShoreTemplateInfo& info = kShoreTemplates[static_cast<std::size_t>(std::clamp(piece, 1, 42) - 1)];
    return info.height;
}

char shoreTemplateSlot(int piece, int x, int y) noexcept
{
    const ShoreTemplateInfo& info = kShoreTemplates[static_cast<std::size_t>(std::clamp(piece, 1, 42) - 1)];
    return templateSlot(info, x, y);
}

ShorePiece shorePieceForCell(const MapGrid& map, Point point) noexcept
{
    const int flags = waterFlagsForCell(map, point);
    const int nativePiece = pieceForNativeFlags(map, point, flags);
    ShorePiece best{std::clamp(nativePiece - 1, 0, 40), 0};
    int bestScore = -100000;
    for (const ShoreSubTileCandidate& candidate : subTileCandidates()) {
        const int score = candidateScore(candidate, flags);
        if (score > bestScore || (score == bestScore && shoreNoise(point, candidate.piece.tile * 257 + candidate.piece.subTile) < shoreNoise(point, best.tile * 257 + best.subTile))) {
            best = candidate.piece;
            bestScore = score;
        }
    }
    return best;
}

void rebuildShorePieceTiles(MapGrid& map) noexcept
{
    if (!enableMultiCellShoreFootprints()) {
        rebuildSimpleShorePieceTiles(map);
        return;
    }

    for (Cell& cell : map.cells()) {
        if (isEncodedShorePiece(cell.slope)) {
            cell.slope = -1;
        }
    }

    std::vector<bool> placed(map.cells().size(), false);

    for (const Cell& cell : map.cells()) {
        const Point point{cell.x, cell.y};
        if (!canOverwriteShoreCell(map, point)) {
            continue;
        }
        const int flags = waterFlagsForCell(map, point);
        const int cardinalCount = countCardinalWater(flags);
        if (cardinalCount == 0) {
            continue;
        }

        const ShorePlacement placement = placementForNativeFlags(map, point, flags);
        if (canPlaceTemplate(map, placed, placement)) {
            applyTemplate(map, placed, placement);
        }
    }

    applyLargeVisualShoreTemplates(map);

    for (Cell& cell : map.cells()) {
        if (cell.terrain != Terrain::Shore) {
            continue;
        }
        const Point point{cell.x, cell.y};
        const auto index = static_cast<std::size_t>(cell.y * map.width() + cell.x);
        if (!placed[index]) {
            cell.slope = encodeShorePiece(shorePieceForCell(map, point));
        }
    }

    applySimpleWaterSideShorePieces(map, &placed);
}

void rebuildSimpleShorePieceTiles(MapGrid& map) noexcept
{
    for (Cell& cell : map.cells()) {
        if (isEncodedShorePiece(cell.slope)) {
            cell.slope = -1;
        }
    }

    for (Cell& cell : map.cells()) {
        if (cell.terrain != Terrain::Shore) {
            continue;
        }
        cell.slope = encodeShorePiece(shorePieceForCell(map, {cell.x, cell.y}));
    }

    applySimpleWaterSideShorePieces(map);
}

} // namespace rmg
