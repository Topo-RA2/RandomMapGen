#include "pack_codec/pack_encoding.h"
#include "rmg/map_evaluation.h"
#include "rmg/native_game_options.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void verifyPackCodecs()
{
    const std::vector<std::uint8_t> sample{
        0, 1, 2, 3, 3, 3, 3, 7, 8, 9, 9, 9, 9, 9, 9, 42,
    };
    require(
        ra2::pack::decodeFormat5Lzo(ra2::pack::encodeFormat5Lzo(sample)) == sample,
        "Format5/LZO round-trip failed");
    require(
        ra2::pack::decodeFormat5Format80(
            ra2::pack::encodeFormat5Format80(sample)) == sample,
        "Format5/Format80 round-trip failed");
    require(
        ra2::pack::base64Decode(ra2::pack::base64Encode(sample)) == sample,
        "Base64 round-trip failed");
}

} // namespace

int main()
{
    try {
        verifyPackCodecs();
        rmg::verifyMapEvaluation();
        rmg::verifyNativeGameOptions();
        std::cout << "RandomMapGen self-tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RandomMapGen self-test failed: " << error.what() << '\n';
        return 1;
    }
}
