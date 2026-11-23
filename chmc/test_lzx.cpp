#include "lzx.h"

extern "C" {
#include <mspack.h>
#include <system.h>
#include <lzx.h>
}

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    const std::string outBase = argc > 1 ? argv[1] : "lzx_test";

    const int total = argc > 2 ? std::stoi(argv[2]) : 75000;
    std::vector<uint8_t> input;
    for (int i = 0; i < total && i < 70000; ++i) {
        const int k = i % 97;
        if (k < 40) input.push_back(static_cast<uint8_t>('a' + (i / 40) % 26));
        else if (k < 70) input.push_back(static_cast<uint8_t>(i & 0xFF));
        else input.push_back(static_cast<uint8_t>((i * 7) & 0xFF));
    }
    for (int i = 0; i < 5000; ++i) input.push_back(static_cast<uint8_t>(i));

    const lzx::Result r = lzx::compress(input);
    const std::string compPath = outBase + ".lzx";
    {
        std::ofstream f(compPath, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char *>(r.stream.data()),
                static_cast<std::streamsize>(r.stream.size()));
    }

    struct mspack_system *sys = mspack_default_system;
    struct mspack_file *in = sys->open(sys, compPath.c_str(), MSPACK_SYS_OPEN_READ);
    const std::string decPath = outBase + ".out";
    struct mspack_file *out = sys->open(sys, decPath.c_str(), MSPACK_SYS_OPEN_WRITE);
    if (!in || !out) {
        std::cerr << "failed to open test files\n";
        return 2;
    }

    struct lzxd_stream *lz = lzxd_init(sys, in, out, 16, 2, 4096,
                                       static_cast<off_t>(r.paddedSize), 0);
    if (!lz) {
        std::cerr << "lzxd_init failed\n";
        return 3;
    }
    const int err = lzxd_decompress(lz, static_cast<off_t>(r.paddedSize));
    std::cerr << "lzxd_decompress error=" << err << " stream=" << r.stream.size()
            << " padded=" << r.paddedSize << " frames="
            << r.resetOffsets.size() << "\n";
    lzxd_free(lz);
    sys->close(in);
    sys->close(out);

    std::ifstream f(decPath, std::ios::binary);
    std::vector<uint8_t> decoded((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
    std::cerr << "decoded=" << decoded.size() << " expected=" << r.paddedSize << "\n";

    std::vector<uint8_t> expected = input;
    expected.resize(static_cast<size_t>(r.paddedSize), 0);
    if (err != MSPACK_ERR_OK || decoded != expected) {
        size_t at = 0;
        while (at < decoded.size() && at < expected.size() && decoded[at] == expected[at]) ++at;
        std::cerr << "DIVERGENCE at " << at << " (dec=" << (at < decoded.size() ? decoded[at] : -1)
                << ", exp=" << (at < expected.size() ? expected[at] : -1) << ")\n";
        return 1;
    }
    std::cerr << "OK\n";
    return 0;
}
