#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ra2formats {

struct MixEntry {
    std::uint32_t hash = 0;
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
};

struct MixArchive {
    bool encrypted = false;
    bool checksum = false;
    std::size_t dataOffset = 0;
    std::uint32_t dataSize = 0;
    // 保留 header 索引顺序；unordered lookup 不能表达同长度 entry 的 legacy 首项语义。
    std::vector<MixEntry> orderedEntries;
    std::unordered_map<std::uint32_t, MixEntry> entries;

    [[nodiscard]] const MixEntry* find(std::string_view filename) const;
    [[nodiscard]] std::span<const std::uint8_t> extract(
        std::span<const std::uint8_t> archive,
        std::string_view filename) const;
};

[[nodiscard]] std::uint32_t mixFilenameHash(std::string_view filename);
// Callers may explicitly discard a bounded author note after the declared body
// and checksum. The default requires an exact file length.
[[nodiscard]] MixArchive decodeMix(
    std::span<const std::uint8_t> bytes, std::size_t maximumTrailingBytes = 0);

}
