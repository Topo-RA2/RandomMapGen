#include "rmg/cliff_pieces.h"
#include "rmg/native_generator.h"
#include "rmg/native_trace.h"
#include "rmg/shore_pieces.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#if defined(_MSC_VER) && defined(_M_IX86)
#include <float.h>
#endif

namespace rmg {
#include "native_generator_detail/helpers.inc"

#include "native_generator_detail/stage_report.inc"

#include "native_generator_detail/workspace.inc"

#include "native_generator_detail/pipeline.inc"

#include "native_generator_detail/terrain_flood.inc"

#include "native_generator_detail/terrain_special.inc"

#include "native_generator_detail/terrain_ordinary.inc"

#include "native_generator_detail/terrain_post.inc"

#include "native_generator_detail/regions.inc"

#include "native_generator_detail/starts_height.inc"

#include "native_generator_detail/objects_resources.inc"

#include "native_generator_detail/height_surface.inc"

#include "native_generator_detail/decorations.inc"

#include "native_generator_detail/trees.inc"

} // namespace rmg
