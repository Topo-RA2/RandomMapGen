#pragma once

#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ra2formats {

struct IniDecodeLimits {
    std::uint64_t maximumInputBytes = 64ULL * 1024 * 1024;
    std::uint64_t maximumLineBytes = 1ULL * 1024 * 1024;
    std::uint64_t maximumLines = 1'000'000;
    std::uint64_t maximumSections = 65'536;
    std::uint64_t maximumEntries = 262'144;
    std::uint64_t maximumStoredTextBytes = 64ULL * 1024 * 1024;
};

struct IniEntry {
    std::string key;
    std::string value;
    bool isAppend = false;
};

class IniDocument;

struct IniDecodeOptions {
    bool aresExtensions = false;
    const IniDocument* base = nullptr;
    bool preserveDuplicateSections = false;
};

class IniSection {
public:
    std::string name;
    std::vector<IniEntry> entries;

    [[nodiscard]] bool contains(std::string_view key) const;
    [[nodiscard]] std::string_view get(std::string_view key, std::string_view fallback = {}) const;
    [[nodiscard]] int getInt(std::string_view key, int fallback = 0) const;
    [[nodiscard]] double getDouble(std::string_view key, double fallback = 0.0) const;
    [[nodiscard]] bool getBool(std::string_view key, bool fallback = false) const;
    void clear();
    void set(std::string key, std::string value, bool overrideExisting = true);

private:
    void append(std::string value);

    std::unordered_map<std::string, std::size_t> entryIndices_;
    std::uint64_t sourceOwnerId_ = 0;
    std::size_t sourceSectionId_ = std::numeric_limits<std::size_t>::max();
    bool rewriteRequested_ = false;

    friend class IniDocument;
    friend IniDocument decodeIni(std::span<const std::uint8_t>, const IniDecodeLimits&, const IniDecodeOptions&);
    friend std::vector<std::uint8_t> encodeIni(const IniDocument&);
};

class IniDocument {
public:
    std::vector<IniSection> sections;

    [[nodiscard]] const IniSection* find(std::string_view name) const;
    [[nodiscard]] IniSection* find(std::string_view name);
    [[nodiscard]] IniSection& getOrCreate(std::string name);
    [[nodiscard]] IniSection& replaceSection(std::string name);
    [[nodiscard]] IniSection& replaceSectionBefore(std::string name, std::string_view before);
    [[nodiscard]] std::size_t eraseSections(std::string_view name);
    [[nodiscard]] std::string_view get(
        std::string_view section,
        std::string_view key,
        std::string_view fallback = {}) const;
    void merge(const IniDocument& other);

private:
    struct SourceSection {
        std::size_t begin = 0;
        std::size_t end = 0;
        std::string name;
        std::vector<IniEntry> entries;
    };

    std::uint64_t sourceOwnerId_ = 0;
    std::size_t sourcePreambleBytes_ = 0;
    std::vector<std::uint8_t> sourceBytes_;
    std::vector<SourceSection> sourceSections_;

    friend IniDocument decodeIni(std::span<const std::uint8_t>, const IniDecodeLimits&, const IniDecodeOptions&);
    friend std::vector<std::uint8_t> encodeIni(const IniDocument&);
};

[[nodiscard]] IniDocument decodeIni(
    std::span<const std::uint8_t> bytes,
    const IniDecodeLimits& limits = {},
    const IniDecodeOptions& options = {});
[[nodiscard]] std::vector<std::uint8_t> encodeIni(const IniDocument& document);

}
