#include "pack_codec/pack_encoding.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace ra2::pack {
namespace {

constexpr bool isBase64Whitespace(unsigned char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\f' || value == '\v';
}

std::uint32_t rotateLeft32(std::uint32_t value, int bits) noexcept
{
    return (value << bits) | (value >> (32 - bits));
}

int countSame(const std::vector<std::uint8_t>& source, std::size_t offset, int maxCount)
{
    maxCount = std::min<int>(maxCount, static_cast<int>(source.size() - offset));
    if (maxCount <= 0) {
        return 0;
    }

    const auto first = source[offset++];
    int count = 1;
    while (count < maxCount && source[offset++] == first) {
        ++count;
    }
    return count;
}

void writeU16(std::vector<std::uint8_t>& out, std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void writeCopyBlocks(const std::vector<std::uint8_t>& source, std::size_t offset, std::size_t count, std::vector<std::uint8_t>& out)
{
    while (count > 0) {
        const auto writeNow = std::min<std::size_t>(count, 0x3F);
        out.push_back(static_cast<std::uint8_t>(0x80u | writeNow));
        out.insert(out.end(), source.begin() + static_cast<std::ptrdiff_t>(offset), source.begin() + static_cast<std::ptrdiff_t>(offset + writeNow));
        offset += writeNow;
        count -= writeNow;
    }
}

std::uint16_t readU16(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    if (offset > bytes.size() || bytes.size() - offset < 2) {
        throw std::runtime_error("unexpected end of uint16");
    }
    return static_cast<std::uint16_t>(bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
}

void copyFormat80FromOutput(std::vector<std::uint8_t>& out, std::size_t srcIndex, std::size_t count, std::size_t expectedSize)
{
    if (srcIndex >= out.size()) {
        throw std::runtime_error("Format80 copy source is outside decoded output");
    }
    if (out.size() > expectedSize || count > expectedSize - out.size()) {
        throw std::runtime_error("Format80 copy exceeds expected output size");
    }

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t readIndex = srcIndex + i;
        if (readIndex >= out.size()) {
            throw std::runtime_error("Format80 copy source overran decoded output");
        }
        out.push_back(out[readIndex]);
    }
}

void copyFormat80Literal(const std::vector<std::uint8_t>& source, std::size_t& offset, std::size_t count, std::vector<std::uint8_t>& out, std::size_t expectedSize)
{
    if (offset > source.size() || count > source.size() - offset) {
        throw std::runtime_error("Format80 literal block exceeds input");
    }
    if (out.size() > expectedSize || count > expectedSize - out.size()) {
        throw std::runtime_error("Format80 literal block exceeds expected output size");
    }

    out.insert(out.end(), source.begin() + static_cast<std::ptrdiff_t>(offset), source.begin() + static_cast<std::ptrdiff_t>(offset + count));
    offset += count;
}

std::size_t readLzoLength(const std::vector<std::uint8_t>& source, std::size_t& offset, std::size_t base)
{
    std::size_t length = base;
    while (offset < source.size() && source[offset] == 0) {
        if (length > std::numeric_limits<std::size_t>::max() - 255) {
            throw std::runtime_error("LZO length overflows size_t");
        }
        length += 255;
        ++offset;
    }
    if (offset >= source.size()) {
        throw std::runtime_error("invalid LZO length");
    }
    const std::size_t tail = source[offset++];
    if (length > std::numeric_limits<std::size_t>::max() - tail) {
        throw std::runtime_error("LZO length overflows size_t");
    }
    length += tail;
    return length;
}

void copyLzoLiteral(
    const std::vector<std::uint8_t>& source,
    std::size_t& offset,
    std::size_t count,
    std::vector<std::uint8_t>& out,
    std::size_t expectedSize)
{
    if (offset > source.size() || count > source.size() - offset) {
        throw std::runtime_error("LZO literal block exceeds input");
    }
    if (out.size() > expectedSize || count > expectedSize - out.size()) {
        throw std::runtime_error("LZO literal block exceeds expected output size");
    }
    out.insert(out.end(), source.begin() + static_cast<std::ptrdiff_t>(offset), source.begin() + static_cast<std::ptrdiff_t>(offset + count));
    offset += count;
}

void copyLzoMatch(std::vector<std::uint8_t>& out, std::size_t distance, std::size_t count, std::size_t expectedSize)
{
    if (distance == 0 || distance > out.size()) {
        throw std::runtime_error("invalid LZO match distance");
    }
    if (out.size() > expectedSize || count > expectedSize - out.size()) {
        throw std::runtime_error("LZO match exceeds expected output size");
    }

    const std::size_t start = out.size() - distance;
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(out[start + i]);
    }
}

void writeLzoLiteralRun(const std::vector<std::uint8_t>& source, std::size_t offset, std::size_t count, std::vector<std::uint8_t>& out, bool allowInitialLiteralCommand)
{
    if (count == 0) {
        return;
    }

    if (allowInitialLiteralCommand && out.empty() && count <= 238) {
        out.push_back(static_cast<std::uint8_t>(17 + count));
    } else if (count <= 18) {
        if (count < 4) {
            throw std::runtime_error("short LZO literal run requires match tail encoding");
        }
        out.push_back(static_cast<std::uint8_t>(count - 3));
    } else {
        out.push_back(0);
        std::size_t tail = count - 18;
        while (tail > 255) {
            out.push_back(0);
            tail -= 255;
        }
        out.push_back(static_cast<std::uint8_t>(tail));
    }

    out.insert(out.end(), source.begin() + static_cast<std::ptrdiff_t>(offset), source.begin() + static_cast<std::ptrdiff_t>(offset + count));
}

void writeLzoLengthExtension(std::vector<std::uint8_t>& out, std::size_t value, std::size_t base)
{
    if (value <= base) {
        throw std::runtime_error("invalid LZO extended length");
    }

    std::size_t tail = value - base;
    while (tail > 255) {
        out.push_back(0);
        tail -= 255;
    }
    out.push_back(static_cast<std::uint8_t>(tail));
}

struct LzoTrailingPatch {
    bool valid = false;
    std::size_t offset = 0;
};

void patchLzoTrailingCount(std::vector<std::uint8_t>& out, LzoTrailingPatch patch, std::size_t trailingLiteralCount)
{
    if (!patch.valid || trailingLiteralCount > 3 || patch.offset >= out.size()) {
        throw std::runtime_error("invalid LZO trailing literal patch");
    }
    out[patch.offset] = static_cast<std::uint8_t>((out[patch.offset] & 0xFCu) | trailingLiteralCount);
}

LzoTrailingPatch writeLzoMatch(std::vector<std::uint8_t>& out, std::size_t distance, std::size_t length, std::size_t trailingLiteralCount)
{
    if (distance == 0 || length < 3 || trailingLiteralCount > 3) {
        throw std::runtime_error("invalid LZO match");
    }

    if (distance <= 0x0800 && length <= 8) {
        const std::size_t encodedDistance = distance - 1;
        const std::size_t patchOffset = out.size();
        out.push_back(static_cast<std::uint8_t>(((length - 1) << 5) | ((encodedDistance & 7u) << 2) | trailingLiteralCount));
        out.push_back(static_cast<std::uint8_t>(encodedDistance >> 3));
        return {trailingLiteralCount == 0, patchOffset};
    }

    if (distance <= 0x4000) {
        const std::size_t matchCount = length - 2;
        if (matchCount <= 31) {
            out.push_back(static_cast<std::uint8_t>(0x20u | matchCount));
        } else {
            out.push_back(0x20);
            writeLzoLengthExtension(out, matchCount, 31);
        }
        const std::size_t encodedDistance = distance - 1;
        const std::uint16_t rawOffset = static_cast<std::uint16_t>((encodedDistance << 2) | trailingLiteralCount);
        const std::size_t patchOffset = out.size();
        writeU16(out, rawOffset);
        return {trailingLiteralCount == 0, patchOffset};
    }

    if (distance <= 0xC000) {
        const std::size_t encodedDistance = distance - 0x4000;
        const bool highDistance = encodedDistance >= 0x4000;
        const std::size_t offsetPart = highDistance ? encodedDistance - 0x4000 : encodedDistance;
        const std::size_t matchCount = length - 2;
        std::uint8_t command = static_cast<std::uint8_t>(0x10u | (highDistance ? 0x08u : 0u));
        if (matchCount <= 7) {
            command = static_cast<std::uint8_t>(command | matchCount);
            out.push_back(command);
        } else {
            out.push_back(command);
            writeLzoLengthExtension(out, matchCount, 7);
        }
        const std::uint16_t rawOffset = static_cast<std::uint16_t>((offsetPart << 2) | trailingLiteralCount);
        const std::size_t patchOffset = out.size();
        writeU16(out, rawOffset);
        return {trailingLiteralCount == 0, patchOffset};
    }

    throw std::runtime_error("LZO match distance is too large");
}

std::uint32_t threeByteKey(const std::vector<std::uint8_t>& source, std::size_t offset)
{
    return static_cast<std::uint32_t>(source[offset])
        | (static_cast<std::uint32_t>(source[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(source[offset + 2]) << 16);
}

void indexLiteralWindow(const std::vector<std::uint8_t>& source, std::size_t begin, std::size_t end, std::unordered_map<std::uint32_t, std::size_t>& lastSeen)
{
    if (end <= begin || source.size() < 3) {
        return;
    }

    const std::size_t cappedEnd = std::min(end, source.size() - 2);
    for (std::size_t pos = begin; pos < cappedEnd; ++pos) {
        lastSeen[threeByteKey(source, pos)] = pos;
    }
}

std::size_t trailingLiteralCountAfterMatch(std::size_t remainingAfterMatch)
{
    if (remainingAfterMatch <= 3) {
        return remainingAfterMatch;
    }
    return 0;
}

} // namespace

std::array<std::uint8_t, 20> sha1Digest(std::string_view input)
{
    std::vector<std::uint8_t> message(input.begin(), input.end());
    const std::uint64_t bitLength = static_cast<std::uint64_t>(message.size()) * 8u;
    message.push_back(0x80);
    while ((message.size() % 64u) != 56u) {
        message.push_back(0);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        message.push_back(static_cast<std::uint8_t>((bitLength >> shift) & 0xFFu));
    }

    std::uint32_t h0 = 0x67452301u;
    std::uint32_t h1 = 0xEFCDAB89u;
    std::uint32_t h2 = 0x98BADCFEu;
    std::uint32_t h3 = 0x10325476u;
    std::uint32_t h4 = 0xC3D2E1F0u;

    for (std::size_t chunk = 0; chunk < message.size(); chunk += 64) {
        std::array<std::uint32_t, 80> words{};
        for (int i = 0; i < 16; ++i) {
            const std::size_t offset = chunk + static_cast<std::size_t>(i) * 4u;
            words[static_cast<std::size_t>(i)] =
                (static_cast<std::uint32_t>(message[offset]) << 24)
                | (static_cast<std::uint32_t>(message[offset + 1]) << 16)
                | (static_cast<std::uint32_t>(message[offset + 2]) << 8)
                | static_cast<std::uint32_t>(message[offset + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            words[static_cast<std::size_t>(i)] = rotateLeft32(
                words[static_cast<std::size_t>(i - 3)]
                    ^ words[static_cast<std::size_t>(i - 8)]
                    ^ words[static_cast<std::size_t>(i - 14)]
                    ^ words[static_cast<std::size_t>(i - 16)],
                1);
        }

        std::uint32_t a = h0;
        std::uint32_t b = h1;
        std::uint32_t c = h2;
        std::uint32_t d = h3;
        std::uint32_t e = h4;
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const std::uint32_t temp = rotateLeft32(a, 5)
                + f
                + e
                + k
                + words[static_cast<std::size_t>(i)];
            e = d;
            d = c;
            c = rotateLeft32(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::array<std::uint8_t, 20> digest{};
    const std::array<std::uint32_t, 5> hash{h0, h1, h2, h3, h4};
    for (std::size_t i = 0; i < hash.size(); ++i) {
        digest[i * 4u] = static_cast<std::uint8_t>((hash[i] >> 24) & 0xFFu);
        digest[i * 4u + 1u] = static_cast<std::uint8_t>((hash[i] >> 16) & 0xFFu);
        digest[i * 4u + 2u] = static_cast<std::uint8_t>((hash[i] >> 8) & 0xFFu);
        digest[i * 4u + 3u] = static_cast<std::uint8_t>(hash[i] & 0xFFu);
    }
    return digest;
}

std::vector<std::uint8_t> encodeFormat80(const std::vector<std::uint8_t>& source)
{
    if (source.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::length_error("Format80 source exceeds the 16-bit chunk limit");
    }

    std::vector<std::uint8_t> out;
    out.reserve(source.size() + source.size() / 16 + 16);

    if (source.empty()) {
        out.push_back(0x80);
        return out;
    }

    out.push_back(0x81);
    out.push_back(source[0]);
    if (source.size() == 1) {
        out.push_back(0x80);
        return out;
    }

    std::size_t offset = 1;
    std::size_t literalHeaderOffset = 0;
    bool appendingLiteral = true;

    while (offset < source.size()) {
        std::size_t searchOffset = 0;
        std::size_t bestOffset = 0;
        std::size_t bestLength = 1;

        while (true) {
            const std::uint8_t value = source[offset];
            if (offset + 0x40 < source.size() && value == source[offset + 0x40]) {
                std::size_t runLength = 0;
                while (offset + runLength < source.size() && source[offset + runLength] == value) {
                    ++runLength;
                }
                if (offset + runLength == source.size()) {
                    --runLength;
                }
                if (runLength >= 0x41) {
                    appendingLiteral = false;
                    out.push_back(0xFE);
                    writeU16(out, static_cast<std::uint16_t>(runLength));
                    out.push_back(value);
                    offset += runLength;
                    continue;
                }
            }

            bool restartSearch = false;
            while (searchOffset != offset) {
                std::size_t found = searchOffset;
                while (found < offset && source[found] != value) {
                    ++found;
                }
                if (found == offset) {
                    searchOffset = offset;
                    break;
                }

                searchOffset = found + 1;
                if (source[offset + bestLength - 1] != source[found + bestLength - 1]) {
                    continue;
                }

                std::size_t matchLength = 0;
                while (offset + matchLength < source.size() && source[offset + matchLength] == source[found + matchLength]) {
                    ++matchLength;
                }
                if (matchLength >= bestLength) {
                    bestLength = matchLength;
                    bestOffset = found;
                }
                restartSearch = true;
                break;
            }
            if (restartSearch) {
                continue;
            }
            break;
        }

        if (bestLength <= 2) {
            if (!appendingLiteral) {
                literalHeaderOffset = out.size();
                out.push_back(0x80);
            }
            if (out[literalHeaderOffset] == 0xBF) {
                literalHeaderOffset = out.size();
                out.push_back(0x80);
            }
            ++out[literalHeaderOffset];
            out.push_back(source[offset++]);
            appendingLiteral = true;
            continue;
        }

        const std::size_t distance = offset - bestOffset;
        if (bestLength <= 0x0A && distance <= 0x0FFF) {
            out.push_back(static_cast<std::uint8_t>(((bestLength - 3) << 4) | (distance >> 8)));
            out.push_back(static_cast<std::uint8_t>(distance & 0xFFu));
        } else {
            if (bestLength > 0x40) {
                out.push_back(0xFF);
                writeU16(out, static_cast<std::uint16_t>(bestLength));
            } else {
                out.push_back(static_cast<std::uint8_t>(0xC0u | (bestLength - 3)));
            }
            writeU16(out, static_cast<std::uint16_t>(bestOffset));
        }
        offset += bestLength;
        appendingLiteral = false;
    }

    out.push_back(0x80);
    return out;
}

std::vector<std::uint8_t> encodeFormat5Format80(const std::vector<std::uint8_t>& source)
{
    std::vector<std::uint8_t> out;
    out.reserve(source.size() / 2);

    std::size_t offset = 0;
    while (offset < source.size()) {
        const std::size_t chunkSize = std::min<std::size_t>(source.size() - offset, 8192);
        std::vector<std::uint8_t> chunk(source.begin() + static_cast<std::ptrdiff_t>(offset), source.begin() + static_cast<std::ptrdiff_t>(offset + chunkSize));
        auto encoded = encodeFormat80(chunk);
        if (encoded.size() > 0xFFFF || chunkSize > 0xFFFF) {
            throw std::runtime_error("Format5 chunk too large");
        }

        writeU16(out, static_cast<std::uint16_t>(encoded.size()));
        writeU16(out, static_cast<std::uint16_t>(chunkSize));
        out.insert(out.end(), encoded.begin(), encoded.end());
        offset += chunkSize;
    }
    return out;
}

std::vector<std::uint8_t> decodeFormat80(const std::vector<std::uint8_t>& source, std::size_t expectedSize)
{
    std::vector<std::uint8_t> out;
    out.reserve(expectedSize);

    std::size_t offset = 0;
    bool sawEndMarker = false;
    while (offset < source.size()) {
        const std::uint8_t command = source[offset++];

        if ((command & 0x80u) == 0) {
            if (offset >= source.size()) {
                throw std::runtime_error("Format80 relative copy missing offset byte");
            }
            const std::size_t count = ((command & 0x70u) >> 4) + 3;
            const std::size_t distance = ((static_cast<std::size_t>(command & 0x0Fu)) << 8) + source[offset++];
            if (distance == 0 || distance > out.size()) {
                throw std::runtime_error("Format80 relative copy distance is invalid");
            }
            copyFormat80FromOutput(out, out.size() - distance, count, expectedSize);
            continue;
        }

        if ((command & 0x40u) == 0) {
            const std::size_t count = command & 0x3Fu;
            if (count == 0) {
                sawEndMarker = true;
                if (offset != source.size()) {
                    throw std::runtime_error("trailing data after Format80 end marker");
                }
                break;
            }
            copyFormat80Literal(source, offset, count, out, expectedSize);
            continue;
        }

        const std::uint8_t countBits = command & 0x3Fu;
        if (countBits < 0x3E) {
            const std::size_t count = static_cast<std::size_t>(countBits) + 3;
            const std::uint16_t srcIndex = readU16(source, offset);
            offset += 2;
            copyFormat80FromOutput(out, srcIndex, count, expectedSize);
            continue;
        }

        if (countBits == 0x3E) {
            const std::uint16_t count = readU16(source, offset);
            offset += 2;
            if (offset >= source.size()) {
                throw std::runtime_error("Format80 fill block missing byte");
            }
            if (out.size() > expectedSize || count > expectedSize - out.size()) {
                throw std::runtime_error("Format80 fill block exceeds expected output size");
            }
            out.insert(out.end(), count, source[offset++]);
            continue;
        }

        const std::uint16_t count = readU16(source, offset);
        offset += 2;
        const std::uint16_t srcIndex = readU16(source, offset);
        offset += 2;
        copyFormat80FromOutput(out, srcIndex, count, expectedSize);
    }

    if (!sawEndMarker) {
        throw std::runtime_error("Format80 chunk missing end marker");
    }
    if (out.size() != expectedSize) {
        throw std::runtime_error("Format80 decoded size mismatch");
    }
    return out;
}

std::vector<std::uint8_t> decodeFormat5Format80(const std::vector<std::uint8_t>& source)
{
    return decodeFormat5Format80(source, kDefaultMaxDecodedPackBytes);
}

std::vector<std::uint8_t> decodeFormat5Format80(const std::vector<std::uint8_t>& source, std::size_t maxOutputSize)
{
    maxOutputSize = std::min(maxOutputSize, kDefaultMaxDecodedPackBytes);
    std::vector<std::uint8_t> out;
    std::size_t offset = 0;
    while (offset < source.size()) {
        if (source.size() - offset < 4) {
            throw std::runtime_error("truncated Format5/80 chunk header");
        }
        const std::uint16_t encodedSize = readU16(source, offset);
        offset += 2;
        const std::uint16_t decodedSize = readU16(source, offset);
        offset += 2;
        if (encodedSize == 0 && decodedSize == 0) {
            if (offset != source.size()) {
                throw std::runtime_error("trailing data after Format5/80 end marker");
            }
            break;
        }
        if (encodedSize == 0 || decodedSize == 0) {
            throw std::runtime_error("invalid Format5/80 zero-sized chunk");
        }
        if (encodedSize > source.size() - offset) {
            throw std::runtime_error("Format5/80 chunk exceeds input");
        }
        if (out.size() > maxOutputSize || static_cast<std::size_t>(decodedSize) > maxOutputSize - out.size()) {
            throw std::length_error("Format5/80 output exceeds configured limit");
        }
        std::vector<std::uint8_t> chunk(source.begin() + static_cast<std::ptrdiff_t>(offset), source.begin() + static_cast<std::ptrdiff_t>(offset + encodedSize));
        auto decoded = decodeFormat80(chunk, decodedSize);
        out.insert(out.end(), decoded.begin(), decoded.end());
        offset += encodedSize;
    }
    return out;
}

void appendU16LE(std::vector<std::uint8_t>& out, std::uint16_t value)
{
    writeU16(out, value);
}

void appendU32LE(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

std::vector<std::uint8_t> encodeLzo1xLiteral(const std::vector<std::uint8_t>& source)
{
    if (source.empty()) {
        return {0x11, 0x00, 0x00};
    }

    std::vector<std::uint8_t> out;
    out.reserve(source.size() + source.size() / 255 + 8);

    const auto count = source.size();
    if (count <= 238) {
        out.push_back(static_cast<std::uint8_t>(17 + count));
    } else {
        out.push_back(0);
        std::size_t tail = count - 18;
        while (tail > 255) {
            out.push_back(0);
            tail -= 255;
        }
        out.push_back(static_cast<std::uint8_t>(tail));
    }

    out.insert(out.end(), source.begin(), source.end());
    out.push_back(0x11);
    out.push_back(0x00);
    out.push_back(0x00);
    return out;
}

std::vector<std::uint8_t> encodeFormat5LzoLiteral(const std::vector<std::uint8_t>& source)
{
    std::vector<std::uint8_t> out;
    out.reserve(source.size() + source.size() / 128 + 16);

    std::size_t offset = 0;
    while (offset < source.size()) {
        const std::size_t chunkSize = std::min<std::size_t>(source.size() - offset, 8192);
        std::vector<std::uint8_t> chunk(source.begin() + static_cast<std::ptrdiff_t>(offset), source.begin() + static_cast<std::ptrdiff_t>(offset + chunkSize));
        auto encoded = encodeLzo1xLiteral(chunk);
        if (encoded.size() > 0xFFFF || chunkSize > 0xFFFF) {
            throw std::runtime_error("Format5 LZO chunk too large");
        }

        writeU16(out, static_cast<std::uint16_t>(encoded.size()));
        writeU16(out, static_cast<std::uint16_t>(chunkSize));
        out.insert(out.end(), encoded.begin(), encoded.end());
        offset += chunkSize;
    }
    return out;
}

std::vector<std::uint8_t> encodeLzo1xWithDictionary(const std::vector<std::uint8_t>& source, std::vector<int>& dict)
{
    if (source.empty()) {
        return {0x11, 0x00, 0x00};
    }
    if (source.size() <= 13) {
        return encodeLzo1xLiteral(source);
    }

    std::vector<std::uint8_t> out;
    out.reserve(source.size());
    if (dict.size() != 0x4000) {
        dict.assign(0x4000, -1);
    }

    auto hashValueAt = [&source](std::size_t offset) -> std::uint32_t {
        return static_cast<std::uint32_t>(source[offset])
            ^ (static_cast<std::uint32_t>(source[offset + 1]) << 5)
            ^ (static_cast<std::uint32_t>(source[offset + 2]) << 10);
    };
    auto hashIndex = [](std::uint32_t value) -> std::uint32_t {
        return ((40799u * value) >> 5) & 0x3FFFu;
    };
    auto rollHash = [&source](std::uint32_t value, std::size_t position) -> std::uint32_t {
        return (value >> 5)
            ^ (static_cast<std::uint32_t>(source[position - 1]) >> 5)
            ^ (static_cast<std::uint32_t>(source[position + 2]) << 10);
    };

    std::size_t literalStart = 0;
    std::size_t position = 4;
    std::uint32_t currentHash = hashValueAt(0);
    LzoTrailingPatch pendingTrailingPatch;

    dict[hashIndex(currentHash)] = 0;
    currentHash = rollHash(currentHash, 1);
    dict[hashIndex(currentHash)] = 1;
    currentHash = rollHash(currentHash, 2);
    dict[hashIndex(currentHash)] = 2;
    currentHash = rollHash(currentHash, 3);
    dict[hashIndex(currentHash)] = 3;

    const std::size_t searchEnd = source.size() - 13;
    while (position < searchEnd) {
        const std::uint32_t key = hashIndex(currentHash);
        const int candidateIndex = dict[key];
        dict[key] = static_cast<int>(position);

        bool acceptedMatch = false;
        std::size_t matchLength = 0;
        std::size_t matchDistance = 0;
        if (candidateIndex >= 0) {
            const std::size_t candidate = static_cast<std::size_t>(candidateIndex);
            if (candidate < position) {
                matchDistance = position - candidate;
                if (matchDistance > 0
                    && matchDistance <= 0xBFFF
                    && source[candidate] == source[position]
                    && source[candidate + 1] == source[position + 1]
                    && source[candidate + 2] == source[position + 2]) {
                    const std::size_t literalCount = position - literalStart;
                    if (matchDistance <= 0x0800 || literalCount == 3 || source[candidate + 3] == source[position + 3]) {
                        matchLength = 3;
                        while (position + matchLength < source.size()
                            && source[candidate + matchLength] == source[position + matchLength]) {
                            ++matchLength;
                        }
                        acceptedMatch = matchDistance <= 0x0800 || literalCount == 3 || matchLength >= 4;
                    }
                }
            }
        }

        if (acceptedMatch) {
            const std::size_t literalCount = position - literalStart;
            if (literalCount > 0 && literalCount < 4) {
                if (pendingTrailingPatch.valid) {
                    patchLzoTrailingCount(out, pendingTrailingPatch, literalCount);
                    out.insert(out.end(),
                        source.begin() + static_cast<std::ptrdiff_t>(literalStart),
                        source.begin() + static_cast<std::ptrdiff_t>(position));
                    pendingTrailingPatch = {};
                } else {
                    ++position;
                    continue;
                }
            } else if (literalCount > 0) {
                pendingTrailingPatch = {};
                writeLzoLiteralRun(source, literalStart, position - literalStart, out, false);
            }

            pendingTrailingPatch = writeLzoMatch(out, matchDistance, matchLength, 0);
            position += matchLength;
            literalStart = position;
            if (position < searchEnd) {
                currentHash = hashValueAt(position);
            }
            continue;
        }

        ++position;
        if (position < searchEnd) {
            currentHash = rollHash(currentHash, position);
        }
    }

    const std::size_t remaining = source.size() - literalStart;
    if (remaining > 0) {
        if (remaining <= 3 && pendingTrailingPatch.valid) {
            patchLzoTrailingCount(out, pendingTrailingPatch, remaining);
            out.insert(out.end(),
                source.begin() + static_cast<std::ptrdiff_t>(literalStart),
                source.end());
        } else {
            writeLzoLiteralRun(source, literalStart, remaining, out, true);
        }
    }
    out.push_back(0x11);
    out.push_back(0x00);
    out.push_back(0x00);
    return out;
}

std::vector<std::uint8_t> encodeLzo1x(const std::vector<std::uint8_t>& source)
{
    std::vector<int> dict(0x4000, -1);
    return encodeLzo1xWithDictionary(source, dict);
}

std::vector<std::uint8_t> encodeFormat5Lzo(const std::vector<std::uint8_t>& source)
{
    std::vector<std::uint8_t> out;
    out.reserve(source.size() / 2);

    std::vector<int> dict(0x4000, -1);
    std::size_t offset = 0;
    while (offset < source.size()) {
        const std::size_t chunkSize = std::min<std::size_t>(source.size() - offset, 8192);
        std::vector<std::uint8_t> chunk(source.begin() + static_cast<std::ptrdiff_t>(offset), source.begin() + static_cast<std::ptrdiff_t>(offset + chunkSize));
        auto encoded = encodeLzo1xWithDictionary(chunk, dict);
        if (encoded.size() > 0xFFFF || chunkSize > 0xFFFF) {
            throw std::runtime_error("Format5 LZO chunk too large");
        }

        writeU16(out, static_cast<std::uint16_t>(encoded.size()));
        writeU16(out, static_cast<std::uint16_t>(chunkSize));
        out.insert(out.end(), encoded.begin(), encoded.end());
        offset += chunkSize;
    }
    return out;
}

std::vector<std::uint8_t> decodeLzo1x(const std::vector<std::uint8_t>& source, std::size_t expectedSize)
{
    if (source.empty()) {
        throw std::runtime_error("empty LZO chunk");
    }

    std::size_t offset = 0;
    std::vector<std::uint8_t> out;
    out.reserve(expectedSize);

    auto requireInput = [&source](std::size_t current, std::size_t count) {
        if (current > source.size() || count > source.size() - current) {
            throw std::runtime_error("LZO command exceeds input");
        }
    };

    std::uint8_t command = source[offset++];
    if (command > 17) {
        copyLzoLiteral(source, offset, static_cast<std::size_t>(command) - 17, out, expectedSize);
        if (offset >= source.size()) {
            throw std::runtime_error("LZO chunk missing end marker");
        }
        command = source[offset++];
    }

    for (;;) {
        if (command < 16) {
            std::size_t literalCount = command;
            if (literalCount == 0) {
                literalCount = readLzoLength(source, offset, 15);
            }
            literalCount += 3;
            copyLzoLiteral(source, offset, literalCount, out, expectedSize);
            if (offset >= source.size()) {
                throw std::runtime_error("LZO chunk missing match after literal run");
            }
            command = source[offset++];
            if (command < 16) {
                requireInput(offset, 1);
                const std::size_t distance = 1 + 0x0800 + (static_cast<std::size_t>(command) >> 2) + (static_cast<std::size_t>(source[offset++]) << 2);
                copyLzoMatch(out, distance, 3, expectedSize);
                const std::size_t trailingLiteralCount = command & 3u;
                if (trailingLiteralCount > 0) {
                    copyLzoLiteral(source, offset, trailingLiteralCount, out, expectedSize);
                }
                if (offset >= source.size()) {
                    throw std::runtime_error("LZO chunk missing command after short match");
                }
                command = source[offset++];
                continue;
            }
        }

        std::size_t trailingLiteralCount = 0;
        if (command >= 64) {
            requireInput(offset, 1);
            const std::size_t distance = 1 + ((static_cast<std::size_t>(command) >> 2) & 7u) + (static_cast<std::size_t>(source[offset++]) << 3);
            const std::size_t matchCount = (static_cast<std::size_t>(command) >> 5) + 1;
            copyLzoMatch(out, distance, matchCount, expectedSize);
            trailingLiteralCount = command & 3u;
        } else if (command >= 32) {
            std::size_t matchCount = command & 31u;
            if (matchCount == 0) {
                matchCount = readLzoLength(source, offset, 31);
            }
            requireInput(offset, 2);
            const std::uint16_t rawOffset = readU16(source, offset);
            offset += 2;
            const std::size_t distance = 1 + (static_cast<std::size_t>(rawOffset) >> 2);
            copyLzoMatch(out, distance, matchCount + 2, expectedSize);
            trailingLiteralCount = rawOffset & 3u;
        } else if (command >= 16) {
            std::size_t matchCount = command & 7u;
            if (matchCount == 0) {
                matchCount = readLzoLength(source, offset, 7);
            }
            requireInput(offset, 2);
            const std::uint16_t rawOffset = readU16(source, offset);
            offset += 2;
            std::size_t distance = (static_cast<std::size_t>(rawOffset) >> 2) + ((static_cast<std::size_t>(command & 8u)) << 11);
            if (distance == 0) {
                if (matchCount != 1 || (rawOffset & 3u) != 0 || out.size() != expectedSize) {
                    throw std::runtime_error("invalid LZO end marker");
                }
                if (offset != source.size()) {
                    throw std::runtime_error("trailing data after LZO end marker");
                }
                return out;
            }
            distance += 0x4000;
            copyLzoMatch(out, distance, matchCount + 2, expectedSize);
            trailingLiteralCount = rawOffset & 3u;
        } else {
            requireInput(offset, 1);
            const std::size_t distance = 1 + (static_cast<std::size_t>(command) >> 2) + (static_cast<std::size_t>(source[offset++]) << 2);
            copyLzoMatch(out, distance, 2, expectedSize);
            trailingLiteralCount = command & 3u;
        }

        if (trailingLiteralCount > 0) {
            copyLzoLiteral(source, offset, trailingLiteralCount, out, expectedSize);
        }
        if (offset >= source.size()) {
            throw std::runtime_error("LZO chunk missing next command");
        }
        command = source[offset++];
    }

    if (out.size() != expectedSize) {
        throw std::runtime_error("LZO decoded size mismatch");
    }
    return out;
}

std::vector<std::uint8_t> decodeFormat5Lzo(const std::vector<std::uint8_t>& source)
{
    return decodeFormat5Lzo(source, kDefaultMaxDecodedPackBytes);
}

std::vector<std::uint8_t> decodeFormat5Lzo(const std::vector<std::uint8_t>& source, std::size_t maxOutputSize)
{
    maxOutputSize = std::min(maxOutputSize, kDefaultMaxDecodedPackBytes);
    std::vector<std::uint8_t> out;
    std::size_t offset = 0;
    while (offset < source.size()) {
        if (source.size() - offset < 4) {
            throw std::runtime_error("truncated Format5/LZO chunk header");
        }
        const std::uint16_t encodedSize = readU16(source, offset);
        offset += 2;
        const std::uint16_t decodedSize = readU16(source, offset);
        offset += 2;
        if (encodedSize == 0 && decodedSize == 0) {
            if (offset != source.size()) {
                throw std::runtime_error("trailing data after Format5/LZO end marker");
            }
            break;
        }
        if (encodedSize == 0 || decodedSize == 0) {
            throw std::runtime_error("invalid Format5/LZO zero-sized chunk");
        }
        if (encodedSize > source.size() - offset) {
            throw std::runtime_error("Format5/LZO chunk exceeds input");
        }
        if (out.size() > maxOutputSize || static_cast<std::size_t>(decodedSize) > maxOutputSize - out.size()) {
            throw std::length_error("Format5/LZO output exceeds configured limit");
        }
        std::vector<std::uint8_t> chunk(source.begin() + static_cast<std::ptrdiff_t>(offset), source.begin() + static_cast<std::ptrdiff_t>(offset + encodedSize));
        auto decoded = decodeLzo1x(chunk, decodedSize);
        out.insert(out.end(), decoded.begin(), decoded.end());
        offset += encodedSize;
    }
    return out;
}

std::string base64Encode(const std::vector<std::uint8_t>& bytes)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);

    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t b0 = bytes[i];
        const std::uint32_t b1 = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
        const std::uint32_t b2 = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;
        const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back((i + 1 < bytes.size()) ? kAlphabet[(triple >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < bytes.size()) ? kAlphabet[triple & 0x3F] : '=');
    }
    return out;
}

std::vector<std::uint8_t> base64Decode(const std::string& text)
{
    return base64Decode(std::string_view(text), kDefaultMaxDecodedPackBytes);
}

std::vector<std::uint8_t> base64Decode(std::string_view text, std::size_t maxOutputSize)
{
    std::array<int, 256> table{};
    table.fill(-1);
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; ++i) {
        table[static_cast<unsigned char>(kAlphabet[i])] = i;
    }

    std::size_t encodedSize = 0;
    std::array<unsigned char, 2> tail = {};
    for (const unsigned char ch : text) {
        if (isBase64Whitespace(ch)) {
            continue;
        }
        tail[0] = tail[1];
        tail[1] = ch;
        if (encodedSize == std::numeric_limits<std::size_t>::max()) {
            throw std::length_error("base64 input is too large");
        }
        ++encodedSize;
    }
    if ((encodedSize & 3U) != 0) {
        throw std::runtime_error("base64 length is not a multiple of four");
    }
    const std::size_t quantumCount = encodedSize / 4;
    if (quantumCount > std::numeric_limits<std::size_t>::max() / 3) {
        throw std::length_error("base64 output size overflow");
    }
    const std::size_t padding = encodedSize == 0 ? 0
        : tail[0] == '=' ? 2
        : tail[1] == '=' ? 1
        : 0;
    const std::size_t decodedSize = quantumCount * 3 - padding;
    if (decodedSize > maxOutputSize) {
        throw std::length_error("base64 output exceeds configured limit");
    }

    std::vector<std::uint8_t> out;
    out.reserve(decodedSize);
    std::array<unsigned char, 4> quantum{};
    std::size_t quantumSize = 0;
    bool sawPadding = false;
    for (unsigned char ch : text) {
        if (isBase64Whitespace(ch)) {
            continue;
        }
        if (sawPadding) {
            throw std::runtime_error("trailing data after base64 padding");
        }
        quantum[quantumSize++] = ch;
        if (quantumSize != quantum.size()) {
            continue;
        }

        const int a = table[quantum[0]];
        const int b = table[quantum[1]];
        const int c = quantum[2] == '=' ? 0 : table[quantum[2]];
        const int d = quantum[3] == '=' ? 0 : table[quantum[3]];
        if (a < 0 || b < 0 || c < 0 || d < 0 || quantum[0] == '=' || quantum[1] == '=') {
            throw std::runtime_error("invalid base64 character");
        }
        if (quantum[2] == '=' && quantum[3] != '=') {
            throw std::runtime_error("invalid base64 padding");
        }

        const std::uint32_t value = (static_cast<std::uint32_t>(a) << 18)
            | (static_cast<std::uint32_t>(b) << 12)
            | (static_cast<std::uint32_t>(c) << 6)
            | static_cast<std::uint32_t>(d);
        out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
        if (quantum[2] == '=') {
            if ((b & 0x0F) != 0) {
                throw std::runtime_error("non-zero unused bits in base64 padding");
            }
            sawPadding = true;
        } else {
            out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
            if (quantum[3] == '=') {
                if ((c & 0x03) != 0) {
                    throw std::runtime_error("non-zero unused bits in base64 padding");
                }
                sawPadding = true;
            } else {
                out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
            }
        }
        quantumSize = 0;
    }
    if (quantumSize != 0 || out.size() != decodedSize) {
        throw std::runtime_error("base64 decoded size mismatch");
    }
    return out;
}

std::vector<std::string> splitEncodedSection(const std::string& encoded, std::size_t lineLength)
{
    if (lineLength == 0) {
        throw std::invalid_argument("encoded section line length must be greater than zero");
    }

    std::vector<std::string> lines;
    for (std::size_t i = 0; i < encoded.size(); i += lineLength) {
        lines.push_back(encoded.substr(i, std::min(lineLength, encoded.size() - i)));
    }
    return lines;
}

} // namespace ra2::pack
