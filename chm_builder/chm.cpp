#include "chm.h"
#include "lzx.h"
#include "objinst_table.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

namespace chm {
namespace {

// ---------------------------------------------------------------------------
// Gravadores little-endian
// ---------------------------------------------------------------------------
void w16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
void w32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
void w64(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
void put16(uint8_t* v, size_t at, uint16_t x) {
    v[at]     = static_cast<uint8_t>(x);
    v[at + 1] = static_cast<uint8_t>(x >> 8);
}
void put32(uint8_t* v, size_t at, uint32_t x) {
    for (int i = 0; i < 4; ++i) v[at + i] = static_cast<uint8_t>(x >> (8 * i));
}
void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
    put32(v.data(), at, x);
}
void put64(uint8_t* v, size_t at, uint64_t x) {
    for (int i = 0; i < 8; ++i) v[at + i] = static_cast<uint8_t>(x >> (8 * i));
}
void put64(std::vector<uint8_t>& v, size_t at, uint64_t x) {
    put64(v.data(), at, x);
}
void raw(uint8_t* v, const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    std::memcpy(v, b, n);
}
void raw(std::vector<uint8_t>& v, const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    v.insert(v.end(), b, b + n);
}
void cstr(std::vector<uint8_t>& v, const std::string& s) {
    raw(v, s.data(), s.size());
    v.push_back(0);
}
void zeros(std::vector<uint8_t>& v, size_t n) { v.insert(v.end(), n, 0); }

std::string lower(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}
std::string stripSlash(const std::string& s) {
    return (!s.empty() && s[0] == '/') ? s.substr(1) : s;
}

// ENCINT: grupos de 7 bits, do mais significativo para o menos significativo.
int encIntLen(uint64_t x) {
    int n = 1;
    while (x >= (1ULL << (7 * n))) ++n;
    return n;
}
void encInt(std::vector<uint8_t>& v, uint64_t x) {
    const int groups = encIntLen(x);
    for (int i = groups - 1; i >= 0; --i) {
        uint8_t b = static_cast<uint8_t>((x >> (7 * i)) & 0x7F);
        if (i > 0) b |= 0x80;
        v.push_back(b);
    }
}
// O $FIftiMain usa grupos de 7 bits em ordem inversa: o byte menos
// significativo vem primeiro e o bit 0x80 indica que ha mais grupos.
void encIntLe7(std::vector<uint8_t>& v, uint64_t x) {
    do {
        uint8_t b = static_cast<uint8_t>(x & 0x7F);
        x >>= 7;
        if (x) b |= 0x80;
        v.push_back(b);
    } while (x);
}
void encIntLe7At(std::vector<uint8_t>& v, size_t& at, uint64_t x) {
    std::vector<uint8_t> tmp;
    encIntLe7(tmp, x);
    for (uint8_t b : tmp) v[at++] = b;
}

// GUIDs ja em ordem de bytes (os tres primeiros campos little-endian).
const uint8_t kGuidDir[16]  = {0x10,0xFD,0x01,0x7C,0xAA,0x7B,0xD0,0x11,
                               0x9E,0x0C,0x00,0xA0,0xC9,0x22,0xE6,0xEC};
const uint8_t kGuidData[16] = {0x11,0xFD,0x01,0x7C,0xAA,0x7B,0xD0,0x11,
                               0x9E,0x0C,0x00,0xA0,0xC9,0x22,0xE6,0xEC};
const uint8_t kGuidItsp[16] = {0x6A,0x92,0x02,0x5D,0x2E,0x21,0xD0,0x11,
                               0x9D,0xF9,0x00,0xA0,0xC9,0x22,0xE6,0xEC};

const char* const kResetTableName =
    "::DataSpace/Storage/MSCompressed/Transform/"
    "{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}/InstanceData/ResetTable";
const char* const kTransformDir =
    "::DataSpace/Storage/MSCompressed/Transform/"
    "{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}/InstanceData/";

// ---------------------------------------------------------------------------
// #STRINGS: blocos de 4096 bytes; offset 0 e uma string vazia
// ---------------------------------------------------------------------------
class StringTable {
public:
    StringTable() { data_.push_back(0); }

    uint32_t add(const std::string& s) {
        const size_t inBlock = data_.size() % 4096;
        if (inBlock + s.size() + 1 > 4096) {
            zeros(data_, 4096 - inBlock);
        }
        const uint32_t off = static_cast<uint32_t>(data_.size());
        cstr(data_, s);
        return off;
    }

    const std::vector<uint8_t>& bytes() const { return data_; }

private:
    std::vector<uint8_t> data_;
};

// ---------------------------------------------------------------------------
// Arquivos internos (#SYSTEM, #TOPICS, #URLTBL, etc.)
// ---------------------------------------------------------------------------
void addSystemEntry(std::vector<uint8_t>& v, uint16_t code,
                    const void* data, size_t len) {
    w16(v, code);
    w16(v, static_cast<uint16_t>(len));
    raw(v, data, len);
}
void addSystemString(std::vector<uint8_t>& v, uint16_t code,
                     const std::string& s) {
    addSystemEntry(v, code, s.data(), s.size() + 1);
}
void addSystemU32(std::vector<uint8_t>& v, uint16_t code, uint32_t x) {
    addSystemEntry(v, code, &x, 4);
}

struct Internal {
    std::vector<uint8_t> system, strings, topics, urltbl, urlstr;
    std::vector<uint8_t> tocidx, idxhdr, windows;
    std::vector<uint8_t> fti, objinst;
    std::vector<uint8_t> aLinksProperty, kLinksBTree, kLinksData;
    std::vector<uint8_t> kLinksMap, kLinksProperty;
};

// Extrai o conteudo da tag <title> de um arquivo HTML/HHC/HHK.
std::string htmlTitle(const std::vector<uint8_t>& data) {
    std::string s(reinterpret_cast<const char*>(data.data()), data.size());
    const std::string low = lower(s);
    const size_t p = low.find("<title>");
    if (p == std::string::npos) return {};
    const size_t q = low.find("</title>", p + 7);
    if (q == std::string::npos) return {};
    size_t b = p + 7, e = q;
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// ID deterministico usado nas entradas especiais de #URLTBL.
uint32_t makeId(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h ^ 0x62A83769u;
}

// ---------------------------------------------------------------------------
// Blobs internos de indice: $OBJINST, $WW* e $FIftiMain
// ---------------------------------------------------------------------------
std::string normalizeLocal(std::string s) {
    for (char& c : s) if (c == '\\') c = '/';
    while (!s.empty() && s[0] == '/') s.erase(0, 1);
    return lower(s);
}

// $OBJINST e uma lista de dois objetos COM usados pelo motor de indice.
std::vector<uint8_t> buildObjInst(uint32_t lcid) {
    std::vector<uint8_t> v;
    w32(v, 0x04000000u);
    w32(v, 2);
    w32(v, 24);
    w32(v, 2655);
    w32(v, 2679);
    w32(v, 36);

    // {4662DAAF-D393-11D0-9A56-00C04FB68BF7}
    const uint8_t guid1[16] = {0xAF,0xDA,0x62,0x46,0x93,0xD3,0xD0,0x11,
                               0x9A,0x56,0x00,0xC0,0x4F,0xB6,0x8B,0xF7};
    raw(v, guid1, sizeof(guid1));
    w32(v, 0x04000000u);
    w32(v, 3);
    w32(v, 1252);
    w32(v, lcid);
    w32(v, 0);
    w32(v, 0);
    w32(v, 0x00145555u);
    w32(v, 0x00000A0Fu);
    w16(v, 0x0100);
    w32(v, 0x00030005u);
    zeros(v, 24);
    w16(v, 0);
    for (const auto& row : kObjInstTable) raw(v, row, sizeof(row));
    const uint8_t tail[15] = {0xC6,0x61,0x65,0xE6,0x61,0x65,0xDF,0x73,
                              0x73,0x8C,0x6F,0x65,0x9C,0x6F,0x65};
    raw(v, tail, sizeof(tail));

    // {4662DAB0-D393-11D0-9A56-00C04FB68B66}
    const uint8_t guid2[16] = {0xB0,0xDA,0x62,0x46,0x93,0xD3,0xD0,0x11,
                               0x9A,0x56,0x00,0xC0,0x4F,0xB6,0x8B,0x66};
    raw(v, guid2, sizeof(guid2));
    w32(v, 666);
    w32(v, 1252);
    w32(v, lcid);
    w32(v, 10031);
    w32(v, 0);
    return v;
}

uint16_t cp1252ToUtf16(uint8_t c) {
    if (c < 0x80 || c >= 0xA0) return c;
    static const uint16_t map[32] = {
        0x20AC,0x0081,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,
        0x02C6,0x2030,0x0160,0x2039,0x0152,0x008D,0x017D,0x008F,
        0x0090,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,
        0x02DC,0x2122,0x0161,0x203A,0x0153,0x009D,0x017E,0x0178
    };
    return map[c - 0x80];
}

void utf16Nt(std::vector<uint8_t>& v, const std::string& s) {
    for (unsigned char c : s) w16(v, cp1252ToUtf16(c));
    w16(v, 0);
}

struct KeywordIndexEntry {
    std::string word;
    std::string local;
    uint32_t topicIndex = 0;
    uint32_t dataOffset = 0;
};

// Escreve um bloco de listagem (palavra + pares topico) no BTree.
std::vector<uint8_t> listingEntryBytes(const KeywordIndexEntry& e) {
    std::vector<uint8_t> v;
    utf16Nt(v, e.word);
    w16(v, 0); // seeAlso
    w16(v, 0); // profundidade
    w32(v, 0); // indice do caractere da virgula
    w32(v, 0); // desconhecido
    w32(v, 1); // numero de pares
    w32(v, e.topicIndex);
    w32(v, 1); // sempre 1
    w32(v, e.dataOffset);
    return v;
}

// Entrada de nivel superior: termina com o numero do bloco filho.
std::vector<uint8_t> indexEntryBytes(const std::vector<uint8_t>& listing,
                                     uint32_t childBlock) {
    std::vector<uint8_t> v(listing.begin(), listing.end() - 8);
    w32(v, childBlock);
    return v;
}

struct BlobSet {
    std::vector<uint8_t> btree, data, map, prop;
};

BlobSet buildKeywordLinks(const std::vector<KeywordIndexEntry>& entries,
                          uint32_t totalTopics, uint32_t lcid) {
    BlobSet out;
    out.prop.assign(4, 0);
    if (entries.empty()) return out;

    // Ordem case-insensitive, como no compilador da Microsoft.
    std::vector<size_t> order(entries.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const std::string la = lower(entries[a].word);
        const std::string lb = lower(entries[b].word);
        if (la != lb) return la < lb;
        return entries[a].word < entries[b].word;
    });

    struct LeafInfo {
        std::vector<uint8_t> lastListing;
        uint32_t offset = 0;
    };
    std::vector<LeafInfo> leaves;
    std::vector<uint8_t> block(2048, 0);
    std::vector<uint8_t> lastListing;
    size_t used = 12;
    int count = 0;
    uint32_t mod13 = 0;
    auto flushLeaf = [&]() {
        put16(block.data(), 0, static_cast<uint16_t>(2048 - used));
        put16(block.data(), 2, static_cast<uint16_t>(count));
        put32(block.data(), 4, static_cast<uint32_t>(leaves.empty() ? -1 : static_cast<int32_t>(leaves.size() - 1)));
        put32(block.data(), 8, static_cast<uint32_t>(leaves.empty() ? -1 : static_cast<int32_t>(leaves.size())));
        leaves.push_back({lastListing, static_cast<uint32_t>(out.btree.size())});
        out.btree.insert(out.btree.end(), block.begin(), block.end());
        std::fill(block.begin(), block.end(), 0);
        used = 12;
        count = 0;
    };

    for (size_t idx : order) {
        KeywordIndexEntry e = entries[idx];
        e.dataOffset = mod13;
        mod13 += 13;
        const std::vector<uint8_t> enc = listingEntryBytes(e);
        if (used + enc.size() > 2048 - 8) {
            flushLeaf();
        }
        std::copy(enc.begin(), enc.end(), block.begin() + static_cast<ptrdiff_t>(used));
        used += enc.size();
        ++count;
        lastListing = enc;
        lastListing.back() = 0; // mod13 nao participa do cabecalho de indice

        // Cada registro de dados e constante; o segundo DWORD parece ser o
        // numero total de nos de topico do CHM.
        uint8_t dataEntry[13] = {0,0,0,0,0,0,0,0,0x80,0,0,0,0};
        put32(dataEntry, 4, totalTopics);
        out.data.insert(out.data.end(), dataEntry, dataEntry + 13);
    }
    flushLeaf();

    // Mapa: quantidade de blocos de listagem e, para cada um, a quantidade
    // acumulada de entradas antes dele e o numero do bloco.
    w16(out.map, static_cast<uint16_t>(leaves.size()));
    uint32_t accumulated = 0;
    for (size_t i = 0; i < leaves.size(); ++i) {
        w32(out.map, accumulated);
        w32(out.map, static_cast<uint32_t>(i));
        accumulated = static_cast<uint32_t>((i + 1) * entries.size() / leaves.size());
    }

    // Constante observada quando existem links deste tipo.
    const uint32_t props[8] = {0,0,0,0x0C,1,1,0,0};
    out.prop.clear();
    for (uint32_t p : props) w32(out.prop, p);

    // Niveis de indice, quando ha mais de um bloco de listagem.
    std::vector<std::vector<uint8_t>> allBlocks;
    allBlocks.push_back(std::vector<uint8_t>(out.btree.begin(), out.btree.begin() + leaves.size() * 2048));
    struct IndexItem {
        std::vector<uint8_t> entry;
        uint32_t block = 0;
    };
    std::vector<IndexItem> level;
    for (size_t i = 0; i < leaves.size(); ++i) {
        level.push_back({indexEntryBytes(leaves[i].lastListing, static_cast<uint32_t>(i)),
                         static_cast<uint32_t>(i)});
    }
    uint32_t treeDepth = 1;
    uint32_t nextBlock = static_cast<uint32_t>(leaves.size());
    while (level.size() > 1) {
        std::vector<IndexItem> next;
        std::vector<uint8_t> ib(2048, 0);
        size_t usedIdx = 8;
        int countIdx = 0;
        std::vector<uint8_t> lastIndexEntry;
        auto flushIndex = [&]() {
            put16(ib.data(), 0, static_cast<uint16_t>(2048 - usedIdx));
            put16(ib.data(), 2, static_cast<uint16_t>(countIdx));
            put32(ib.data(), 4, level.front().block);
            allBlocks.push_back(ib);
            next.push_back({indexEntryBytes(lastIndexEntry, nextBlock), nextBlock});
            ++nextBlock;
            std::fill(ib.begin(), ib.end(), 0);
            usedIdx = 8;
            countIdx = 0;
        };
        for (const IndexItem& item : level) {
            if (usedIdx + item.entry.size() > 2048 - 8) flushIndex();
            std::copy(item.entry.begin(), item.entry.end(), ib.begin() + static_cast<ptrdiff_t>(usedIdx));
            usedIdx += item.entry.size();
            ++countIdx;
            lastIndexEntry = item.entry;
        }
        if (countIdx > 0) flushIndex();
        level = std::move(next);
        ++treeDepth;
    }

    out.btree.clear();
    for (const auto& b : allBlocks) out.btree.insert(out.btree.end(), b.begin(), b.end());

    std::vector<uint8_t> header(76, 0);
    raw(header.data(), ";)" , 2);
    put16(header.data(), 2, 0x0104);
    put16(header.data(), 4, 2048);
    raw(header.data() + 6, "X44", 3);
    put32(header.data(), 0x1A, static_cast<uint32_t>(leaves.size() - 1));
    put32(header.data(), 0x1E, static_cast<uint32_t>(allBlocks.empty() ? 0 : allBlocks.size() - 1));
    put32(header.data(), 0x22, 0xFFFFFFFFu);
    put32(header.data(), 0x26, static_cast<uint32_t>(allBlocks.size()));
    put16(header.data(), 0x2A, static_cast<uint16_t>(treeDepth));
    put32(header.data(), 0x2C, static_cast<uint32_t>(order.size()));
    put32(header.data(), 0x30, 1252);
    put32(header.data(), 0x34, lcid);
    put32(header.data(), 0x38, 1);
    put32(header.data(), 0x3C, 10031);
    out.btree.insert(out.btree.begin(), header.begin(), header.end());
    return out;
}

// ---------------------------------------------------------------------------
// $FIftiMain: indice de texto integral
// ---------------------------------------------------------------------------
struct HtmlWord {
    std::string text;
    bool isTitle = false;
    uint32_t position = 0;
};

std::string decodeEntityRef(const std::string& s) {
    if (s.size() < 3 || s[0] != '&' || s.back() != ';') return " ";
    const std::string name = s.substr(1, s.size() - 2);
    if (!name.empty() && name[0] == '#') {
        unsigned long cp = 0;
        try {
            if (name.size() > 2 && (name[1] == 'x' || name[1] == 'X'))
                cp = std::stoul(name.substr(2), nullptr, 16);
            else
                cp = std::stoul(name.substr(1), nullptr, 10);
        } catch (...) {
            return " ";
        }
        if (cp == 0 || cp > 0x10FFFF) return " ";
        std::string r;
        if (cp < 0x80) r.push_back(static_cast<char>(cp));
        else if (cp <= 0xFF) r.push_back(static_cast<char>(cp));
        else r.push_back(' ');
        return r;
    }
    if (name == "amp") return "&";
    if (name == "lt") return "<";
    if (name == "gt") return ">";
    if (name == "quot") return "\"";
    if (name == "apos" || name == "39") return "'";
    if (name == "nbsp") return " ";
    return " ";
}

bool isIndexWordByte(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '_' || c == 0x01 || c == 0xDE || c == 0xFE;
}

void tokenizeIndexedText(const std::string& raw, bool isTitle, uint32_t& position,
                         std::vector<HtmlWord>& out) {
    // Substitui entidades por um unico caractere (ou separador) e minúsculas.
    std::string s;
    for (size_t i = 0; i < raw.size();) {
        if (raw[i] == '&') {
            size_t j = i + 1;
            while (j < raw.size() && j - i <= 10 && raw[j] != ';' && raw[j] != ' ' && raw[j] != '&') ++j;
            if (j < raw.size() && raw[j] == ';') {
                s += decodeEntityRef(raw.substr(i, j - i + 1));
                i = j + 1;
                continue;
            }
        }
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(raw[i]))));
        ++i;
    }

    std::string word;
    bool inWord = false;
    bool numberWord = false;
    auto flush = [&]() {
        if (word.empty()) return;
        std::string clean;
        for (char c : word) if (c != '\'' && c != ',') clean.push_back(c);
        if (!clean.empty()) out.push_back({clean, isTitle, position++});
        word.clear();
        inWord = false;
        numberWord = false;
    };
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const unsigned char next = (i + 1 < s.size()) ? static_cast<unsigned char>(s[i + 1]) : 0;
        bool ends = !isIndexWordByte(c);
        if (ends && numberWord && c == ',') ends = false;
        if (ends && numberWord && c == '.' && next >= '0' && next <= '9') ends = false;
        if (ends && inWord && c == '\'') ends = false;
        if (ends) {
            flush();
        } else if (!inWord) {
            inWord = true;
            numberWord = (c >= '0' && c <= '9');
            word.push_back(static_cast<char>(c));
        } else {
            word.push_back(static_cast<char>(c));
        }
    }
    flush();
}

void extractIndexedWords(const std::vector<uint8_t>& data, std::vector<HtmlWord>& out) {
    const std::string s(reinterpret_cast<const char*>(data.data()), data.size());
    bool inTitle = false, inBody = false, inScript = false, inStyle = false;
    uint32_t position = 0;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] != '<') {
            const size_t j = s.find('<', i);
            const size_t end = (j == std::string::npos) ? s.size() : j;
            if ((inTitle || inBody) && !inScript && !inStyle) {
                tokenizeIndexedText(s.substr(i, end - i), inTitle && !inBody, position, out);
            }
            i = end;
            continue;
        }
        const size_t j = s.find('>', i);
        if (j == std::string::npos) break;
        std::string tag = lower(s.substr(i, j - i + 1));
        if (!inBody) {
            if (tag.rfind("<title", 0) == 0) inTitle = true;
            if (tag.find("</title") != std::string::npos) inTitle = false;
            if (tag.rfind("<body", 0) == 0) inBody = true;
        } else {
            if (tag.find("</body") != std::string::npos) inBody = false;
            if (tag.rfind("<script", 0) == 0) inScript = true;
            if (tag.find("</script") != std::string::npos) inScript = false;
            if (tag.rfind("<style", 0) == 0) inStyle = true;
            if (tag.find("</style") != std::string::npos) inStyle = false;
        }
        i = j + 1;
    }
}

struct FtiPosting {
    uint32_t topic = 0;
    std::vector<uint32_t> positions;
};
struct FtiWord {
    std::string text;
    bool isTitle = false;
    std::vector<FtiPosting> docs;
};
struct FtiStats {
    uint32_t files = 0;
    uint64_t totalOccurrences = 0;
    uint64_t totalOccurrenceLength = 0;
    uint64_t totalDistinctLength = 0;
    uint32_t longest = 0;
};

struct BitWriter {
    std::vector<uint8_t>& out;
    uint8_t buf = 0;
    int used = 0;
    explicit BitWriter(std::vector<uint8_t>& o) : out(o) {}
    void write(uint32_t value, int bits) {
        for (int i = bits - 1; i >= 0; --i) {
            buf = static_cast<uint8_t>((buf << 1) | ((value >> i) & 1u));
            if (++used == 8) {
                out.push_back(buf);
                buf = 0;
                used = 0;
            }
        }
    }
    void flush() {
        if (used > 0) {
            out.push_back(static_cast<uint8_t>(buf << (8 - used)));
            buf = 0;
            used = 0;
        }
    }
};

void writeScaleRoot(BitWriter& bw, uint32_t value, int root) {
    uint32_t tmp = value;
    int needed = 0;
    while (tmp) { ++needed; tmp >>= 1; }
    const int prefix = std::max(0, needed - root);
    int rootBits = std::max(needed - 1, root);
    if (rootBits < 0) rootBits = 0;
    const uint32_t mask = rootBits >= 32 ? 0xFFFFFFFFu : ((1u << rootBits) - 1u);
    uint32_t bits = ((~mask) << 1) | (value & mask);
    const int total = prefix + 1 + rootBits;
    if (total >= 32) bits = ((~mask) << 1) | (value & mask);
    else bits &= (1u << total) - 1u;
    bw.write(bits, total);
}

std::string adjustedWord(const std::string& word, uint8_t& offset,
                         const std::string& previous) {
    if (word == previous) {
        offset = static_cast<uint8_t>(word.size());
        return {};
    }
    const size_t count = std::min(word.size(), previous.size());
    for (size_t i = 0; i < count; ++i) {
        if (word[i] != previous[i]) {
            offset = static_cast<uint8_t>(i);
            return word.substr(i);
        }
    }
    offset = 0;
    return word;
}

std::vector<uint8_t> buildFtiMain(const std::vector<FtiWord>& words,
                                  const FtiStats& stats, uint32_t lcid) {
    if (words.empty()) return {};
    std::vector<uint8_t> out(0x400, 0);

    // O escritor do FPC usa estas raizes provisorias e grava os mesmos valores
    // no cabecalho final. As escalas ficam em 2.
    constexpr int kDocRoot = 1, kCountRoot = 1, kLocRoot = 5;
    constexpr size_t kNodeSize = 4096;
    struct NodeRef { uint32_t offset = 0; std::string lastWord; };
    std::vector<NodeRef> leaves;
    std::vector<uint8_t> block(kNodeSize, 0);
    size_t used = 8; // DWORD nextLeaf + WORD 0 + WORD freeSpace
    std::string lastWord;
    uint32_t lastLeafAt = 0;
    uint32_t lastLeafFree = 0;

    auto flushLeaf = [&]() {
        put32(block.data(), 0, 0);
        lastLeafFree = static_cast<uint32_t>(kNodeSize - used);
        put16(block.data(), 6, static_cast<uint16_t>(lastLeafFree));
        if (lastLeafAt != 0) put32(out.data(), lastLeafAt, static_cast<uint32_t>(out.size()));
        leaves.push_back({static_cast<uint32_t>(out.size()), lastWord});
        out.insert(out.end(), block.begin(), block.end());
        lastLeafAt = out.size() - kNodeSize;
        std::fill(block.begin(), block.end(), 0);
        used = 8;
        lastWord.clear();
    };

    for (const FtiWord& w : words) {
        if (w.text.size() > 99) continue;
        uint8_t offset = 0;
        std::string part = adjustedWord(w.text, offset, lastWord);
        if (17 + part.size() >= kNodeSize - used) {
            flushLeaf();
            part = adjustedWord(w.text, offset, lastWord);
        }

        const uint32_t wlcAt = static_cast<uint32_t>(out.size());
        {
            BitWriter bw(out);
            uint32_t lastDoc = 0;
            for (const FtiPosting& p : w.docs) {
                uint32_t lastLoc = 0;
                writeScaleRoot(bw, p.topic - lastDoc, kDocRoot);
                lastDoc = p.topic;
                writeScaleRoot(bw, static_cast<uint32_t>(p.positions.size()), kCountRoot);
                for (uint32_t pos : p.positions) {
                    writeScaleRoot(bw, pos - lastLoc, kLocRoot);
                    lastLoc = pos;
                }
                bw.flush();
            }
        }
        const uint32_t wlcSize = static_cast<uint32_t>(out.size() - wlcAt);

        block[used++] = static_cast<uint8_t>(part.size() + 1);
        block[used++] = offset;
        std::copy(part.begin(), part.end(), block.begin() + static_cast<ptrdiff_t>(used));
        used += part.size();
        block[used++] = w.isTitle ? 1 : 0;
        encIntLe7At(block, used, w.docs.size());
        put32(block.data(), used, wlcAt); used += 4;
        put16(block.data(), used, 0); used += 2;
        encIntLe7At(block, used, wlcSize);
        lastWord = w.text;
    }
    flushLeaf();

    // Nos de indice: cada entrada guarda o ultimo termo do bloco filho e o
    // offset absoluto desse bloco. O cabecalho de um no de indice tem apenas
    // o WORD de espaco livre; as entradas comecam no offset 2.
    std::vector<NodeRef> level = leaves;
    size_t indexLevels = 0;
    // TODO: o nivel de indice ainda faz o hh.exe rejeitar CHMs grandes;
    // mantido desabilitado ate a estrutura ser totalmente compativel com o FPC.
    while (false && level.size() > 1) {
        std::vector<NodeRef> next;
        std::vector<uint8_t> ib(kNodeSize, 0);
        size_t iu = 2;
        std::string lastIndexWord;
        auto flushIndex = [&]() {
            put16(ib.data(), 0, static_cast<uint16_t>(kNodeSize - iu));
            const uint32_t at = static_cast<uint32_t>(out.size());
            out.insert(out.end(), ib.begin(), ib.end());
            next.push_back({at, lastIndexWord});
            std::fill(ib.begin(), ib.end(), 0);
            iu = 2;
            lastIndexWord.clear();
        };
        for (const NodeRef& node : level) {
            uint8_t off = 0;
            std::string part = adjustedWord(node.lastWord, off, lastIndexWord);
            if (iu + 8 + part.size() >= kNodeSize) {
                flushIndex();
                part = adjustedWord(node.lastWord, off, lastIndexWord);
            }
            ib[iu++] = static_cast<uint8_t>(part.size() + 1);
            ib[iu++] = off;
            std::copy(part.begin(), part.end(), ib.begin() + static_cast<ptrdiff_t>(iu));
            iu += part.size();
            put32(ib.data(), iu, node.offset); iu += 4;
            put16(ib.data(), iu, 0); iu += 2;
            lastIndexWord = node.lastWord;
        }
        flushIndex();
        level = std::move(next);
        ++indexLevels;
    }

    const uint32_t rootOffset = static_cast<uint32_t>(out.size() - kNodeSize);
    const uint16_t depth = static_cast<uint16_t>(1 + indexLevels);

    std::vector<uint8_t> header(0x400, 0);
    header[2] = 0x28;
    put32(header, 4, stats.files);
    put32(header, 8, rootOffset);
    put32(header, 0x10, static_cast<uint32_t>(leaves.size()));
    put32(header, 0x14, rootOffset);
    put16(header.data(), 0x18, depth);
    put32(header, 0x1A, 7);
    header[0x1E] = 2; header[0x1F] = kDocRoot;
    header[0x20] = 2; header[0x21] = kCountRoot;
    header[0x22] = 2; header[0x23] = kLocRoot;
    put32(header, 0x2E, static_cast<uint32_t>(kNodeSize));
    put32(header, 0x32, 0);
    put32(header, 0x36, 1);
    put32(header, 0x3A, 5);
    put32(header, 0x3E, stats.longest);
    put32(header, 0x42, static_cast<uint32_t>(stats.totalOccurrences));
    put32(header, 0x46, static_cast<uint32_t>(words.size()));
    put32(header, 0x4A, static_cast<uint32_t>(stats.totalOccurrenceLength));
    put32(header, 0x4E, 0);
    put32(header, 0x52, static_cast<uint32_t>(stats.totalDistinctLength));
    put32(header, 0x56, lastLeafFree);
    put32(header, 0x5A, 0);
    put32(header, 0x5E, stats.files ? stats.files - 1 : 0);
    put32(header, 0x7A, 1252);
    put32(header, 0x7E, lcid);
    std::copy(header.begin(), header.end(), out.begin());
    return out;
}

Internal buildInternal(const BuildOptions& opt,
                       const std::vector<InputFile>& files) {
    Internal in;

    // --- #STRINGS -----------------------------------------------------------
    // Ordem observada em CHMs compilados pelo HHA: "", nome da janela, titulo,
    // sumario, indice, topico inicial, titulos dos topicos (HTML) e titulos
    // dos arquivos de sumario/indice. A fonte fica no codigo 16 de #SYSTEM.
    auto findFile = [&](const std::string& internal) -> const InputFile* {
        for (const InputFile& f : files) {
            if (stripSlash(f.internalName) == internal) return &f;
        }
        return nullptr;
    };
    auto pageTitle = [&](const std::string& internal,
                         const std::string& fallback) {
        if (const InputFile* f = findFile(internal)) {
            const std::string t = htmlTitle(f->data);
            if (!t.empty()) return t;
        }
        return fallback;
    };

    StringTable st;
    const uint32_t sWindow  = st.add(opt.defaultWindow);
    const uint32_t sTitle   = st.add(opt.title);
    const uint32_t sHhc     = st.add(opt.contentsFile);
    const uint32_t sHhk     = st.add(opt.indexFile);
    const uint32_t sDefault = st.add(stripSlash(opt.defaultTopic));
    // O compilador da Microsoft inclui o valor de fsValidMembers em #STRINGS.
    // Ele e usado por versoes antigas do hh.exe ao localizar a janela inicial.
    const uint32_t sValidFlags = st.add("0x4238");

    // --- Topicos: HTML do sumario, HTML solto, sumario.hhc e indice.hhk -----
    struct Topic {
        std::string local;
        std::string title;
        uint32_t titleOff = 0xFFFFFFFFu;
        bool inToc = false;
        int tocNode = -1;
    };
    std::vector<Topic> topics;
    std::vector<int> tocTopicIndex(opt.toc.size(), -1);
    for (size_t i = 0; i < opt.toc.size(); ++i) {
        if (opt.toc[i].local.empty()) continue;
        const std::string loc = stripSlash(opt.toc[i].local);
        tocTopicIndex[i] = static_cast<int>(topics.size());
        topics.push_back({loc, pageTitle(loc, opt.toc[i].name), 0xFFFFFFFFu,
                          true, static_cast<int>(i)});
    }
    for (const InputFile& f : files) {
        const std::string loc = stripSlash(f.internalName);
        const std::string lo = lower(loc);
        if (lo.find(".htm") == std::string::npos) continue;
        bool found = false;
        for (const Topic& t : topics) {
            if (lower(t.local) == lo) { found = true; break; }
        }
        if (!found) topics.push_back({loc, pageTitle(loc, loc), 0xFFFFFFFFu,
                                     false, -1});
    }
    if (topics.empty()) {
        topics.push_back({stripSlash(opt.defaultTopic), opt.title,
                          0xFFFFFFFFu, false, -1});
    }
    const size_t htmlTopicCount = topics.size();
    topics.push_back({opt.contentsFile,
                      pageTitle(opt.contentsFile, opt.contentsTitle),
                      0xFFFFFFFFu, false, -1});
    topics.push_back({opt.indexFile,
                      pageTitle(opt.indexFile, opt.indexTitle),
                      0xFFFFFFFFu, false, -1});

    for (Topic& t : topics) t.titleOff = st.add(t.title);

    // --- #URLSTR: prefixo de 8 zeros antes de cada URL ----------------------
    in.urlstr.push_back(0x42);
    std::vector<uint32_t> urlSlotOff(topics.size(), 0);
    for (size_t i = 0; i < topics.size(); ++i) {
        urlSlotOff[i] = static_cast<uint32_t>(in.urlstr.size());
        zeros(in.urlstr, 8);
        cstr(in.urlstr, topics[i].local);
    }

    // --- #URLTBL: entradas curtas, sem blocos de 4096 bytes -----------------
    const uint32_t id7 = 0x62A83769u;
    auto urlEntry = [&](uint32_t id, uint32_t topicIndex, uint32_t urlOff) {
        w32(in.urltbl, id);
        w32(in.urltbl, topicIndex);
        w32(in.urltbl, urlOff);
    };
    urlEntry(makeId("contents"), 0, urlSlotOff[0]);
    urlEntry(id7, static_cast<uint32_t>(htmlTopicCount + 1),
             urlSlotOff[htmlTopicCount + 1]);
    for (size_t i = 1; i < htmlTopicCount; ++i) {
        urlEntry(makeId(topics[i].local), static_cast<uint32_t>(i),
                 urlSlotOff[i]);
    }
    urlEntry(makeId("index"), static_cast<uint32_t>(htmlTopicCount),
             urlSlotOff[htmlTopicCount]);

    // --- #TOCIDX ------------------------------------------------------------
    const size_t n = opt.toc.size();
    std::vector<int> parent(n, -1), firstChild(n, -1), nextSibling(n, -1);
    std::vector<int> lastAtDepth(64, -1);
    for (size_t i = 0; i < n; ++i) {
        const int d = std::max(0, std::min(63, opt.toc[i].depth));
        if (d > 0) parent[i] = lastAtDepth[d - 1];
        if (parent[i] >= 0 && firstChild[parent[i]] < 0) {
            firstChild[parent[i]] = static_cast<int>(i);
        }
        if (lastAtDepth[d] >= 0) nextSibling[lastAtDepth[d]] = static_cast<int>(i);
        lastAtDepth[d] = static_cast<int>(i);
    }

    std::vector<uint32_t> nodeOffset(n, 0);
    std::vector<size_t> nodeSize(n, 20);
    uint32_t cursor = 4096;
    for (size_t i = 0; i < n; ++i) {
        nodeOffset[i] = cursor;
        nodeSize[i] = opt.toc[i].hasChildren ? 28u : 20u;
        cursor += static_cast<uint32_t>(nodeSize[i]);
    }
    const uint32_t dwordListOff = cursor;
    const uint32_t linkOff = dwordListOff + static_cast<uint32_t>(n * 4);

    in.tocidx.assign(4096, 0);
    put32(in.tocidx, 0, 4096);
    put32(in.tocidx, 4, linkOff);
    put32(in.tocidx, 8, static_cast<uint32_t>(n));
    put32(in.tocidx, 0xC, dwordListOff);

    for (size_t i = 0; i < n; ++i) {
        in.tocidx.resize(nodeOffset[i] + nodeSize[i], 0);
        uint32_t flags = 0;
        if (!opt.toc[i].local.empty()) flags |= 0x8;
        if (opt.toc[i].hasChildren) flags |= 0x4;
        put32(in.tocidx, nodeOffset[i] + 4, flags);
        const int ti = tocTopicIndex[i];
        put32(in.tocidx, nodeOffset[i] + 8,
              ti >= 0 ? static_cast<uint32_t>(ti) : 0xFFFFFFFFu);
        put32(in.tocidx, nodeOffset[i] + 0xC,
              parent[i] >= 0 ? nodeOffset[parent[i]] : 0);
        put32(in.tocidx, nodeOffset[i] + 0x10,
              nextSibling[i] >= 0 ? nodeOffset[nextSibling[i]] : 0xFFFFFFFFu);
        if (nodeSize[i] == 28) {
            put32(in.tocidx, nodeOffset[i] + 0x14,
                  firstChild[i] >= 0 ? nodeOffset[firstChild[i]] : 0xFFFFFFFFu);
            put32(in.tocidx, nodeOffset[i] + 0x18, 0);
        }
    }

    in.tocidx.resize(linkOff, 0);
    for (size_t i = 0; i < n; ++i) {
        put32(in.tocidx, dwordListOff + static_cast<uint32_t>(i) * 4,
              tocTopicIndex[i] >= 0 ? static_cast<uint32_t>(tocTopicIndex[i])
                                    : 0xFFFFFFFFu);
    }
    for (size_t i = 0; i < n; ++i) {
        const size_t at = in.tocidx.size();
        in.tocidx.resize(at + 16, 0);
        put32(in.tocidx, at, nodeOffset[i]);
        put32(in.tocidx, at + 4, 666u + static_cast<uint32_t>(i));
        put32(in.tocidx, at + 8,
              dwordListOff + static_cast<uint32_t>(i) * 4);
        put32(in.tocidx, at + 0xC,
              tocTopicIndex[i] >= 0 ? static_cast<uint32_t>(tocTopicIndex[i])
                                    : 0xFFFFFFFFu);
    }
    if (n == 0) in.tocidx.resize(4096 + 16, 0);

    // --- #TOPICS ------------------------------------------------------------
    for (size_t i = 0; i < topics.size(); ++i) {
        const Topic& t = topics[i];
        uint32_t tocOff = t.tocNode >= 0 ? nodeOffset[t.tocNode] : 0;
        uint32_t urlTblOff;
        if (i == 0) urlTblOff = 0;
        else if (i < htmlTopicCount) urlTblOff = static_cast<uint32_t>((i + 1) * 12);
        else if (i == htmlTopicCount) urlTblOff = static_cast<uint32_t>((htmlTopicCount + 1) * 12);
        else urlTblOff = 12;
        w32(in.topics, tocOff);
        w32(in.topics, t.titleOff);
        w32(in.topics, urlTblOff);
        w16(in.topics, t.inToc ? 6 : 2);
        w16(in.topics, 0);
    }
    if (topics.empty()) zeros(in.topics, 16);

    // --- #IDXHDR (mesmos bytes da entrada 13 de #SYSTEM) -------------------
    const uint32_t idxChecksum = makeId("IDXHDR") ^ opt.unixTime;
    in.idxhdr.assign(4096, 0);
    put32(in.idxhdr, 0, 0x4D532354u); // "T#SM" em little-endian
    put32(in.idxhdr, 4, idxChecksum);
    put32(in.idxhdr, 8, 1);
    put32(in.idxhdr, 0xC, static_cast<uint32_t>(topics.size()));
    put32(in.idxhdr, 0x10, 0);
    put32(in.idxhdr, 0x14, 0xFFFFFFFFu);
    put32(in.idxhdr, 0x18, 0);
    put32(in.idxhdr, 0x1C, 0);
    for (size_t at = 0x20; at < 0x40; at += 4) {
        put32(in.idxhdr, at, 0xFFFFFFFFu);
    }
    put32(in.idxhdr, 0x40, 0);
    put32(in.idxhdr, 0x44, 1);
    put32(in.idxhdr, 0x48, 0);

    // --- #WINDOWS -----------------------------------------------------------
    in.windows.assign(8 + 196, 0);
    put32(in.windows, 0, 1);
    put32(in.windows, 4, 196);
    put32(in.windows, 8, 196);              // cbStruct
    put32(in.windows, 8 + 2 * 4, sWindow);  // nome da janela
    put32(in.windows, 8 + 3 * 4, opt.winValidFlags);
    put32(in.windows, 8 + 4 * 4, opt.winArg10);
    put32(in.windows, 8 + 5 * 4, sTitle);
    put32(in.windows, 8 + 24 * 4, sHhc);
    put32(in.windows, 8 + 25 * 4, sHhk);
    put32(in.windows, 8 + 26 * 4, sDefault);
    put32(in.windows, 8 + 27 * 4, sDefault);
    put32(in.windows, 8 + 28 * 4, opt.winArg12);
    // O HHC aponta pszUrlJump2 para a string de fsValidMembers em #STRINGS.
    put32(in.windows, 8 + 0xA8, sValidFlags);

    // --- Indice binario de palavras-chave ($WW*) ----------------------------
    std::map<std::string, uint32_t> topicByLocal;
    for (size_t i = 0; i < htmlTopicCount; ++i) {
        topicByLocal[normalizeLocal(topics[i].local)] = static_cast<uint32_t>(i);
    }
    std::vector<KeywordIndexEntry> keywordEntries;
    for (const KeywordEntry& k : opt.keywords) {
        KeywordIndexEntry e;
        e.word = k.name;
        e.local = k.local;
        const auto it = topicByLocal.find(normalizeLocal(k.local));
        e.topicIndex = (it == topicByLocal.end()) ? 0 : it->second;
        keywordEntries.push_back(std::move(e));
    }
    const BlobSet kw = buildKeywordLinks(keywordEntries,
                                         static_cast<uint32_t>(topics.size()), opt.lcid);
    in.kLinksBTree = kw.btree;
    in.kLinksData = kw.data;
    in.kLinksMap = kw.map;
    in.kLinksProperty = kw.prop;
    in.aLinksProperty.assign(4, 0);
    in.objinst = buildObjInst(opt.lcid);

    // --- Indice de texto integral ($FIftiMain) ------------------------------
    std::vector<FtiWord> ftiWords;
    std::map<std::string, size_t> ftiIndex;
    FtiStats stats;
    for (size_t i = 0; i < htmlTopicCount; ++i) {
        const InputFile* f = findFile(topics[i].local);
        if (!f) continue;
        ++stats.files;
        std::vector<HtmlWord> words;
        extractIndexedWords(f->data, words);
        for (const HtmlWord& hw : words) {
            if (hw.text.empty()) continue;
            stats.totalOccurrences++;
            stats.totalOccurrenceLength += hw.text.size();
            std::string key = hw.text;
            key.push_back(hw.isTitle ? '\0' : '\1');
            auto inserted = ftiIndex.emplace(key, ftiWords.size());
            if (inserted.second) {
                ftiWords.push_back({hw.text, hw.isTitle, {}});
                stats.totalDistinctLength += hw.text.size();
                stats.longest = static_cast<uint32_t>(
                    std::max<size_t>(stats.longest, hw.text.size()));
            }
            const auto it = inserted.first;
            FtiWord& fw = ftiWords[it->second];
            if (fw.docs.empty() || fw.docs.back().topic != static_cast<uint32_t>(i)) {
                fw.docs.push_back({static_cast<uint32_t>(i), {}});
            }
            fw.docs.back().positions.push_back(hw.position);
        }
    }
    // O leitor percorre a arvore em ordem de CompareStr; a forma de titulo vem
    // antes da forma de corpo quando a palavra e a mesma.
    std::sort(ftiWords.begin(), ftiWords.end(), [](const FtiWord& a, const FtiWord& b) {
        if (a.text != b.text) return a.text < b.text;
        return a.isTitle && !b.isTitle;
    });
    in.fti = buildFtiMain(ftiWords, stats, opt.lcid);

    // --- #SYSTEM ------------------------------------------------------------
    w32(in.system, 3);
    addSystemU32(in.system, 10, opt.unixTime);
    addSystemString(in.system, 9, "HHA Version 4.74.8702");
    {
        std::vector<uint8_t> info(36, 0);
        put32(info, 0, opt.lcid);
        put32(info, 4, 0);                              // DBCS
        put32(info, 8, in.fti.empty() ? 0 : 1);          // full-text search
        put32(info, 0xC, keywordEntries.empty() ? 0 : 1); // KLinks
        put32(info, 0x10, 0);                           // ALinks
        put64(info, 0x14, opt.timestamp);
        put32(info, 0x1C, 0);
        put32(info, 0x20, 0);
        addSystemEntry(in.system, 4, info.data(), info.size());
    }
    addSystemString(in.system, 2, stripSlash(opt.defaultTopic));
    addSystemString(in.system, 3, opt.title);
    addSystemString(in.system, 16, opt.font);
    addSystemString(in.system, 6, lower(opt.compiledName));
    addSystemString(in.system, 5, opt.defaultWindow);
    addSystemU32(in.system, 7, 0x62A83769u); // ID do indice binario
    addSystemU32(in.system, 12, 0);
    addSystemEntry(in.system, 13, in.idxhdr.data(), in.idxhdr.size());
    addSystemU32(in.system, 15, 0);

    in.strings = st.bytes();
    return in;
}

// ---------------------------------------------------------------------------
// Diretorio ITSP/PMGL/PMGI
// ---------------------------------------------------------------------------
struct DirEntry {
    std::string name;
    uint32_t section = 0;
    uint64_t offset = 0;
    uint64_t length = 0;
};

struct Chunk {
    std::array<uint8_t, 4096> data{};
    size_t dataLen = 0;   // bytes de entradas, a partir do offset 20
    int count = 0;

    Chunk() { data.fill(0); }
};

std::vector<uint8_t> encodeDirEntry(const DirEntry& e) {
    std::vector<uint8_t> v;
    encInt(v, e.name.size());
    raw(v, e.name.data(), e.name.size());
    encInt(v, e.section);
    encInt(v, e.offset);
    encInt(v, e.length);
    return v;
}

// Escreve um chunk PMGL. 'quickrefs' sao offsets das entradas 5, 10, 15...
void finishPmgl(Chunk& c, int prev, int next) {
    raw(c.data.data(), "PMGL", 4);
    put32(c.data.data(), 8, 0);
    put32(c.data.data(), 12, static_cast<uint32_t>(prev));
    put32(c.data.data(), 16, static_cast<uint32_t>(next));

    std::vector<uint16_t> quickrefs;
    size_t inChunk = 20;
    for (int i = 0; i < c.count; ++i) {
        if ((i + 1) % 5 == 0) {
            quickrefs.push_back(static_cast<uint16_t>(inChunk - 20));
        }
        const uint8_t* p = c.data.data() + inChunk;
        uint64_t nameLen = 0;
        int shift = 0;
        while ((*p & 0x80u) != 0) { nameLen |= (uint64_t)(*p & 0x7Fu) << shift; shift += 7; ++p; }
        nameLen |= (uint64_t)(*p & 0x7Fu) << shift; ++p;
        size_t entryLen = static_cast<size_t>(p - (c.data.data() + inChunk));
        entryLen += static_cast<size_t>(nameLen);
        p += nameLen;
        for (int k = 0; k < 3; ++k) {
            while ((*p & 0x80u) != 0) ++p;
            ++p;
        }
        entryLen = static_cast<size_t>(p - (c.data.data() + inChunk));
        inChunk += entryLen;
    }
    // O HHC grava os quickrefs em ordem inversa e relativos ao inicio da
    // area de entradas (apos os 20 bytes de cabecalho do chunk).
    int qpos = 4094;
    for (auto it = quickrefs.rbegin(); it != quickrefs.rend(); ++it) {
        qpos -= 2;
        put16(c.data.data(), static_cast<size_t>(qpos), *it);
    }
    // QuickRefSize cobre toda a area livre apos as entradas, incluindo a
    // tabela de quickrefs e o WORD final de contagem.
    put32(c.data.data(), 4, static_cast<uint32_t>(4096 - 20 - inChunk));
    put16(c.data.data(), 4094, static_cast<uint16_t>(c.count));
}

void finishPmgi(Chunk& c) {
    raw(c.data.data(), "PMGI", 4);
    std::vector<uint16_t> quickrefs;
    size_t inChunk = 8;
    for (int i = 0; i < c.count; ++i) {
        if ((i + 1) % 5 == 0) {
            quickrefs.push_back(static_cast<uint16_t>(inChunk - 8));
        }
        const uint8_t* p = c.data.data() + inChunk;
        uint64_t nameLen = 0;
        int shift = 0;
        while ((*p & 0x80u) != 0) { nameLen |= (uint64_t)(*p & 0x7Fu) << shift; shift += 7; ++p; }
        nameLen |= (uint64_t)(*p & 0x7Fu) << shift; ++p;
        size_t entryLen = static_cast<size_t>(p - (c.data.data() + inChunk));
        p += nameLen;
        while ((*p & 0x80u) != 0) ++p;
        ++p;
        entryLen = static_cast<size_t>(p - (c.data.data() + inChunk));
        inChunk += entryLen;
    }
    int qpos = 4094;
    for (auto it = quickrefs.rbegin(); it != quickrefs.rend(); ++it) {
        qpos -= 2;
        put16(c.data.data(), static_cast<size_t>(qpos), *it);
    }
    put32(c.data.data(), 4, static_cast<uint32_t>(4096 - 8 - inChunk));
    put16(c.data.data(), 4094, static_cast<uint16_t>(c.count));
}

struct Directory {
    std::vector<Chunk> pmgl;
    std::vector<Chunk> pmgi;
    uint32_t depth = 1;
    uint32_t indexRoot = 0xFFFFFFFFu;
    uint32_t firstPmgl = 0;
    uint32_t lastPmgl = 0;
    uint32_t numChunks = 1;
};

bool buildDirectory(const std::vector<DirEntry>& entries,
                    Directory& dir, std::string& error) {
    std::vector<const DirEntry*> sorted;
    sorted.reserve(entries.size());
    for (const DirEntry& e : entries) sorted.push_back(&e);
    std::sort(sorted.begin(), sorted.end(),
              [](const DirEntry* a, const DirEntry* b) { return a->name < b->name; });

    dir.pmgl.clear();
    dir.pmgi.clear();
    Chunk cur;
    for (const DirEntry* e : sorted) {
        const std::vector<uint8_t> enc = encodeDirEntry(*e);
        const int quickAfter = ((cur.count + 1) % 5 == 0)
                                   ? (cur.count + 1) / 5 : cur.count / 5;
        const size_t limit = 4094u - static_cast<size_t>(2 * quickAfter) - 20u;
        if (cur.dataLen + enc.size() > limit) {
            finishPmgl(cur,
                       dir.pmgl.empty() ? -1 : static_cast<int>(dir.pmgl.size() - 1),
                       static_cast<int>(dir.pmgl.size()));
            dir.pmgl.push_back(cur);
            cur = Chunk();
        }
        const size_t at = 20 + cur.dataLen;
        std::copy(enc.begin(), enc.end(), cur.data.begin() + static_cast<ptrdiff_t>(at));
        cur.dataLen += enc.size();
        ++cur.count;
    }
    finishPmgl(cur,
               dir.pmgl.empty() ? -1 : static_cast<int>(dir.pmgl.size() - 1),
               -1);
    dir.pmgl.push_back(cur);

    dir.firstPmgl = 0;
    dir.lastPmgl = static_cast<uint32_t>(dir.pmgl.size() - 1);
    dir.depth = 1;
    dir.indexRoot = 0xFFFFFFFFu;
    dir.numChunks = static_cast<uint32_t>(dir.pmgl.size());

    if (dir.pmgl.size() > 1) {
        // Um unico chunk PMGI com o primeiro nome de cada chunk PMGL.
        Chunk pmgi;
        for (size_t i = 0; i < dir.pmgl.size(); ++i) {
            const uint8_t* p = pmgi.data.data() + 8 + pmgi.dataLen;
            (void)p;
            uint64_t nameLen = 0;
            int shift = 0;
            const uint8_t* q = dir.pmgl[i].data.data() + 20;
            while ((*q & 0x80u) != 0) { nameLen |= (uint64_t)(*q & 0x7Fu) << shift; shift += 7; ++q; }
            nameLen |= (uint64_t)(*q & 0x7Fu) << shift; ++q;
            std::string name(reinterpret_cast<const char*>(q), static_cast<size_t>(nameLen));

            std::vector<uint8_t> enc;
            encInt(enc, name.size());
            raw(enc, name.data(), name.size());
            encInt(enc, i);

            const int quickAfter = ((pmgi.count + 1) % 5 == 0)
                                       ? (pmgi.count + 1) / 5 : pmgi.count / 5;
            const size_t limit = 4094u - static_cast<size_t>(2 * quickAfter) - 8u;
            if (pmgi.dataLen + enc.size() > limit) {
                error = "muitos chunks PMGL para o indice PMGI implementado";
                return false;
            }
            std::copy(enc.begin(), enc.end(),
                      pmgi.data.begin() + static_cast<ptrdiff_t>(8 + pmgi.dataLen));
            pmgi.dataLen += enc.size();
            ++pmgi.count;
        }
        finishPmgi(pmgi);
        dir.pmgi.push_back(pmgi);
        dir.depth = 2;
        dir.indexRoot = static_cast<uint32_t>(dir.pmgl.size());
        dir.numChunks = static_cast<uint32_t>(dir.pmgl.size() + dir.pmgi.size());
    }
    return true;
}

// ---------------------------------------------------------------------------
// Blocos de metadados da secao MSCompressed
// ---------------------------------------------------------------------------
std::vector<uint8_t> makeNameList() {
    const char* names[] = {"Uncompressed", "MSCompressed"};

    std::vector<uint8_t> v;
    // O primeiro campo e o offset do terminador da primeira string, medido
    // a partir do inicio do arquivo (0x1E para "Uncompressed").
    w16(v, static_cast<uint16_t>(2 + 4 + 2 * std::strlen(names[0])));
    w16(v, 2);
    for (const char* nm : names) {
        const size_t len = std::strlen(nm);
        w16(v, static_cast<uint16_t>(len));
        for (size_t i = 0; i < len; ++i) w16(v, static_cast<uint16_t>(nm[i]));
        w16(v, 0);
    }
    return v;
}

std::vector<uint8_t> makeTransformList() {
    const char* s = "{7FC28940-9D31-11D0";
    std::vector<uint8_t> v;
    for (const char* p = s; *p; ++p) w16(v, static_cast<uint16_t>(*p));
    return v;
}

std::vector<uint8_t> makeControlData() {
    std::vector<uint8_t> v;
    w32(v, 6);       // campo desconhecido (0x06 nos CHMs do HHA)
    raw(v, "LZXC", 4);
    w32(v, 2);       // versao
    w32(v, 2);       // resetInterval, em frames de 0x8000 bytes
    w32(v, 2);       // windowSize, em frames de 0x8000 bytes
    w32(v, 1);       // windowsPerReset
    w32(v, 0);
    return v;
}

std::vector<uint8_t> makeResetTable(const lzx::Result& lz) {
    // O perfil LZX implementado reinicia a cada 2 frames de 0x8000 bytes.
    std::vector<uint64_t> entries;
    const size_t frameCount = lz.resetOffsets.empty() ? 0 : lz.resetOffsets.size() - 1;
    for (size_t i = 0; i < frameCount; ++i) {
        entries.push_back(lz.resetOffsets[i]);
    }
    if (entries.empty()) entries.push_back(0);

    std::vector<uint8_t> v;
    w32(v, 2);
    w32(v, static_cast<uint32_t>(entries.size()));
    w32(v, 8);
    w32(v, 0x28);
    w64(v, lz.uncompressedSize);
    w64(v, static_cast<uint64_t>(lz.stream.size()));
    w64(v, 0x8000);
    for (uint64_t off : entries) w64(v, off);
    return v;
}

// ---------------------------------------------------------------------------
// Entrada pendente: dados ainda nao posicionados numa secao
// ---------------------------------------------------------------------------
struct Pending {
    std::string name;
    uint32_t section = 0;
    std::vector<uint8_t> data;
    bool hasData = true;
    uint64_t offset = 0;
    uint64_t length = 0;
};

} // namespace

bool build(const std::string& outPath,
           const std::vector<InputFile>& files,
           const BuildOptions& options,
           std::string& error) {
    error.clear();

    Internal in = buildInternal(options, files);

    // --- Secao 1: todos os arquivos comprimidos com LZX --------------------
    std::vector<Pending> compressed;
    auto addCompressed = [&](const std::string& name,
                             const std::vector<uint8_t>& data) {
        compressed.push_back({name, 1, data, true, 0, 0});
    };
    for (const InputFile& f : files) addCompressed(f.internalName, f.data);
    addCompressed("/#IDXHDR", in.idxhdr);
    addCompressed("/#WINDOWS", in.windows);
    addCompressed("/#STRINGS", in.strings);
    addCompressed("/#TOPICS", in.topics);
    addCompressed("/#URLTBL", in.urltbl);
    addCompressed("/#URLSTR", in.urlstr);

    // Arquivos internos de indice do HTML Help Workshop.
    addCompressed("/$FIftiMain", in.fti);
    addCompressed("/$OBJINST", in.objinst);
    addCompressed("/$WWAssociativeLinks/Property", in.aLinksProperty);
    addCompressed("/$WWKeywordLinks/BTree", in.kLinksBTree);
    addCompressed("/$WWKeywordLinks/Data", in.kLinksData);
    addCompressed("/$WWKeywordLinks/Map", in.kLinksMap);
    addCompressed("/$WWKeywordLinks/Property", in.kLinksProperty);

    std::vector<uint8_t> sec1;
    for (Pending& p : compressed) {
        p.offset = sec1.size();
        p.length = p.data.size();
        sec1.insert(sec1.end(), p.data.begin(), p.data.end());
    }
    const lzx::Result lz = lzx::compress(sec1);

    // --- Secao 0: arquivos nao comprimidos ---------------------------------
    std::vector<Pending> rawFiles;
    auto addRaw = [&](const std::string& name,
                      const std::vector<uint8_t>& data) {
        rawFiles.push_back({name, 0, data, true, 0, 0});
    };
    // O HHC coloca NameList no inicio da secao 0, seguido de #SYSTEM.
    addRaw("::DataSpace/NameList", makeNameList());
    addRaw("/#SYSTEM", in.system);
    addRaw("/#ITBITS", {});
    addRaw("::DataSpace/Storage/MSCompressed/Transform/List",
           makeTransformList());
    addRaw("::DataSpace/Storage/MSCompressed/ControlData", makeControlData());
    {
        std::vector<uint8_t> span(8, 0);
        put64(span, 0, lz.uncompressedSize);
        addRaw("::DataSpace/Storage/MSCompressed/SpanInfo", span);
    }
    addRaw(kResetTableName, makeResetTable(lz));

    std::vector<uint8_t> sec0;
    for (Pending& p : rawFiles) {
        p.offset = sec0.size();
        p.length = p.data.size();
        sec0.insert(sec0.end(), p.data.begin(), p.data.end());
    }

    // Content: ponteiro para o fluxo LZX; seus dados ficam depois da secao 0.
    rawFiles.push_back({"::DataSpace/Storage/MSCompressed/Content", 0, {}, false,
                        static_cast<uint64_t>(sec0.size()), lz.stream.size()});

    // Diretorios (entries de tamanho zero).
    rawFiles.push_back({"/", 0, {}, false, 0, 0});
    rawFiles.push_back({"/$WWAssociativeLinks/", 0, {}, false, 0, 0});
    rawFiles.push_back({"/$WWKeywordLinks/", 0, {}, false, 0, 0});
    rawFiles.push_back({kTransformDir, 0, {}, false, 0, 0});

    std::vector<DirEntry> entries;
    // O HHC registra arquivos vazios com offset zero.
    for (const Pending& p : rawFiles) {
        entries.push_back({p.name, p.section,
                           p.length == 0 ? 0 : p.offset, p.length});
    }
    for (const Pending& p : compressed) {
        entries.push_back({p.name, p.section,
                           p.length == 0 ? 0 : p.offset, p.length});
    }

    Directory dir;
    if (!buildDirectory(entries, dir, error)) return false;

    const uint64_t dirBytes = static_cast<uint64_t>(dir.pmgl.size() + dir.pmgi.size()) * 4096;
    const uint64_t contentOffset = 0x60 + 0x18 + 0x54 + dirBytes;
    const uint64_t totalLen = contentOffset + sec0.size() + lz.stream.size();

    // --- ITSF + tabela de secoes -------------------------------------------
    std::vector<uint8_t> out;
    raw(out, "ITSF", 4);
    w32(out, 3);
    w32(out, 0x60);
    w32(out, 1);
    w32(out, static_cast<uint32_t>(options.timestamp));
    w32(out, options.lcid);
    raw(out, kGuidDir, 16);
    raw(out, kGuidData, 16);
    w64(out, 0x60);          // OffsetHS0
    w64(out, 0x18);          // LengthHS0
    w64(out, 0x78);                  // OffsetHS1
    w64(out, 0x54 + dirBytes);       // LengthHS1 (cabecalho + chunks ITSP)
    w64(out, contentOffset);         // OffsetCS0

    // --- Header Section 0 ---------------------------------------------------
    w32(out, 510);
    w32(out, 0);
    w64(out, totalLen);
    w32(out, 0);
    w32(out, 0);

    // --- Header Section 1 (ITSP) --------------------------------------------
    raw(out, "ITSP", 4);
    w32(out, 1);
    w32(out, 0x54);
    w32(out, 10);
    w32(out, 4096);
    w32(out, 2);             // density / block index interval
    w32(out, dir.depth);
    w32(out, dir.indexRoot);
    w32(out, dir.firstPmgl);
    w32(out, dir.lastPmgl);
    w32(out, 0xFFFFFFFFu);
    w32(out, dir.numChunks);
    w32(out, options.lcid);
    raw(out, kGuidItsp, 16);
    w32(out, 0x54);
    zeros(out, 12);

    if (out.size() != 0xCC) {
        error = "tamanho inesperado do cabecalho CHM";
        return false;
    }

    for (const Chunk& c : dir.pmgl) raw(out, c.data.data(), c.data.size());
    for (const Chunk& c : dir.pmgi) raw(out, c.data.data(), c.data.size());
    if (out.size() != contentOffset) {
        error = "offset de conteudo inconsistente";
        return false;
    }
    out.insert(out.end(), sec0.begin(), sec0.end());
    out.insert(out.end(), lz.stream.begin(), lz.stream.end());

    if (out.size() != totalLen) {
        error = "tamanho final inconsistente";
        return false;
    }

    std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "nao foi possivel criar: " + outPath;
        return false;
    }
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!f) {
        error = "falha ao escrever: " + outPath;
        return false;
    }
    return true;
}

} // namespace chm
