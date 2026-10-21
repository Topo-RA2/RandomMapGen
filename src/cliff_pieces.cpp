#include "rmg/cliff_pieces.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace rmg {
namespace {

struct CliffTemplateInfo {
    int width = 1;
    int height = 1;
    std::string_view terrain;
    std::string_view cellHeights;
};

struct CliffPlacement {
    int tile = 0;
    Point anchor{};
    int score = 0;
};

constexpr std::array<CliffTemplateInfo, kCliffPieceCount> kCliffTemplates{{
    {2, 3, "#.##.#", "4.04.0"}, {1, 2, "##", "40"}, {2, 3, "#.##.#", "4.04.0"}, {2, 3, "#.##.#", "4.04.0"},
    {2, 2, "####", "4400"}, {2, 2, "####", "4400"}, {2, 2, "####", "4400"}, {1, 2, "##", "40"},
    {2, 2, "###.", "400."}, {2, 2, "###.", "400."}, {2, 2, "###.", "400."}, {1, 1, "#", "0"},
    {1, 1, "#", "0"}, {1, 1, "#", "0"}, {2, 2, "####", "4040"}, {2, 2, "####", "4040"},
    {2, 2, "####", "4040"}, {2, 1, "##", "40"}, {3, 2, "##..##", "40..40"}, {3, 2, "##..##", "40..40"},
    {3, 2, "##..##", "40..40"}, {2, 1, "##", "40"}, {2, 1, "##", "44"}, {2, 1, "##", "44"},
    {2, 1, "##", "44"}, {1, 1, "#", "4"}, {2, 2, "###.", "444."}, {2, 2, ".###", ".444"},
    {1, 1, "#", "4"}, {1, 1, "#", "4"}, {2, 2, "###.", "444."}, {2, 2, ".###", ".444"},
    {1, 1, "#", "4"}, {1, 1, "#", "4"}, {1, 2, "##", "44"}, {1, 2, "##", "44"},
    {1, 2, "##", "44"}, {1, 1, "#", "4"}, {1, 1, "#", "4"}, {1, 1, "#", "4"},
}};

static_assert(kCliffTemplates.size() == kCliffPieceCount);

const CliffTemplateInfo& cliffTemplate(int tile) noexcept
{
    return kCliffTemplates[static_cast<std::size_t>(std::clamp(tile, 0, kCliffPieceCount - 1))];
}

std::uint32_t cliffNoise(Point point, int salt) noexcept
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

template <std::size_t N>
int chooseFrom(Point point, const std::array<int, N>& pieces, int salt) noexcept
{
    return pieces[static_cast<std::size_t>(cliffNoise(point, salt) % pieces.size())];
}

int straightPiece(Point point, int direction) noexcept
{
    constexpr int kNorth = 0x80;
    constexpr int kEast = 0x02;
    constexpr int kSouth = 0x08;
    constexpr int kWest = 0x20;

    switch (direction) {
    case kSouth:
        return chooseFrom(point, std::array<int, 4>{{0, 1, 2, 3}}, 103);
    case kEast:
        return chooseFrom(point, std::array<int, 4>{{8, 9, 10, 11}}, 107);
    case kWest:
        return chooseFrom(point, std::array<int, 4>{{24, 25, 26, 27}}, 109);
    case kNorth:
        return chooseFrom(point, std::array<int, 4>{{16, 17, 18, 19}}, 101);
    default:
        return 16;
    }
}

int cornerPiece(Point point, int cardinal) noexcept
{
    constexpr int kNorth = 0x80;
    constexpr int kEast = 0x02;
    constexpr int kSouth = 0x08;
    constexpr int kWest = 0x20;

    switch (cardinal) {
    case kNorth | kEast:
        return 21;
    case kEast | kSouth:
        return 5;
    case kSouth | kWest:
        return 28;
    case kWest | kNorth:
        return 29;
    default:
        return straightPiece(point, cardinal & -cardinal);
    }
}

int innerCornerPiece(Point point, int flags) noexcept
{
    constexpr int kNorthEast = 0x01;
    constexpr int kSouthEast = 0x04;
    constexpr int kSouthWest = 0x10;

    if (flags & kNorthEast) {
        return chooseFrom(point, std::array<int, 2>{{34, 35}}, 401);
    }
    if (flags & kSouthEast) {
        return chooseFrom(point, std::array<int, 2>{{32, 33}}, 403);
    }
    if (flags & kSouthWest) {
        return chooseFrom(point, std::array<int, 2>{{38, 39}}, 407);
    }
    return chooseFrom(point, std::array<int, 2>{{36, 37}}, 409);
}

int pieceForNativeFlags(Point point, int flags) noexcept
{
    constexpr int kNorthWest = 0x40;
    constexpr int kNorth = 0x80;
    constexpr int kNorthEast = 0x01;
    constexpr int kWest = 0x20;
    constexpr int kEast = 0x02;
    constexpr int kSouthWest = 0x10;
    constexpr int kSouth = 0x08;
    constexpr int kSouthEast = 0x04;

    const int cardinal = flags & (kNorth | kEast | kSouth | kWest);
    const int cardinalCount = (cardinal & kNorth ? 1 : 0)
        + (cardinal & kEast ? 1 : 0)
        + (cardinal & kSouth ? 1 : 0)
        + (cardinal & kWest ? 1 : 0);

    if ((flags & (kNorth | kWest)) == (kNorth | kWest)) {
        if ((flags & (kNorthEast | kSouthWest)) == (kNorthEast | kSouthWest)) {
            return chooseFrom(point, std::array<int, 2>{{22, 23}}, 223);
        }
        return (flags & kNorthEast) ? 20 : 29;
    }

    if ((flags & (kNorth | kEast)) == (kNorth | kEast)) {
        if ((flags & (kSouthEast | kNorthWest)) == (kSouthEast | kNorthWest)) {
            return chooseFrom(point, std::array<int, 2>{{14, 15}}, 215);
        }
        return (flags & kSouthEast) ? 13 : 21;
    }

    if ((flags & (kSouth | kEast)) == (kSouth | kEast)) {
        if ((flags & (kNorthEast | kSouthWest)) == (kNorthEast | kSouthWest)) {
            return chooseFrom(point, std::array<int, 2>{{6, 7}}, 207);
        }
        return (flags & kNorthEast) ? 12 : 5;
    }

    if ((flags & (kSouth | kWest)) == (kSouth | kWest)) {
        if ((flags & (kSouthEast | kNorthWest)) == (kSouthEast | kNorthWest)) {
            return chooseFrom(point, std::array<int, 2>{{30, 31}}, 231);
        }
        return (flags & kSouthEast) ? 4 : 28;
    }

    if (cardinalCount == 1) {
        return straightPiece(point, cardinal);
    }

    if (cardinalCount == 2) {
        if (cardinal == (kNorth | kSouth)) {
            return chooseFrom(point, std::array<int, 2>{{16, 24}}, 503);
        }
        if (cardinal == (kEast | kWest)) {
            return chooseFrom(point, std::array<int, 2>{{0, 8}}, 509);
        }
        if (cardinal == (kNorth | kEast)) {
            return 21;
        }
        if (cardinal == (kEast | kSouth)) {
            return 5;
        }
        if (cardinal == (kSouth | kWest)) {
            return 28;
        }
        if (cardinal == (kWest | kNorth)) {
            return 29;
        }
        return cornerPiece(point, cardinal);
    }

    if (cardinalCount == 3) {
        return cornerPiece(point, cardinal & (kNorth | kEast | kSouth | kWest));
    }

    if (cardinalCount == 4) {
        return 29;
    }

    if ((flags & (kNorthEast | kSouthEast | kSouthWest | kNorthWest)) != 0) {
        return innerCornerPiece(point, flags);
    }

    return 16;
}

int cliffSlopeFlags(const MapGrid& map, Point point) noexcept
{
    constexpr int kNorthWest = 0x40;
    constexpr int kNorth = 0x80;
    constexpr int kNorthEast = 0x01;
    constexpr int kWest = 0x20;
    constexpr int kEast = 0x02;
    constexpr int kSouthWest = 0x10;
    constexpr int kSouth = 0x08;
    constexpr int kSouthEast = 0x04;

    int flags = 0;
    if (map.inBounds({point.x - 1, point.y - 1}) && map.at({point.x - 1, point.y - 1}).terrain == Terrain::Water) {
        flags |= kNorthWest;
    }
    if (map.inBounds({point.x, point.y - 1}) && map.at({point.x, point.y - 1}).terrain == Terrain::Water) {
        flags |= kNorth;
    }
    if (map.inBounds({point.x + 1, point.y - 1}) && map.at({point.x + 1, point.y - 1}).terrain == Terrain::Water) {
        flags |= kNorthEast;
    }
    if (map.inBounds({point.x - 1, point.y}) && map.at({point.x - 1, point.y}).terrain == Terrain::Water) {
        flags |= kWest;
    }
    if (map.inBounds({point.x + 1, point.y}) && map.at({point.x + 1, point.y}).terrain == Terrain::Water) {
        flags |= kEast;
    }
    if (map.inBounds({point.x - 1, point.y + 1}) && map.at({point.x - 1, point.y + 1}).terrain == Terrain::Water) {
        flags |= kSouthWest;
    }
    if (map.inBounds({point.x, point.y + 1}) && map.at({point.x, point.y + 1}).terrain == Terrain::Water) {
        flags |= kSouth;
    }
    if (map.inBounds({point.x + 1, point.y + 1}) && map.at({point.x + 1, point.y + 1}).terrain == Terrain::Water) {
        flags |= kSouthEast;
    }
    return flags;
}

int templateSlotCount(const CliffTemplateInfo& info) noexcept
{
    return static_cast<int>(std::count(info.terrain.begin(), info.terrain.end(), '#'));
}

int templatePriority(int tile) noexcept
{
    const CliffTemplateInfo& info = cliffTemplate(tile);
    if (info.width == 3 && info.height == 2) {
        return 260;
    }
    if (info.width == 2 && info.height == 3) {
        return 250;
    }
    if (info.width == 2 && info.height == 2 && templateSlotCount(info) == 4) {
        return 230;
    }
    if (info.width == 2 && info.height == 2 && templateSlotCount(info) == 3) {
        return 210;
    }
    if (info.width == 2 && info.height == 1) {
        return 160;
    }
    if (info.width == 1 && info.height == 2) {
        return 150;
    }
    return 20;
}

int neighborMask(const MapGrid& map, Point p) noexcept
{
    int mask = 0;
    if (map.inBounds({p.x, p.y - 1}) && map.at({p.x, p.y - 1}).terrain == Terrain::Cliff) {
        mask |= 1;
    }
    if (map.inBounds({p.x + 1, p.y}) && map.at({p.x + 1, p.y}).terrain == Terrain::Cliff) {
        mask |= 2;
    }
    if (map.inBounds({p.x, p.y + 1}) && map.at({p.x, p.y + 1}).terrain == Terrain::Cliff) {
        mask |= 4;
    }
    if (map.inBounds({p.x - 1, p.y}) && map.at({p.x - 1, p.y}).terrain == Terrain::Cliff) {
        mask |= 8;
    }
    return mask;
}

int slotContactMask(int tile, int x, int y) noexcept
{
    int mask = 0;
    if (isValidCliffPieceSlot(tile, x, y - 1)) {
        mask |= 1;
    }
    if (isValidCliffPieceSlot(tile, x + 1, y)) {
        mask |= 2;
    }
    if (isValidCliffPieceSlot(tile, x, y + 1)) {
        mask |= 4;
    }
    if (isValidCliffPieceSlot(tile, x - 1, y)) {
        mask |= 8;
    }
    return mask;
}

int bitCount4(int mask) noexcept
{
    int count = 0;
    while (mask != 0) {
        mask &= mask - 1;
        ++count;
    }
    return count;
}

bool canPlaceTemplate(const MapGrid& map, const std::vector<bool>& placed, CliffPlacement placement) noexcept
{
    const CliffTemplateInfo& info = cliffTemplate(placement.tile);
    for (int y = 0; y < info.height; ++y) {
        for (int x = 0; x < info.width; ++x) {
            if (!isValidCliffPieceSlot(placement.tile, x, y)) {
                continue;
            }

            const Point p{placement.anchor.x + x, placement.anchor.y + y};
            if (!map.inBounds(p) || map.at(p).terrain != Terrain::Cliff) {
                return false;
            }
            if (placed[static_cast<std::size_t>(p.y * map.width() + p.x)]) {
                return false;
            }
        }
    }
    return true;
}

int placementScore(const MapGrid& map, CliffPlacement placement, bool useCellHeightTargets) noexcept
{
    const CliffTemplateInfo& info = cliffTemplate(placement.tile);
    int score = templateSlotCount(info) * 240 + templatePriority(placement.tile);
    int cliffNeighbors = 0;
    int contactMatches = 0;
    int contactMisses = 0;
    int heightMatches = 0;
    int heightMisses = 0;
    for (int y = 0; y < info.height; ++y) {
        for (int x = 0; x < info.width; ++x) {
            if (!isValidCliffPieceSlot(placement.tile, x, y)) {
                continue;
            }
            const Point p{placement.anchor.x + x, placement.anchor.y + y};
            if (!map.inBounds(p) || map.at(p).terrain != Terrain::Cliff) {
                return std::numeric_limits<int>::min();
            }
            for (Point neighbor : map.neighbors4(p)) {
                if (map.at(neighbor).terrain == Terrain::Cliff) {
                    ++cliffNeighbors;
                }
            }
            const int actual = neighborMask(map, p);
            const int local = slotContactMask(placement.tile, x, y);
            contactMatches += bitCount4(actual & local);
            contactMisses += bitCount4(actual & ~local);
            if (useCellHeightTargets) {
                const int subTile = cliffPieceSubTile(placement.tile, x, y);
                const int pieceHeight = cliffPieceCellHeight(placement.tile, subTile);
                const int targetHeight = map.at(p).height >= 2 ? 4 : 0;
                if (pieceHeight == targetHeight) {
                    ++heightMatches;
                } else {
                    ++heightMisses;
                }
            }
        }
    }
    score += cliffNeighbors * 10;
    score += contactMatches * 36;
    score -= contactMisses * 18;
    score += heightMatches * 190;
    score -= heightMisses * 420;
    score += static_cast<int>(cliffNoise(placement.anchor, placement.tile * 97 + 11) % 13);
    return score;
}

void applyTemplate(MapGrid& map, std::vector<bool>& placed, CliffPlacement placement, bool writeCellHeights) noexcept
{
    const CliffTemplateInfo& info = cliffTemplate(placement.tile);
    for (int y = 0; y < info.height; ++y) {
        for (int x = 0; x < info.width; ++x) {
            if (!isValidCliffPieceSlot(placement.tile, x, y)) {
                continue;
            }
            const Point p{placement.anchor.x + x, placement.anchor.y + y};
            if (!map.inBounds(p) || map.at(p).terrain != Terrain::Cliff) {
                continue;
            }
            Cell& cell = map.at(p);
            const int subTile = cliffPieceSubTile(placement.tile, x, y);
            cell.slope = encodeCliffPiece({placement.tile, subTile});
            if (writeCellHeights) {
                cell.height = cliffPieceCellHeight(placement.tile, subTile);
                cell.level = cell.height;
            }
            placed[static_cast<std::size_t>(p.y * map.width() + p.x)] = true;
        }
    }
}

int fallbackSingleTile(Point point, int targetHeight) noexcept
{
    constexpr std::array<int, 3> kLowSingleTiles{{11, 12, 13}};
    constexpr std::array<int, 7> kHighSingleTiles{{25, 28, 29, 32, 33, 37, 38}};
    if (targetHeight >= 2) {
        return kHighSingleTiles[static_cast<std::size_t>(cliffNoise(point, 421) % kHighSingleTiles.size())];
    }
    return kLowSingleTiles[static_cast<std::size_t>(cliffNoise(point, 409) % kLowSingleTiles.size())];
}

} // namespace

int encodeCliffPiece(CliffPiece piece) noexcept
{
    const int tile = std::clamp(piece.tile, 0, kCliffPieceCount - 1);
    const int subTile = std::clamp(piece.subTile, 0, 255);
    return kCliffPieceEncodingBase + tile * kCliffPieceEncodingStride + subTile;
}

CliffPiece decodeCliffPiece(int encoded) noexcept
{
    if (!isEncodedCliffPiece(encoded)) {
        return {std::clamp(encoded, 0, kCliffPieceCount - 1), 0};
    }
    const int payload = encoded - kCliffPieceEncodingBase;
    return {
        std::clamp(payload / kCliffPieceEncodingStride, 0, kCliffPieceCount - 1),
        std::clamp(payload % kCliffPieceEncodingStride, 0, 255),
    };
}

bool isEncodedCliffPiece(int encoded) noexcept
{
    return encoded >= kCliffPieceEncodingBase
        && encoded < kCliffPieceEncodingBase + kCliffPieceCount * kCliffPieceEncodingStride;
}

int cliffPieceWidth(int tile) noexcept
{
    return cliffTemplate(tile).width;
}

int cliffPieceHeight(int tile) noexcept
{
    return cliffTemplate(tile).height;
}

bool isValidCliffPieceSlot(int tile, int x, int y) noexcept
{
    const CliffTemplateInfo& info = cliffTemplate(tile);
    if (x < 0 || y < 0 || x >= info.width || y >= info.height) {
        return false;
    }
    return info.terrain[static_cast<std::size_t>(y * info.width + x)] == '#';
}

int cliffPieceSubTile(int tile, int x, int y) noexcept
{
    return isValidCliffPieceSlot(tile, x, y) ? y * cliffPieceWidth(tile) + x : -1;
}

int cliffPieceCellHeight(int tile, int subTile) noexcept
{
    const CliffTemplateInfo& info = cliffTemplate(tile);
    if (subTile < 0 || static_cast<std::size_t>(subTile) >= info.cellHeights.size()) {
        return 0;
    }
    return info.cellHeights[static_cast<std::size_t>(subTile)] == '4' ? 4 : 0;
}

int cliffPieceValidSlotCount(int tile) noexcept
{
    return templateSlotCount(cliffTemplate(tile));
}

void rebuildCliffPieceTiles(MapGrid& map, bool useCellHeightTargets, bool writeCellHeights) noexcept
{
    for (Cell& cell : map.cells()) {
        if (isEncodedCliffPiece(cell.slope)) {
            cell.slope = -1;
        }
    }

    std::vector<bool> placed(map.cells().size(), false);
    std::vector<CliffPlacement> candidates;
    candidates.reserve(map.cells().size() * 4);

    for (const Cell& cell : map.cells()) {
        if (cell.terrain != Terrain::Cliff) {
            continue;
        }

        const Point point{cell.x, cell.y};
        for (int tile = 0; tile < kCliffPieceCount; ++tile) {
            const CliffTemplateInfo& info = cliffTemplate(tile);
            for (int y = 0; y < info.height; ++y) {
                for (int x = 0; x < info.width; ++x) {
                    if (!isValidCliffPieceSlot(tile, x, y)) {
                        continue;
                    }
                    CliffPlacement placement{tile, {point.x - x, point.y - y}, 0};
                    placement.score = placementScore(map, placement, useCellHeightTargets);
                    if (placement.score != std::numeric_limits<int>::min()) {
                        candidates.push_back(placement);
                    }
                }
            }
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](CliffPlacement lhs, CliffPlacement rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        if (lhs.anchor.y != rhs.anchor.y) {
            return lhs.anchor.y < rhs.anchor.y;
        }
        if (lhs.anchor.x != rhs.anchor.x) {
            return lhs.anchor.x < rhs.anchor.x;
        }
        return lhs.tile < rhs.tile;
    });

    for (CliffPlacement placement : candidates) {
        if (canPlaceTemplate(map, placed, placement)) {
            applyTemplate(map, placed, placement, writeCellHeights);
        }
    }

    for (Cell& cell : map.cells()) {
        if (cell.terrain != Terrain::Cliff) {
            continue;
        }
        const auto index = static_cast<std::size_t>(cell.y * map.width() + cell.x);
        if (!placed[index]) {
            const int tile = fallbackSingleTile({cell.x, cell.y}, cell.height);
            cell.slope = encodeCliffPiece({tile, 0});
            if (writeCellHeights) {
                cell.height = cliffPieceCellHeight(tile, 0);
                cell.level = cell.height;
            }
        }
    }
}

} // namespace rmg
