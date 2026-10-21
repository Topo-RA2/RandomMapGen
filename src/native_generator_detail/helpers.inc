namespace {

constexpr int kMinimumNativePatchSize = 400;
constexpr int kNativeBaseMapLevel = 4;
constexpr int kNativeTileClear = 0;
constexpr int kNativeTileRampBase = 29;
constexpr int kNativeTileRampSmoothBase = 510;
constexpr int kNativeSnowTileRampSmoothBase = 444;
constexpr int kNativeHeightSlopeCount = 19;
constexpr int kNativeTileCliffBase = 49;
constexpr int kNativeTileShoreBase = 89;
constexpr int kNativeTileRoughBase = 131;
constexpr int kNativeTileWaterCliffBase = 148;
constexpr int kNativeTileBridgeBase = 277;
constexpr int kNativeTileWaterBase = 314;
constexpr int kNativeTileDirtBase = 418;
constexpr int kNativeTileGreenBase = 493;
constexpr int kNativeSnowTileGreenBase = 713;
constexpr int kNativeTilePaveBase = 534;
constexpr double kNativePiOverTwo = std::bit_cast<double>(0x3FF921FB54442D18ull);
constexpr double kNativeSevenPiOverFour = std::bit_cast<double>(0x4015FDBBE9BBA775ull);
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr const char* kNativeStageCellDumpPathEnv = "RMG_NATIVE_STAGE_CELL_DUMP_PATH";
constexpr const char* kNativeType2WorkspaceDumpPathEnv = "RMG_NATIVE_TYPE2_WORKSPACE_DUMP_PATH";
constexpr const char* kNativeType2PatchDumpPathEnv = "RMG_NATIVE_TYPE2_PATCH_DUMP_PATH";
constexpr const char* kNativeType2PatchStateDumpPathEnv = "RMG_NATIVE_TYPE2_PATCH_STATE_DUMP_PATH";
constexpr const char* kNativeType2RecalcProfileEnv = "RMG_NATIVE_TYPE2_RECALC_PROFILE";
constexpr const char* kNativeType2SmoothRngCellsEnv = "RMG_NATIVE_TYPE2_SMOOTH_RNG_CELLS";
constexpr const char* kNativeType2CliffRestorePassesEnv = "RMG_NATIVE_TYPE2_CLIFF_RESTORE_PASSES";
constexpr const char* kNativeType2CliffRestoreOrderEnv = "RMG_NATIVE_TYPE2_CLIFF_RESTORE_ORDER";
constexpr const char* kNativeType2CliffRestoreAlgorithmEnv = "RMG_NATIVE_TYPE2_CLIFF_RESTORE_ALGORITHM";
constexpr const char* kNativeType2ProcessCliffHeightEnv = "RMG_NATIVE_TYPE2_PROCESS_CLIFF_HEIGHT";
constexpr const char* kNativeType2SmoothFootprintEnv = "RMG_NATIVE_TYPE2_SMOOTH_FOOTPRINT";
constexpr const char* kNativeType2FinalPostpassEnv = "RMG_NATIVE_TYPE2_FINAL_POSTPASS";
constexpr const char* kNativeType2CardinalOffsetsEnv = "RMG_NATIVE_TYPE2_CARDINAL_OFFSETS";
constexpr const char* kNativeType2EmptyNeighborModeEnv = "RMG_NATIVE_TYPE2_EMPTY_NEIGHBOR_MODE";
constexpr const char* kNativeType2PlaceCliffCompatEnv = "RMG_NATIVE_TYPE2_PLACE_CLIFF_COMPAT";
constexpr const char* kNativePondDumpPathEnv = "RMG_NATIVE_POND_DUMP_PATH";
constexpr const char* kNativeGreenDumpPathEnv = "RMG_NATIVE_GREEN_DUMP_PATH";
constexpr const char* kNativeTechDumpPathEnv = "RMG_NATIVE_TECH_DUMP_PATH";
constexpr const char* kNativeTiberiumDumpPathEnv = "RMG_NATIVE_TIBERIUM_DUMP_PATH";
constexpr const char* kNativeTiberiumEventDumpPathEnv = "RMG_NATIVE_TIBERIUM_EVENT_DUMP_PATH";
constexpr const char* kNativeSpawnDumpPathEnv = "RMG_NATIVE_SPAWN_DUMP_PATH";
constexpr const char* kNativeSpawnCandidateDumpPathEnv = "RMG_NATIVE_SPAWN_CANDIDATE_DUMP_PATH";
constexpr const char* kNativeSpawnExclusionEventDumpPathEnv = "RMG_NATIVE_SPAWN_EXCLUSION_EVENT_DUMP_PATH";
constexpr const char* kNativeRegionDumpPathEnv = "RMG_NATIVE_REGION_DUMP_PATH";
constexpr const char* kNativeOreChainDumpPathEnv = "RMG_NATIVE_ORE_CHAIN_DUMP_PATH";
constexpr const char* kNativeOreChainRecalculateDumpPathEnv = "RMG_NATIVE_ORE_CHAIN_RECALC_DUMP_PATH";
constexpr const char* kNativeOreChainQueueDumpPathEnv = "RMG_NATIVE_ORE_CHAIN_QUEUE_DUMP_PATH";
constexpr const char* kNativeOreChainPlacementDumpPathEnv = "RMG_NATIVE_ORE_CHAIN_PLACEMENT_DUMP_PATH";
constexpr const char* kNativeOreChainSeedDumpPathEnv = "RMG_NATIVE_ORE_CHAIN_SEED_DUMP_PATH";
constexpr const char* kNativeCliffLineTracePathEnv = "RMG_NATIVE_CLIFF_LINE_TRACE_PATH";
constexpr const char* kNativeCliffLineRecalculateDumpPathEnv = "RMG_NATIVE_CLIFF_LINE_RECALC_DUMP_PATH";
constexpr const char* kNativeCliffLinePostDumpPathEnv = "RMG_NATIVE_CLIFF_LINE_POST_DUMP_PATH";
constexpr const char* kNativeOreChainTracePathEnv = "RMG_NATIVE_ORE_CHAIN_TRACE_PATH";
constexpr const char* kNativeSurfaceDumpPathEnv = "RMG_NATIVE_SURFACE_DUMP_PATH";
constexpr const char* kNativeSurfaceCandidateDumpPathEnv = "RMG_NATIVE_SURFACE_CANDIDATE_DUMP_PATH";
constexpr const char* kNativeTreeCenterDumpPathEnv = "RMG_NATIVE_TREE_CENTER_DUMP_PATH";
constexpr const char* kNativeGrassDumpPathEnv = "RMG_NATIVE_GRASS_DUMP_PATH";
constexpr const char* kNativeHeightNoiseDumpPathEnv = "RMG_NATIVE_HEIGHT_NOISE_DUMP_PATH";
constexpr const char* kNativeHeightNoiseTraceIndexEnv = "RMG_NATIVE_HEIGHT_NOISE_TRACE_INDEX";
constexpr double kNativeUInt32Unit = 1.0 / 4294967295.0;
constexpr float kNativeFloatUInt32Unit = 1.0f / 4294967296.0f;

int nativeTechUniqueIdSpan(int nativeTechIndex) noexcept
{
    // The first four neutral-tech types create four additional Abstract
    // objects; CAMACH/CAPOWR/CASLAB create two. The next BuildingClass ID
    // therefore advances by five or three respectively.
    return nativeTechIndex >= 0 && nativeTechIndex <= 3 ? 5 : 3;
}

#if defined(_MSC_VER)
class ScopedNativeX87Chop {
public:
    ScopedNativeX87Chop()
    {
        unsigned int ignored = 0;
        _controlfp_s(&previous_, 0, 0);
        _controlfp_s(&ignored, _RC_CHOP, _MCW_RC);
    }

    ~ScopedNativeX87Chop()
    {
        unsigned int ignored = 0;
        _controlfp_s(&ignored, previous_, _MCW_RC);
    }

private:
    unsigned int previous_ = 0;
};
#else
class ScopedNativeX87Chop {
};
#endif

constexpr std::array<int, 42> kNativePlaceCliffGroupCompat{
    0, 0, 0, 1, 2, 3, 4, 4, 5, 5, 5, 6, 7, 8,
    9, 9, 10, 10, 10, 11, 12, 13, 14, 14, 15, 15, 15, 16,
    17, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 25,
};

constexpr std::array<int, 42> kNativePlaceCliffBandCompat{
    4, 4, 4, 4, 4, 4, 3, 3, 2, 2, 2, 2, 2, 2,
    1, 1, 0, 0, 0, 0, 0, 0, 7, 7, 6, 6, 6, 6,
    6, 6, 5, 5, 3, 3, 1, 1, 7, 7, 5, 5, 4, 4,
};

struct HeapNode {
    Point point;
    double priority = 0.0;

    friend bool operator>(const HeapNode& lhs, const HeapNode& rhs) noexcept
    {
        return lhs.priority > rhs.priority;
    }
};

struct SurfaceDensity {
    double groundA = 0.0;
    double groundB = 0.0;
    double green = 0.0;
};

struct NativeStageCell {
    Point cell;
    const Cell* source = nullptr;
    int isoTileTypeIndex = 0;
    int overlayTypeIndex = -1;
    int smudgeTypeIndex = -1;
    int height = 0;
    int level = 4;
    int slopeIndex = 0;
    int overlayData = 0;
    int smudgeData = 0;
    bool iceGrowthAllowed = false;
    int landType = 0;
    int objectAbstractType = -1;
    int objectTypeIndex = -1;
    int altObjectAbstractType = -1;
    int altObjectTypeIndex = -1;
    int mapgenSurfaceBlocked = 0;
    int mapgenTempTag = 0;
    int mapgenZoneId = 0;
};

struct NativeIteratorCell {
    Point cell;
    const Cell* source = nullptr;
    int isoTileTypeIndex = kNativeTileWaterBase;
    int height = 0;
    int level = 4;
    int slopeIndex = 0;
    int landType = 2;
    bool iceGrowthAllowed = false;
};

struct NativeMapgenScratchCellInfo {
    Point cell;
    int isoTileTypeIndex = kNativeTileWaterBase;
    int level = 4;
};

std::vector<NativeIteratorCell> buildNativeIteratorCells(const MapGrid& map, const std::vector<IsoTileOverride>& isoTileOverrides);

struct NativeGreenCandidate {
    Point point;
    Point anchor;
    int facing = 0;
    bool invalidCell = false;
};

struct NativeGreenProbe {
    Point point;
    Point anchor;
    int facing = 0;
    int tile = -1;
    bool blank = false;
    bool invalidCell = false;
};

int g_nativeSurfaceSeq = 0;
int g_nativeSurfaceBaseIndex = 0;
int g_nativeSurfaceDensityIndex = 0;
int g_nativeSurfaceGroundBlobIndex = 0;
int g_nativeSurfaceTreePatchIndex = 0;
int g_nativeTileTheater = 0;

constexpr int kNativeTemperateSlopeSetPiecesBase = 384;

const NativeIsoTileTemplate* nativeTemperateSlopeSetPiece(int tileTypeIndex) noexcept
{
    // Theater=0 SlopeSetPieces metadata captured from the original YR TMP files.
    static const std::array<NativeIsoTileTemplate, 10> tiles{{
        {384, "RAMP01.TEM", false, 3, 4, {
            {},
            {true, 3, 13, 11},
            {true, 3, 13, 4},
            {true, 4, 15, 0},
            {true, 3, 15, 3},
            {true, 2, 13, 4},
            {true, 0, 15, 0},
            {true, 0, 15, 1},
            {true, 1, 13, 4},
            {},
            {true, 0, 13, 8},
            {true, 0, 13, 4},
        }},
        {385, "RAMP02.TEM", false, 3, 4, {
            {true, 3, 13, 4},
            {true, 3, 13, 12},
            {},
            {true, 2, 13, 4},
            {true, 3, 15, 1},
            {true, 4, 15, 0},
            {true, 1, 13, 4},
            {true, 0, 15, 3},
            {true, 0, 15, 0},
            {true, 0, 13, 4},
            {true, 0, 13, 7},
            {},
        }},
        {386, "RAMP03.TEM", false, 4, 3, {
            {true, 3, 13, 3},
            {true, 2, 13, 3},
            {true, 1, 13, 3},
            {true, 0, 13, 3},
            {true, 3, 13, 10},
            {true, 3, 15, 2},
            {true, 0, 15, 4},
            {true, 0, 13, 7},
            {},
            {true, 4, 15, 0},
            {true, 0, 15, 0},
            {},
        }},
        {387, "RAMP04.TEM", false, 4, 3, {
            {},
            {true, 4, 15, 0},
            {true, 0, 15, 0},
            {},
            {true, 3, 13, 11},
            {true, 3, 15, 4},
            {true, 0, 15, 2},
            {true, 0, 13, 6},
            {true, 3, 13, 3},
            {true, 2, 13, 3},
            {true, 1, 13, 3},
            {true, 0, 13, 3},
        }},
        {388, "RAMP05.TEM", false, 3, 4, {
            {},
            {true, 0, 13, 5},
            {true, 0, 13, 2},
            {true, 0, 15, 0},
            {true, 0, 15, 0},
            {true, 1, 13, 2},
            {true, 4, 13, 0},
            {true, 3, 13, 3},
            {true, 2, 13, 2},
            {},
            {true, 3, 13, 10},
            {true, 3, 13, 2},
        }},
        {389, "RAMP06.TEM", false, 3, 4, {
            {true, 0, 13, 2},
            {true, 0, 13, 0},
            {},
            {true, 1, 13, 2},
            {true, 0, 15, 3},
            {true, 0, 15, 0},
            {true, 2, 13, 2},
            {},
            {},
            {true, 3, 13, 2},
            {},
            {},
        }},
        {390, "RAMP07.TEM", false, 2, 2, {
            {true, 3, 15, 1},
            {true, 4, 15, 0},
            {true, 3, 13, 9},
            {},
        }},
        {391, "RAMP08.TEM", false, 4, 3, {
            {},
            {true, 0, 15, 0},
            {true, 4, 15, 0},
            {},
            {true, 0, 13, 5},
            {true, 0, 15, 0},
            {true, 3, 15, 4},
            {true, 3, 13, 12},
            {true, 0, 13, 1},
            {true, 1, 13, 1},
            {true, 2, 13, 1},
            {true, 3, 13, 1},
        }},
        {392, "RAMP09.TEM", false, 4, 3, {
            {true, 0, 13, 1},
            {true, 1, 13, 1},
            {true, 2, 13, 1},
            {true, 3, 13, 1},
            {true, 0, 13, 8},
            {true, 0, 15, 0},
            {},
            {},
            {},
            {true, 0, 15, 0},
            {},
            {},
        }},
        {393, "RAMP10.TEM", false, 2, 2, {
            {true, 3, 15, 2},
            {true, 3, 13, 9},
            {true, 4, 15, 0},
            {},
        }},
    }};

    if (g_nativeTileTheater != 0
        || tileTypeIndex < kNativeTemperateSlopeSetPiecesBase
        || tileTypeIndex >= kNativeTemperateSlopeSetPiecesBase + static_cast<int>(tiles.size())) {
        return nullptr;
    }
    return &tiles[static_cast<std::size_t>(tileTypeIndex - kNativeTemperateSlopeSetPiecesBase)];
}

int nativeSlopeSetPiecesTileBase(const NativeTheaterCatalog* catalog) noexcept
{
    if (catalog != nullptr) {
        return catalog->slopeSetPiecesBase;
    }
    return g_nativeTileTheater == 0 ? kNativeTemperateSlopeSetPiecesBase : -1;
}

int nativeGreenTileBase() noexcept
{
    return g_nativeTileTheater == 1 ? kNativeSnowTileGreenBase : kNativeTileGreenBase;
}

int nativeRampSmoothTileBase() noexcept
{
    return g_nativeTileTheater == 1 ? kNativeSnowTileRampSmoothBase : kNativeTileRampSmoothBase;
}

const char* nativeSurfaceDumpPath() noexcept
{
    const char* path = std::getenv(kNativeSurfaceDumpPathEnv);
    return (path != nullptr && path[0] != '\0') ? path : nullptr;
}

void resetNativeSurfaceTrace()
{
    g_nativeSurfaceSeq = 0;
    g_nativeSurfaceBaseIndex = 0;
    g_nativeSurfaceDensityIndex = 0;
    g_nativeSurfaceGroundBlobIndex = 0;
    g_nativeSurfaceTreePatchIndex = 0;
}

void appendNativeSurfaceTrace(
    const char* event,
    int index,
    Point cell,
    std::uint32_t raw,
    double value,
    double threshold,
    int hit,
    int size,
    int placed,
    int tile,
    int overlay,
    int landType,
    int firstObject,
    int objectType,
    int extra0,
    int extra1,
    const std::string& label = {})
{
    const char* path = nativeSurfaceDumpPath();
    if (path == nullptr) {
        return;
    }

    std::ofstream out(path, std::ios::out | std::ios::app);
    if (!out.is_open()) {
        return;
    }

    static bool headerWritten = false;
    if (!headerWritten) {
        out << "seq,event,index,x,y,packed,raw,value,threshold,hit,size,placed,tile,overlay,landType,firstObject,objectType,mapgenScratch,mapgenSurfaceBlocked,extra0,extra1,label\n";
        headerWritten = true;
    }

    const int packed = (cell.y << 16) | (cell.x & 0xFFFF);
    out << (++g_nativeSurfaceSeq) << ','
        << (event ? event : "") << ','
        << index << ','
        << cell.x << ','
        << cell.y << ','
        << packed << ','
        << raw << ','
        << std::setprecision(17) << value << ','
        << std::setprecision(17) << threshold << ','
        << hit << ','
        << size << ','
        << placed << ','
        << tile << ','
        << overlay << ','
        << landType << ','
        << firstObject << ','
        << objectType << ','
        << 0 << ','
        << 0 << ','
        << extra0 << ','
        << extra1 << ','
        << label << '\n';
}

void appendNativeTreeCenterTrace(
    int patchIndex,
    int attempt,
    Point cell,
    std::uint32_t raw,
    int sampleIndex,
    bool insideDiamond,
    bool hasCoords,
    int tile,
    int overlay,
    int landType,
    int firstObject,
    bool blocked,
    bool accepted)
{
    const char* path = std::getenv(kNativeTreeCenterDumpPathEnv);
    if (path == nullptr || path[0] == '\0') {
        return;
    }

    std::ofstream out(path, std::ios::out | std::ios::app);
    if (!out.is_open()) {
        return;
    }

    static bool headerWritten = false;
    if (!headerWritten) {
        out << "patch,attempt,x,y,packed,raw,sampleIndex,insideDiamond,hasCoords,tile,overlay,landType,firstObject,blocked,accepted\n";
        headerWritten = true;
    }

    const int packed = (cell.y << 16) | (cell.x & 0xFFFF);
    out << patchIndex << ','
        << attempt << ','
        << cell.x << ','
        << cell.y << ','
        << packed << ','
        << raw << ','
        << sampleIndex << ','
        << (insideDiamond ? 1 : 0) << ','
        << (hasCoords ? 1 : 0) << ','
        << tile << ','
        << overlay << ','
        << landType << ','
        << firstObject << ','
        << (blocked ? 1 : 0) << ','
        << (accepted ? 1 : 0) << '\n';
}

double tracedRandomUnit(NativeRandomizer& rng, std::uint32_t* rawOut)
{
    const std::uint32_t raw = rng.random();
    if (rawOut != nullptr) {
        *rawOut = raw;
    }
    return static_cast<double>(raw) * kNativeUInt32Unit;
}

double nativeHeapPriorityNoise(std::uint32_t raw) noexcept
{
    return static_cast<double>(raw) * 5.0 * kNativeUInt32Unit;
}

int clampOddMinimum(int value, int minimum)
{
    return std::max(value, minimum);
}

Point rectCenter(Rect rect)
{
    return {rect.x + rect.w / 2, rect.y + rect.h / 2};
}

Point toNativeCellCoords(Point p, int mapWidth)
{
    return {p.x + p.y + 1, mapWidth + p.y - p.x};
}

Point offsetPoint(Point p, Point offset) noexcept
{
    return {p.x + offset.x, p.y + offset.y};
}

int nativeDoubleToInt(double value) noexcept
{
    // gamemd sets _controlfp(0x300, 0x300), so double2int_7C5F00 uses x87 chop/truncate.
    return static_cast<int>(value);
}

double nativeX87Chop53(double rounded, double exactMinusRounded) noexcept
{
    const bool roundedAwayFromZero = (rounded > 0.0 && exactMinusRounded < 0.0)
        || (rounded < 0.0 && exactMinusRounded > 0.0);
    return roundedAwayFromZero ? std::nextafter(rounded, 0.0) : rounded;
}

#if defined(RMG_HAS_NATIVE_X87_HELPER)
extern "C" double rmg_native_x87_chop53_add(double left, double right);
extern "C" double rmg_native_x87_chop53_multiply(double left, double right);
#endif

double nativeX87Chop53Multiply(double left, double right) noexcept
{
#if defined(RMG_HAS_NATIVE_X87_HELPER)
    return rmg_native_x87_chop53_multiply(left, right);
#else
    // double2int_7C5F00 leaves the x87 control word at 0x0E7F (PC53 + RC_CHOP).
    // Compute the nearest double first, then use the exact FMA residual to undo
    // only a rounding step that moved away from zero.
    volatile double storedRounded = left * right;
    const double rounded = storedRounded;
    return nativeX87Chop53(rounded, std::fma(left, right, -rounded));
#endif
}

double nativeX87Chop53Divide(double left, double right) noexcept
{
    volatile double storedRounded = left / right;
    const double rounded = storedRounded;
    const double remainder = std::fma(-rounded, right, left);
    return nativeX87Chop53(rounded, remainder / right);
}

double nativeX87Chop53Add(double left, double right) noexcept
{
#if defined(RMG_HAS_NATIVE_X87_HELPER)
    return rmg_native_x87_chop53_add(left, right);
#else
    volatile double storedRounded = left + right;
    const double rounded = storedRounded;
    volatile double storedVirtualRight = rounded - left;
    const double virtualRight = storedVirtualRight;
    volatile double storedVirtualLeft = rounded - virtualRight;
    const double virtualLeft = storedVirtualLeft;
    volatile double storedLeftError = left - virtualLeft;
    volatile double storedRightError = right - virtualRight;
    const double exactMinusRounded = storedLeftError + storedRightError;
    return nativeX87Chop53(rounded, exactMinusRounded);
#endif
}

double nativeX87Chop53Subtract(double left, double right) noexcept
{
    return nativeX87Chop53Add(left, -right);
}

double nativeX87Chop53MultiplyAdd(double value, double scale, double addend) noexcept
{
    return nativeX87Chop53Add(nativeX87Chop53Multiply(value, scale), addend);
}

int nativeTiberiumBudget(int tiberium, int minimum, int maximum) noexcept
{
    double value = nativeX87Chop53Multiply(static_cast<double>(tiberium), 0.01);
    value = nativeX87Chop53Multiply(value, static_cast<double>(maximum - minimum));
    value = nativeX87Chop53Add(value, static_cast<double>(minimum));
    return nativeDoubleToInt(value);
}

float nativeDoubleToFloatChop(double value) noexcept
{
    float result = static_cast<float>(value);
    if ((value > 0.0 && static_cast<double>(result) > value)
        || (value < 0.0 && static_cast<double>(result) < value)) {
        result = std::nextafter(result, 0.0f);
    }
    return result;
}

float nativeLegacySineTableValue(int index) noexcept
{
    constexpr double kStep = 6.28318530717958647692 / 8192.0;
    return nativeDoubleToFloatChop(std::sin(static_cast<double>(index) * kStep));
}

int nativeLegacyTrigBaseIndex(double angle) noexcept
{
    constexpr float kAngleScale = std::bit_cast<float>(0x4522F983u);
    const int scaled = nativeDoubleToInt(nativeX87Chop53Multiply(
        angle,
        static_cast<double>(kAngleScale)));
    int index = (scaled / 2) % 8192;
    if (index < 0) {
        index += 8192;
    }
    if ((scaled & 1) != 0 && index < 8191) {
        ++index;
    }
    return index;
}

double nativeLegacySin(double angle) noexcept
{
    return static_cast<double>(nativeLegacySineTableValue(nativeLegacyTrigBaseIndex(angle)));
}

double nativeLegacyCos(double angle) noexcept
{
    int index = nativeLegacyTrigBaseIndex(angle) + 2048;
    if (index >= 10240) {
        index -= 8192;
    }
    return static_cast<double>(nativeLegacySineTableValue(index));
}

float nativePatchedSinglePrecisionSqrt(float input) noexcept
{
    return nativeApproximateSqrt(input);
}

double nativePatchedSqrt(double value) noexcept
{
    // The strict Origin artifacts retain sqrt_4CAC40's float LUT result.
    const float input = static_cast<float>(std::fabs(value));
    return static_cast<double>(nativePatchedSinglePrecisionSqrt(input));
}

double nativeRegionDistancePriority(
    int diamondDx,
    int diamondDy,
    int majorDistance,
    float diamondXScale,
    float diamondYScale) noexcept
{
    if (diamondDx == 0 && diamondDy == 0) {
        return 0.0;
    }

    const float axisDistance = nativeApproximateSqrt(
        static_cast<double>(diamondDx * diamondDx + diamondDy * diamondDy));
#if defined(_MSC_VER) && defined(_M_IX86)
    double xRatio = 0.0;
    double result = 0.0;
    __asm {
        fild dword ptr [diamondDx]
        fdiv dword ptr [axisDistance]
        fstp qword ptr [xRatio]
        fild dword ptr [diamondDy]
        fdiv dword ptr [axisDistance]
        fmul dword ptr [diamondYScale]
        fld dword ptr [diamondXScale]
        fmul qword ptr [xRatio]
        faddp st(1), st(0)
        fild dword ptr [majorDistance]
        fmulp st(1), st(0)
        fstp qword ptr [result]
    }
    return result;
#else
    const double xRatio = nativeX87Chop53Divide(
        static_cast<double>(diamondDx), static_cast<double>(axisDistance));
    const double yRatio = nativeX87Chop53Divide(
        static_cast<double>(diamondDy), static_cast<double>(axisDistance));
    const double weightedY = nativeX87Chop53Multiply(
        yRatio, static_cast<double>(diamondYScale));
    const double weightedX = nativeX87Chop53Multiply(
        xRatio, static_cast<double>(diamondXScale));
    const double weighted = nativeX87Chop53Add(weightedY, weightedX);
    const double priority = nativeX87Chop53Multiply(
        static_cast<double>(majorDistance), weighted);
    return priority;
#endif
}

float nativeOreChainSqrt(double value) noexcept
{
    return nativeApproximateSqrt(value);
}

float nativeOreChainPriority(std::uint32_t rawRandom, float distance, int placedBefore) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    ScopedNativeX87Chop scopedRounding;
    const std::uint64_t rawRandom64 = rawRandom;
    const double nativeUnit = kNativeUInt32Unit;
    const float priorityScale = 10.0f;
    const float distanceScale = 0.5f;
    const float placedPenalty = 0.02f;
    float result = 0.0f;
    __asm {
        fild qword ptr [rawRandom64]
        fmul qword ptr [nativeUnit]
        fld dword ptr [priorityScale]
        fmul st(0), st(1)
        fld dword ptr [distance]
        fmul dword ptr [distanceScale]
        faddp st(1), st(0)
        fild dword ptr [placedBefore]
        fmul dword ptr [placedPenalty]
        fsubp st(1), st(0)
        fstp dword ptr [result]
        fstp st(0)
    }
    return result;
#else
    const double randomUnit = static_cast<double>(rawRandom) * kNativeUInt32Unit;
    return static_cast<float>(
        static_cast<float>(randomUnit * 10.0f)
        + static_cast<float>(distance * 0.5f)
        - static_cast<float>(placedBefore * 0.02f));
#endif
}

int nativeInterpolatedPlayableSizeImpl(int seedSize, int players, bool highResourceTerrain) noexcept
{
    static constexpr std::array<int, 7> kWidthLowByPlayers{70, 70, 70, 80, 90, 100, 100};
    static constexpr std::array<int, 7> kWidthHighByPlayers{80, 80, 80, 90, 100, 110, 120};

    (void)highResourceTerrain;
    const int index = std::clamp(players, 2, 8) - 2;
    const double scale = std::min(
        nativeX87Chop53Divide(static_cast<double>(seedSize), 3.0),
        1.2);
    const double inverseScale = nativeX87Chop53Subtract(1.0, scale);
    const double lowTerm = nativeX87Chop53Multiply(
        static_cast<double>(kWidthLowByPlayers[static_cast<std::size_t>(index)]), inverseScale);
    const double highTerm = nativeX87Chop53Multiply(
        static_cast<double>(kWidthHighByPlayers[static_cast<std::size_t>(index)]), scale);
    return nativeDoubleToInt(nativeX87Chop53Add(lowTerm, highTerm));
}

int resolvedPlayableSize(int requestedSize, int players, bool highResourceTerrain, bool useNativeInterpolation) noexcept
{
    if (useNativeInterpolation) {
        return nativeInterpolatedPlayableSizeImpl(requestedSize, players, highResourceTerrain);
    }
    return std::max(1, requestedSize);
}

int nativeSeedWidthForTreeTarget(const MapSeedOptions& options) noexcept
{
    return std::max(0, options.nativeSeedWidth >= 0 ? options.nativeSeedWidth : options.width);
}

int nativeRegionReprocessMinimumSize(const MapGrid& map, const MapSeedOptions& options) noexcept
{
    const int regionSize = std::clamp(options.regionSize, 0, 100);
    const double scale = static_cast<double>(regionSize) * 0.005 + 0.05;
    return nativeDoubleToInt(scale * static_cast<double>(map.width()) * static_cast<double>(map.height()) * 2.0);
}

bool isInsideNativeEllipseCell(Point cell, Rect rect, int mapWidth);
double nativeRegionDistanceCells(Point noisyNative, Point cellNative, Point anchorNative, Rect rect);

bool isInsideNativeEllipse(Point p, Rect rect, int mapWidth, Point mapOffset = {})
{
    if (rect.w <= 0 || rect.h <= 0) {
        return false;
    }

    const Point cell = toNativeCellCoords(offsetPoint(p, mapOffset), mapWidth);
    return isInsideNativeEllipseCell(cell, rect, mapWidth);
}

bool isInsideNativeEllipseCell(Point cell, Rect rect, int mapWidth)
{
    const double widthRadius = std::max(1.0, nativeX87Chop53Multiply(static_cast<double>(rect.w), 0.5));
    const double heightRadius = std::max(1.0, nativeX87Chop53Multiply(static_cast<double>(rect.h), 0.5));
    const double inverseWidthRadiusSquared = nativeX87Chop53Divide(
        1.0,
        nativeX87Chop53Multiply(widthRadius, widthRadius));
    const double inverseHeightRadiusSquared = nativeX87Chop53Divide(
        1.0,
        nativeX87Chop53Multiply(heightRadius, heightRadius));
    const bool traceEnabled = native_trace::coreFunctionEnabled(native_trace::IsCellInsideRegion);
    native_trace::EventData traceData;
    if (traceEnabled) {
        const std::uint64_t widthScaleBits = std::bit_cast<std::uint64_t>(inverseWidthRadiusSquared);
        const std::uint64_t heightScaleBits = std::bit_cast<std::uint64_t>(inverseHeightRadiusSquared);
        traceData.args[0] = static_cast<std::uint32_t>(static_cast<std::uint16_t>(cell.x))
            | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(cell.y)) << 16);
        traceData.args[1] = static_cast<std::uint32_t>(static_cast<std::uint16_t>(rect.x))
            | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(rect.y)) << 16);
        traceData.args[2] = static_cast<std::uint32_t>(static_cast<std::uint16_t>(rect.w))
            | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(rect.h)) << 16);
        traceData.args[3] = 1;
        traceData.args[4] = static_cast<std::uint32_t>(widthScaleBits);
        traceData.args[5] = static_cast<std::uint32_t>(widthScaleBits >> 32);
        traceData.regionIndex = static_cast<std::int32_t>(heightScaleBits);
        traceData.regionId = static_cast<std::int32_t>(heightScaleBits >> 32);
        traceData.regionCount = mapWidth;
        traceData.cellX = static_cast<std::int16_t>(cell.x);
        traceData.cellY = static_cast<std::int16_t>(cell.y);
        traceData.resultKind = native_trace::ResultNone;
        native_trace::enterDetailed(0x59BAB0, native_trace::IsCellInsideRegion, traceData);
    }

    if (rect.w <= 0 || rect.h <= 0) {
        if (traceEnabled) {
            traceData.result[0] = 0;
            traceData.resultKind = native_trace::ResultLegacy;
            native_trace::exitDetailed(0x59BAB0, native_trace::IsCellInsideRegion, traceData);
        }
        return false;
    }

    const double diamondX = (static_cast<double>(cell.x - 2 * rect.x - cell.y + mapWidth - 1) * 0.5) - static_cast<double>(rect.w) * 0.5;
    const double diamondY = (static_cast<double>(cell.x - 2 * rect.y - mapWidth + cell.y - 1) * 0.5) - static_cast<double>(rect.h) * 0.5;
    const bool result = diamondY * diamondY * inverseHeightRadiusSquared
        + diamondX * diamondX * inverseWidthRadiusSquared < 1.0;
    if (traceEnabled) {
        traceData.result[0] = result ? 1u : 0u;
        traceData.resultKind = native_trace::ResultLegacy;
        native_trace::exitDetailed(0x59BAB0, native_trace::IsCellInsideRegion, traceData);
    }
    return result;
}

std::vector<Point> nativeCellCardinalSourceNeighbors(const MapGrid& map, Point p)
{
    static constexpr std::array<Point, 4> kOffsets{
        Point{1, -1},
        Point{1, 1},
        Point{-1, 1},
        Point{-1, -1},
    };

    std::vector<Point> result;
    result.reserve(kOffsets.size());
    for (Point offset : kOffsets) {
        const Point next{p.x + offset.x, p.y + offset.y};
        if (map.inBounds(next)) {
            result.push_back(next);
        }
    }
    return result;
}

double nativeRegionDistance(Point noisyCenter, Point p, Point regionAnchor, Rect rect, int mapWidth, Point mapOffset = {})
{
    if (rect.w <= 0 || rect.h <= 0) {
        return euclidean(noisyCenter, p);
    }

    const Point noisyNative = toNativeCellCoords(offsetPoint(noisyCenter, mapOffset), mapWidth);
    const Point cellNative = toNativeCellCoords(offsetPoint(p, mapOffset), mapWidth);
    const Point anchorNative = toNativeCellCoords(offsetPoint(regionAnchor, mapOffset), mapWidth);
    return nativeRegionDistanceCells(noisyNative, cellNative, anchorNative, rect);
}

double nativeRegionDistanceCells(Point noisyNative, Point cellNative, Point anchorNative, Rect rect)
{
    const bool traceEnabled = native_trace::coreFunctionEnabled(native_trace::ComputeTerrainDistance);
    native_trace::EventData traceData;
    const auto packPoint = [](Point point) {
        return static_cast<std::uint32_t>(static_cast<std::uint16_t>(point.x))
            | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(point.y)) << 16);
    };

    float diamondXScale = 1.0f;
    float diamondYScale = 1.0f;
    if (rect.w > 0 && rect.h > 0) {
        if (rect.w <= rect.h) {
            diamondXScale = nativeDoubleToFloatChop(
                static_cast<double>(rect.h) / static_cast<double>(std::max(1, rect.w)) * 1.2);
        } else {
            diamondYScale = nativeDoubleToFloatChop(
                static_cast<double>(rect.w) / static_cast<double>(std::max(1, rect.h)) * 1.2);
        }
    }
    if (traceEnabled) {
        traceData.args[0] = packPoint(noisyNative);
        traceData.args[1] = packPoint(cellNative);
        traceData.args[2] = packPoint({rect.x, rect.y});
        traceData.args[3] = packPoint({rect.w, rect.h});
        traceData.args[4] = packPoint(anchorNative);
        traceData.args[5] = std::bit_cast<std::uint32_t>(diamondXScale);
        traceData.regionIndex = static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(diamondYScale));
        traceData.regionId = 0;
        traceData.cellX = static_cast<std::int16_t>(noisyNative.x);
        traceData.cellY = static_cast<std::int16_t>(noisyNative.y);
        traceData.resultKind = native_trace::ResultNone;
        native_trace::enterDetailed(0x59B940, native_trace::ComputeTerrainDistance, traceData);
    }
    const auto finishTrace = [&](double result) {
        if (traceEnabled) {
            const std::uint64_t resultBits = std::bit_cast<std::uint64_t>(result);
            traceData.result[0] = static_cast<std::uint32_t>(resultBits);
            traceData.result[1] = static_cast<std::uint32_t>(resultBits >> 32);
            traceData.result[2] = 0;
            traceData.result[3] = 0;
            traceData.resultKind = native_trace::ResultBinary64;
            native_trace::exitDetailed(0x59B940, native_trace::ComputeTerrainDistance, traceData);
        }
        return result;
    };

    if (rect.w <= 0 || rect.h <= 0) {
        return finishTrace(euclidean(noisyNative, cellNative));
    }

    const int anchorDiamondX = (anchorNative.x - anchorNative.y) >> 1;
    const int anchorDiamondY = (anchorNative.x + anchorNative.y) >> 1;
    const int pointDiamondX = (cellNative.x - cellNative.y) >> 1;
    const int pointDiamondY = (cellNative.x + cellNative.y) >> 1;
    const int dx = std::abs(pointDiamondX - anchorDiamondX);
    const int dy = std::abs(pointDiamondY - anchorDiamondY);
    const int majorDistance = std::max(
        std::abs(noisyNative.x - cellNative.x),
        std::abs(noisyNative.y - cellNative.y));
    return finishTrace(nativeRegionDistancePriority(
        dx,
        dy,
        majorDistance,
        diamondXScale,
        diamondYScale));
}

double nativeRegionSeedDistance(Point p, Point regionAnchor, Rect rect, int mapWidth, Point mapOffset = {})
{
    if (rect.w <= 0 || rect.h <= 0) {
        return euclidean(p, regionAnchor);
    }

    const Point cellNative = toNativeCellCoords(offsetPoint(p, mapOffset), mapWidth);
    const Point anchorNative = toNativeCellCoords(offsetPoint(regionAnchor, mapOffset), mapWidth);
    double xScale = 1.0;
    double yScale = 1.0;
    if (rect.h <= rect.w) {
        yScale = static_cast<double>(rect.w) / std::max(1.0, static_cast<double>(rect.h)) * 1.2;
    } else {
        xScale = static_cast<double>(rect.h) / std::max(1.0, static_cast<double>(rect.w)) * 1.2;
    }
    return static_cast<double>(std::abs(cellNative.x - anchorNative.x)) * xScale
        + static_cast<double>(std::abs(cellNative.y - anchorNative.y)) * yScale;
}

double nativeAreaRatio(std::uint32_t raw)
{
    return 0.45 + static_cast<double>(raw) * (1.0 / 4294967296.0) * 0.05;
}

bool isNativeHighResourceMode(const MapSeedOptions& options) noexcept
{
    return options.mapType == 3 || options.mapType == 4;
}

// GenerateRandomMap reads MapSeedClass +0x3C (MapType) at 0x58E2F9 and
// removes level 0 candidates for the special MapType 3/4 pipelines.
bool nativeRegionLevelZeroCandidateRemoved(const MapSeedOptions& options) noexcept
{
    return options.mapType == 3 || options.mapType == 4;
}

bool nativeType2FinalPostpassEnabled() noexcept
{
    const char* value = std::getenv(kNativeType2FinalPostpassEnv);
    return value != nullptr && value[0] == '1';
}

int nativeSurfaceDataCount(const MapGrid& map) noexcept
{
    const Rect mapRect = map.hasNativeGeometry()
        ? map.nativeMapRect()
        : Rect{0, 0, map.width() + 4, map.height() + 12};
    return 2 * mapRect.w * (mapRect.h + 4);
}

Rect nativeSavedMapRect(const MapGrid& map) noexcept
{
    if (map.hasNativeGeometry()) {
        return map.nativeMapRect();
    }
    return {0, 0, std::max(1, map.width()) + 4, std::max(1, map.height()) + 12};
}

Rect nativeLocalRect(const MapGrid& map) noexcept
{
    if (map.hasNativeGeometry()) {
        return map.nativeLocalRect();
    }
    return {2, 5, std::max(1, map.width()), std::max(1, map.height())};
}

void adjustNativeGaussianRange(double& mean, double& deviation, double minimum, double maximum)
{
    const double lowerEdge = nativeX87Chop53Add(mean, -deviation);
    const double upperEdge = nativeX87Chop53Add(mean, deviation);
    if (lowerEdge > maximum || upperEdge < minimum) {
        deviation = nativeX87Chop53Multiply(
            nativeX87Chop53Add(maximum, -minimum),
            0.5);
        mean = nativeX87Chop53Add(minimum, deviation);
    }
}

double clampGaussian(NativeRandomizer& rng, double mean, double deviation, double minimum, double maximum)
{
    adjustNativeGaussianRange(mean, deviation, minimum, maximum);
    double value = mean;
    do {
        value = nativeX87Chop53Add(
            nativeX87Chop53Multiply(rng.gaussian(), deviation),
            mean);
    } while (value < minimum || value > maximum);
    return value;
}

void traceCliffLineGaussianParameters(
    std::uint32_t site,
    double angle,
    double mean,
    double deviation,
    int zone,
    Point seed)
{
    if (!native_trace::active()) {
        return;
    }

    native_trace::EventData data;
    const std::array<double, 3> values{angle, mean, deviation};
    for (std::size_t index = 0; index < values.size(); ++index) {
        const std::uint64_t bits = std::bit_cast<std::uint64_t>(values[index]);
        data.args[index * 2] = static_cast<std::uint32_t>(bits);
        data.args[index * 2 + 1] = static_cast<std::uint32_t>(bits >> 32);
    }
    data.regionId = zone;
    data.cellX = static_cast<std::int16_t>(seed.x);
    data.cellY = static_cast<std::int16_t>(seed.y);
    native_trace::callDetailed(site, native_trace::PlaceCliffLine, data);
}

double clampCliffLineGaussian(
    NativeRandomizer& rng,
    double mean,
    double deviation,
    double minimum,
    double maximum,
    std::uint32_t traceSite,
    double angle,
    int zone,
    Point seed)
{
    adjustNativeGaussianRange(mean, deviation, minimum, maximum);
    double value = mean;
    do {
        traceCliffLineGaussianParameters(traceSite, angle, mean, deviation, zone, seed);
        value = nativeX87Chop53Add(
            nativeX87Chop53Multiply(rng.gaussian(), deviation),
            mean);
    } while (value < minimum || value > maximum);
    return value;
}

int randomNativeSurfaceMean(NativeRandomizer& rng)
{
    int value = 20;
    do {
        value = nativeDoubleToInt(rng.randomUnit() * 21.0 + 20.0);
    } while (value > 40);
    return value;
}

int randomNativeSurfaceBlobSize(NativeRandomizer& rng, int mean, double deviation = 20.0)
{
    double effectiveMean = static_cast<double>(mean);
    double effectiveDeviation = deviation;
    if (effectiveMean - effectiveDeviation > 80.0 || effectiveMean + effectiveDeviation < 4.0) {
        effectiveMean = 42.0;
        effectiveDeviation = 38.0;
    }
    return std::max(1, nativeDoubleToInt(clampGaussian(rng, effectiveMean, effectiveDeviation, 4.0, 80.0)));
}

int randomNativeSimpleSurfaceBlobSize(NativeRandomizer& rng)
{
    return std::max(1, nativeDoubleToInt(clampGaussian(rng, 20.0, 15.0, 4.0, 60.0)));
}

double randomNativeTreePatchChance(NativeRandomizer& rng)
{
    return clampGaussian(rng, 0.2, 0.1, 0.05, 0.4);
}

double nativeSurfaceHeapPriority(double distance, std::uint32_t raw) noexcept
{
    const double scaled = nativeX87Chop53Multiply(
        nativeX87Chop53Multiply(static_cast<double>(raw), 5.0),
        kNativeUInt32Unit);
    return nativeX87Chop53Add(distance, scaled);
}

int nativeTreeTarget(int seedWidth, int vegetation, int maxTrees) noexcept
{
    const double widthTerm = nativeX87Chop53Add(
        nativeX87Chop53Multiply(static_cast<double>(seedWidth), 0.1),
        0.7);
    const double vegetationTerm = nativeX87Chop53Multiply(
        static_cast<double>(std::max(0, vegetation)),
        0.01);
    const double scaledTarget = nativeX87Chop53Multiply(widthTerm, vegetationTerm);
    return nativeDoubleToInt(nativeX87Chop53Multiply(
        scaledTarget,
        static_cast<double>(std::max(0, maxTrees))));
}

int randomNativeTreePatchSize(NativeRandomizer& rng)
{
    return std::max(1, nativeDoubleToInt(clampGaussian(rng, 25.0, 10.0, 10.0, 35.0)));
}

int randomNativeFloatScaledInclusive(NativeRandomizer& rng, int minValue, int maxValue, std::uint32_t* rawOut = nullptr)
{
    if (minValue == maxValue) {
        if (rawOut != nullptr) {
            *rawOut = 0;
        }
        return minValue;
    }

    const int lo = std::min(minValue, maxValue);
    const int hi = std::max(minValue, maxValue);
    const double span = static_cast<double>(static_cast<std::int64_t>(hi) - static_cast<std::int64_t>(lo) + 1);
    const double base = static_cast<double>(lo);

    int candidate = hi + 1;
    std::uint32_t raw = 0;
    while (candidate > hi) {
        raw = rng.random();
        const double scaled = nativeX87Chop53Multiply(
            nativeX87Chop53Multiply(static_cast<double>(raw), span),
            kNativeUInt32Unit);
        candidate = nativeDoubleToInt(nativeX87Chop53Add(scaled, base));
    }
    if (rawOut != nullptr) {
        *rawOut = raw;
    }
    return candidate;
}

void clearTags(MapGrid& map, int tag)
{
    for (auto& cell : map.cells()) {
        if (cell.tempTag == tag) {
            cell.tempTag = 0;
        }
    }
}

std::vector<Point> passableCells(const MapGrid& map)
{
    std::vector<Point> result;
    result.reserve(static_cast<std::size_t>(map.width() * map.height()));
    for (const auto& cell : map.cells()) {
        if (isPassable(cell)) {
            result.push_back({cell.x, cell.y});
        }
    }
    return result;
}

bool isResourcePatchCompatibleOverlay(Overlay overlay) noexcept
{
    return overlay == Overlay::None
        || overlay == Overlay::Ore
        || overlay == Overlay::Gems
        || overlay == Overlay::Start
        || overlay == Overlay::Tech
        || overlay == Overlay::NeutralBuilding
        || overlay == Overlay::NeutralUnit
        || overlay == Overlay::TerrainChain;
}

bool isCandidateResourceCell(const Cell& cell)
{
    return isPassable(cell)
        && !cell.spawnExclusion
        && !cell.cliffShoreExclusion
        && cell.terrainObject != TerrainObject::OrePatchLamp
        && isResourcePatchCompatibleOverlay(cell.overlay);
}

bool canTraverseResourcePatch(const Cell& cell)
{
    return isPassable(cell)
        && cell.terrainObject != TerrainObject::OrePatchLamp
        && isResourcePatchCompatibleOverlay(cell.overlay);
}

int nativeLandTypeForCell(const Cell& cell) noexcept;

bool isNativeRegionPassable(const Cell& cell) noexcept
{
    return isPassable(cell) && cell.terrain != Terrain::Green;
}

bool isNativeRegionCandidateCell(const Cell& cell, Point nativeCell) noexcept
{
    (void) nativeCell;
    if (!isPassable(cell)) {
        return false;
    }
    return cell.terrain != Terrain::Shore;
}

bool isNativeRegionCandidateShoreSubTile(int variant, int subTile) noexcept
{
    if (variant < 0 || variant >= 42 || subTile < 0) {
        return false;
    }
    const int piece = variant + 1;
    const int width = shoreTemplateWidth(piece);
    if (width <= 0) {
        return false;
    }
    return shoreTemplateSlot(piece, subTile % width, subTile / width) == 'L';
}

bool isNativeRegionCandidateOverride(const IsoTileOverride& isoOverride) noexcept
{
    switch (isoOverride.terrain) {
    case Terrain::Water:
    case Terrain::Cliff:
    case Terrain::Pave:
        return false;
    case Terrain::Shore:
        return isNativeRegionCandidateShoreSubTile(isoOverride.variant, isoOverride.subTile);
    case Terrain::Land:
    case Terrain::Dirt:
    case Terrain::Rough:
    case Terrain::Green:
        return true;
    }
    return false;
}

bool isMapgenBlankCell(const Cell& cell)
{
    return isSolidLand(cell)
        && cell.overlay == Overlay::None
        && cell.terrainObject == TerrainObject::None;
}

bool isNativeBlankTile(const Cell& cell)
{
    return cell.terrain == Terrain::Land
        && cell.overlay == Overlay::None
        && cell.terrainObject == TerrainObject::None;
}

bool isGroundTileBlobCandidateCell(const Cell& cell, bool allowPave)
{
    // Engine SlopeIndex is a tile placement attribute; this model's slope is an export
    // subtile variant, so cliff/shore exclusion carries the unsafe-edge filter here.
    if (cell.spawnExclusion
        || cell.cliffShoreExclusion
        || cell.overlay != Overlay::None
        || cell.terrainObject != TerrainObject::None) {
        return false;
    }
    if (cell.terrain == Terrain::Pave) {
        return allowPave;
    }
    return isMapgenBlankCell(cell);
}

bool isVegetationDensityAnchorCell(const Cell& cell)
{
    return cell.terrain == Terrain::Cliff;
}

std::size_t mapIndex(const MapGrid& map, Point p)
{
    return static_cast<std::size_t>(p.y * map.width() + p.x);
}

Point nativeCellToMapPoint(Point cell, int mapWidth) noexcept
{
    return {
        (cell.x - cell.y + mapWidth - 1) / 2,
        (cell.x + cell.y - mapWidth - 1) / 2,
    };
}

bool isInsideNativeCellDiamond(Point cell, Rect mapRect) noexcept
{
    const int workspaceSize = mapRect.w + mapRect.h + 1;
    if (cell.x < 0 || cell.y < 0 || cell.x >= workspaceSize || cell.y >= workspaceSize) {
        return false;
    }

    const int sum = cell.x + cell.y;
    return sum > mapRect.w
        && cell.x - cell.y < mapRect.w
        && cell.y - cell.x < mapRect.w
        && sum <= mapRect.w + 2 * mapRect.h;
}

Point nativeCellToSourcePoint(const MapGrid& map, Point nativeCell) noexcept
{
    const Rect mapRect = nativeSavedMapRect(map);
    const Rect localRect = nativeLocalRect(map);
    const Point mapPoint = nativeCellToMapPoint(nativeCell, mapRect.w);
    return {mapPoint.x - localRect.x, mapPoint.y - localRect.y};
}

Point sourcePointToNativeCell(const MapGrid& map, Point source) noexcept
{
    const Rect mapRect = nativeSavedMapRect(map);
    const Rect localRect = nativeLocalRect(map);
    const Point mapPoint{source.x + localRect.x, source.y + localRect.y};
    return {mapPoint.x + mapPoint.y + 1, mapRect.w - mapPoint.x + mapPoint.y};
}

Point nativeDisplayToIso(int dx, int row, int width)
{
    const int dy = row * 2 + (dx & 1);
    const int rx = (dx + dy) / 2 + 1;
    const int ry = dy - rx + width + 1;
    return {ry, rx};
}

std::uint64_t nativeIsoKey(Point iso) noexcept
{
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(iso.x)) << 32)
        | static_cast<std::uint32_t>(iso.y);
}

int nativeMapgenScratchTagForCell(Point nativeCell) noexcept
{
    return nativeCell.x + (nativeCell.y << 9);
}

bool canTraverseTreePatchCell(const Cell& cell)
{
    return !cell.spawnExclusion && !cell.cliffShoreExclusion;
}

bool canPlaceTreePatchCell(const Cell& cell)
{
    return isMapgenBlankCell(cell) && cell.terrain != Terrain::Rough;
}

enum class DecorationUnitRejection {
    None,
    Bounds,
    Occupied,
    Exclusion
};

DecorationUnitRejection decorationUnitRejection(const MapGrid& map, Point p)
{
    if (!map.inBounds(p)) {
        return DecorationUnitRejection::Bounds;
    }

    const Cell& cell = map.at(p);
    if (cell.overlay != Overlay::None || cell.terrainObject != TerrainObject::None) {
        return DecorationUnitRejection::Occupied;
    }
    if (!isPassable(cell) || cell.spawnExclusion || cell.cliffShoreExclusion) {
        return DecorationUnitRejection::Exclusion;
    }
    return DecorationUnitRejection::None;
}

int overlayValue(const Cell& cell)
{
    if (cell.overlay != Overlay::Ore && cell.overlay != Overlay::Gems) {
        return 0;
    }
    return std::max(1, cell.overlayValue);
}

int nativeTiberiumGroupForOverlay(int overlayTypeIndex) noexcept
{
    if (overlayTypeIndex >= 102 && overlayTypeIndex <= 113) {
        return 0;
    }
    if (overlayTypeIndex >= 27 && overlayTypeIndex <= 38) {
        return 1;
    }
    return -1;
}

int nativeTiberiumOverlayDataForNeighborCount(int count) noexcept
{
    constexpr std::array<int, 9> kOverlayDataByNeighborCount{{0, 1, 3, 4, 6, 7, 8, 10, 11}};
    return kOverlayDataByNeighborCount[static_cast<std::size_t>(std::clamp(count, 0, 8))];
}

void recalculateNativeTiberiumOverlayData(std::vector<NativeOverlayOverride>& overlays)
{
    std::unordered_map<std::uint64_t, int> tiberiumGroupByIso;
    tiberiumGroupByIso.reserve(overlays.size());
    for (const NativeOverlayOverride& overlay : overlays) {
        const int group = nativeTiberiumGroupForOverlay(overlay.overlayTypeIndex);
        if (group >= 0) {
            tiberiumGroupByIso[nativeIsoKey(overlay.iso)] = group;
        }
    }

    constexpr std::array<Point, 8> kAdjacent{{
        {0, -1}, {1, -1}, {1, 0}, {1, 1},
        {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
    }};
    for (NativeOverlayOverride& overlay : overlays) {
        const int group = nativeTiberiumGroupForOverlay(overlay.overlayTypeIndex);
        if (group < 0) {
            continue;
        }

        int sameTypeNeighbors = 0;
        for (Point offset : kAdjacent) {
            const Point neighbor{overlay.iso.x + offset.x, overlay.iso.y + offset.y};
            const auto it = tiberiumGroupByIso.find(nativeIsoKey(neighbor));
            if (it != tiberiumGroupByIso.end() && it->second == group) {
                ++sameTypeNeighbors;
            }
        }
        overlay.overlayData = nativeTiberiumOverlayDataForNeighborCount(sameTypeNeighbors);
    }
}

void hashByte(std::uint64_t& hash, std::uint8_t value)
{
    hash ^= value;
    hash *= kFnvPrime;
}

void hashU32(std::uint64_t& hash, std::uint32_t value)
{
    hashByte(hash, static_cast<std::uint8_t>(value & 0xFFu));
    hashByte(hash, static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    hashByte(hash, static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    hashByte(hash, static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

void hashI32(std::uint64_t& hash, int value)
{
    hashU32(hash, static_cast<std::uint32_t>(value));
}

void hashBool(std::uint64_t& hash, bool value)
{
    hashByte(hash, value ? 1 : 0);
}

std::string hex64(std::uint64_t value)
{
    char buffer[17]{};
    std::snprintf(buffer, sizeof(buffer), "%016llX", static_cast<unsigned long long>(value));
    return buffer;
}

void hashCellPosition(std::uint64_t& hash, const Cell& cell)
{
    hashI32(hash, cell.x);
    hashI32(hash, cell.y);
}

void hashPointPosition(std::uint64_t& hash, Point p)
{
    hashI32(hash, p.x);
    hashI32(hash, p.y);
}

int nativeLandTypeForTile(Terrain terrain, int variant, int subTile) noexcept
{
    switch (terrain) {
    case Terrain::Water: {
        const int waterVariant = std::clamp(variant, 0, 13);
        return (waterVariant == 6 || waterVariant == 7 || waterVariant == 13) ? 3 : 2;
    }
    case Terrain::Land:
        return 0;
    case Terrain::Shore: {
        const int piece = std::clamp(variant, 0, 41) + 1;
        const int width = std::max(1, shoreTemplateWidth(piece));
        const int slot = std::max(0, subTile);
        const char shoreSlot = shoreTemplateSlot(piece, slot % width, slot / width);
        if (shoreSlot == 'W') {
            return 6;
        }
        if (shoreSlot == 'L') {
            return 7;
        }
        return 0;
    }
    case Terrain::Rough:
        return 0;
    case Terrain::Dirt:
    case Terrain::Green:
        return 7;
    case Terrain::Pave:
        return 1;
    case Terrain::Cliff:
        return 3;
    }
    return 0;
}

int nativeLandTypeForTmpTerrainType(std::uint8_t terrainType) noexcept
{
    // dword_8288E4, indexed by the TMP slot TerrainType byte in
    // IsometricTileTypeClass::GetLandType (0x544BE0).
    static constexpr std::array<int, 16> kLandTypes{{
        0, 8, 8, 8, 8, 10, 9, 3, 3, 2, 6, 1, 1, 0, 7, 3,
    }};
    return terrainType < kLandTypes.size()
        ? kLandTypes[terrainType]
        : -1;
}

int nativeFinalLatLandTypeForTile(Terrain terrain, int variant, int subTile) noexcept
{
    if (terrain == Terrain::Rough) {
        return g_nativeTileTheater == 1 && variant != 0 ? 7 : 0;
    }
    return nativeLandTypeForTile(terrain, variant, subTile);
}

bool isNativeHeightSlopeIndex(int slopeIndex) noexcept
{
    return slopeIndex > 0 && slopeIndex < kNativeHeightSlopeCount;
}

bool isNativeHeightSlopeCell(const Cell& cell) noexcept
{
    return cell.landType == 3
        && isSolidLand(cell)
        && isNativeHeightSlopeIndex(cell.slope);
}

int nativeLandTypeForCell(const Cell& cell) noexcept
{
    if (isEncodedShorePiece(cell.slope)) {
        const ShorePiece piece = decodeShorePiece(cell.slope);
        return nativeLandTypeForTile(Terrain::Shore, piece.tile, piece.subTile);
    }
    if (isEncodedCliffPiece(cell.slope)) {
        return 3;
    }
    return nativeLandTypeForTile(cell.terrain, std::max(0, cell.slope), cell.height);
}

int nativeSlopeIndexForCell(const Cell& cell) noexcept
{
    if (isEncodedShorePiece(cell.slope) || isEncodedCliffPiece(cell.slope)) {
        return 0;
    }
    return isNativeHeightSlopeCell(cell) ? cell.slope : 0;
}

int nativeTileTypeIndexForTerrainValue(Terrain terrain, int variant) noexcept
{
    switch (terrain) {
    case Terrain::Water:
        return kNativeTileWaterBase + std::clamp(variant, 0, 13);
    case Terrain::Land:
        return kNativeTileClear;
    case Terrain::Shore:
        return kNativeTileShoreBase + std::clamp(variant, 0, 41);
    case Terrain::Dirt:
        return kNativeTileDirtBase + std::clamp(variant, 0, 16);
    case Terrain::Rough:
        return kNativeTileRoughBase + std::clamp(variant, 0, 16);
    case Terrain::Pave:
        return kNativeTilePaveBase + std::clamp(variant, 0, 16);
    case Terrain::Green:
        return nativeGreenTileBase() + std::clamp(variant, 0, 16);
    case Terrain::Cliff:
        return kNativeTileCliffBase + std::clamp(variant, 0, 39);
    }
    return kNativeTileClear;
}

int nativeTileTypeIndexForTerrain(const Cell& cell) noexcept
{
    if (isEncodedShorePiece(cell.slope)) {
        return kNativeTileShoreBase + std::clamp(decodeShorePiece(cell.slope).tile, 0, 41);
    }
    if (isEncodedCliffPiece(cell.slope)) {
        return kNativeTileCliffBase + std::clamp(decodeCliffPiece(cell.slope).tile, 0, 39);
    }
    if (isNativeHeightSlopeCell(cell)) {
        return kNativeTileRampBase + cell.slope - 1;
    }

    switch (cell.terrain) {
    case Terrain::Water:
        return nativeTileTypeIndexForTerrainValue(Terrain::Water, std::max(0, cell.slope));
    case Terrain::Land:
        return kNativeTileClear;
    case Terrain::Shore:
        return nativeTileTypeIndexForTerrainValue(Terrain::Shore, std::max(0, cell.slope));
    case Terrain::Dirt:
        return nativeTileTypeIndexForTerrainValue(Terrain::Dirt, std::max(0, cell.slope));
    case Terrain::Rough:
        return nativeTileTypeIndexForTerrainValue(Terrain::Rough, std::max(0, cell.slope));
    case Terrain::Pave:
        return nativeTileTypeIndexForTerrainValue(Terrain::Pave, std::max(0, cell.slope));
    case Terrain::Green:
        return nativeTileTypeIndexForTerrainValue(Terrain::Green, std::max(0, cell.slope));
    case Terrain::Cliff:
        return nativeTileTypeIndexForTerrainValue(Terrain::Cliff, std::max(0, cell.slope));
    }
    return kNativeTileClear;
}

int nativeTileTypeIndexForOverride(const IsoTileOverride& override) noexcept
{
    if (override.nativeTileTypeIndex >= 0) {
        return override.nativeTileTypeIndex;
    }
    if (isNativeHeightSlopeIndex(override.slopeIndex)) {
        return kNativeTileRampBase + override.slopeIndex - 1;
    }
    return nativeTileTypeIndexForTerrainValue(override.terrain, override.variant);
}

bool isNativeClearTileIndex(int tile) noexcept
{
    return tile == kNativeTileClear || tile == 0xFFFF;
}

bool isNativeCliffRangeTileIndex(int tile) noexcept
{
    return tile >= kNativeTileCliffBase && tile < kNativeTileShoreBase;
}

bool isNativeShoreTileIndex(int tile) noexcept
{
    return tile >= kNativeTileShoreBase && tile < kNativeTileRoughBase;
}

bool isNativeFlatSlopeBlockingTile(
    const NativeTheaterCatalog* catalog,
    int tile,
    int height) noexcept
{
    const auto inTileRange = [tile](int base, int count) {
        return base >= 0 && tile >= base && tile < base + count;
    };
    if (catalog == nullptr) {
        return isNativeCliffRangeTileIndex(tile);
    }
    if (inTileRange(catalog->cliffSetBase, 40)
        || inTileRange(catalog->cliffRampsBase, 20)
        || inTileRange(catalog->slopeSetPiecesBase, 10)
        || inTileRange(catalog->slopeSetPieces2Base, 10)
        || inTileRange(catalog->waterCavesBase, 4)
        || inTileRange(catalog->bridgeSetBase, 16)
        || inTileRange(catalog->woodBridgeSetBase, 16)
        || inTileRange(catalog->destroyableCliffsBase, 2)
        || inTileRange(catalog->waterCliffsBase, 28)) {
        return true;
    }

    const auto cornerBlocks = [&](int base, auto edgeBlocks) {
        if (!inTileRange(base, 4)) {
            return false;
        }
        if (tile != base && tile != base + 3) {
            return true;
        }
        return edgeBlocks(height);
    };
    return cornerBlocks(catalog->waterfallEastBase, [](int subTile) {
            return subTile != 0 && subTile != 4;
        })
        || cornerBlocks(catalog->waterfallWestBase, [](int subTile) {
            return subTile != 1 && subTile != 3;
        })
        || cornerBlocks(catalog->waterfallSouthBase, [](int subTile) {
            return subTile >= 2;
        })
        || cornerBlocks(catalog->waterfallNorthBase, [](int subTile) {
            return subTile != 2 && subTile != 3;
        });
}

bool isNativeGreenTileIndex(int tile) noexcept
{
    const int base = nativeGreenTileBase();
    return tile >= base && tile <= base + 16;
}

bool isNativePaveTileIndex(int tile) noexcept
{
    return tile >= kNativeTilePaveBase && tile <= kNativeTilePaveBase + 16;
}

bool isNativeMapgenWetOrGreenTileIndex(int tile) noexcept
{
    return isNativeShoreTileIndex(tile)
        || (tile >= kNativeTileWaterBase && tile < kNativeTileDirtBase)
        || isNativeGreenTileIndex(tile);
}

bool isNativeMapgenBlankOrGreenTileIndex(int tile) noexcept
{
    return isNativeClearTileIndex(tile) || isNativeGreenTileIndex(tile);
}

std::unordered_map<std::uint64_t, NativeMapgenScratchCellInfo> buildNativeMapgenScratchCellInfo(
    const MapGrid& map,
    const std::vector<IsoTileOverride>& isoTileOverrides)
{
    std::unordered_map<std::uint64_t, NativeMapgenScratchCellInfo> cells;
    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, isoTileOverrides);
    cells.reserve(iteratorCells.size() * 2);
    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        cells[nativeIsoKey(iteratorCell.cell)] = NativeMapgenScratchCellInfo{
            iteratorCell.cell,
            iteratorCell.isoTileTypeIndex,
            iteratorCell.level,
        };
    }
    return cells;
}

std::vector<Point> nativeMapgenWorkspaceScanOrder(const MapGrid& map)
{
    const Rect mapRect = nativeSavedMapRect(map);
    const int workspaceSize = mapRect.w + mapRect.h + 1;
    std::vector<Point> cells;
    cells.reserve(static_cast<std::size_t>(workspaceSize * workspaceSize / 2));
    for (int y = 0; y < workspaceSize; ++y) {
        for (int x = 0; x < workspaceSize; ++x) {
            const Point cell{x, y};
            if ((x != 0 || y != 0) && isInsideNativeCellDiamond(cell, mapRect)) {
                cells.push_back(cell);
            }
        }
    }
    return cells;
}

std::vector<Point> nativeMapCellIteratorScanOrder(const MapGrid& map)
{
    const Rect mapRect = nativeSavedMapRect(map);
    const int workspaceSize = mapRect.w + mapRect.h + 1;
    std::vector<Point> cells;
    cells.reserve(static_cast<std::size_t>(workspaceSize * workspaceSize / 2));
    int nextX = 1;
    int nextY = mapRect.w;
    int remainingOnDiagonal = mapRect.w - 1;
    const int maxIterations = std::max(1, workspaceSize * workspaceSize * 2);
    for (int i = 0; i < maxIterations; ++i) {
        const Point current{nextX, nextY};
        if (remainingOnDiagonal != 0) {
            ++nextX;
            --nextY;
            --remainingOnDiagonal;
        } else {
            const int oldNextY = nextY;
            const int oldNextX = nextX;
            nextX = oldNextY;
            nextY = oldNextX;
            if (((oldNextY - mapRect.w + oldNextX - 1) & 1) != 0) {
                remainingOnDiagonal = mapRect.w - 1;
                nextY = oldNextX + 1;
            } else {
                remainingOnDiagonal = mapRect.w - 2;
                nextX = oldNextY + 1;
            }
        }

        if (!isInsideNativeCellDiamond(current, mapRect)) {
            break;
        }
        cells.push_back(current);
    }
    return cells;
}

enum class NativeLatGroup {
    None,
    Dirt,
    Rough,
    Green,
    Pave,
};

NativeLatGroup nativeLatGroupForTerrain(Terrain terrain) noexcept
{
    switch (terrain) {
    case Terrain::Dirt:
        return NativeLatGroup::Dirt;
    case Terrain::Rough:
        return NativeLatGroup::Rough;
    case Terrain::Green:
        return NativeLatGroup::Green;
    case Terrain::Pave:
        return NativeLatGroup::Pave;
    default:
        return NativeLatGroup::None;
    }
}

bool isNativeLatTerrain(Terrain terrain) noexcept
{
    return nativeLatGroupForTerrain(terrain) != NativeLatGroup::None;
}

bool isNativeTileInLatGroup(int tile, NativeLatGroup group) noexcept
{
    switch (group) {
    case NativeLatGroup::Dirt:
        return tile >= kNativeTileDirtBase && tile <= kNativeTileDirtBase + 16;
    case NativeLatGroup::Rough:
        return tile >= kNativeTileRoughBase && tile <= kNativeTileRoughBase + 16;
    case NativeLatGroup::Green:
        return isNativeGreenTileIndex(tile)
            || isNativeShoreTileIndex(tile);
    case NativeLatGroup::Pave:
        return tile >= kNativeTilePaveBase && tile <= kNativeTilePaveBase + 16;
    default:
        return false;
    }
}

int nativeLatVariantForCell(
    const std::unordered_map<std::uint64_t, int>& tileByIso,
    Point iso,
    NativeLatGroup group,
    bool missingNeighborIsSameGroup = false,
    int missingNeighborTile = -1) noexcept
{
    int mask = 0;
    constexpr Point offsets[]{{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (int i = 0; i < 4; ++i) {
        const Point neighbor{iso.x + offsets[i].x, iso.y + offsets[i].y};
        const auto it = tileByIso.find(nativeIsoKey(neighbor));
        if (it == tileByIso.end()) {
            const bool missingMatchesGroup = missingNeighborTile >= 0
                ? isNativeTileInLatGroup(missingNeighborTile, group)
                : missingNeighborIsSameGroup && group == NativeLatGroup::Green;
            if (!missingMatchesGroup) {
                mask |= 1 << i;
            }
        } else if (!isNativeTileInLatGroup(it->second, group)) {
            mask |= 1 << i;
        }
    }
    return mask == 0 ? 0 : mask + 1;
}

void upsertIsoTileOverride(
    std::vector<IsoTileOverride>& overrides,
    std::unordered_map<std::uint64_t, std::size_t>& indexByIso,
    Point iso,
    Terrain terrain,
    int variant,
    int subTile,
    int level,
    bool iceGrowthAllowed)
{
    const int landType = nativeLandTypeForTile(terrain, variant, subTile);
    const std::uint64_t key = nativeIsoKey(iso);
    if (const auto it = indexByIso.find(key); it != indexByIso.end()) {
        IsoTileOverride& override = overrides[it->second];
        override.terrain = terrain;
        override.variant = variant;
        override.subTile = subTile;
        override.level = level;
        override.iceGrowthAllowed = iceGrowthAllowed;
        override.landType = landType;
        override.slopeIndex = 0;
        override.nativeTileTypeIndex = -1;
        return;
    }

    overrides.push_back({
        iso,
        terrain,
        variant,
        subTile,
        level,
        iceGrowthAllowed,
        landType,
    });
    indexByIso[key] = overrides.size() - 1;
}

void refreshNativeLatOverridesWithTileMap(
    std::vector<IsoTileOverride>& overrides,
    const std::unordered_map<std::uint64_t, int>* baseTileByIso,
    bool finalLandTypes = false,
    bool missingNeighborIsSameGroup = false,
    int missingNeighborTile = -1)
{
    std::unordered_map<std::uint64_t, int> tileByIso;
    tileByIso.reserve((baseTileByIso ? baseTileByIso->size() : 0) + overrides.size());
    if (baseTileByIso != nullptr) {
        tileByIso.insert(baseTileByIso->begin(), baseTileByIso->end());
    }
    for (const IsoTileOverride& override : overrides) {
        tileByIso[nativeIsoKey(override.iso)] = nativeTileTypeIndexForOverride(override);
    }

    for (IsoTileOverride& override : overrides) {
        if (!isNativeLatTerrain(override.terrain)) {
            continue;
        }
        override.variant = nativeLatVariantForCell(
            tileByIso,
            override.iso,
            nativeLatGroupForTerrain(override.terrain),
            missingNeighborIsSameGroup,
            missingNeighborTile);
        if (!finalLandTypes && (override.terrain == Terrain::Dirt || override.terrain == Terrain::Rough)) {
            override.landType = 0;
        } else {
            override.landType = finalLandTypes
                ? nativeFinalLatLandTypeForTile(override.terrain, override.variant, override.subTile)
                : nativeLandTypeForTile(override.terrain, override.variant, override.subTile);
        }
        tileByIso[nativeIsoKey(override.iso)] = nativeTileTypeIndexForOverride(override);
    }
}

void refreshNativeLatOverrides(
    std::vector<IsoTileOverride>& overrides,
    bool finalLandTypes = false,
    bool missingNeighborIsSameGroup = false,
    int missingNeighborTile = -1)
{
    refreshNativeLatOverridesWithTileMap(
        overrides,
        nullptr,
        finalLandTypes,
        missingNeighborIsSameGroup,
        missingNeighborTile);
}

void refreshNativeRampSmoothOverrides(
    const std::vector<NativeIteratorCell>& iteratorCells,
    std::vector<IsoTileOverride>& overrides)
{
    std::unordered_map<std::uint64_t, int> slopeByIso;
    slopeByIso.reserve(iteratorCells.size());
    for (const NativeIteratorCell& cell : iteratorCells) {
        slopeByIso[nativeIsoKey(cell.cell)] = cell.slopeIndex;
    }

    const int smoothBase = nativeRampSmoothTileBase();
    auto neighborHasNoSlope = [&slopeByIso](Point cell) {
        const auto it = slopeByIso.find(nativeIsoKey(cell));
        return it == slopeByIso.end() || it->second == 0;
    };

    // CellClass::SetupLAT_47CA80 uses two slope-specific cardinal neighbors to
    // select one of three smooth-ramp joins, or the plain ramp when both join.
    for (IsoTileOverride& override : overrides) {
        const int tile = nativeTileTypeIndexForOverride(override);
        const bool rampTile = tile >= kNativeTileRampBase && tile <= kNativeTileRampBase + 19;
        const bool smoothRampTile = tile >= smoothBase && tile <= smoothBase + 11;
        if ((!rampTile && !smoothRampTile) || !isNativeHeightSlopeIndex(override.slopeIndex)) {
            continue;
        }
        if (override.slopeIndex > 4) {
            override.nativeTileTypeIndex = kNativeTileRampBase + override.slopeIndex - 1;
            continue;
        }

        Point firstOffset;
        Point secondOffset;
        switch (override.slopeIndex) {
        case 1:
            firstOffset = {-1, 0};
            secondOffset = {1, 0};
            break;
        case 2:
            firstOffset = {0, -1};
            secondOffset = {0, 1};
            break;
        case 3:
            firstOffset = {1, 0};
            secondOffset = {-1, 0};
            break;
        default:
            firstOffset = {0, 1};
            secondOffset = {0, -1};
            break;
        }

        int mask = 0;
        if (neighborHasNoSlope({override.iso.x + firstOffset.x, override.iso.y + firstOffset.y})) {
            mask |= 1;
        }
        if (neighborHasNoSlope({override.iso.x + secondOffset.x, override.iso.y + secondOffset.y})) {
            mask |= 2;
        }
        override.nativeTileTypeIndex = mask == 0
            ? kNativeTileRampBase + override.slopeIndex - 1
            : smoothBase + (override.slopeIndex - 1) * 3 + mask - 1;
    }
}

void refreshNativeLatOverridesForMap(
    const MapGrid& map,
    std::vector<IsoTileOverride>& overrides,
    bool finalLandTypes = false,
    bool missingNeighborIsSameGroup = false,
    int missingNeighborTile = -1)
{
    std::unordered_map<std::uint64_t, int> baseTileByIso;
    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, overrides);
    baseTileByIso.reserve(iteratorCells.size());
    for (const NativeIteratorCell& cell : iteratorCells) {
        baseTileByIso[nativeIsoKey(cell.cell)] = cell.isoTileTypeIndex;
    }
    refreshNativeLatOverridesWithTileMap(
        overrides,
        &baseTileByIso,
        finalLandTypes,
        missingNeighborIsSameGroup,
        missingNeighborTile);
    refreshNativeRampSmoothOverrides(iteratorCells, overrides);
}

void upsertNativeObjectOverride(
    std::vector<NativeObjectOverride>& overrides,
    Point iso,
    int objectAbstractType,
    int objectTypeIndex,
    int altObjectAbstractType = -1,
    int altObjectTypeIndex = -1)
{
    const std::uint64_t key = nativeIsoKey(iso);
    for (NativeObjectOverride& override : overrides) {
        if (nativeIsoKey(override.iso) != key) {
            continue;
        }
        override.objectAbstractType = objectAbstractType;
        override.objectTypeIndex = objectTypeIndex;
        override.altObjectAbstractType = altObjectAbstractType;
        override.altObjectTypeIndex = altObjectTypeIndex;
        return;
    }

    overrides.push_back({iso, objectAbstractType, objectTypeIndex, altObjectAbstractType, altObjectTypeIndex});
}

int nativeGreenLatVariantForCell(const std::unordered_map<std::uint64_t, int>& tileByIso, Point iso) noexcept
{
    int mask = 0;
    constexpr Point offsets[]{{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (int i = 0; i < 4; ++i) {
        const Point neighbor{iso.x + offsets[i].x, iso.y + offsets[i].y};
        const auto it = tileByIso.find(nativeIsoKey(neighbor));
        if (it != tileByIso.end() && isNativeClearTileIndex(it->second)) {
            mask |= 1 << i;
        }
    }
    return mask == 0 ? 0 : std::min(mask + 1, 15);
}

int nativeOverlayTypeIndexForCell(const Cell& cell) noexcept
{
    switch (cell.overlay) {
    case Overlay::None:
    case Overlay::Tree:
    case Overlay::Tech:
    case Overlay::Start:
    case Overlay::NeutralBuilding:
    case Overlay::NeutralUnit:
    case Overlay::TerrainChain:
        return -1;
    case Overlay::Ore:
        return 102;
    case Overlay::Gems:
        return 103;
    case Overlay::Grass:
        return std::clamp(cell.overlayValue, 168, 177);
    case Overlay::Bridge:
        return cell.overlayValue <= 8 ? 24 : 25;
    }
    return -1;
}

bool isNativeTiberiumOverlayType(int overlayTypeIndex) noexcept
{
    return (overlayTypeIndex >= 27 && overlayTypeIndex <= 38)
        || (overlayTypeIndex >= 102 && overlayTypeIndex <= 113);
}

int nativeObjectAbstractTypeForCell(const Cell& cell) noexcept
{
    if (cell.terrainObject != TerrainObject::None
        || cell.overlay == Overlay::Tree
        || cell.overlay == Overlay::Tech
        || cell.overlay == Overlay::NeutralBuilding
        || cell.overlay == Overlay::NeutralUnit
        || cell.overlay == Overlay::TerrainChain) {
        return 0;
    }
    return -1;
}

int nativeObjectTypeIndexForCell(const Cell& cell) noexcept
{
    if (cell.terrainObject != TerrainObject::None) {
        return 100 + cell.terrainObjectValue;
    }
    switch (cell.overlay) {
    case Overlay::Tree:
        return 200 + cell.overlayValue;
    case Overlay::Tech:
        return 300 + cell.overlayValue;
    case Overlay::NeutralBuilding:
        return 400 + cell.overlayValue;
    case Overlay::NeutralUnit:
        return 500 + cell.overlayValue;
    case Overlay::TerrainChain:
        return 600 + cell.overlayValue;
    case Overlay::None:
    case Overlay::Ore:
    case Overlay::Gems:
    case Overlay::Start:
    case Overlay::Grass:
    case Overlay::Bridge:
        break;
    }
    return -1;
}

std::vector<NativeStageCell> buildNativeStageCells(
    const MapGrid& map,
    bool initializedClear,
    const std::vector<IsoTileOverride>* isoTileOverrides,
    const std::vector<NativeOverlayOverride>* nativeOverlayOverrides,
    const std::vector<NativeObjectOverride>* nativeObjectOverrides,
    const std::unordered_set<std::uint64_t>* mapgenSurfaceBlockedKeys,
    const std::unordered_map<std::uint64_t, int>* mapgenTempTags,
    const std::unordered_map<std::uint64_t, int>* mapgenZoneIds,
    bool mapgenDataActive,
    int mapgenDefaultTempTag,
    int mapgenDefaultZoneId)
{
    const Rect mapRect = nativeSavedMapRect(map);
    const Rect localRect = nativeLocalRect(map);
    std::unordered_map<std::uint64_t, const IsoTileOverride*> overrideByIso;
    if (isoTileOverrides != nullptr) {
        overrideByIso.reserve(isoTileOverrides->size());
        for (const IsoTileOverride& override : *isoTileOverrides) {
            overrideByIso[nativeIsoKey(override.iso)] = &override;
        }
    }
    std::unordered_map<std::uint64_t, const NativeOverlayOverride*> overlayByIso;
    if (nativeOverlayOverrides != nullptr) {
        overlayByIso.reserve(nativeOverlayOverrides->size());
        for (const NativeOverlayOverride& override : *nativeOverlayOverrides) {
            overlayByIso[nativeIsoKey(override.iso)] = &override;
        }
    }
    std::unordered_map<std::uint64_t, const NativeObjectOverride*> objectByIso;
    if (nativeObjectOverrides != nullptr) {
        objectByIso.reserve(nativeObjectOverrides->size());
        for (const NativeObjectOverride& override : *nativeObjectOverrides) {
            objectByIso[nativeIsoKey(override.iso)] = &override;
        }
    }
    std::vector<NativeStageCell> cells;
    cells.reserve(static_cast<std::size_t>(mapRect.w * mapRect.h / 2 + mapRect.w));

    for (int row = 0; row < mapRect.h; ++row) {
        for (int dx = 0; dx < mapRect.w * 2 - 1; ++dx) {
            const Point cellCoords = nativeDisplayToIso(dx, row, mapRect.w);
            if (!contains(mapRect, cellCoords)) {
                continue;
            }

            NativeStageCell projected;
            projected.cell = cellCoords;
            const std::uint64_t scratchKey = nativeIsoKey(cellCoords);
            if (mapgenDataActive) {
                projected.mapgenSurfaceBlocked = mapgenSurfaceBlockedKeys != nullptr && mapgenSurfaceBlockedKeys->find(scratchKey) != mapgenSurfaceBlockedKeys->end() ? 1 : 0;
                const auto tempIt = mapgenTempTags == nullptr ? std::unordered_map<std::uint64_t, int>::const_iterator{} : mapgenTempTags->find(scratchKey);
                projected.mapgenTempTag = mapgenTempTags != nullptr && tempIt != mapgenTempTags->end() ? tempIt->second : mapgenDefaultTempTag;
                const auto zoneIt = mapgenZoneIds == nullptr ? std::unordered_map<std::uint64_t, int>::const_iterator{} : mapgenZoneIds->find(scratchKey);
                projected.mapgenZoneId = mapgenZoneIds != nullptr && zoneIt != mapgenZoneIds->end() ? zoneIt->second : mapgenDefaultZoneId;
            }
            if (initializedClear) {
                cells.push_back(projected);
                continue;
            }
            projected.isoTileTypeIndex = kNativeTileWaterBase;

            const auto overrideIt = overrideByIso.find(nativeIsoKey(cellCoords));
            const IsoTileOverride* isoOverride = overrideIt == overrideByIso.end() ? nullptr : overrideIt->second;
            const Point mapPoint = nativeCellToMapPoint(cellCoords, mapRect.w);
            if (contains(localRect, mapPoint)) {
                const Point sourcePoint{mapPoint.x - localRect.x, mapPoint.y - localRect.y};
                if (map.inBounds(sourcePoint)) {
                    const Cell& source = map.at(sourcePoint);
                    projected.source = &source;
                    projected.isoTileTypeIndex = nativeTileTypeIndexForTerrain(source);
                    projected.overlayTypeIndex = nativeOverlayTypeIndexForCell(source);
                    projected.height = isEncodedShorePiece(source.slope)
                        ? std::clamp(decodeShorePiece(source.slope).subTile, 0, 255)
                        : std::clamp(source.height, 0, 255);
                    projected.level = std::clamp(source.level, 0, 255);
                    projected.slopeIndex = nativeSlopeIndexForCell(source);
                    projected.overlayData = overlayValue(source);
                    projected.iceGrowthAllowed = source.iceGrowthAllowed;
                    projected.landType = source.landType;
                    projected.objectAbstractType = nativeObjectAbstractTypeForCell(source);
                    projected.objectTypeIndex = nativeObjectTypeIndexForCell(source);
                    if ((source.overlay == Overlay::Tree || source.overlay == Overlay::Tech) && nativeObjectOverrides != nullptr) {
                        projected.objectAbstractType = -1;
                        projected.objectTypeIndex = -1;
                        projected.altObjectAbstractType = -1;
                        projected.altObjectTypeIndex = -1;
                    }
                    if ((source.overlay == Overlay::Ore || source.overlay == Overlay::Gems || source.overlay == Overlay::Grass) && nativeOverlayOverrides != nullptr) {
                        projected.overlayTypeIndex = -1;
                        projected.overlayData = 0;
                    }
                }
            }
            if (isoOverride != nullptr) {
                projected.isoTileTypeIndex = nativeTileTypeIndexForOverride(*isoOverride);
                projected.height = std::clamp(isoOverride->subTile, 0, 255);
                projected.level = std::clamp(isoOverride->level, 0, 255);
                projected.slopeIndex = std::clamp(isoOverride->slopeIndex, 0, kNativeHeightSlopeCount - 1);
                projected.iceGrowthAllowed = isoOverride->iceGrowthAllowed;
                projected.landType = isoOverride->landType;
            }
            const auto overlayIt = overlayByIso.find(nativeIsoKey(cellCoords));
            if (overlayIt != overlayByIso.end()) {
                projected.overlayTypeIndex = overlayIt->second->overlayTypeIndex;
                projected.overlayData = overlayIt->second->overlayData;
                if (overlayIt->second->landTypeOverride >= 0) {
                    projected.landType = overlayIt->second->landTypeOverride;
                }
            }
            const auto objectIt = objectByIso.find(nativeIsoKey(cellCoords));
            if (objectIt != objectByIso.end()) {
                projected.objectAbstractType = objectIt->second->objectAbstractType;
                projected.objectTypeIndex = objectIt->second->objectTypeIndex;
                projected.altObjectAbstractType = objectIt->second->altObjectAbstractType;
                projected.altObjectTypeIndex = objectIt->second->altObjectTypeIndex;
            }

            cells.push_back(projected);
        }
    }

    std::sort(cells.begin(), cells.end(), [](const NativeStageCell& lhs, const NativeStageCell& rhs) {
        if (lhs.cell.y != rhs.cell.y) {
            return lhs.cell.y < rhs.cell.y;
        }
        return lhs.cell.x < rhs.cell.x;
    });
    return cells;
}

std::vector<NativeIteratorCell> buildNativeIteratorCells(const MapGrid& map, const std::vector<IsoTileOverride>& isoTileOverrides)
{
    const Rect mapRect = nativeSavedMapRect(map);
    const Rect localRect = nativeLocalRect(map);
    const int workspaceSize = mapRect.w + mapRect.h + 1;
    const int diamondBoundsMin = mapRect.w;
    const int diamondBoundsMax = mapRect.w + 2 * mapRect.h;

    std::unordered_map<std::uint64_t, const IsoTileOverride*> overrideByIso;
    overrideByIso.reserve(isoTileOverrides.size());
    for (const IsoTileOverride& override : isoTileOverrides) {
        overrideByIso[nativeIsoKey(override.iso)] = &override;
    }

    auto workspaceInRange = [&](Point cell) {
        return cell.x >= 0 && cell.y >= 0 && cell.x < workspaceSize && cell.y < workspaceSize;
    };
    auto insideNativeDiamond = [&](Point cell) {
        if (!workspaceInRange(cell)) {
            return false;
        }
        const int sum = cell.x + cell.y;
        return sum > diamondBoundsMin
            && cell.x - cell.y < diamondBoundsMin
            && cell.y - cell.x < diamondBoundsMin
            && sum <= diamondBoundsMax;
    };

    std::vector<NativeIteratorCell> cells;
    cells.reserve(static_cast<std::size_t>(workspaceSize * workspaceSize / 2));

    int nextX = 1;
    int nextY = mapRect.w;
    int currentY = mapRect.w - 1;
    const int maxIterations = std::max(1, workspaceSize * workspaceSize * 2);
    for (int i = 0; i < maxIterations; ++i) {
        const Point current{nextX, nextY};
        if (currentY != 0) {
            ++nextX;
            --nextY;
            --currentY;
        } else {
            const int oldNextY = nextY;
            const int oldNextX = nextX;
            nextX = oldNextY;
            nextY = oldNextX;
            if (((oldNextY - mapRect.w + oldNextX - 1) & 1) != 0) {
                currentY = mapRect.w - 1;
                nextY = oldNextX + 1;
            } else {
                currentY = mapRect.w - 2;
                nextX = oldNextY + 1;
            }
        }

        if (!insideNativeDiamond(current)) {
            break;
        }

        NativeIteratorCell projected;
        projected.cell = current;
        const auto overrideIt = overrideByIso.find(nativeIsoKey(current));
        const IsoTileOverride* isoOverride = overrideIt == overrideByIso.end() ? nullptr : overrideIt->second;
        const Point mapPoint = nativeCellToMapPoint(current, mapRect.w);
        if (contains(localRect, mapPoint)) {
            const Point sourcePoint{mapPoint.x - localRect.x, mapPoint.y - localRect.y};
            if (map.inBounds(sourcePoint)) {
                const Cell& source = map.at(sourcePoint);
                projected.source = &source;
                projected.isoTileTypeIndex = nativeTileTypeIndexForTerrain(source);
                projected.height = isEncodedShorePiece(source.slope)
                    ? std::clamp(decodeShorePiece(source.slope).subTile, 0, 255)
                    : std::clamp(source.height, 0, 255);
                projected.level = std::clamp(source.level, 0, 255);
                projected.slopeIndex = nativeSlopeIndexForCell(source);
                projected.landType = source.landType;
                projected.iceGrowthAllowed = source.iceGrowthAllowed;
            }
        }
        if (isoOverride != nullptr) {
            projected.height = std::clamp(isoOverride->subTile, 0, 255);
            projected.level = std::clamp(isoOverride->level, 0, 255);
            projected.slopeIndex = std::clamp(isoOverride->slopeIndex, 0, kNativeHeightSlopeCount - 1);
            projected.landType = isoOverride->landType;
            projected.iceGrowthAllowed = isoOverride->iceGrowthAllowed;
            projected.isoTileTypeIndex = nativeTileTypeIndexForOverride(*isoOverride);
        }
        cells.push_back(projected);
    }
    return cells;
}

std::uint32_t packNativeCell(Point cell) noexcept;

struct NativePondTraceSnapshot {
    std::uint64_t hash = kFnvOffset;
    std::uint32_t slotCount = 0;
    std::vector<std::uint32_t> detail;
};

struct NativeZoneWorkspaceTraceSnapshot {
    std::uint64_t hash = kFnvOffset;
    std::uint32_t slotCount = 0;
    std::uint32_t regionCount = 0;
    std::uint32_t nextZoneId = 0;
    std::uint32_t valueCount = 0;
    std::vector<std::uint32_t> detail;
};

NativeZoneWorkspaceTraceSnapshot captureNativeZoneWorkspaceTraceSnapshot(
    const MapGrid& map,
    const std::vector<IsoTileOverride>& isoTileOverrides,
    const std::unordered_map<std::uint64_t, int>& scratchIds,
    const std::unordered_map<std::uint64_t, int>& zoneIds,
    const std::unordered_map<std::uint64_t, int>& cacheFlags,
    int defaultScratchId,
    int defaultZoneId,
    int defaultCacheFlags,
    const std::vector<int>& zoneLevels,
    const std::vector<int>& zoneCellCounts,
    const std::vector<unsigned char>& zoneWetFlags,
    Point invalidCellPoint,
    int invalidCellTile,
    bool includeDetail)
{
    constexpr std::uint32_t kPreambleWords = 8;
    constexpr std::uint32_t kWordsPerSlot = 10;
    constexpr std::uint32_t kWordsPerRegionHeader = 8;
    const Rect mapRect = nativeSavedMapRect(map);
    const int mapSize = mapRect.w + mapRect.h + 1;
    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, isoTileOverrides);
    std::unordered_map<std::uint64_t, const NativeIteratorCell*> cellByIso;
    cellByIso.reserve(iteratorCells.size());
    for (const NativeIteratorCell& cell : iteratorCells) {
        cellByIso[nativeIsoKey(cell.cell)] = &cell;
    }

    NativeZoneWorkspaceTraceSnapshot snapshot;
    snapshot.slotCount = static_cast<std::uint32_t>(mapSize * mapSize);
    snapshot.regionCount = static_cast<std::uint32_t>(zoneLevels.size());
    snapshot.nextZoneId = snapshot.regionCount;
    snapshot.valueCount = kPreambleWords
        + snapshot.slotCount * kWordsPerSlot
        + snapshot.regionCount * kWordsPerRegionHeader;
    if (includeDetail) {
        snapshot.detail.reserve(
            kPreambleWords
            + static_cast<std::size_t>(snapshot.slotCount) * kWordsPerSlot
            + static_cast<std::size_t>(snapshot.regionCount) * kWordsPerRegionHeader);
    }
    const auto append = [&](std::uint32_t value) {
        hashU32(snapshot.hash, value);
        if (includeDetail) {
            snapshot.detail.push_back(value);
        }
    };

    append(static_cast<std::uint32_t>(mapSize));
    append(snapshot.slotCount);
    append(snapshot.regionCount);
    append(snapshot.nextZoneId);
    append(packNativeCell(invalidCellPoint));
    append(static_cast<std::uint32_t>(invalidCellTile));
    append(kWordsPerSlot);
    append(kWordsPerRegionHeader);

    std::vector<Point> zoneSeeds(snapshot.regionCount);
    std::vector<unsigned char> hasZoneSeed(snapshot.regionCount, 0);
    for (int y = 0; y < mapSize; ++y) {
        for (int x = 0; x < mapSize; ++x) {
            const Point point{x, y};
            const std::uint64_t key = nativeIsoKey(point);
            const auto cellIt = cellByIso.find(key);
            const NativeIteratorCell* cell = cellIt == cellByIso.end() ? nullptr : cellIt->second;
            const auto scratchIt = scratchIds.find(key);
            const auto zoneIt = zoneIds.find(key);
            const auto cacheIt = cacheFlags.find(key);
            const int scratchId = scratchIt == scratchIds.end() ? defaultScratchId : scratchIt->second;
            const int zoneId = zoneIt == zoneIds.end() ? defaultZoneId : zoneIt->second;
            const int cachedFlags = cacheIt == cacheFlags.end() ? defaultCacheFlags : cacheIt->second;
            if (zoneId >= 0
                && zoneId < static_cast<int>(snapshot.regionCount)
                && !hasZoneSeed[static_cast<std::size_t>(zoneId)]) {
                zoneSeeds[static_cast<std::size_t>(zoneId)] = point;
                hasZoneSeed[static_cast<std::size_t>(zoneId)] = 1;
            }
            const std::uint32_t flags = 1u;
            append(packNativeCell(point));
            append(cell ? packNativeCell(cell->cell) : 0u);
            append(static_cast<std::uint32_t>(cell ? cell->isoTileTypeIndex : -1));
            append(static_cast<std::uint32_t>(cell ? cell->height : -1));
            append(static_cast<std::uint32_t>(cell ? cell->level : -1));
            append(static_cast<std::uint32_t>(cell ? cell->slopeIndex : -1));
            append(static_cast<std::uint32_t>(zoneId));
            append(static_cast<std::uint32_t>(scratchId));
            append(static_cast<std::uint32_t>(cell ? cachedFlags : -1));
            append(flags);
        }
    }

    for (std::size_t index = 0; index < zoneLevels.size(); ++index) {
        const bool wet = index < zoneWetFlags.size() && zoneWetFlags[index] != 0;
        const int cellCount = index < zoneCellCounts.size() ? zoneCellCounts[index] : 0;
        append(static_cast<std::uint32_t>(index));
        append(static_cast<std::uint32_t>(index));
        append(static_cast<std::uint32_t>(cellCount));
        append(static_cast<std::uint32_t>(zoneLevels[index]));
        append((wet ? 1u : 0u) | (1u << 16));
        append(1u);
        append(hasZoneSeed[index] ? packNativeCell(zoneSeeds[index]) : 0u);
        append(0u);
    }
    return snapshot;
}

NativePondTraceSnapshot captureNativePondTraceSnapshot(
    const MapGrid& map,
    const std::vector<IsoTileOverride>& isoTileOverrides,
    const std::unordered_map<std::uint64_t, int>& zoneIds,
    int defaultZoneId,
    bool mapgenDataActive,
    Point invalidCellPoint,
    int invalidCellTile,
    bool includeDetail)
{
    const Rect mapRect = nativeSavedMapRect(map);
    const int mapSize = mapRect.w + mapRect.h + 1;
    const std::vector<NativeIteratorCell> iteratorCells = buildNativeIteratorCells(map, isoTileOverrides);
    std::unordered_map<std::uint64_t, const NativeIteratorCell*> cellByIso;
    cellByIso.reserve(iteratorCells.size());
    for (const NativeIteratorCell& cell : iteratorCells) {
        cellByIso[nativeIsoKey(cell.cell)] = &cell;
    }

    NativePondTraceSnapshot snapshot;
    snapshot.slotCount = static_cast<std::uint32_t>(mapSize * mapSize + 1);
    if (includeDetail) {
        snapshot.detail.reserve(static_cast<std::size_t>(snapshot.slotCount) * 7);
    }
    const auto appendState = [&](Point point, int tile, int height, int level, int slope, int zone, std::uint32_t kind) {
        const std::array<std::uint32_t, 7> values{
            packNativeCell(point),
            static_cast<std::uint32_t>(tile),
            static_cast<std::uint32_t>(height),
            static_cast<std::uint32_t>(level),
            static_cast<std::uint32_t>(slope),
            static_cast<std::uint32_t>(zone),
            kind,
        };
        for (std::uint32_t value : values) {
            hashU32(snapshot.hash, value);
            if (includeDetail) {
                snapshot.detail.push_back(value);
            }
        }
    };

    for (int y = 0; y < mapSize; ++y) {
        for (int x = 0; x < mapSize; ++x) {
            const Point point{x, y};
            const auto cellIt = cellByIso.find(nativeIsoKey(point));
            if (cellIt == cellByIso.end()) {
                appendState(point, -1, -1, -1, -1, -1, 0);
                continue;
            }
            const NativeIteratorCell& cell = *cellIt->second;
            int zone = -1;
            if (mapgenDataActive) {
                const auto zoneIt = zoneIds.find(nativeIsoKey(point));
                zone = zoneIt == zoneIds.end() ? defaultZoneId : zoneIt->second;
            }
            appendState(
                point,
                cell.isoTileTypeIndex,
                cell.height,
                cell.level,
                cell.slopeIndex,
                zone,
                1);
        }
    }
    appendState(invalidCellPoint, invalidCellTile, 0, 0, 0, -1, 2);
    return snapshot;
}

std::uint32_t packNativeCell(Point cell) noexcept
{
    return static_cast<std::uint32_t>(static_cast<std::uint16_t>(cell.x))
        | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(cell.y)) << 16);
}

void hashNativeCell(std::uint32_t& hash, Point cell) noexcept
{
    const std::uint32_t packed = packNativeCell(cell);
    for (int shift = 0; shift < 32; shift += 8) {
        hash ^= (packed >> shift) & 0xFFu;
        hash *= 16777619u;
    }
}

std::vector<Point> collectNativeZoneBoundaryCells(
    const std::vector<NativeIteratorCell>& iteratorCells,
    Rect mapRect,
    const std::unordered_map<std::uint64_t, int>& zoneIds,
    int defaultZoneId,
    int targetZoneId,
    int regionIndex,
    int regionCount,
    int regionCellCount,
    Point firstRegionCell,
    Point* invalidCellPoint = nullptr,
    bool forceTrace = false)
{
    const bool traceBoundary = native_trace::active() && (forceTrace
        || native_trace::currentFunctionId() == native_trace::BuildRegionNeighborList
        || native_trace::currentFunctionId() == native_trace::ProcessRegionBridgesAndCliffs);
    native_trace::EventData traceData;
    traceData.regionIndex = regionIndex;
    traceData.regionId = targetZoneId;
    traceData.regionCount = regionCount;
    traceData.regionCellCount = regionCellCount;
    traceData.cellX = static_cast<std::int16_t>(firstRegionCell.x);
    traceData.cellY = static_cast<std::int16_t>(firstRegionCell.y);
    if (traceBoundary) {
        native_trace::enterDetailed(0x58D410, native_trace::CollectZoneBoundaryCells, traceData);
    }

    const auto zoneFor = [&](Point cell) {
        const auto it = zoneIds.find(nativeIsoKey(cell));
        return it == zoneIds.end() ? defaultZoneId : it->second;
    };
    constexpr std::array<Point, 8> kNeighbors{{
        {0, -1}, {1, -1}, {1, 0}, {1, 1},
        {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
    }};

    std::vector<Point> boundary;
    boundary.reserve(static_cast<std::size_t>(std::max(0, regionCellCount)));
    for (const NativeIteratorCell& iteratorCell : iteratorCells) {
        if (zoneFor(iteratorCell.cell) != targetZoneId) {
            continue;
        }
        bool isBoundary = false;
        for (Point offset : kNeighbors) {
            const Point neighbor{iteratorCell.cell.x + offset.x, iteratorCell.cell.y + offset.y};
            if (!isInsideNativeCellDiamond(neighbor, mapRect)) {
                if (invalidCellPoint != nullptr) {
                    *invalidCellPoint = neighbor;
                }
                continue;
            }
            isBoundary = isBoundary || zoneFor(neighbor) != targetZoneId;
        }
        if (isBoundary) {
            boundary.push_back(iteratorCell.cell);
        }
    }

    if (traceBoundary) {
        std::uint32_t orderedHash = 2166136261u;
        std::vector<std::uint32_t> detailValues;
        if (native_trace::detailRequested()) {
            detailValues.reserve(boundary.size());
        }
        traceData.args[3] = 0xFFFFFFFFu;
        traceData.args[4] = 0xFFFFFFFFu;
        traceData.args[5] = 0xFFFFFFFFu;
        for (std::size_t index = 0; index < boundary.size(); ++index) {
            if (index < 3) {
                traceData.args[3 + index] = packNativeCell(boundary[index]);
            }
            if (native_trace::detailRequested()) {
                detailValues.push_back(packNativeCell(boundary[index]));
            }
            hashNativeCell(orderedHash, boundary[index]);
        }
        traceData.result[0] = static_cast<std::uint32_t>(boundary.size());
        traceData.result[1] = orderedHash;
        traceData.result[2] = boundary.empty() ? 0xFFFFFFFFu : packNativeCell(boundary.front());
        traceData.result[3] = boundary.empty() ? 0xFFFFFFFFu : packNativeCell(boundary.back());
        native_trace::exitDetailed(
            iteratorCells.empty() ? 0x58D594 : 0x58D58B,
            native_trace::CollectZoneBoundaryCells,
            traceData,
            native_trace::DetailOrderedPackedCells,
            detailValues);
    }
    return boundary;
}

bool isNativeCellWithinUsableArea(
    Point cell,
    const NativeIteratorCell& iteratorCell,
    Rect mapRect,
    Rect visibleRect) noexcept
{
    int level = iteratorCell.level;
    const int sum = cell.x + cell.y;
    if (iteratorCell.slopeIndex != 0
        && sum < level + mapRect.w + 2 * visibleRect.y + 4) {
        ++level;
    }
    return sum > level + mapRect.w + 2 * visibleRect.y
        && sum <= level + mapRect.w + 2 * (visibleRect.y + visibleRect.h) + 2
        && cell.x - cell.y < 2 * (visibleRect.x + visibleRect.w) - mapRect.w
        && cell.y - cell.x < mapRect.w - 2 * visibleRect.x;
}

bool hasEnoughNativeCellsInZone(
    Point topLeft,
    int width,
    int height,
    int targetZoneId,
    int required,
    Rect mapRect,
    const std::unordered_map<std::uint64_t, int>& zoneIds,
    int defaultZoneId)
{
    if (width <= 0 || height <= 0) {
        return false;
    }
    int matching = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Point cell{topLeft.x + x, topLeft.y + y};
            if (!isInsideNativeCellDiamond(cell, mapRect)) {
                continue;
            }
            const auto it = zoneIds.find(nativeIsoKey(cell));
            const int zoneId = it == zoneIds.end() ? defaultZoneId : it->second;
            if (zoneId == targetZoneId && ++matching >= required) {
                return true;
            }
        }
    }
    return false;
}

struct NativeCliffNeighborMaskResult {
    int mask = 0;
    bool centerAccepted = false;
};

NativeCliffNeighborMaskResult nativeCliffNeighborMask(
    Point cell,
    int highRegionId,
    float fallbackBias,
    Rect mapRect,
    const std::unordered_map<std::uint64_t, int>& zoneIds,
    int defaultZoneId)
{
    const int required = nativeDoubleToInt(
        (15.0 - 5.0) * (1.0 - static_cast<double>(fallbackBias)) + 5.0);
    const Point centerTopLeft{cell.x - 2, cell.y - 2};
    if (!hasEnoughNativeCellsInZone(
            centerTopLeft,
            5,
            5,
            highRegionId,
            required,
            mapRect,
            zoneIds,
            defaultZoneId)) {
        return {};
    }

    constexpr std::array<int, 9> kNeighborMasks{{
        0x40, 0x80, 0x01,
        0x20, 0x00, 0x02,
        0x10, 0x08, 0x04,
    }};
    NativeCliffNeighborMaskResult result;
    result.centerAccepted = true;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            if (row == 1 && column == 1) {
                continue;
            }
            const Point blockTopLeft{
                centerTopLeft.x + (column - 1) * 5,
                centerTopLeft.y + (row - 1) * 5,
            };
            if (hasEnoughNativeCellsInZone(
                    blockTopLeft,
                    5,
                    5,
                    highRegionId,
                    required,
                    mapRect,
                    zoneIds,
                    defaultZoneId)) {
                result.mask |= kNeighborMasks[static_cast<std::size_t>(row * 3 + column)];
            }
        }
    }
    return result;
}

void dumpNativeStageCellsIfRequested(const std::string& stageName, const std::vector<NativeStageCell>& nativeCells)
{
    const char* dumpPath = std::getenv(kNativeStageCellDumpPathEnv);
    if (dumpPath == nullptr || dumpPath[0] == '\0') {
        return;
    }

    std::filesystem::path path{dumpPath};
    path += ".";
    path += stageName.empty() ? "unnamed" : stageName;
    path += ".csv";
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path, std::ios::binary);
    out << "stage,index,x,y,isoTileTypeIndex,overlayTypeIndex,smudgeTypeIndex,height,level,slopeIndex,"
           "overlayData,smudgeData,iceGrowth,landType,objectAbstractType,objectTypeIndex,"
           "altObjectAbstractType,altObjectTypeIndex,mapgenSurfaceBlocked,mapgenTempTag,mapgenZoneId\n";
    int index = 0;
    for (const NativeStageCell& cell : nativeCells) {
        out << stageName << ','
            << index++ << ','
            << cell.cell.x << ','
            << cell.cell.y << ','
            << cell.isoTileTypeIndex << ','
            << cell.overlayTypeIndex << ','
            << cell.smudgeTypeIndex << ','
            << static_cast<int>(static_cast<unsigned char>(cell.height)) << ','
            << static_cast<int>(static_cast<unsigned char>(cell.level)) << ','
            << cell.slopeIndex << ','
            << cell.overlayData << ','
            << cell.smudgeData << ','
            << static_cast<int>(static_cast<unsigned char>(cell.iceGrowthAllowed)) << ','
            << cell.landType << ','
            << cell.objectAbstractType << ','
            << cell.objectTypeIndex << ','
            << cell.altObjectAbstractType << ','
            << cell.altObjectTypeIndex << ','
            << cell.mapgenSurfaceBlocked << ','
            << cell.mapgenTempTag << ','
            << cell.mapgenZoneId << '\n';
    }
}

int cardinalBoundaryVariant(const MapGrid& map, Point p, bool (*matches)(const Cell&))
{
    int mask = 0;
    if (map.inBounds({p.x, p.y - 1}) && matches(map.at({p.x, p.y - 1}))) {
        mask |= 1;
    }
    if (map.inBounds({p.x + 1, p.y}) && matches(map.at({p.x + 1, p.y}))) {
        mask |= 2;
    }
    if (map.inBounds({p.x, p.y + 1}) && matches(map.at({p.x, p.y + 1}))) {
        mask |= 4;
    }
    if (map.inBounds({p.x - 1, p.y}) && matches(map.at({p.x - 1, p.y}))) {
        mask |= 8;
    }
    return mask;
}

bool isLandBoundaryCell(const Cell& cell)
{
    return isSolidLand(cell) || cell.terrain == Terrain::Cliff;
}

bool isLowerCliffBoundaryCell(const Cell& cell)
{
    return cell.terrain != Terrain::Cliff && !isWet(cell);
}

bool isNativeShorePiece(const Cell& cell)
{
    return cell.terrain == Terrain::Shore;
}

bool isNativeGreenAnchor(const Cell& cell)
{
    return cell.terrain == Terrain::Green;
}

int cliffPlateauDirection(double angle)
{
    const double dx = std::cos(angle);
    const double dy = std::sin(angle);
    if (std::abs(dx) >= std::abs(dy)) {
        return dx >= 0.0 ? 2 : 6;
    }
    return dy >= 0.0 ? 4 : 0;
}

Point directionOffset(int direction)
{
    switch (direction) {
    case 0:
        return {0, -1};
    case 2:
        return {1, 0};
    case 4:
        return {0, 1};
    case 6:
        return {-1, 0};
    default:
        return {0, 0};
    }
}

} // namespace
