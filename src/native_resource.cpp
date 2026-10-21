#include "rmg/native_resource.h"

#include "ra2formats/byte_reader.h"
#include "ra2formats/error.h"
#include "ra2formats/mix.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace rmg::native_assets {
namespace {

constexpr std::uint64_t kMaximumMappedFileBytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaximumArchives = 4096;
constexpr std::size_t kMaximumNestedDepth = 16;
constexpr std::size_t kMaximumMixEntries = 2'000'000;

const std::array<TheaterDefinition, 6> kYuriTheaters{{
    {"TEMPERATE", "temperatmd.ini", ".tem", {"isotemp.mix", "isotemmd.mix", "temperat.mix", "tem.mix"}},
    {"SNOW", "snowmd.ini", ".sno", {"isosnomd.mix", "snowmd.mix", "isosnow.mix", "snow.mix", "sno.mix"}},
    {"URBAN", "urbanmd.ini", ".urb", {"isourbmd.mix", "isourb.mix", "urb.mix", "urban.mix"}},
    {"DESERT", "desertmd.ini", ".des", {"isodesmd.mix", "desert.mix", "des.mix", "isodes.mix"}},
    {"NEWURBAN", "urbannmd.ini", ".ubn", {"isoubnmd.mix", "isoubn.mix", "ubn.mix", "urbann.mix"}},
    {"LUNAR", "lunarmd.ini", ".lun", {"isolunmd.mix", "isolun.mix", "lun.mix", "lunar.mix"}},
}};

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string pathText(const std::filesystem::path& path)
{
    const std::u8string encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

std::optional<std::filesystem::path> normalizeResourcePath(std::string_view filename)
{
    if (filename.empty() || filename.find('\0') != std::string_view::npos) {
        return std::nullopt;
    }
    std::string portable(filename);
    std::replace(portable.begin(), portable.end(), '\\', '/');
    const std::filesystem::path path(portable);
    if (path.empty() || path.is_absolute() || path.has_root_path()) {
        return std::nullopt;
    }
    for (const auto& component : path) {
        if (component == ".." || component == ".") {
            return std::nullopt;
        }
    }
    return path;
}

class MappedFile {
public:
    explicit MappedFile(const std::filesystem::path& path)
    {
#ifdef _WIN32
        file_ = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr);
        if (file_ == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("无法打开游戏资源: " + pathText(path));
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file_, &size)
            || size.QuadPart <= 0
            || static_cast<unsigned long long>(size.QuadPart) > kMaximumMappedFileBytes) {
            close();
            throw std::runtime_error("游戏资源大小无效: " + pathText(path));
        }
        size_ = static_cast<std::size_t>(size.QuadPart);
        mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping_ == nullptr) {
            close();
            throw std::runtime_error("无法映射游戏资源: " + pathText(path));
        }
        data_ = static_cast<const std::uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
        if (data_ == nullptr) {
            close();
            throw std::runtime_error("无法读取游戏资源: " + pathText(path));
        }
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
#else
        file_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (file_ < 0) {
            throw std::runtime_error("无法打开游戏资源: " + pathText(path));
        }
        struct stat information{};
        if (fstat(file_, &information) != 0
            || !S_ISREG(information.st_mode)
            || information.st_size <= 0
            || static_cast<unsigned long long>(information.st_size) > kMaximumMappedFileBytes) {
            close();
            throw std::runtime_error("游戏资源大小无效: " + pathText(path));
        }
        size_ = static_cast<std::size_t>(information.st_size);
        void* mapped = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, file_, 0);
        if (mapped == MAP_FAILED) {
            close();
            throw std::runtime_error("无法映射游戏资源: " + pathText(path));
        }
        data_ = static_cast<const std::uint8_t*>(mapped);
        ::close(file_);
        file_ = -1;
#endif
    }

    ~MappedFile()
    {
        close();
    }

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept
    {
        return {data_, size_};
    }

private:
    void close() noexcept
    {
#ifdef _WIN32
        if (data_ != nullptr) {
            UnmapViewOfFile(data_);
            data_ = nullptr;
        }
        if (mapping_ != nullptr) {
            CloseHandle(mapping_);
            mapping_ = nullptr;
        }
        if (file_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_);
            file_ = INVALID_HANDLE_VALUE;
        }
#else
        if (data_ != nullptr) {
            munmap(const_cast<std::uint8_t*>(data_), size_);
            data_ = nullptr;
        }
        if (file_ >= 0) {
            ::close(file_);
            file_ = -1;
        }
#endif
        size_ = 0;
    }

    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
#ifdef _WIN32
    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
#else
    int file_ = -1;
#endif
};

struct ResourceData {
    std::shared_ptr<MappedFile> file;
    std::size_t offset = 0;
    std::size_t length = 0;

    [[nodiscard]] std::span<const std::uint8_t> bytes() const
    {
        if (file == nullptr) {
            return {};
        }
        const auto all = file->bytes();
        if (offset > all.size() || length > all.size() - offset) {
            throw std::runtime_error("MIX 资源范围越界");
        }
        return all.subspan(offset, length);
    }
};

bool couldBeMix(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < 6) {
        return false;
    }
    try {
        ra2formats::ByteReader reader(bytes);
        const std::uint32_t flags = reader.readU32Le();
        std::size_t checksumSize = 0;
        if ((flags & 0xFFFFU) != 0) {
            reader.seek(0);
        } else {
            if ((flags & ~0x00030000U) != 0) {
                return false;
            }
            checksumSize = (flags & 0x00010000U) != 0 ? 20 : 0;
            if ((flags & 0x00020000U) != 0) {
                return bytes.size() >= 4 + 80 + 24 + checksumSize;
            }
        }
        const std::uint16_t count = reader.readU16Le();
        const std::uint32_t dataSize = reader.readU32Le();
        const std::uint64_t expected = reader.position()
            + static_cast<std::uint64_t>(count) * 12
            + dataSize
            + checksumSize;
        return count != 0 && expected == bytes.size();
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace

const TheaterDefinition& theaterDefinition(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= kYuriTheaters.size()) {
        throw std::invalid_argument("Theater 编号必须在 0..5 之间");
    }
    return kYuriTheaters[static_cast<std::size_t>(index)];
}

class ResourceFileSystem::Impl {
public:
    struct Archive {
        ResourceData data;
        ra2formats::MixArchive index;
        bool explicitLoad = false;
    };

    void addDirectory(const std::filesystem::path& path)
    {
        std::error_code error;
        if (!std::filesystem::is_directory(path, error) || error) {
            throw std::runtime_error("游戏资源目录不存在: " + pathText(path));
        }
        root = std::filesystem::canonical(path, error);
        if (error) {
            throw std::runtime_error("无法解析游戏资源目录: " + pathText(path));
        }
        directFiles.clear();
    }

    [[nodiscard]] std::optional<std::filesystem::path> findDirect(std::string_view filename) const
    {
        if (root.empty()) {
            return std::nullopt;
        }
        const auto relative = normalizeResourcePath(filename);
        if (!relative.has_value()) {
            return std::nullopt;
        }
        const std::string key = lowerAscii(relative->generic_string());
        if (const auto cached = directFiles.find(key); cached != directFiles.end()) {
            return cached->second;
        }

        std::filesystem::path current = root;
        for (const auto& component : *relative) {
#ifdef _WIN32
            current /= component;
#else
            std::filesystem::path match;
            const std::string wanted = lowerAscii(component.string());
            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator(
                     current,
                     std::filesystem::directory_options::skip_permission_denied,
                     error)) {
                if (lowerAscii(entry.path().filename().string()) != wanted) {
                    continue;
                }
                if (!match.empty()) {
                    throw std::runtime_error(
                        "游戏资源目录存在大小写冲突: " + relative->generic_string());
                }
                match = entry.path().filename();
            }
            if (error || match.empty()) {
                directFiles.emplace(key, std::nullopt);
                return std::nullopt;
            }
            current /= match;
#endif
        }
        std::error_code error;
        if (!std::filesystem::is_regular_file(current, error) || error) {
            directFiles.emplace(key, std::nullopt);
            return std::nullopt;
        }
        const auto canonical = std::filesystem::canonical(current, error);
        if (error) {
            directFiles.emplace(key, std::nullopt);
            return std::nullopt;
        }
        directFiles.emplace(key, canonical);
        return canonical;
    }

    std::shared_ptr<MappedFile> mapFile(const std::filesystem::path& path)
    {
        const std::filesystem::path canonical = std::filesystem::canonical(path);
        const std::string key = pathText(canonical);
        if (const auto cached = mappedFiles.find(key); cached != mappedFiles.end()) {
            return cached->second;
        }
        if (mappedFileCount >= kMaximumArchives) {
            throw std::runtime_error("游戏资源文件数量超过限制");
        }
        auto mapped = std::make_shared<MappedFile>(canonical);
        if (mapped->bytes().size() > kMaximumMappedFileBytes - mappedBytes) {
            throw std::runtime_error("游戏资源累计映射大小超过 4 GiB");
        }
        mappedFiles.emplace(key, mapped);
        ++mappedFileCount;
        mappedBytes += mapped->bytes().size();
        return mapped;
    }

    bool addMix(std::string_view name)
    {
        const auto direct = findDirect(name);
        if (!direct.has_value()) {
            return false;
        }
        const auto mapped = mapFile(*direct);
        const ResourceData data{mapped, 0, mapped->bytes().size()};
        for (const auto& archive : archives) {
            if (archive->data.file == data.file
                && archive->data.offset == data.offset
                && archive->data.length == data.length) {
                if (!archive->explicitLoad) {
                    archive->explicitLoad = true;
                }
                return true;
            }
        }
        std::vector<std::unique_ptr<Archive>> staged;
        stageMix(data, 0, staged);
        Archive* rootArchive = staged.front().get();
        for (auto& archive : staged) {
            archives.push_back(std::move(archive));
        }
        rootArchive->explicitLoad = true;
        return true;
    }

    void stageMix(
        ResourceData data,
        std::size_t depth,
        std::vector<std::unique_ptr<Archive>>& staged)
    {
        if (depth > kMaximumNestedDepth) {
            throw std::runtime_error("MIX 嵌套深度超过 16");
        }
        if (archives.size() + staged.size() >= kMaximumArchives) {
            throw std::runtime_error("MIX 归档数量超过 4096");
        }
        auto archive = std::make_unique<Archive>();
        archive->data = std::move(data);
        archive->index = ra2formats::decodeMix(archive->data.bytes());
        if (mixEntries > kMaximumMixEntries
            || archive->index.entries.size() > kMaximumMixEntries - mixEntries) {
            throw std::runtime_error("MIX 条目数量超过限制");
        }
        mixEntries += archive->index.entries.size();
        Archive& current = *archive;
        staged.push_back(std::move(archive));
        for (const ra2formats::MixEntry& entry : current.index.orderedEntries) {
            const ResourceData child{
                current.data.file,
                current.data.offset + current.index.dataOffset + entry.offset,
                entry.length,
            };
            if (!couldBeMix(child.bytes())) {
                continue;
            }
            try {
                stageMix(child, depth + 1, staged);
            } catch (const ra2formats::FormatError&) {
                continue;
            }
        }
    }

    void addDirectoryMixes(
        std::string_view prefix,
        std::string_view suffix,
        bool descending)
    {
        if (root.empty()) {
            return;
        }
        const std::string wantedPrefix = lowerAscii(std::string(prefix));
        const std::string wantedSuffix = lowerAscii(std::string(suffix));
        std::vector<std::pair<std::string, std::string>> names;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(
                 root,
                 std::filesystem::directory_options::skip_permission_denied,
                 error)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string original = entry.path().filename().string();
            const std::string normalized = lowerAscii(original);
            if (normalized.starts_with(wantedPrefix) && normalized.ends_with(wantedSuffix)) {
                names.emplace_back(normalized, original);
            }
        }
        if (error) {
            throw std::runtime_error("无法扫描游戏资源目录");
        }
        if (descending) {
            std::sort(names.begin(), names.end(), std::greater<>());
        }
        for (const auto& [normalized, original] : names) {
            static_cast<void>(normalized);
            addMix(original);
        }
    }

    std::optional<ResourceData> find(std::string_view filename) const
    {
        const auto direct = findDirect(filename);
        if (direct.has_value()) {
            const auto mapped = mappedFiles.find(pathText(*direct));
            if (mapped != mappedFiles.end()) {
                return ResourceData{mapped->second, 0, mapped->second->bytes().size()};
            }
            auto loaded = const_cast<Impl*>(this)->mapFile(*direct);
            return ResourceData{loaded, 0, loaded->bytes().size()};
        }
        const std::string normalized = lowerAscii(
            std::filesystem::path(std::string(filename)).generic_string());
        for (const auto& archive : archives) {
            if (!archive->explicitLoad) {
                continue;
            }
            if (const ResourceData data = findInMix(*archive, filename, normalized); data.length != 0) {
                return data;
            }
        }
        for (const auto& archive : archives) {
            if (archive->explicitLoad) {
                continue;
            }
            if (const ResourceData data = findInMix(*archive, filename, normalized); data.length != 0) {
                return data;
            }
        }
        return std::nullopt;
    }

    static ResourceData findInMix(
        const Archive& archive,
        std::string_view filename,
        const std::string& normalized)
    {
        const ra2formats::MixEntry* entry = archive.index.find(filename);
        if (entry == nullptr && normalized != filename) {
            entry = archive.index.find(normalized);
        }
        if (entry == nullptr && normalized.find('/') != std::string::npos) {
            std::string windowsName = normalized;
            std::replace(windowsName.begin(), windowsName.end(), '/', '\\');
            entry = archive.index.find(windowsName);
        }
        if (entry == nullptr) {
            return {};
        }
        return {
            archive.data.file,
            archive.data.offset + archive.index.dataOffset + entry->offset,
            entry->length,
        };
    }

    std::filesystem::path root;
    mutable std::unordered_map<std::string, std::optional<std::filesystem::path>> directFiles;
    std::unordered_map<std::string, std::shared_ptr<MappedFile>> mappedFiles;
    std::vector<std::unique_ptr<Archive>> archives;
    std::size_t mappedFileCount = 0;
    std::uint64_t mappedBytes = 0;
    std::size_t mixEntries = 0;
};

ResourceFileSystem::ResourceFileSystem()
    : impl_(std::make_unique<Impl>())
{
}

ResourceFileSystem::~ResourceFileSystem() = default;
ResourceFileSystem::ResourceFileSystem(ResourceFileSystem&&) noexcept = default;
ResourceFileSystem& ResourceFileSystem::operator=(ResourceFileSystem&&) noexcept = default;

bool ResourceFileSystem::addDirectory(const std::filesystem::path& path)
{
    impl_->addDirectory(path);
    return true;
}

void ResourceFileSystem::loadYuriMixes()
{
    for (int index = 99; index >= 0; --index) {
        const std::string number = index < 10 ? "0" + std::to_string(index) : std::to_string(index);
        impl_->addMix("expandmd" + number + ".mix");
    }
    for (const char* name : {
             "langmd.mix", "language.mix", "ra2md.mix", "ra2.mix", "cachemd.mix",
             "cache.mix", "localmd.mix", "local.mix", "audiomd.mix"}) {
        impl_->addMix(name);
    }
    impl_->addDirectoryMixes("ecache", ".mix", true);
    for (int index = 99; index >= 0; --index) {
        const std::string number = index < 10 ? "0" + std::to_string(index) : std::to_string(index);
        impl_->addMix("elocal" + number + ".mix");
    }
    impl_->addDirectoryMixes("", ".mmx", true);
    impl_->addDirectoryMixes("", ".yro", true);
    for (const char* name : {
             "conquer.mix", "conqmd.mix", "genermd.mix", "generic.mix", "isogenmd.mix",
             "isogen.mix", "cameomd.mix", "cameo.mix", "mapsmd03.mix", "multimd.mix",
             "thememd.mix"}) {
        impl_->addMix(name);
    }
}

void ResourceFileSystem::loadTheater(const TheaterDefinition& theater)
{
    for (const std::string& name : theater.mixes) {
        impl_->addMix(name);
    }
    if (!contains(theater.iniFile)) {
        throw std::runtime_error("未找到 Theater INI: " + theater.iniFile);
    }
}

bool ResourceFileSystem::contains(std::string_view filename) const
{
    return impl_->find(filename).has_value();
}

std::vector<std::uint8_t> ResourceFileSystem::open(std::string_view filename) const
{
    const std::optional<ResourceData> data = impl_->find(filename);
    if (!data.has_value()) {
        throw std::runtime_error("未找到游戏资源: " + std::string(filename));
    }
    const auto bytes = data->bytes();
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

} // namespace rmg::native_assets
