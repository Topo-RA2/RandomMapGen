#pragma once

#include "rmg/map_types.h"

#include <vector>

namespace rmg {

struct ShorePiece {
    int tile = 0;
    int subTile = 0;
};

constexpr int kShorePieceEncodingBase = 256;
constexpr int kShorePieceEncodingLimit = 16384;

int encodeShorePiece(ShorePiece piece) noexcept;
ShorePiece decodeShorePiece(int encoded) noexcept;
bool isEncodedShorePiece(int encoded) noexcept;
int shoreTemplateWidth(int piece) noexcept;
int shoreTemplateHeight(int piece) noexcept;
char shoreTemplateSlot(int piece, int x, int y) noexcept;
ShorePiece shorePieceForCell(const MapGrid& map, Point point) noexcept;
void rebuildShorePieceTiles(MapGrid& map) noexcept;
void rebuildSimpleShorePieceTiles(MapGrid& map) noexcept;

} // namespace rmg
