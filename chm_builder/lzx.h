#pragma once

#include <cstdint>
#include <vector>

namespace lzx {

// Resultado da compressao de um fluxo LZX no formato usado por CHM.
struct Result {
    std::vector<uint8_t> stream;          // fluxo comprimido (já alinhado em words)
    std::vector<uint64_t> resetOffsets;   // offset comprimido de cada frame de 32 KiB
    uint64_t uncompressedSize = 0;        // tamanho REAL (sem padding) da entrada
    uint64_t paddedSize = 0;              // tamanho após padding para multiplo de 32 KiB
};

// Comprime 'input' em LZX com janela de 64 KiB e reset a cada 0x10000 bytes.
// windowBits so precisa ser 16 para o perfil CHM (documentado para futuras extensoes).
Result compress(const std::vector<uint8_t>& input);

} // namespace lzx
