#include "ra2formats/ini.h"

#include "ra2formats/error.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ra2formats {
namespace {

bool isIniWhitespace(char value)
{
    return static_cast<unsigned char>(value) <= 0x20;
}

std::string trimIniLine(std::string value)
{
    std::size_t start = 0;
    while (start < value.size() && isIniWhitespace(value[start])) {
        ++start;
    }
    std::size_t end = value.find(';', start);
    if (end == std::string::npos) {
        end = value.size();
    }
    while (end > start && isIniWhitespace(value[end - 1])) {
        --end;
    }
    return value.substr(start, end - start);
}

bool isObjectArray(std::string_view name)
{
    constexpr std::string_view Arrays[] = {
        "BuildingTypes",
        "AircraftTypes",
        "InfantryTypes",
        "OverlayTypes",
        "TerrainTypes",
        "SmudgeTypes",
        "VehicleTypes",
    };
    return std::find(std::begin(Arrays), std::end(Arrays), name) != std::end(Arrays);
}

struct ParsedIniSectionHeader {
    std::string name;
    std::string parent;
};

std::string_view trimIniWhitespace(std::string_view value)
{
    while (!value.empty() && isIniWhitespace(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && isIniWhitespace(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

std::optional<ParsedIniSectionHeader> parseIniSectionHeader(std::string_view line, bool aresExtensions)
{
    line = trimIniWhitespace(line);
    if (line.empty() || line.front() != '[') {
        return std::nullopt;
    }

    // Keep the classic parser's exact section-name behavior. Ares extensions
    // use the same raw names and only add the `:[Parent]` suffix.
    if (!aresExtensions) {
        if (line.back() != ']') {
            return std::nullopt;
        }
        return ParsedIniSectionHeader { .name = std::string(line.substr(1, line.size() - 2)), .parent = {} };
    }

    const std::size_t nameEnd = line.find(']', 1);
    if (nameEnd == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view name = line.substr(1, nameEnd - 1);
    if (name.empty()) {
        return std::nullopt;
    }
    const std::string_view suffix = trimIniWhitespace(line.substr(nameEnd + 1));
    if (suffix.empty() || suffix.front() == ';'
        || (suffix.size() >= 2 && suffix.substr(0, 2) == "//")) {
        return ParsedIniSectionHeader { .name = std::string(name), .parent = {} };
    }
    if (!aresExtensions || suffix.size() < 2 || suffix.substr(0, 2) != ":[") {
        return std::nullopt;
    }
    const std::size_t parentEnd = suffix.find(']', 2);
    if (parentEnd == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view parent = suffix.substr(2, parentEnd - 2);
    const std::string_view trailing = trimIniWhitespace(suffix.substr(parentEnd + 1));
    if (parent.empty() || (!trailing.empty() && trailing.front() != ';'
        && !(trailing.size() >= 2 && trailing.substr(0, 2) == "//"))) {
        return std::nullopt;
    }
    return ParsedIniSectionHeader { .name = std::string(name), .parent = std::string(parent) };
}

void appendCanonicalSection(std::string& text, const IniSection& section)
{
    text += '[';
    text += section.name;
    text += "]\r\n";
    for (const IniEntry& entry : section.entries) {
        text += entry.key;
        text += '=';
        text += entry.value;
        text += "\r\n";
    }
}

bool entriesEqual(const std::vector<IniEntry>& left, const std::vector<IniEntry>& right)
{
    return left.size() == right.size()
        && std::equal(left.begin(), left.end(), right.begin(), [](const IniEntry& first, const IniEntry& second) {
               return first.key == second.key && first.value == second.value && first.isAppend == second.isAppend;
           });
}

}

bool IniSection::contains(std::string_view key) const
{
    return entryIndices_.find(std::string(key)) != entryIndices_.end();
}

std::string_view IniSection::get(std::string_view key, std::string_view fallback) const
{
    const auto entry = entryIndices_.find(std::string(key));
    return entry == entryIndices_.end() ? fallback : std::string_view(entries[entry->second].value);
}

int IniSection::getInt(std::string_view key, int fallback) const
{
    const std::string_view value = get(key);
    if (value.empty()) {
        return fallback;
    }
    int result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc() && parsed.ptr == value.data() + value.size() ? result : fallback;
}

double IniSection::getDouble(std::string_view key, double fallback) const
{
    std::string value(get(key));
    if (value.empty()) {
        return fallback;
    }
    std::replace(value.begin(), value.end(), ',', '.');
    char* end = nullptr;
    const double result = std::strtod(value.c_str(), &end);
    return end == value.c_str() + value.size() ? result : fallback;
}

bool IniSection::getBool(std::string_view key, bool fallback) const
{
    std::string value(get(key));
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (value == "yes" || value == "1" || value == "true" || value == "on") {
        return true;
    }
    if (value == "no" || value == "0" || value == "false" || value == "off") {
        return false;
    }
    return fallback;
}

void IniSection::clear()
{
    entries.clear();
    entryIndices_.clear();
}

void IniSection::append(std::string value)
{
    entries.push_back({ .key = "+", .value = std::move(value), .isAppend = true });
}

void IniSection::set(std::string key, std::string value, bool overrideExisting)
{
    const auto existing = entryIndices_.find(key);
    if (existing == entryIndices_.end()) {
        entryIndices_.emplace(key, entries.size());
        entries.push_back({ .key = std::move(key), .value = std::move(value) });
        return;
    }
    if (!overrideExisting) {
        return;
    }

    const std::size_t oldIndex = existing->second;
    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(oldIndex));
    for (std::size_t index = oldIndex; index < entries.size(); ++index) {
        if (!entries[index].isAppend) {
            entryIndices_[entries[index].key] = index;
        }
    }
    entryIndices_[key] = entries.size();
    entries.push_back({ .key = std::move(key), .value = std::move(value) });
}

const IniSection* IniDocument::find(std::string_view name) const
{
    const auto section = std::find_if(sections.begin(), sections.end(), [&](const IniSection& value) {
        return value.name == name;
    });
    return section == sections.end() ? nullptr : &*section;
}

IniSection* IniDocument::find(std::string_view name)
{
    return const_cast<IniSection*>(std::as_const(*this).find(name));
}

IniSection& IniDocument::getOrCreate(std::string name)
{
    if (IniSection* existing = find(name)) {
        return *existing;
    }
    sections.push_back({});
    sections.back().name = std::move(name);
    return sections.back();
}

IniSection& IniDocument::replaceSection(std::string name)
{
    const auto first = std::find_if(sections.begin(), sections.end(), [&](const IniSection& section) {
        return section.name == name;
    });
    if (first == sections.end()) {
        sections.push_back({});
        IniSection& section = sections.back();
        section.name = std::move(name);
        section.rewriteRequested_ = true;
        return section;
    }

    const std::size_t firstIndex = static_cast<std::size_t>(first - sections.begin());
    sections.erase(
        std::remove_if(sections.begin() + static_cast<std::ptrdiff_t>(firstIndex + 1), sections.end(), [&](const IniSection& section) {
            return section.name == name;
        }),
        sections.end());
    IniSection& section = sections[firstIndex];
    section.clear();
    section.rewriteRequested_ = true;
    return section;
}

IniSection& IniDocument::replaceSectionBefore(std::string name, std::string_view before)
{
    if (find(name) != nullptr) {
        return replaceSection(std::move(name));
    }

    const auto position = std::find_if(sections.begin(), sections.end(), [&](const IniSection& section) {
        return section.name == before;
    });
    IniSection section;
    section.name = std::move(name);
    section.rewriteRequested_ = true;
    return *sections.insert(position, std::move(section));
}

std::size_t IniDocument::eraseSections(std::string_view name)
{
    const std::string target(name);
    const std::size_t oldSize = sections.size();
    sections.erase(
        std::remove_if(sections.begin(), sections.end(), [&](const IniSection& section) {
            return section.name == target;
        }),
        sections.end());
    return oldSize - sections.size();
}

std::string_view IniDocument::get(
    std::string_view section,
    std::string_view key,
    std::string_view fallback) const
{
    const IniSection* found = find(section);
    return found == nullptr ? fallback : found->get(key, fallback);
}

void IniDocument::merge(const IniDocument& other)
{
    if (this == &other) {
        const IniDocument copy(other);
        merge(copy);
        return;
    }

    std::unordered_map<std::string, std::size_t> sectionIndices;
    sectionIndices.reserve(sections.size() + other.sections.size());
    for (std::size_t index = 0; index < sections.size(); ++index) {
        sectionIndices.try_emplace(sections[index].name, index);
    }
    for (const IniSection& source : other.sections) {
        auto found = sectionIndices.find(source.name);
        if (found == sectionIndices.end()) {
            sections.push_back({});
            sections.back().name = source.name;
            found = sectionIndices.emplace(source.name, sections.size() - 1).first;
        }
        IniSection& destination = sections[found->second];
        if (isObjectArray(source.name) && !destination.entries.empty()) {
            int next = 0;
            const auto parsed = std::from_chars(
                destination.entries.back().key.data(),
                destination.entries.back().key.data() + destination.entries.back().key.size(),
                next);
            if (parsed.ec == std::errc() && parsed.ptr == destination.entries.back().key.data() + destination.entries.back().key.size()) {
                std::int64_t nextIndex = static_cast<std::int64_t>(next) + 1;
                for (const IniEntry& entry : source.entries) {
                    if (nextIndex > std::numeric_limits<int>::max()) {
                        throw FormatError(ErrorCode::SizeLimitExceeded, "INI 对象数组编号溢出");
                    }
                    destination.set(std::to_string(nextIndex++), entry.value);
                }
                continue;
            }
        }

        std::unordered_set<std::string_view> replacedKeys;
        replacedKeys.reserve(source.entries.size());
        for (const IniEntry& entry : source.entries) {
            if (!entry.isAppend) {
                replacedKeys.insert(entry.key);
            }
        }
        IniSection merged;
        merged.name = destination.name;
        for (const IniEntry& entry : destination.entries) {
            if (entry.isAppend) {
                merged.append(entry.value);
            } else if (!replacedKeys.contains(entry.key)) {
                merged.set(entry.key, entry.value, false);
            }
        }
        for (const IniEntry& entry : source.entries) {
            if (entry.isAppend) {
                merged.append(entry.value);
            } else {
                merged.set(entry.key, entry.value);
            }
        }
        destination = std::move(merged);
    }
}

IniDocument decodeIni(
    std::span<const std::uint8_t> bytes,
    const IniDecodeLimits& limits,
    const IniDecodeOptions& options)
{
    if (bytes.size() > limits.maximumInputBytes) {
        throw FormatError(ErrorCode::SizeLimitExceeded, "INI 输入大小超过限制");
    }
    IniDocument document;
    IniSection* current = nullptr;
    std::size_t offset = 0;
    std::uint64_t lineCount = 0;
    std::uint64_t sectionCount = 0;
    std::uint64_t entryCount = 0;
    std::uint64_t storedTextBytes = 0;
    std::vector<std::size_t> sectionStarts;
    std::unordered_map<std::string, std::size_t> sectionIndices;
    bool sourceLayoutCompatible = true;
    const auto addDecodedEntry = [](IniSection& section, IniEntry entry) {
        if (!entry.isAppend) {
            section.entryIndices_[entry.key] = section.entries.size();
        }
        section.entries.push_back(std::move(entry));
    };
    const auto consume = [](std::uint64_t& total, std::uint64_t amount, std::uint64_t maximum, const char* message) {
        if (amount > maximum || total > maximum - amount) {
            throw FormatError(ErrorCode::SizeLimitExceeded, message);
        }
        total += amount;
    };
    while (offset < bytes.size()) {
        consume(lineCount, 1, limits.maximumLines, "INI 行数超过限制");
        const std::size_t lineStart = offset;
        while (offset < bytes.size() && bytes[offset] != '\n') {
            ++offset;
        }
        if (offset - lineStart > limits.maximumLineBytes) {
            throw FormatError(ErrorCode::SizeLimitExceeded, "INI 单行长度超过限制");
        }
        std::string line(reinterpret_cast<const char*>(bytes.data() + lineStart), offset - lineStart);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        line = trimIniLine(std::move(line));
        if (!line.empty()) {
            if (const auto header = parseIniSectionHeader(line, options.aresExtensions)) {
                std::vector<IniEntry> inherited;
                if (options.aresExtensions && !header->parent.empty()) {
                    const std::string& parent = header->parent;
                    // Ares 在读取子段时复制已有父段，后续父段修改不追溯到子段。
                    const auto copyParent = [&](const IniDocument* source) {
                        const IniSection* section = source == nullptr ? nullptr : source->find(parent);
                        if (section == nullptr) {
                            return;
                        }
                        for (std::size_t index = 0; index < section->entries.size(); ++index) {
                            const IniEntry& entry = section->entries[index];
                            if (source == &document && !entry.isAppend
                                && section->entryIndices_.at(entry.key) != index) {
                                continue;
                            }
                            consume(entryCount, 1, limits.maximumEntries, "INI 继承 entry 数量超过限制");
                            consume(storedTextBytes,
                                static_cast<std::uint64_t>(entry.key.size()) + entry.value.size(),
                                limits.maximumStoredTextBytes, "INI 继承累计文本超过限制");
                            inherited.push_back(entry);
                        }
                    };
                    copyParent(options.base);
                    copyParent(&document);
                }
                consume(sectionCount, 1, limits.maximumSections, "INI section 数量超过限制");
                consume(storedTextBytes, line.size() - 2, limits.maximumStoredTextBytes, "INI 累计文本超过限制");
                const auto existing = sectionIndices.find(header->name);
                if (existing != sectionIndices.end() && !options.preserveDuplicateSections) {
                    current = &document.sections[existing->second];
                    for (const IniEntry& entry : inherited) {
                        addDecodedEntry(*current, entry);
                    }
                    sourceLayoutCompatible = false;
                } else {
                    document.sections.push_back({});
                    const std::size_t index = document.sections.size() - 1;
                    sectionIndices.emplace(header->name, index);
                    current = &document.sections[index];
                    current->name = header->name;
                    for (const IniEntry& entry : inherited) {
                        addDecodedEntry(*current, entry);
                    }
                }
                sectionStarts.push_back(lineStart);
            } else if (current != nullptr && line.front() != ';') {
                const std::size_t equals = line.find('=');
                if (equals != std::string::npos) {
                    consume(entryCount, 1, limits.maximumEntries, "INI entry 数量超过限制");
                    std::string key = trimIniLine(line.substr(0, equals));
                    std::string value = trimIniLine(line.substr(equals + 1));
                    consume(
                        storedTextBytes,
                        static_cast<std::uint64_t>(key.size()) + value.size(),
                        limits.maximumStoredTextBytes,
                        "INI 累计文本超过限制");
                    if (options.aresExtensions && key == "+") {
                        addDecodedEntry(*current, { .key = "+", .value = std::move(value), .isAppend = true });
                    } else {
                        addDecodedEntry(*current, { .key = std::move(key), .value = std::move(value) });
                    }
                }
            }
        }
        if (offset < bytes.size()) {
            ++offset;
        }
    }

    for (IniSection& section : document.sections) {
        std::vector<IniEntry> effective;
        effective.reserve(section.entries.size());
        for (std::size_t index = 0; index < section.entries.size(); ++index) {
            IniEntry& entry = section.entries[index];
            if (entry.isAppend || section.entryIndices_.at(entry.key) == index) {
                effective.push_back(std::move(entry));
            }
        }
        section.entries = std::move(effective);
        section.entryIndices_.clear();
        for (std::size_t index = 0; index < section.entries.size(); ++index) {
            if (!section.entries[index].isAppend) {
                section.entryIndices_.emplace(section.entries[index].key, index);
            }
        }
    }

    static std::atomic<std::uint64_t> nextSourceOwnerId { 1 };
    if (sourceLayoutCompatible && document.sections.size() == sectionStarts.size()) {
        document.sourceOwnerId_ = nextSourceOwnerId.fetch_add(1, std::memory_order_relaxed);
        if (document.sourceOwnerId_ == 0) {
            document.sourceOwnerId_ = nextSourceOwnerId.fetch_add(1, std::memory_order_relaxed);
        }
        document.sourceBytes_.assign(bytes.begin(), bytes.end());
        document.sourcePreambleBytes_ = sectionStarts.empty() ? bytes.size() : sectionStarts.front();
        document.sourceSections_.reserve(sectionStarts.size());
        for (std::size_t index = 0; index < sectionStarts.size(); ++index) {
            IniSection& section = document.sections[index];
            section.sourceOwnerId_ = document.sourceOwnerId_;
            section.sourceSectionId_ = index;
            document.sourceSections_.push_back({
                .begin = sectionStarts[index],
                .end = index + 1 < sectionStarts.size() ? sectionStarts[index + 1] : bytes.size(),
                .name = section.name,
                .entries = section.entries,
            });
        }
    }
    return document;
}

std::vector<std::uint8_t> encodeIni(const IniDocument& document)
{
    std::string text;
    if (document.sourceOwnerId_ == 0) {
        for (std::size_t sectionIndex = 0; sectionIndex < document.sections.size(); ++sectionIndex) {
            appendCanonicalSection(text, document.sections[sectionIndex]);
            if (sectionIndex + 1 < document.sections.size()) {
                text += "\r\n";
            }
        }
        return { text.begin(), text.end() };
    }

    if (document.sourcePreambleBytes_ != 0) {
        text.append(
            reinterpret_cast<const char*>(document.sourceBytes_.data()),
            document.sourcePreambleBytes_);
    }
    bool emittedSection = false;
    bool previousWasNew = false;
    for (const IniSection& section : document.sections) {
        const bool hasSource = section.sourceOwnerId_ == document.sourceOwnerId_
            && section.sourceSectionId_ < document.sourceSections_.size();
        const IniDocument::SourceSection* source = hasSource
            ? &document.sourceSections_[section.sourceSectionId_]
            : nullptr;
        const bool useSource = source != nullptr
            && !section.rewriteRequested_
            && source->name == section.name
            && entriesEqual(source->entries, section.entries);
        const bool currentIsNew = source == nullptr;
        if ((emittedSection && (previousWasNew || currentIsNew))
            || (!emittedSection && currentIsNew && !text.empty())) {
            text += "\r\n";
        }
        if (useSource) {
            text.append(
                reinterpret_cast<const char*>(document.sourceBytes_.data() + source->begin),
                source->end - source->begin);
        } else {
            appendCanonicalSection(text, section);
        }
        emittedSection = true;
        previousWasNew = currentIsNew;
    }
    return { text.begin(), text.end() };
}

}
