#include "rmg/native_api.h"

namespace rmg {

NativeGenerationResult generateNativeMap(
    const MapSeedOptions& options,
    const RmgConfig& config,
    const NativeTheaterCatalog* theaterCatalog)
{
    NativeMapGenerator generator(theaterCatalog);
    NativeGenerationResult result;
    result.map = generator.generate(options, config);
    result.report = generator.report();
    return result;
}

} // namespace rmg
