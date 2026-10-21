#include "ra2formats/mix.h"

#include "ra2formats/byte_reader.h"
#include "ra2formats/error.h"

#include <openssl/blowfish.h>
#include <openssl/bn.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace ra2formats {
namespace {

constexpr std::uint32_t ChecksumFlag = 0x00010000;
constexpr std::uint32_t EncryptedFlag = 0x00020000;
constexpr std::size_t KeyBlockSize = 80;
constexpr std::size_t BlowfishKeySize = 56;
constexpr std::size_t EntrySize = 12;
constexpr std::size_t MaximumEntries = 1'000'000;
constexpr std::size_t RsaBlockSize = 40;
constexpr std::size_t RsaPayloadSize = 39;
constexpr std::array<std::uint8_t, RsaBlockSize> WestwoodPublicModulus = {
    0x51, 0xBC, 0xDA, 0x08, 0x6D, 0x39, 0xFC, 0xE4,
    0x56, 0x51, 0x60, 0xD6, 0x51, 0x71, 0x3F, 0xA2,
    0xE8, 0xAA, 0x54, 0xFA, 0x66, 0x82, 0xB0, 0x4A,
    0xAB, 0xDD, 0x0E, 0x6A, 0xF8, 0xB0, 0xC1, 0xE6,
    0xD1, 0xFB, 0x4F, 0x3D, 0xAA, 0x43, 0x7F, 0x15,
};

using BigNumber = std::unique_ptr<BIGNUM, decltype(&BN_free)>;
using BigNumberContext = std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)>;

std::array<std::uint8_t, BlowfishKeySize> decodeBlowfishKey(
    std::span<const std::uint8_t> keyBlock)
{
    if (keyBlock.size() != KeyBlockSize) {
        throw FormatError(ErrorCode::InvalidData, "MIX RSA key block 大小无效");
    }

    BigNumber modulus(
        BN_bin2bn(WestwoodPublicModulus.data(), static_cast<int>(WestwoodPublicModulus.size()), nullptr),
        &BN_free);
    BigNumber exponent(BN_new(), &BN_free);
    BigNumberContext context(BN_CTX_new(), &BN_CTX_free);
    if (!modulus || !exponent || !context || BN_set_word(exponent.get(), 0x10001U) != 1) {
        throw FormatError(ErrorCode::InvalidData, "MIX RSA 初始化失败");
    }

    std::array<std::uint8_t, RsaPayloadSize * 2> decoded = {};
    for (std::size_t offset = 0; offset < KeyBlockSize; offset += RsaBlockSize) {
        BigNumber input(
            BN_lebin2bn(keyBlock.data() + offset, static_cast<int>(RsaBlockSize), nullptr),
            &BN_free);
        BigNumber output(BN_new(), &BN_free);
        if (!input || !output || BN_mod_exp(output.get(), input.get(), exponent.get(), modulus.get(), context.get()) != 1) {
            throw FormatError(ErrorCode::InvalidData, "MIX RSA key block 解码失败");
        }

        std::array<std::uint8_t, RsaBlockSize> block = {};
        if (BN_bn2lebinpad(output.get(), block.data(), static_cast<int>(block.size())) < 0) {
            throw FormatError(ErrorCode::InvalidData, "MIX RSA key block 输出无效");
        }
        std::copy_n(block.begin(), RsaPayloadSize, decoded.begin() + static_cast<std::ptrdiff_t>(offset / RsaBlockSize * RsaPayloadSize));
    }

    std::array<std::uint8_t, BlowfishKeySize> key = {};
    std::copy_n(decoded.begin(), key.size(), key.begin());
    return key;
}

// OpenSSL 3 弃用了低层 BF_* API；MIX 格式兼容仍要求其固定 ECB 行为。
#if defined(OPENSSL_VERSION_MAJOR) && (OPENSSL_VERSION_MAJOR >= 3)
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#endif

std::vector<std::uint8_t> decryptBlocks(
    std::span<const std::uint8_t> encrypted,
    const std::array<std::uint8_t, BlowfishKeySize>& key)
{
    if (encrypted.empty() || encrypted.size() % 8 != 0) {
        throw FormatError(ErrorCode::InvalidData, "MIX 加密 header 未按 8 字节对齐");
    }

    BF_KEY blowfish = {};
    BF_set_key(&blowfish, static_cast<int>(key.size()), key.data());
    std::vector<std::uint8_t> result(encrypted.size());
    for (std::size_t offset = 0; offset < encrypted.size(); offset += 8) {
        BF_ecb_encrypt(
            encrypted.data() + offset,
            result.data() + offset,
            &blowfish,
            BF_DECRYPT);
    }
    return result;
}

#if defined(OPENSSL_VERSION_MAJOR) && (OPENSSL_VERSION_MAJOR >= 3)
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

std::size_t alignedEncryptedHeaderSize(std::uint16_t entryCount)
{
    const std::size_t raw = 6 + static_cast<std::size_t>(entryCount) * EntrySize;
    if (raw > std::numeric_limits<std::size_t>::max() - 7) {
        throw FormatError(ErrorCode::SizeLimitExceeded, "MIX header 大小溢出");
    }
    return (raw + 7) & ~static_cast<std::size_t>(7);
}

void parseEntries(ByteReader& header, std::uint16_t entryCount, MixArchive& result)
{
    result.orderedEntries.reserve(entryCount);
    result.entries.reserve(entryCount);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> occupiedRanges;
    occupiedRanges.reserve(entryCount);
    for (std::uint16_t index = 0; index < entryCount; ++index) {
        MixEntry entry {
            .hash = header.readU32Le(),
            .offset = header.readU32Le(),
            .length = header.readU32Le(),
        };
        if (entry.offset > result.dataSize || entry.length > result.dataSize - entry.offset) {
            throw FormatError(ErrorCode::InvalidData, "MIX entry 超出数据区");
        }
        if (!result.entries.emplace(entry.hash, entry).second) {
            throw FormatError(ErrorCode::InvalidData, "MIX entry ID 重复");
        }
        result.orderedEntries.push_back(entry);
        if (entry.length != 0) {
            occupiedRanges.emplace_back(entry.offset, static_cast<std::uint64_t>(entry.offset) + entry.length);
        }
    }
    std::sort(occupiedRanges.begin(), occupiedRanges.end());
    for (std::size_t index = 1; index < occupiedRanges.size(); ++index) {
        if (occupiedRanges[index].first < occupiedRanges[index - 1].second) {
            throw FormatError(ErrorCode::InvalidData, "MIX entry 数据范围重叠");
        }
    }
}

}

std::uint32_t mixFilenameHash(std::string_view filename)
{
    std::string padded(filename);
    for (char& character : padded) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    const std::size_t originalLength = padded.size();
    const std::size_t fullWords = originalLength >> 2;
    if ((originalLength & 3U) != 0) {
        padded.push_back(static_cast<char>(originalLength - (fullWords << 2)));
        for (std::size_t index = 0; index < 3 - (originalLength & 3U); ++index) {
            padded.push_back(padded[fullWords << 2]);
        }
    }

    std::uint32_t crc = 0xFFFFFFFFU;
    for (const unsigned char value : padded) {
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

const MixEntry* MixArchive::find(std::string_view filename) const
{
    const auto entry = entries.find(mixFilenameHash(filename));
    return entry == entries.end() ? nullptr : &entry->second;
}

std::span<const std::uint8_t> MixArchive::extract(
    std::span<const std::uint8_t> archive,
    std::string_view filename) const
{
    const MixEntry* entry = find(filename);
    if (entry == nullptr) {
        return {};
    }
    if (dataOffset > archive.size()
        || entry->offset > archive.size() - dataOffset
        || entry->length > archive.size() - dataOffset - entry->offset) {
        throw FormatError(ErrorCode::InvalidData, "MIX entry 读取范围越界");
    }
    return archive.subspan(dataOffset + entry->offset, entry->length);
}

MixArchive decodeMix(std::span<const std::uint8_t> bytes, std::size_t maximumTrailingBytes)
{
    if (bytes.size() < 6) {
        throw FormatError(ErrorCode::InvalidData, "MIX header 被截断");
    }
    ByteReader reader(bytes);
    const std::uint32_t signature = reader.readU32Le();
    const bool classic = (signature & 0xFFFFU) != 0;
    MixArchive result;
    std::uint16_t entryCount = 0;

    if (classic) {
        reader.seek(0);
        entryCount = reader.readU16Le();
        result.dataSize = reader.readU32Le();
        result.dataOffset = 6 + static_cast<std::size_t>(entryCount) * EntrySize;
        parseEntries(reader, entryCount, result);
    } else {
        if ((signature & ~(ChecksumFlag | EncryptedFlag)) != 0) {
            throw FormatError(ErrorCode::InvalidData, "MIX flags 无效");
        }
        result.checksum = (signature & ChecksumFlag) != 0;
        result.encrypted = (signature & EncryptedFlag) != 0;
        if (!result.encrypted) {
            entryCount = reader.readU16Le();
            result.dataSize = reader.readU32Le();
            result.dataOffset = 10 + static_cast<std::size_t>(entryCount) * EntrySize;
            parseEntries(reader, entryCount, result);
        } else {
            const std::span<const std::uint8_t> keyBlock = reader.readBytes(KeyBlockSize);
            const std::array<std::uint8_t, BlowfishKeySize> key = decodeBlowfishKey(keyBlock);
            const std::size_t encryptedHeaderStart = reader.position();
            const std::vector<std::uint8_t> firstBlock = decryptBlocks(reader.readBytes(8), key);
            ByteReader first(firstBlock);
            entryCount = first.readU16Le();
            result.dataSize = first.readU32Le();
            const std::size_t encryptedHeaderSize = alignedEncryptedHeaderSize(entryCount);
            if (encryptedHeaderSize > bytes.size() - encryptedHeaderStart) {
                throw FormatError(ErrorCode::InvalidData, "MIX 加密 header 被截断");
            }
            const std::vector<std::uint8_t> decryptedHeader = decryptBlocks(
                bytes.subspan(encryptedHeaderStart, encryptedHeaderSize),
                key);
            ByteReader header(decryptedHeader);
            if (header.readU16Le() != entryCount || header.readU32Le() != result.dataSize) {
                throw FormatError(ErrorCode::InvalidData, "MIX 加密 header 首块不一致");
            }
            result.dataOffset = encryptedHeaderStart + encryptedHeaderSize;
            parseEntries(header, entryCount, result);
        }
    }

    if (entryCount == 0 || entryCount > MaximumEntries) {
        throw FormatError(ErrorCode::InvalidData, "MIX entry 数量无效");
    }
    const std::size_t checksumSize = result.checksum ? 20 : 0;
    if (result.dataOffset > bytes.size()
        || result.dataSize > bytes.size() - result.dataOffset
        || checksumSize > bytes.size() - result.dataOffset - result.dataSize
        || bytes.size() - result.dataOffset - result.dataSize - checksumSize > maximumTrailingBytes) {
        throw FormatError(ErrorCode::InvalidData, "MIX 数据区长度与 header 不一致");
    }
    if (result.checksum) {
        std::array<std::uint8_t, SHA_DIGEST_LENGTH> digest = {};
        if (SHA1(bytes.data() + result.dataOffset, result.dataSize, digest.data()) == nullptr) {
            throw FormatError(ErrorCode::InvalidData, "MIX SHA-1 校验初始化失败");
        }
        const auto stored = bytes.subspan(result.dataOffset + result.dataSize, digest.size());
        if (!std::equal(digest.begin(), digest.end(), stored.begin())) {
            throw FormatError(ErrorCode::InvalidData, "MIX body SHA-1 校验失败");
        }
    }
    return result;
}

}
