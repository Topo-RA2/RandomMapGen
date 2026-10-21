#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ra2::pack {

inline constexpr std::size_t kDefaultMaxDecodedPackBytes = 64u * 1024u * 1024u;

std::vector<std::uint8_t> encodeFormat80(const std::vector<std::uint8_t>& source);
std::vector<std::uint8_t> encodeFormat5Format80(const std::vector<std::uint8_t>& source);
std::vector<std::uint8_t> encodeLzo1xLiteral(const std::vector<std::uint8_t>& source);
std::vector<std::uint8_t> encodeFormat5LzoLiteral(const std::vector<std::uint8_t>& source);
std::vector<std::uint8_t> encodeLzo1x(const std::vector<std::uint8_t>& source);
std::vector<std::uint8_t> encodeFormat5Lzo(const std::vector<std::uint8_t>& source);
std::vector<std::uint8_t> decodeFormat80(const std::vector<std::uint8_t>& source, std::size_t expectedSize);
std::vector<std::uint8_t> decodeFormat5Format80(const std::vector<std::uint8_t>& source);
std::vector<std::uint8_t> decodeFormat5Format80(const std::vector<std::uint8_t>& source, std::size_t maxOutputSize);
std::vector<std::uint8_t> decodeLzo1x(const std::vector<std::uint8_t>& source, std::size_t expectedSize);
std::vector<std::uint8_t> decodeFormat5Lzo(const std::vector<std::uint8_t>& source);
std::vector<std::uint8_t> decodeFormat5Lzo(const std::vector<std::uint8_t>& source, std::size_t maxOutputSize);
void appendU16LE(std::vector<std::uint8_t>& out, std::uint16_t value);
void appendU32LE(std::vector<std::uint8_t>& out, std::uint32_t value);
std::string base64Encode(const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> base64Decode(const std::string& text);
std::vector<std::uint8_t> base64Decode(std::string_view text, std::size_t maxOutputSize);
std::vector<std::string> splitEncodedSection(const std::string& encoded, std::size_t lineLength = 70);
std::array<std::uint8_t, 20> sha1Digest(std::string_view input);

} // namespace ra2::pack
