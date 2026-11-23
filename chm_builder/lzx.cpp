#include "lzx.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <queue>

namespace lzx {
namespace {

// ---------------------------------------------------------------------------
// Constantes do perfil LZX usado pelo CHM
// ---------------------------------------------------------------------------
constexpr int kFrameSize           = 32768;  // 0x8000 bytes por frame
constexpr int kResetFrames         = 2;      // reset a cada 2 frames (0x10000 bytes)
constexpr int kWindowBits          = 16;     // janela de 64 KiB
constexpr int kWindowSize          = 1 << kWindowBits;
constexpr int kNumChars            = 256;    // literais
constexpr int kNumPrimaryLengths   = 7;      // cabecalhos de comprimento 2..8
constexpr int kNumSecondaryLengths = 249;    // simbolos da Length Tree (9..257)
constexpr int kPretreeSymbols      = 20;
constexpr int kMaxMatch            = 257;
constexpr int kBlockTypeVerbatim   = 1;

// Número de slots de posição para uma janela de 2^16 bytes (ver libmspack).
constexpr int kNumPositionSlots = 32;
constexpr int kMainTreeSymbols  = kNumChars + 8 * kNumPositionSlots; // 496

int extraBitsForSlot(int slot) {
    if (slot < 4) return 0;
    if (slot >= 36) return 17;
    return slot / 2 - 1;
}

// position_base[slot]: menor "formatted offset" representado pelo slot.
// formatted offset = offset + 2 (ou 0/1/2 para R0/R1/R2).
std::vector<uint32_t> makePositionBase(int slots) {
    std::vector<uint32_t> base(slots + 1, 0);
    for (int i = 1; i <= slots; ++i) {
        base[i] = base[i - 1] + (1u << extraBitsForSlot(i - 1));
    }
    return base;
}

// ---------------------------------------------------------------------------
// Escritor de bits: MSB primeiro, words little-endian de 16 bits
// ---------------------------------------------------------------------------
class BitWriter {
public:
    void putBit(unsigned int bit) {
        word_ = static_cast<uint16_t>((word_ << 1) | (bit & 1u));
        if (++bits_ == 16) flushWord();
    }

    void putBits(uint32_t value, int count) {
        for (int i = count - 1; i >= 0; --i) putBit((value >> i) & 1u);
    }

    // O decodificador realinha a cada frame para uma fronteira de 16 bits.
    void align16() {
        if (bits_ != 0) {
            word_ = static_cast<uint16_t>(word_ << (16 - bits_));
            flushWord();
        }
    }

    [[nodiscard]] size_t size() const { return bytes_.size(); }
    [[nodiscard]] const std::vector<uint8_t>& bytes() const { return bytes_; }

private:
    void flushWord() {
        // LZX le words little-endian e consome os bits de MSB para LSB.
        bytes_.push_back(static_cast<uint8_t>(word_));
        bytes_.push_back(static_cast<uint8_t>(word_ >> 8));
        word_ = 0;
        bits_ = 0;
    }

    std::vector<uint8_t> bytes_;
    uint16_t word_ = 0;
    int bits_ = 0;
};

// ---------------------------------------------------------------------------
// Huffman canonico
// ---------------------------------------------------------------------------
struct Huffman {
    std::vector<uint8_t> length;
    std::vector<uint32_t> code;
    bool complete = false;
};

// Comprimentos "fixos" completos: d = ceil(log2(n)); alguns simbolos recebem
// d-1 bits de modo que a soma de Kraft seja exatamente 1.
std::vector<uint8_t> fixedLengths(int n) {
    int d = 0;
    while ((1 << d) < n) ++d;
    std::vector<uint8_t> len(n, static_cast<uint8_t>(d));
    const int promote = (1 << d) - n;
    for (int i = 0; i < promote; ++i) len[i] = static_cast<uint8_t>(d - 1);
    return len;
}

std::vector<uint8_t> computeLengths(const std::vector<uint32_t>& freq, int maxBits = 16) {
    const size_t n = freq.size();
    std::vector<uint8_t> len(n, 0);
    std::vector<int> syms;
    syms.reserve(n);
    for (size_t i = 0; i < n; ++i) if (freq[i] > 0) syms.push_back(static_cast<int>(i));
    if (syms.empty()) return len;
    if (syms.size() == 1) { len[syms[0]] = 1; return len; }

    struct Node { uint64_t w; int sym; Node* l; Node* r; };
    struct Cmp { bool operator()(Node* a, Node* b) const { return a->w > b->w; } };
    std::priority_queue<Node*, std::vector<Node*>, Cmp> pq;
    std::vector<Node*> all;
    all.reserve(2*n);
    for (int s : syms) { Node* nd = new Node{freq[s], s, nullptr, nullptr}; all.push_back(nd); pq.push(nd); }
    while (pq.size() > 1) {
        Node* a = pq.top(); pq.pop();
        Node* b = pq.top(); pq.pop();
        Node* nd = new Node{a->w + b->w, -1, a, b};
        all.push_back(nd); pq.push(nd);
    }
    std::function<void(Node*,int)> walk = [&](Node* nd, int d) {
        if (!nd) return;
        if (nd->sym >= 0) { len[nd->sym] = static_cast<uint8_t>(d); return; }
        walk(nd->l, d+1); walk(nd->r, d+1);
    };
    walk(pq.top(), 0);
    for (Node* nd : all) delete nd;

    // Limit to maxBits by redistributing Kraft sum.
    int over = 0;
    for (size_t i = 0; i < n; ++i) if (len[i] > maxBits) over++;
    if (over > 0) {
        for (size_t i = 0; i < n; ++i) if (len[i] > maxBits) len[i] = static_cast<uint8_t>(maxBits);
    }
    // Fix Kraft sum to 1<<16 (2^maxBits).
    auto kraft = [&](void)->int {
        int k=0; for (uint8_t l:len) if(l) k += 1 << (maxBits - l); return k;
    };
    int k = kraft();
    const int target = 1 << maxBits;
    // If oversubscribed, increase lengths of least-frequent symbols.
    while (k > target) {
        int best = -1;
        for (int s : syms) {
            if (len[s] < maxBits && (best < 0 || freq[s] < freq[best] || (freq[s]==freq[best] && len[s] < len[best]))) best=s;
        }
        if (best < 0) break;
        len[best]++;
        k = kraft();
    }
    // If undersubscribed, decrease lengths of most-frequent/shortest feasible symbols.
    while (k < target) {
        int best = -1;
        for (int s : syms) {
            if (len[s] > 1) {
                int nk = k + (1 << (maxBits - len[s])); // decreasing len by 1 adds 2^(maxBits-l) to kraft? wait: delta = 2^(maxBits-(l-1)) - 2^(maxBits-l)=2^(maxBits-l)
                if (nk <= target) {
                    if (best < 0 || freq[s] > freq[best] || (freq[s]==freq[best] && len[s]>len[best])) best=s;
                }
            }
        }
        if (best < 0) {
            // allow overshoot fix not possible; break
            break;
        }
        k += (1 << (maxBits - len[best]));
        len[best]--;
    }
    // Final exact fix
    k = kraft();
    while (k != target) {
        bool changed=false;
        if (k < target) {
            for (int s : syms) {
                if (len[s] > 1) { int d=1<<(maxBits-len[s]); if (k+d<=target){k+=d; len[s]--; changed=true; break;} }
            }
        } else {
            for (int s : syms) {
                if (len[s] < maxBits) { int d=1<<(maxBits-len[s]-1); if (k-d>=0){k-=d; len[s]++; changed=true; break;} }
            }
        }
        if(!changed) break;
    }
    return len;
}

Huffman buildHuffman(const std::vector<uint8_t>& len) {
    Huffman h;
    h.length = len;
    h.code.assign(len.size(), 0);

    std::array<int, 17> count{};
    for (uint8_t l : len) if (l > 0 && l <= 16) ++count[l];

    uint32_t next = 0;
    std::array<uint32_t, 17> first{};
    for (int bits = 1; bits <= 16; ++bits) {
        next = (next + (bits == 1 ? 0u : static_cast<uint32_t>(count[bits - 1]))) << 1;
        first[bits] = next;
    }
    for (size_t s = 0; s < len.size(); ++s) {
        const int l = len[s];
        if (l == 0) continue;
        h.code[s] = first[l]++;
    }

    int kraft = 0;
    for (uint8_t l : len) if (l > 0) kraft += 1 << (16 - l);
    h.complete = (kraft == (1 << 16));
    return h;
}

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------
struct Token {
    bool isMatch = false;
    uint32_t mainSymbol = 0;    // literal (0..255) ou 256 + (slot<<3) + lenHeader
    int lengthSymbol = -1;      // simbolo da Length Tree, -1 se nao usado
    uint32_t footer = 0;        // bits extras do offset
    int extraBits = 0;
    uint32_t offset = 0;        // offset real do match (para atualizar R0..R2)
    int length = 1;             // bytes emitidos (literal ou match)
};

struct LzState {
    uint32_t R0 = 1, R1 = 1, R2 = 1;

    void update(int slot, uint32_t offset) {
        if (slot == 0) {
            // R0 inalterado
        } else if (slot == 1) {
            R1 = R0; R0 = offset;
        } else if (slot == 2) {
            R2 = R0; R0 = offset;
        } else {
            R2 = R1; R1 = R0; R0 = offset;
        }
    }

    int slotFor(uint32_t offset, uint32_t& formatted) const {
        if (offset == R0) { formatted = 0; return 0; }
        if (offset == R1) { formatted = 1; return 1; }
        if (offset == R2) { formatted = 2; return 2; }
        formatted = offset + 2;
        return static_cast<int>(std::upper_bound(kBase.begin() + 3, kBase.end(),
                                                 formatted) - kBase.begin()) - 1;
    }

    std::vector<uint32_t> kBase = makePositionBase(kNumPositionSlots);
};

uint32_t hash3(const uint8_t* p) {
    return (((static_cast<uint32_t>(p[0]) << 8) |
             (static_cast<uint32_t>(p[1]) << 4) |
             static_cast<uint32_t>(p[2])) & 0xFFFFu);
}

// Tokeniza um intervalo de reset (2 frames, 64 KiB). O historico LZ e
// reiniciado apenas no início de cada intervalo.
std::vector<Token> tokenizeFrame(const uint8_t* data, size_t len,
                                 const std::vector<uint32_t>& base,
                                 LzState& lz) {
    std::vector<Token> tokens;
    tokens.reserve(len / 3);

    constexpr int kHashBits = 16;
    constexpr int kHashSize = 1 << kHashBits;
    std::vector<int> head(kHashSize, -1);
    std::vector<int> prev(len, -1);

    auto insert = [&](size_t p) {
        if (p + 2 >= len) return;
        const uint32_t h = hash3(data + p);
        prev[p] = head[h];
        head[h] = static_cast<int>(p);
    };

    const auto mainFixed = fixedLengths(kMainTreeSymbols);
    const auto lenFixed = fixedLengths(kNumSecondaryLengths);
    auto matchBits = [&](int l, uint32_t dist) -> int {
        uint32_t fo = dist + 2;
        int slot = 0;
        for (int s = kNumPositionSlots - 1; s >= 0; --s) { if (base[s] <= fo) { slot = s; break; } }
        const int extra = extraBitsForSlot(slot);
        const int lenHeader = std::min(l - 2, 7);
        const uint32_t mainSym = kNumChars + (static_cast<uint32_t>(slot) << 3) + static_cast<uint32_t>(lenHeader);
        int bits = mainFixed[mainSym] + extra;
        if (lenHeader == kNumPrimaryLengths) bits += lenFixed[l - 9];
        return bits;
    };
    auto findBest = [&](size_t p, int& outLen, uint32_t& outOff, int& outBits) {
        outLen = 0; outOff = 0; outBits = 0;
        if (p + 2 >= len) return;
        const uint32_t h = hash3(data + p);
        int cand = head[h];
        const size_t frameEnd = ((p / kFrameSize) + 1) * kFrameSize;
        const int maxLen = static_cast<int>(std::min<size_t>(
            kMaxMatch, std::min<size_t>(len - p, frameEnd - p)));
        int examined = 0;
        while (cand >= 0 && examined++ < 256) {
            const uint32_t dist = static_cast<uint32_t>(p - static_cast<size_t>(cand));
            if (dist > static_cast<uint32_t>(kWindowSize) - 3u) break;
            if (outLen >= maxLen) break;
            if (data[cand + outLen] == data[p + outLen] || outLen == 0) {
                int l = 0;
                while (l < maxLen && data[cand + l] == data[p + l]) ++l;
                if (l >= 3) {
                    const int b = matchBits(l, dist);
                    if (outLen == 0 || b * outLen < outBits * l || (b * outLen == outBits * l && l > outLen)) {
                        outLen = l; outOff = dist; outBits = b;
                        if (l == maxLen) break;
                    }
                }
            }
            cand = prev[cand];
        }
    };

    size_t pos = 0;
    while (pos < len) {
        int bestLen = 0; uint32_t bestOffset = 0; int bestBits = 0;
        findBest(pos, bestLen, bestOffset, bestBits);

        bool posInserted = false;
        if (bestLen >= 3 && bestLen < kMaxMatch && pos + 1 < len) {
            int nextLen = 0; uint32_t nextOff = 0; int nextBits = 0;
            insert(pos); posInserted = true;
            findBest(pos + 1, nextLen, nextOff, nextBits);
            // literal at pos + match at pos+1 vs match at pos: prefer if next is cheaper per byte
            const int litCost = mainFixed[data[pos]];
            if (nextLen >= 3) {
                const int curCost = bestBits;
                const int altCost = litCost + nextBits;
                // compare coverage: cur covers bestLen, alt covers 1+nextLen (approx) - use simple heuristic
                if (nextLen > bestLen + 1 && altCost * bestLen < curCost * (1 + nextLen)) {
                    Token t; t.isMatch = false; t.mainSymbol = data[pos]; tokens.push_back(t); ++pos; continue;
                }
            }
        }

        if (bestLen >= 3) {
            uint32_t formatted = 0;
            const int slot = lz.slotFor(bestOffset, formatted);
            const int extra = extraBitsForSlot(slot);
            const uint32_t footer = (slot < 3) ? 0u : (formatted - base[slot]);
            const int lenHeader = std::min(bestLen - 2, 7);

            Token t;
            t.isMatch = true;
            t.mainSymbol = kNumChars + (static_cast<uint32_t>(slot) << 3) +
                           static_cast<uint32_t>(lenHeader);
            if (lenHeader == kNumPrimaryLengths) {
                t.lengthSymbol = bestLen - 9;
            }
            t.footer = footer;
            t.extraBits = extra;
            t.offset = bestOffset;
            t.length = bestLen;
            tokens.push_back(t);
            lz.update(slot, bestOffset);

            if (!posInserted) insert(pos);
            for (int k = 1; k < bestLen; ++k) insert(pos + k);
            pos += bestLen;
        } else {
            Token t;
            t.isMatch = false;
            t.mainSymbol = data[pos];
            tokens.push_back(t);
            if (!posInserted) insert(pos);
            ++pos;
        }
    }
    return tokens;
}

// ---------------------------------------------------------------------------
// Escrita das árvores (pre-trees + comprimentos diferenciais)
// ---------------------------------------------------------------------------
struct TreeWriter {
    BitWriter& bw;
    Huffman pre;
    std::vector<uint8_t> preLen;

    explicit TreeWriter(BitWriter& b) : bw(b) {
        preLen = fixedLengths(kPretreeSymbols); // 12 simbolos com 4 bits, 8 com 5
        pre = buildHuffman(preLen);
    }

    void writePretreeHeader() {
        for (int i = 0; i < kPretreeSymbols; ++i) bw.putBits(preLen[i], 4);
    }

    void direct(uint8_t target, uint8_t& previous) {
        const int sym = (static_cast<int>(previous) - static_cast<int>(target) + 17) % 17;
        bw.putBits(pre.code[sym], pre.length[sym]);
        previous = target;
    }

    // Escreve os comprimentos de 'first' até 'first+count-1'. 'previous' é o
    // estado persistente do decodificador (zero no início de cada frame).
    void write(int first, int count, const std::vector<uint8_t>& lengths,
               std::vector<uint8_t>& previous) {
        writePretreeHeader();
        int i = first;
        const int last = first + count;
        while (i < last) {
            if (lengths[i] != 0) {
                direct(lengths[i], previous[i]);
                ++i;
                continue;
            }
            int run = 0;
            while (i + run < last && lengths[i + run] == 0) ++run;
            if (run >= 4) {
                while (run > 0) {
                    if (run >= 20) {
                        const int n = std::min(run, 51);
                        bw.putBits(pre.code[18], pre.length[18]);
                        bw.putBits(static_cast<uint32_t>(n - 20), 5);
                        for (int k = 0; k < n; ++k) previous[i + k] = 0;
                        i += n; run -= n;
                    } else {
                        const int n = std::min(run, 19);
                        bw.putBits(pre.code[17], pre.length[17]);
                        bw.putBits(static_cast<uint32_t>(n - 4), 4);
                        for (int k = 0; k < n; ++k) previous[i + k] = 0;
                        i += n; run -= n;
                    }
                }
            } else {
                direct(0, previous[i]);
                ++i;
            }
        }
    }
};

// ---------------------------------------------------------------------------
// Um frame LZX independente
// ---------------------------------------------------------------------------
void encodeInterval(const std::vector<uint8_t>& data, BitWriter& bw,
                    uint64_t& midOffset, uint64_t& endOffset) {
    const std::vector<uint32_t> base = makePositionBase(kNumPositionSlots);
    LzState lz;
    const std::vector<Token> tokens = tokenizeFrame(data.data(), data.size(), base, lz);

    // Árvores Huffman dinâmicas por intervalo, baseadas nas frequências reais.
    std::vector<uint32_t> mainFreq(kMainTreeSymbols, 0);
    std::vector<uint32_t> lenFreq(kNumSecondaryLengths, 0);
    for (const Token& t : tokens) {
        if (t.mainSymbol < mainFreq.size()) mainFreq[t.mainSymbol]++;
        if (t.lengthSymbol >= 0 && t.lengthSymbol < static_cast<int>(lenFreq.size())) lenFreq[t.lengthSymbol]++;
    }
    std::vector<uint32_t> mainFreq2 = mainFreq;
    for (auto &f : mainFreq2) if (f == 0) f = 1;
    std::vector<uint8_t> mainLen = computeLengths(mainFreq2, 16);
    std::vector<uint32_t> lenFreq2 = lenFreq;
    for (auto &f : lenFreq2) if (f == 0) f = 1;
    std::vector<uint8_t> lengthLen = computeLengths(lenFreq2, 16);
    Huffman mainHuff = buildHuffman(mainLen);
    Huffman lengthHuff = buildHuffman(lengthLen);
    if (!mainHuff.complete) { mainLen = fixedLengths(kMainTreeSymbols); mainHuff = buildHuffman(mainLen); }
    if (!lengthHuff.complete) { lengthLen = fixedLengths(kNumSecondaryLengths); lengthHuff = buildHuffman(lengthLen); }

    // Cabeçalho E8 desabilitado. Ele é lido apenas no início do intervalo
    // de reset (o decodificador relê o estado de Huffman a cada 2 frames).
    bw.putBit(0);

    TreeWriter tw(bw);
    std::vector<uint8_t> prevMain(kMainTreeSymbols, 0);
    std::vector<uint8_t> prevLength(kNumSecondaryLengths + 1, 0);
    size_t emitted = 0;

    for (const Token& t : tokens) {
        if ((emitted % kFrameSize) == 0) {
            // Cada frame começa com seu próprio bloco VERBATIM de 32 KiB.
            // As árvores são transmitidas em todos os blocos, mas os deltas
            // sao relativos ao bloco anterior (zerados no reset do intervalo).
            bw.putBits(kBlockTypeVerbatim, 3);
            bw.putBits(static_cast<uint32_t>(kFrameSize), 24);
            tw.write(0, kNumChars, mainLen, prevMain);
            tw.write(kNumChars, kMainTreeSymbols - kNumChars,
                     mainLen, prevMain);
            tw.write(0, kNumSecondaryLengths, lengthLen, prevLength);
        }

        const uint32_t sym = t.mainSymbol;
        bw.putBits(mainHuff.code[sym], mainHuff.length[sym]);
        if (t.lengthSymbol >= 0) {
            const int ls = t.lengthSymbol;
            bw.putBits(lengthHuff.code[ls], lengthHuff.length[ls]);
        }
        if (t.extraBits > 0) bw.putBits(t.footer, t.extraBits);
        emitted += static_cast<size_t>(t.length);

        // O decodificador realinha a entrada em word de 16 bits no fim de
        // cada frame de 32 KiB.
        if ((emitted % kFrameSize) == 0) {
            bw.align16();
            if (emitted == kFrameSize) midOffset = static_cast<uint64_t>(bw.size());
        }
    }

    endOffset = static_cast<uint64_t>(bw.size());
}

} // namespace

Result compress(const std::vector<uint8_t>& input, int windowBits) {
    Result out;
    if (windowBits != kWindowBits) {
        // O perfil CHM usa exclusivamente 64 KiB. Mantemos o parametro para
        // documentar a restrição, sem silenciosamente gerar um fluxo inválido.
        windowBits = kWindowBits;
    }

    const size_t intervalSize = static_cast<size_t>(kFrameSize) * kResetFrames;
    const size_t intervals = (input.size() + intervalSize - 1) / intervalSize;
    out.uncompressedSize = input.size();
    out.paddedSize = static_cast<uint64_t>(intervals) * intervalSize;

    BitWriter bw;
    // Tabela de reset do CHM: um offset comprimido por frame de 32 KiB.
    // O decoder usa essa tabela para localizar frames; o codec reinicia o
    // estado a cada 2 frames (intervalo de 64 KiB).
    out.resetOffsets.push_back(0);
    for (size_t i = 0; i < intervals; ++i) {
        const size_t begin = i * intervalSize;
        const size_t end = std::min(begin + intervalSize, input.size());
        std::vector<uint8_t> interval(intervalSize, 0);
        if (end > begin) {
            std::memcpy(interval.data(), input.data() + begin, end - begin);
        }
        uint64_t midOffset = 0;
        uint64_t intervalEnd = 0;
        encodeInterval(interval, bw, midOffset, intervalEnd);
        out.resetOffsets.push_back(midOffset);
        out.resetOffsets.push_back(intervalEnd);
    }
    if (intervals == 0) {
        std::vector<uint8_t> interval(intervalSize, 0);
        uint64_t midOffset = 0;
        uint64_t intervalEnd = 0;
        encodeInterval(interval, bw, midOffset, intervalEnd);
        out.resetOffsets.push_back(midOffset);
        out.resetOffsets.push_back(intervalEnd);
        out.paddedSize = intervalSize;
    }
    bw.align16();
    out.stream = bw.bytes();

    // Frame extra de 32 KiB com tamanho zero, usado pelo decoder para reler o
    // bit de preprocessing e terminar o fluxo.
    out.stream.insert(out.stream.end(), 4, 0);
    return out;
}

} // namespace lzx
