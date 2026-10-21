#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rmg::native_assets {

struct TheaterDefinition {
    std::string name;
    std::string iniFile;
    std::string extension;
    std::vector<std::string> mixes;
};

[[nodiscard]] const TheaterDefinition& theaterDefinition(int index);

class ResourceFileSystem {
public:
    ResourceFileSystem();
    ~ResourceFileSystem();
    ResourceFileSystem(ResourceFileSystem&&) noexcept;
    ResourceFileSystem& operator=(ResourceFileSystem&&) noexcept;
    ResourceFileSystem(const ResourceFileSystem&) = delete;
    ResourceFileSystem& operator=(const ResourceFileSystem&) = delete;

    bool addDirectory(const std::filesystem::path& path);
    void loadYuriMixes();
    void loadTheater(const TheaterDefinition& theater);

    [[nodiscard]] bool contains(std::string_view filename) const;
    [[nodiscard]] std::vector<std::uint8_t> open(std::string_view filename) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rmg::native_assets
