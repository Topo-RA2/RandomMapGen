#include "rmg/native_trace.h"

#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace rmg::native_trace {
namespace {

#pragma pack(push, 1)
struct FxsaveSummary {
    std::uint16_t controlWord = 0;
    std::uint16_t statusWord = 0;
    std::uint16_t tagWord = 0;
    std::uint16_t opcode = 0;
    std::uint32_t mxcsr = 0;
    std::uint32_t mxcsrMask = 0;
    unsigned char st[8][10]{};
};

struct Event {
    std::uint64_t sequence = 0;
    std::uint32_t site = 0;
    std::uint16_t functionId = 0;
    std::uint8_t event = 0;
    std::uint8_t depth = 0;
    std::uint32_t callId = 0;
    std::uint32_t parentCallId = 0;
    std::uint32_t callerReturn = 0;
    std::uint32_t eax = 0;
    std::uint32_t ebx = 0;
    std::uint32_t ecx = 0;
    std::uint32_t edx = 0;
    std::uint32_t esi = 0;
    std::uint32_t edi = 0;
    std::uint32_t ebp = 0;
    std::uint32_t esp = 0;
    std::uint32_t eflags = 0;
    std::uint32_t args[6]{};
    std::uint32_t result[4]{};
    std::int32_t regionIndex = -1;
    std::int32_t regionId = -1;
    std::int32_t regionCount = 0;
    std::int32_t regionCellCount = 0;
    std::int16_t cellX = -1;
    std::int16_t cellY = -1;
    std::uint16_t resultKind = ResultLegacy;
    std::uint32_t rngNext1 = 0;
    std::uint32_t rngNext2 = 0;
    std::uint32_t rngRaw = 0;
    std::uint32_t gaussianSpareFlag = 0;
    std::uint64_t gaussianSpareBits = 0;
    FxsaveSummary fx;
};

struct Header {
    std::uint32_t magic = 0x31544652; // RFT1
    std::uint32_t version = 3;
    std::uint32_t eventSize = sizeof(Event);
    std::uint32_t eventCount = 0;
    std::uint32_t overflow = 0;
    std::uint32_t detailBytes = 0;
};

struct DetailHeader {
    std::uint32_t eventIndex = 0;
    std::uint32_t callId = 0;
    std::uint16_t functionId = 0;
    std::uint8_t event = 0;
    std::uint8_t kind = 0;
    std::uint32_t valueCount = 0;
};
#pragma pack(pop)

static_assert(sizeof(FxsaveSummary) == 0x60);
static_assert(sizeof(Event) == 246);
static_assert(sizeof(Header) == 24);
static_assert(sizeof(DetailHeader) == 16);

constexpr std::size_t kCapacity = 262144;
constexpr std::size_t kMaxDepth = 256;
constexpr std::size_t kDetailCapacity = 8 * 1024 * 1024;
constexpr std::uint16_t kNativeX87ControlWord = 0x0E7F;
constexpr std::uint16_t kFirstCoreFunctionId = 256;
constexpr std::size_t kCoreFunctionTableSize = 512;

struct Frame {
    std::uint32_t callId = 0;
    std::uint16_t functionId = 0;
    EventData data;
};

std::string g_path;
std::vector<Event> g_events;
std::vector<std::uint8_t> g_details;
std::array<Frame, kMaxDepth> g_stack{};
std::size_t g_depth = 0;
std::uint32_t g_nextCallId = 0;
bool g_active = false;
bool g_started = false;
bool g_overflow = false;
std::uint32_t g_detailCallId = 0;
std::uint16_t g_detailFunctionId = 0;
std::uint16_t g_stopAfterFunctionId = 0;
std::uint32_t g_stopAfterEventCount = 0;
std::uint32_t g_skipEventCount = 0;
std::uint16_t g_startAtFunctionId = 0;
std::uint32_t g_seenEventCount = 0;
std::uint32_t g_rngNext1 = 0;
std::uint32_t g_rngNext2 = 0;
std::uint32_t g_gaussianSpareFlag = 0;
std::uint64_t g_gaussianSpareBits = 0;
std::array<bool, kCoreFunctionTableSize> g_coreFunctions{};

void loadCoreFunctionFilter() noexcept
{
    g_coreFunctions.fill(false);
    const char* value = std::getenv("RMG_NATIVE_RANDOM_MAP_CORE_TRACE_FUNCTIONS");
    if (value == nullptr) {
        return;
    }

    while (*value != '\0') {
        while (*value == ' ' || *value == '\t' || *value == ',') {
            ++value;
        }
        if (*value == '\0') {
            break;
        }
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 0);
        if (end == value) {
            while (*value != '\0' && *value != ',') {
                ++value;
            }
            continue;
        }
        if (parsed >= kFirstCoreFunctionId && parsed < g_coreFunctions.size()) {
            g_coreFunctions[parsed] = true;
        }
        value = end;
    }
}

std::uint32_t detailCallIdFromEnvironment() noexcept
{
    const char* value = std::getenv("RMG_NATIVE_RANDOM_MAP_FUNCTION_TRACE_DETAIL_CALL_ID");
    if (value == nullptr || value[0] == '\0') {
        return 0;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 0);
    return end != nullptr && end[0] == '\0' && parsed <= 0xFFFFFFFFul
        ? static_cast<std::uint32_t>(parsed)
        : 0;
}

std::uint16_t detailFunctionIdFromEnvironment() noexcept
{
    const char* value = std::getenv("RMG_NATIVE_RANDOM_MAP_FUNCTION_TRACE_DETAIL_FUNCTION_ID");
    if (value == nullptr || value[0] == '\0') {
        return 0;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 0);
    return end != nullptr && end[0] == '\0' && parsed > 0 && parsed <= 0xFFFFul
        ? static_cast<std::uint16_t>(parsed)
        : 0;
}

std::uint16_t stopAfterFunctionIdFromEnvironment() noexcept
{
    const char* value = std::getenv("RMG_NATIVE_RANDOM_MAP_FUNCTION_TRACE_STOP_AFTER_FUNCTION_ID");
    if (value == nullptr || value[0] == '\0') {
        return 0;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 0);
    return end != nullptr && end[0] == '\0' && parsed > 0 && parsed <= 0xFFFFul
        ? static_cast<std::uint16_t>(parsed)
        : 0;
}

std::uint32_t stopAfterEventCountFromEnvironment() noexcept
{
    const char* value = std::getenv("RMG_NATIVE_RANDOM_MAP_FUNCTION_TRACE_STOP_AFTER_EVENT_COUNT");
    if (value == nullptr || value[0] == '\0') {
        return 0;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 0);
    return end != nullptr && end[0] == '\0' && parsed > 0 && parsed <= kCapacity
        ? static_cast<std::uint32_t>(parsed)
        : 0;
}

std::uint32_t skipEventCountFromEnvironment() noexcept
{
    const char* value = std::getenv("RMG_NATIVE_RANDOM_MAP_FUNCTION_TRACE_SKIP_EVENTS");
    if (value == nullptr || value[0] == '\0') {
        return 0;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 0);
    return end != nullptr && end[0] == '\0' && parsed <= 0xFFFFFFFFul
        ? static_cast<std::uint32_t>(parsed)
        : 0;
}

std::uint16_t startAtFunctionIdFromEnvironment() noexcept
{
    const char* value = std::getenv("RMG_NATIVE_RANDOM_MAP_FUNCTION_TRACE_START_AT_FUNCTION_ID");
    if (value == nullptr || value[0] == '\0') {
        return 0;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 0);
    return end != nullptr && end[0] == '\0' && parsed > 0 && parsed <= 0xFFFFul
        ? static_cast<std::uint16_t>(parsed)
        : 0;
}

bool startIfRequested(std::uint16_t functionId) noexcept
{
    if (g_started) {
        return true;
    }
    if (g_startAtFunctionId == 0 || g_startAtFunctionId != functionId) {
        return false;
    }
    g_started = true;
    g_depth = 0;
    g_nextCallId = 0;
    g_seenEventCount = 0;
    return true;
}

bool shouldFinishAtEventLimit() noexcept
{
    return g_stopAfterEventCount != 0 && g_events.size() >= g_stopAfterEventCount;
}

bool shouldFinishAfterExit(std::uint16_t functionId) noexcept
{
    if (g_stopAfterFunctionId != functionId) {
        return false;
    }
    for (std::size_t index = 0; index < g_depth; ++index) {
        if (g_stack[index].functionId == functionId) {
            return false;
        }
    }
    return true;
}

void setBinary64X87Return(FxsaveSummary& fx, std::uint64_t bits) noexcept
{
    const std::uint64_t fraction = bits & ((std::uint64_t{1} << 52) - 1);
    const std::uint16_t binaryExponent = static_cast<std::uint16_t>((bits >> 52) & 0x7FFu);
    std::uint16_t extendedExponent = 0;
    std::uint64_t significand = 0;

    if (binaryExponent == 0x7FFu) {
        extendedExponent = 0x7FFFu;
        significand = fraction == 0
            ? (std::uint64_t{1} << 63)
            : ((std::uint64_t{3} << 62) | (fraction << 11));
    } else if (binaryExponent != 0) {
        extendedExponent = static_cast<std::uint16_t>(binaryExponent - 1023 + 16383);
        significand = ((std::uint64_t{1} << 52) | fraction) << 11;
    } else if (fraction != 0) {
        const int leadingBit = 63 - std::countl_zero(fraction);
        extendedExponent = static_cast<std::uint16_t>(leadingBit - 1074 + 16383);
        significand = fraction << (63 - leadingBit);
    }

    extendedExponent |= static_cast<std::uint16_t>((bits >> 48) & 0x8000u);
    std::memcpy(fx.st[0], &significand, sizeof(significand));
    std::memcpy(fx.st[0] + sizeof(significand), &extendedExponent, sizeof(extendedExponent));
    fx.statusWord = static_cast<std::uint16_t>((fx.statusWord & ~0x3800u) | 0x3800u);
    fx.tagWord = 0x80u;
}

void appendDetail(const Event& event, std::uint8_t kind, std::span<const std::uint32_t> values)
{
    if (((g_detailCallId == 0 || event.callId != g_detailCallId)
            && (g_detailFunctionId == 0 || event.functionId != g_detailFunctionId))
        || kind == DetailNone) {
        return;
    }
    const std::size_t valueBytes = values.size_bytes();
    const std::size_t required = sizeof(DetailHeader) + valueBytes;
    if (required > kDetailCapacity - g_details.size()) {
        g_overflow = true;
        return;
    }

    DetailHeader header;
    header.eventIndex = static_cast<std::uint32_t>(event.sequence);
    header.callId = event.callId;
    header.functionId = event.functionId;
    header.event = event.event;
    header.kind = kind;
    header.valueCount = static_cast<std::uint32_t>(values.size());
    const auto* headerBytes = reinterpret_cast<const std::uint8_t*>(&header);
    g_details.insert(g_details.end(), headerBytes, headerBytes + sizeof(header));
    if (valueBytes != 0) {
        const auto* valueData = reinterpret_cast<const std::uint8_t*>(values.data());
        g_details.insert(g_details.end(), valueData, valueData + valueBytes);
    }
}

void append(
    std::uint32_t site,
    std::uint16_t functionId,
    std::uint8_t eventKind,
    std::uint32_t callId,
    std::uint32_t parentCallId,
    int x,
    int y,
    std::uint32_t raw,
    std::uint32_t next1,
    std::uint32_t next2,
    std::uint64_t resultBits,
    const EventData* data = nullptr)
{
    if (!g_active || !g_started) {
        return;
    }
    ++g_seenEventCount;
    if (g_seenEventCount <= g_skipEventCount) {
        return;
    }
    if (g_events.size() >= kCapacity) {
        g_overflow = true;
        return;
    }
    Event event;
    event.sequence = static_cast<std::uint64_t>(g_events.size() + 1);
    event.site = site;
    event.functionId = functionId;
    event.event = eventKind;
    event.depth = static_cast<std::uint8_t>(g_depth);
    event.callId = callId;
    event.parentCallId = parentCallId;
    const EventData* semanticData = data;
    if (semanticData == nullptr
        && (eventKind == CallSite || eventKind == Exit)
        && g_depth != 0) {
        semanticData = &g_stack[g_depth - 1].data;
    }
    event.rngNext1 = next1 != 0 || next2 != 0 ? next1 : g_rngNext1;
    event.rngNext2 = next1 != 0 || next2 != 0 ? next2 : g_rngNext2;
    event.rngRaw = raw;
    event.gaussianSpareFlag = g_gaussianSpareFlag;
    event.gaussianSpareBits = g_gaussianSpareBits;
    // The native generator keeps PC53 + RC_CHOP after double2int_7C5F00.
    // Standalone builds may execute the equivalent arithmetic without leaving
    // that host FPU state active, so the trace records the native semantic CW.
    event.fx.controlWord = kNativeX87ControlWord;
    if (semanticData != nullptr) {
        event.args[0] = semanticData->args[0];
        event.args[1] = semanticData->args[1];
        event.args[2] = semanticData->args[2];
        event.args[3] = semanticData->args[3];
        event.args[4] = semanticData->args[4];
        event.args[5] = semanticData->args[5];
        event.result[0] = semanticData->result[0];
        event.result[1] = semanticData->result[1];
        event.result[2] = semanticData->result[2];
        event.result[3] = semanticData->result[3];
        event.regionIndex = semanticData->regionIndex;
        event.regionId = semanticData->regionId;
        event.regionCount = semanticData->regionCount;
        event.regionCellCount = semanticData->regionCellCount;
        event.cellX = semanticData->cellX;
        event.cellY = semanticData->cellY;
        event.resultKind = semanticData->resultKind;
    }
    if (x >= 0) {
        event.cellX = static_cast<std::int16_t>(x);
    }
    if (y >= 0) {
        event.cellY = static_cast<std::int16_t>(y);
    }
    if (data == nullptr) {
        event.result[0] = static_cast<std::uint32_t>(resultBits & 0xFFFFFFFFull);
        event.result[1] = static_cast<std::uint32_t>(resultBits >> 32);
    }
    if (functionId == Randomizer && eventKind == CallSite) {
        event.resultKind = ResultLegacy;
        if (site == 0x65C7A5u) {
            event.result[0] = raw;
            event.result[1] = next1;
        }
    }
    if (functionId == GaussianRandom) {
        event.args[0] = 0;
        event.args[1] = 0;
        event.args[2] = 0;
        event.args[3] = 0;
        event.args[4] = 0;
        event.args[5] = 0;
        event.result[2] = 0;
        event.result[3] = 0;
        if (eventKind == Exit) {
            event.resultKind = ResultBinary64;
        } else {
            event.result[0] = 0;
            event.result[1] = 0;
            event.resultKind = ResultNone;
        }
    }
    if (functionId == CompareAngle && eventKind == Exit) {
        event.result[2] = 0;
        event.result[3] = 0;
        event.resultKind = ResultBinary64;
    }
    if ((functionId == CompareAngle
            || functionId == ComputeTerrainDistance
            || functionId == GaussianRandom)
        && eventKind == Exit
        && event.resultKind == ResultBinary64) {
        setBinary64X87Return(
            event.fx,
            static_cast<std::uint64_t>(event.result[0])
                | (static_cast<std::uint64_t>(event.result[1]) << 32));
    }
    g_events.push_back(event);
}

std::uint32_t parentCallId() noexcept
{
    return g_depth == 0 ? 0 : g_stack[g_depth - 1].callId;
}

} // namespace

bool active() noexcept
{
    return g_active;
}

bool coreFunctionEnabled(std::uint16_t functionId) noexcept
{
    return g_active
        && functionId < g_coreFunctions.size()
        && g_coreFunctions[functionId];
}

bool detailRequested() noexcept
{
    return g_active
        && g_depth != 0
        && ((g_detailCallId != 0 && g_stack[g_depth - 1].callId == g_detailCallId)
            || (g_detailFunctionId != 0 && g_stack[g_depth - 1].functionId == g_detailFunctionId));
}

bool detailRequestedForFunction(std::uint16_t functionId) noexcept
{
    return g_active && g_detailFunctionId != 0 && g_detailFunctionId == functionId;
}

void detailCurrent(std::uint8_t detailKind, std::span<const std::uint32_t> detailValues)
{
    if (!detailRequested() || g_events.empty()) {
        return;
    }
    const Event& event = g_events.back();
    const Frame& frame = g_stack[g_depth - 1];
    if (event.event != Enter || event.callId != frame.callId) {
        return;
    }
    appendDetail(event, detailKind, detailValues);
}

std::uint16_t currentFunctionId() noexcept
{
    return g_depth == 0 ? 0 : g_stack[g_depth - 1].functionId;
}

void beginFromEnvironment()
{
    const char* path = std::getenv("RMG_NATIVE_RANDOM_MAP_FUNCTION_TRACE_PATH");
    g_path = path != nullptr ? path : "";
    g_events.clear();
    g_events.reserve(kCapacity);
    g_details.clear();
    g_details.reserve(kDetailCapacity);
    g_depth = 0;
    g_nextCallId = 0;
    g_overflow = false;
    g_detailCallId = detailCallIdFromEnvironment();
    g_detailFunctionId = detailFunctionIdFromEnvironment();
    g_stopAfterFunctionId = stopAfterFunctionIdFromEnvironment();
    g_stopAfterEventCount = stopAfterEventCountFromEnvironment();
    g_skipEventCount = skipEventCountFromEnvironment();
    g_startAtFunctionId = startAtFunctionIdFromEnvironment();
    g_seenEventCount = 0;
    g_rngNext1 = 0;
    g_rngNext2 = 0;
    g_gaussianSpareFlag = 0;
    g_gaussianSpareBits = 0;
    loadCoreFunctionFilter();
    g_active = !g_path.empty();
    g_started = g_startAtFunctionId == 0;
}

void setRandomizerState(std::uint32_t next1, std::uint32_t next2) noexcept
{
    g_rngNext1 = next1;
    g_rngNext2 = next2;
}

void setGaussianState(bool hasSpare, std::uint64_t spareBits) noexcept
{
    g_gaussianSpareFlag = hasSpare ? 1u : 0u;
    g_gaussianSpareBits = spareBits;
}

void finish()
{
    if (!g_active || g_path.empty()) {
        return;
    }
    Header header;
    header.eventCount = static_cast<std::uint32_t>(g_events.size());
    header.overflow = g_overflow ? 1u : 0u;
    header.detailBytes = static_cast<std::uint32_t>(g_details.size());
    std::ofstream out(g_path, std::ios::binary | std::ios::trunc);
    if (out.is_open()) {
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!g_events.empty()) {
            out.write(
                reinterpret_cast<const char*>(g_events.data()),
                static_cast<std::streamsize>(g_events.size() * sizeof(Event)));
        }
        if (!g_details.empty()) {
            out.write(
                reinterpret_cast<const char*>(g_details.data()),
                static_cast<std::streamsize>(g_details.size()));
        }
    }
    g_active = false;
    g_started = false;
    g_depth = 0;
    g_detailCallId = 0;
    g_detailFunctionId = 0;
    g_stopAfterFunctionId = 0;
    g_stopAfterEventCount = 0;
    g_skipEventCount = 0;
    g_startAtFunctionId = 0;
    g_seenEventCount = 0;
}

void enter(std::uint32_t site, std::uint16_t functionId, int x, int y)
{
    if (!g_active || !startIfRequested(functionId)) {
        return;
    }
    if (g_depth >= kMaxDepth) {
        g_overflow = true;
        return;
    }
    const std::uint32_t parent = parentCallId();
    const std::uint32_t callId = ++g_nextCallId;
    EventData data;
    data.cellX = static_cast<std::int16_t>(x);
    data.cellY = static_cast<std::int16_t>(y);
    g_stack[g_depth] = {callId, functionId, data};
    append(site, functionId, Enter, callId, parent, x, y, 0, 0, 0, 0);
    ++g_depth;
    if (shouldFinishAtEventLimit()) {
        finish();
    }
}

void exit(std::uint32_t site, std::uint16_t functionId, std::uint64_t resultBits, int x, int y)
{
    if (!g_active || g_depth == 0) {
        return;
    }
    const Frame frame = g_stack[g_depth - 1];
    const std::uint32_t parent = g_depth > 1 ? g_stack[g_depth - 2].callId : 0;
    --g_depth;
    EventData data = frame.data;
    data.result[0] = static_cast<std::uint32_t>(resultBits & 0xFFFFFFFFull);
    data.result[1] = static_cast<std::uint32_t>(resultBits >> 32);
    if (x >= 0) {
        data.cellX = static_cast<std::int16_t>(x);
    }
    if (y >= 0) {
        data.cellY = static_cast<std::int16_t>(y);
    }
    append(site, functionId, Exit, frame.callId, parent, -1, -1, 0, 0, 0, 0, &data);
    if (shouldFinishAfterExit(functionId) || shouldFinishAtEventLimit()) {
        finish();
    }
}

void call(
    std::uint32_t site,
    std::uint16_t functionId,
    int x,
    int y,
    std::uint32_t raw,
    std::uint32_t next1,
    std::uint32_t next2,
    std::uint64_t resultBits)
{
    append(site, functionId, CallSite, 0, parentCallId(), x, y, raw, next1, next2, resultBits);
    if (shouldFinishAtEventLimit()) {
        finish();
    }
}

void enterDetailed(std::uint32_t site, std::uint16_t functionId, const EventData& data)
{
    if (!g_active || !startIfRequested(functionId)) {
        return;
    }
    if (g_depth >= kMaxDepth) {
        g_overflow = true;
        return;
    }
    const std::uint32_t parent = parentCallId();
    const std::uint32_t callId = ++g_nextCallId;
    g_stack[g_depth] = {callId, functionId, data};
    append(site, functionId, Enter, callId, parent, -1, -1, 0, 0, 0, 0, &data);
    ++g_depth;
    if (shouldFinishAtEventLimit()) {
        finish();
    }
}

void exitDetailed(
    std::uint32_t site,
    std::uint16_t functionId,
    const EventData& data,
    std::uint8_t detailKind,
    std::span<const std::uint32_t> detailValues)
{
    if (!g_active || g_depth == 0) {
        return;
    }
    const Frame frame = g_stack[g_depth - 1];
    const std::uint32_t parent = g_depth > 1 ? g_stack[g_depth - 2].callId : 0;
    --g_depth;
    const std::size_t countBefore = g_events.size();
    append(site, functionId, Exit, frame.callId, parent, -1, -1, 0, 0, 0, 0, &data);
    if (g_events.size() > countBefore) {
        appendDetail(g_events.back(), detailKind, detailValues);
    }
    if (shouldFinishAfterExit(functionId) || shouldFinishAtEventLimit()) {
        finish();
    }
}

void callDetailed(std::uint32_t site, std::uint16_t functionId, const EventData& data)
{
    append(site, functionId, CallSite, 0, parentCallId(), -1, -1, 0, 0, 0, 0, &data);
    if (shouldFinishAtEventLimit()) {
        finish();
    }
}

void callWithDetail(
    std::uint32_t site,
    std::uint16_t functionId,
    std::uint8_t detailKind,
    std::span<const std::uint32_t> detailValues)
{
    const std::size_t countBefore = g_events.size();
    append(site, functionId, CallSite, 0, parentCallId(), -1, -1, 0, 0, 0, 0);
    if (g_events.size() > countBefore) {
        appendDetail(g_events.back(), detailKind, detailValues);
    }
    if (shouldFinishAtEventLimit()) {
        finish();
    }
}

void randomizerRaw(std::uint32_t raw, std::uint32_t next1, std::uint32_t next2)
{
    call(0x65C7A5, Randomizer, -1, -1, raw, next1, next2, raw);
    g_rngNext1 = (next1 + 1u) % 250u;
    g_rngNext2 = (next2 + 1u) % 250u;
}

void gaussianRawCallSite()
{
    call(0x598008, Randomizer);
}

} // namespace rmg::native_trace
