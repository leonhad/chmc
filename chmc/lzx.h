#pragma once

#include <cstdint>
#include <vector>

namespace lzx {

// Result of compressing an LZX stream in the format used by CHM.
struct Result {
    std::vector<uint8_t> stream;          // compressed stream (already aligned to words)
    std::vector<uint64_t> resetOffsets;   // compressed offset of each 32 KiB frame
    uint64_t uncompressedSize = 0;        // REAL size (without padding) of the input
    uint64_t paddedSize = 0;              // size after padding to multiple of 32 KiB
};

// Compresses 'input' in LZX with a 64 KiB window and reset every 0x10000 bytes.
// windowBits only needs to be 16 for the CHM profile (documented for future extensions).
Result compress(const std::vector<uint8_t>& input);

} // namespace lzx
