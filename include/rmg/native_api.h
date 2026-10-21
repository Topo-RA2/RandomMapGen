#pragma once

#include "rmg/map_types.h"
#include "rmg/native_generator.h"
#include "rmg/native_theater_catalog.h"

namespace rmg {

struct NativeGenerationResult {
    MapGrid map;
    NativeGenerationReport report;
};

NativeGenerationResult generateNativeMap(
    const MapSeedOptions& options,
    const RmgConfig& config = {},
    const NativeTheaterCatalog* theaterCatalog = nullptr);

} // namespace rmg
