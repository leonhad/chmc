extern "C" {
#include <mspack.h>
#include <system.h>
}

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::vector<uint8_t> readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Uso: test_chm <arquivo.chm> <diretorio-original>\n";
        return 2;
    }
    const std::string chmPath = argv[1];
    const fs::path sourceDir = argv[2];

    struct mschm_decompressor* chmd = mspack_create_chm_decompressor(mspack_default_system);
    if (!chmd) {
        std::cerr << "falha ao criar decompressor CHM\n";
        return 2;
    }
    struct mschmd_header* chm = chmd->open(chmd, chmPath.c_str());
    if (!chm) {
        std::cerr << "falha ao abrir CHM erro=" << chmd->last_error(chmd) << "\n";
        mspack_destroy_chm_decompressor(chmd);
        return 1;
    }

    std::cout << "sec0.offset=" << chm->sec0.offset
              << " dir_offset=" << chm->dir_offset
              << " chunks=" << chm->num_chunks << "\n";
    for (struct mschmd_file* f = chm->sysfiles; f; f = f->next) {
        std::cout << "SYS " << f->filename << " secao=" << f->section->id
                  << " offset=" << f->offset << " bytes=" << f->length << "\n";
    }

    int failures = 0;
    int checked = 0;
    for (struct mschmd_file* f = chm->files; f; f = f->next) {
        std::string name = f->filename;
        while (!name.empty() && name[0] == '/') name.erase(name.begin());
        if (name.empty() || name.rfind("::", 0) == 0 || name[0] == '#') {
            std::cout << "SISTEMA " << f->filename << " secao=" << f->section->id
                      << " bytes=" << f->length << "\n";
            continue;
        }
        const fs::path original = sourceDir / name;
        if (!fs::exists(original)) continue;

        const fs::path extracted = fs::temp_directory_path() / ("chmtest_" + name);
        const int err = chmd->extract(chmd, f, extracted.string().c_str());
        if (err != MSPACK_ERR_OK) {
            std::cerr << "FALHA extract " << f->filename << " erro=" << err << "\n";
            ++failures;
            continue;
        }
        const auto expected = readFile(original);
        const auto actual = readFile(extracted);
        if (expected != actual) {
            std::cerr << "DIVERGENCIA " << f->filename << " esperado=" << expected.size()
                      << " obtido=" << actual.size() << "\n";
            ++failures;
        } else {
            std::cout << "OK " << f->filename << " (" << actual.size() << " bytes)\n";
        }
        ++checked;
        std::error_code ec;
        fs::remove(extracted, ec);
    }

    chmd->close(chmd, chm);
    mspack_destroy_chm_decompressor(chmd);
    std::cerr << "verificados=" << checked << " falhas=" << failures << "\n";
    return failures == 0 && checked > 0 ? 0 : 1;
}
