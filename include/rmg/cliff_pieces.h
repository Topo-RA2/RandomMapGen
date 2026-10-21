#pragma once

#include "rmg/map_types.h"

namespace rmg {

struct CliffPiece {
    int tile = 0;
    int subTile = 0;
};

constexpr int kCliffPieceCount = 40;
constexpr int kCliffPieceEncodingBase = 16384;
constexpr int kCliffPieceEncodingStride = 256;

int encodeCliffPiece(CliffPiece piece) noexcept;
CliffPiece decodeCliffPiece(int encoded) noexcept;
bool isEncodedCliffPiece(int encoded) noexcept;

int cliffPieceWidth(int tile) noexcept;
int cliffPieceHeight(int tile) noexcept;
bool isValidCliffPieceSlot(int tile, int x, int y) noexcept;
int cliffPieceSubTile(int tile, int x, int y) noexcept;
int cliffPieceCellHeight(int tile, int subTile) noexcept;
int cliffPieceValidSlotCount(int tile) noexcept;

void rebuildCliffPieceTiles(MapGrid& map, bool useCellHeightTargets = false, bool writeCellHeights = false) noexcept;

} // namespace rmg
