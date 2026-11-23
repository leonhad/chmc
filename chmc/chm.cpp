#include "chm.h"
#include "lzx.h"
#include "objinst_table.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <utility>

namespace chm {
    namespace {
        // ---------------------------------------------------------------------------
        // Little-endian writers
        // ---------------------------------------------------------------------------
        void w16(std::vector<uint8_t> &v, const uint16_t x) {
            v.push_back(static_cast<uint8_t>(x));
            v.push_back(static_cast<uint8_t>(x >> 8));
        }

        void w32(std::vector<uint8_t> &v, const uint32_t x) {
            for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
        }

        void w64(std::vector<uint8_t> &v, const uint64_t x) {
            for (int i = 0; i < 8; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
        }

        void put16(uint8_t *v, const size_t at, const uint16_t x) {
            v[at] = static_cast<uint8_t>(x);
            v[at + 1] = static_cast<uint8_t>(x >> 8);
        }

        void put32(uint8_t *v, const size_t at, const uint32_t x) {
            for (int i = 0; i < 4; ++i) v[at + i] = static_cast<uint8_t>(x >> (8 * i));
        }

        void put32(std::vector<uint8_t> &v, const size_t at, const uint32_t x) {
            put32(v.data(), at, x);
        }

        void put64(uint8_t *v, const size_t at, const uint64_t x) {
            for (int i = 0; i < 8; ++i) v[at + i] = static_cast<uint8_t>(x >> (8 * i));
        }

        void put64(std::vector<uint8_t> &v, const size_t at, const uint64_t x) {
            put64(v.data(), at, x);
        }

        void raw(uint8_t *v, const void *p, const size_t n) {
            auto b = static_cast<const uint8_t *>(p);
            std::memcpy(v, b, n);
        }

        void raw(std::vector<uint8_t> &v, const void *p, const size_t n) {
            const auto *b = static_cast<const uint8_t *>(p);
            v.insert(v.end(), b, b + n);
        }

        void cstr(std::vector<uint8_t> &v, const std::string &s) {
            raw(v, s.data(), s.size());
            v.push_back(0);
        }

        void zeros(std::vector<uint8_t> &v, const size_t n) { v.insert(v.end(), n, 0); }

        std::string lower(const std::string &s) {
            std::string r;
            r.reserve(s.size());
            for (char c: s) r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            return r;
        }

        std::string stripSlash(const std::string &s) {
            return !s.empty() && s[0] == '/' ? s.substr(1) : s;
        }

        // ENCINT: 7-bit groups, from most significant to least significant.
        int encIntLen(const uint64_t x) {
            int n = 1;
            while (x >= 1ULL << (7 * n)) ++n;
            return n;
        }

        void encInt(std::vector<uint8_t> &v, const uint64_t x) {
            const int groups = encIntLen(x);
            for (int i = groups - 1; i >= 0; --i) {
                auto b = static_cast<uint8_t>((x >> (7 * i)) & 0x7F);
                if (i > 0) b |= 0x80;
                v.push_back(b);
            }
        }

        // $FIftiMain uses 7-bit groups in reverse order: the least
        // significant byte comes first and bit 0x80 indicates there are more groups.
        void encIntLe7(std::vector<uint8_t> &v, uint64_t x) {
            do {
                auto b = static_cast<uint8_t>(x & 0x7F);
                x >>= 7;
                if (x) b |= 0x80;
                v.push_back(b);
            } while (x);
        }

        void encIntLe7At(std::vector<uint8_t> &v, size_t &at, const uint64_t x) {
            std::vector<uint8_t> tmp;
            encIntLe7(tmp, x);
            for (const uint8_t b: tmp) v[at++] = b;
        }

        // GUIDs already in byte order (the first three fields little-endian).
        const uint8_t kGuidDir[16] = {
            0x10, 0xFD, 0x01, 0x7C, 0xAA, 0x7B, 0xD0, 0x11,
            0x9E, 0x0C, 0x00, 0xA0, 0xC9, 0x22, 0xE6, 0xEC
        };
        const uint8_t kGuidData[16] = {
            0x11, 0xFD, 0x01, 0x7C, 0xAA, 0x7B, 0xD0, 0x11,
            0x9E, 0x0C, 0x00, 0xA0, 0xC9, 0x22, 0xE6, 0xEC
        };
        const uint8_t kGuidItsp[16] = {
            0x6A, 0x92, 0x02, 0x5D, 0x2E, 0x21, 0xD0, 0x11,
            0x9D, 0xF9, 0x00, 0xA0, 0xC9, 0x22, 0xE6, 0xEC
        };

        const char *const kResetTableName =
                "::DataSpace/Storage/MSCompressed/Transform/"
                "{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}/InstanceData/ResetTable";
        const char *const kTransformDir =
                "::DataSpace/Storage/MSCompressed/Transform/"
                "{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}/InstanceData/";

        // ---------------------------------------------------------------------------
        // #STRINGS: 4096-byte blocks; offset 0 and an empty string
        // ---------------------------------------------------------------------------
        class StringTable {
        public:
            StringTable() { data_.push_back(0); }

            void reserve(size_t n) { data_.reserve(data_.size() + n + 1); }

            uint32_t add(const std::string &s) {
                const size_t inBlock = data_.size() % 4096;
                if (inBlock + s.size() + 1 > 4096) {
                    zeros(data_, 4096 - inBlock);
                }
                const uint32_t off = data_.size();
                cstr(data_, s);
                return off;
            }

            [[nodiscard]] const std::vector<uint8_t> &bytes() const { return data_; }

        private:
            std::vector<uint8_t> data_;
        };

        // ---------------------------------------------------------------------------
        // Internal files (#SYSTEM, #TOPICS, #URLTBL, etc.)
        // ---------------------------------------------------------------------------
        void addSystemEntry(std::vector<uint8_t> &v, const uint16_t code,
                            const void *data, const size_t len) {
            w16(v, code);
            w16(v, static_cast<uint16_t>(len));
            raw(v, data, len);
        }

        void addSystemString(std::vector<uint8_t> &v, const uint16_t code,
                             const std::string &s) {
            addSystemEntry(v, code, s.data(), s.size() + 1);
        }

        void addSystemU32(std::vector<uint8_t> &v, const uint16_t code, const uint32_t x) {
            addSystemEntry(v, code, &x, 4);
        }

        struct Internal {
            std::vector<uint8_t> system, strings, topics, urltbl, urlstr;
            std::vector<uint8_t> tocidx, idxhdr, windows;
            std::vector<uint8_t> fti, objinst;
            std::vector<uint8_t> aLinksProperty, kLinksBTree, kLinksData;
            std::vector<uint8_t> kLinksMap, kLinksProperty;
            bool synthWindow = false;
        };

        // Extracts the content of the <title> tag from an HTML/HHC/HHK file.
        std::string htmlTitle(const std::vector<uint8_t> &data) {
            const std::string s(reinterpret_cast<const char *>(data.data()), data.size());
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

        // Deterministic ID used in special #URLTBL entries.
        uint32_t makeId(const std::string &s) {
            uint32_t h = 2166136261u;
            for (const unsigned char c: s) {
                h ^= c;
                h *= 16777619u;
            }
            return h ^ 0x62A83769u;
        }

        // ---------------------------------------------------------------------------
        // Internal index blobs: $OBJINST, $WW* and $FIftiMain
        // ---------------------------------------------------------------------------
        std::string normalizeLocal(const std::string &s) {
            size_t start = 0;
            while (start < s.size() && s[start] == '/') ++start;
            std::string r;
            r.reserve(s.size() - start);
            for (size_t i = start; i < s.size(); ++i) {
                char c = s[i];
                if (c == '\\') c = '/';
                r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            return r;
        }

        // $OBJINST is a list of two COM objects used by the index engine.
        std::vector<uint8_t> buildObjInst(const uint32_t lcid) {
            std::vector<uint8_t> v;
            w32(v, 0x04000000u);
            w32(v, 2);
            w32(v, 24);
            w32(v, 2655);
            w32(v, 2679);
            w32(v, 36);

            // {4662DAAF-D393-11D0-9A56-00C04FB68BF7}
            const uint8_t guid1[16] = {
                0xAF, 0xDA, 0x62, 0x46, 0x93, 0xD3, 0xD0, 0x11,
                0x9A, 0x56, 0x00, 0xC0, 0x4F, 0xB6, 0x8B, 0xF7
            };
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
            for (const auto &row: kObjInstTable) raw(v, row, sizeof(row));
            const uint8_t tail[15] = {
                0xC6, 0x61, 0x65, 0xE6, 0x61, 0x65, 0xDF, 0x73,
                0x73, 0x8C, 0x6F, 0x65, 0x9C, 0x6F, 0x65
            };
            raw(v, tail, sizeof(tail));

            // {4662DAB0-D393-11D0-9A56-00C04FB68B66}
            const uint8_t guid2[16] = {
                0xB0, 0xDA, 0x62, 0x46, 0x93, 0xD3, 0xD0, 0x11,
                0x9A, 0x56, 0x00, 0xC0, 0x4F, 0xB6, 0x8B, 0x66
            };
            raw(v, guid2, sizeof(guid2));
            w32(v, 666);
            w32(v, 1252);
            w32(v, lcid);
            w32(v, 10031);
            w32(v, 0);
            return v;
        }

        uint16_t cp1252ToUtf16(const uint8_t c) {
            if (c < 0x80 || c >= 0xA0) return c;
            static const uint16_t map[32] = {
                0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
                0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
            };
            return map[c - 0x80];
        }

        void utf16Nt(std::vector<uint8_t> &v, const std::string &s) {
            for (const unsigned char c: s) w16(v, cp1252ToUtf16(c));
            w16(v, 0);
        }

        struct KeywordIndexEntry {
            std::string word;
            std::string local;
            uint32_t topicIndex = 0;
            uint32_t dataOffset = 0;
        };

        // Writes a listing block (word + topic pairs) to the BTree.
        std::vector<uint8_t> listingEntryBytes(const KeywordIndexEntry &e) {
            std::vector<uint8_t> v;
            utf16Nt(v, e.word);
            w16(v, 0); // seeAlso
            w16(v, 0); // depth
            w32(v, 0); // comma character index
            w32(v, 0); // unknown
            w32(v, 1); // number of pairs
            w32(v, e.topicIndex);
            w32(v, 1); // always 1
            w32(v, e.dataOffset);
            return v;
        }

        // Top-level entry: ends with the child block number.
        std::vector<uint8_t> indexEntryBytes(const std::vector<uint8_t> &listing,
                                             const uint32_t childBlock) {
            std::vector v(listing.begin(), listing.end() - 8);
            w32(v, childBlock);
            return v;
        }

        struct BlobSet {
            std::vector<uint8_t> btree, data, map, prop;
        };

        BlobSet buildKeywordLinks(const std::vector<KeywordIndexEntry> &entries,
                                  uint32_t totalTopics, uint32_t lcid) {
            BlobSet out;
            out.prop.assign(4, 0);
            if (entries.empty()) return out;

            // Case-insensitive order, as in the Microsoft compiler.
            std::vector<size_t> order(entries.size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::sort(order.begin(), order.end(), [&](const size_t a, const size_t b) {
                const std::string la = lower(entries[a].word);
                if (const std::string lb = lower(entries[b].word); la != lb) return la < lb;
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
            auto flushLeaf = [&] {
                put16(block.data(), 0, static_cast<uint16_t>(2048 - used));
                put16(block.data(), 2, static_cast<uint16_t>(count));
                put32(block.data(), 4,
                      static_cast<uint32_t>(leaves.empty() ? -1 : static_cast<int32_t>(leaves.size() - 1)));
                put32(block.data(), 8,
                      static_cast<uint32_t>(leaves.empty() ? -1 : static_cast<int32_t>(leaves.size())));
                leaves.push_back({lastListing, (out.btree.size())});
                out.btree.insert(out.btree.end(), block.begin(), block.end());
                std::fill(block.begin(), block.end(), 0);
                used = 12;
                count = 0;
            };

            for (size_t idx: order) {
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
                lastListing.back() = 0; // mod13 does not participate in the index header

                // Each data record is constant; the second DWORD appears to be the
                // total number of topic nodes in the CHM.
                uint8_t dataEntry[13] = {0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0, 0, 0, 0};
                put32(dataEntry, 4, totalTopics);
                out.data.insert(out.data.end(), dataEntry, dataEntry + 13);
            }
            flushLeaf();

            // Map: number of listing blocks and, for each, the accumulated
            // number of entries before it and the block number.
            w16(out.map, static_cast<uint16_t>(leaves.size()));
            uint32_t accumulated = 0;
            for (size_t i = 0; i < leaves.size(); ++i) {
                w32(out.map, accumulated);
                w32(out.map, i);
                accumulated = (i + 1) * entries.size() / leaves.size();
            }

            // Observed constant when there are links of this type.
            const uint32_t props[8] = {0, 0, 0, 0x0C, 1, 1, 0, 0};
            out.prop.clear();
            for (uint32_t p: props) w32(out.prop, p);

            // Index levels, when there is more than one listing block.
            std::vector<std::vector<uint8_t> > allBlocks;
            allBlocks.emplace_back(out.btree.begin(), out.btree.begin() + static_cast<std::ptrdiff_t>(leaves.size()) * 2048);
            struct IndexItem {
                std::vector<uint8_t> entry;
                uint32_t block = 0;
            };
            std::vector<IndexItem> level;
            level.reserve(leaves.size());
            for (size_t i = 0; i < leaves.size(); ++i) {
                level.push_back({
                    indexEntryBytes(leaves[i].lastListing, i),
                    i
                });
            }
            uint32_t treeDepth = 1;
            uint32_t nextBlock = leaves.size();
            while (level.size() > 1) {
                std::vector<IndexItem> next;
                std::vector<uint8_t> ib(2048, 0);
                size_t usedIdx = 8;
                int countIdx = 0;
                std::vector<uint8_t> lastIndexEntry;
                auto flushIndex = [&] {
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
                for (const IndexItem &item: level) {
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
            for (const auto &b: allBlocks) out.btree.insert(out.btree.end(), b.begin(), b.end());

            std::vector<uint8_t> header(76, 0);
            raw(header.data(), ";)", 2);
            put16(header.data(), 2, 0x0104);
            put16(header.data(), 4, 2048);
            raw(header.data() + 6, "X44", 3);
            put32(header.data(), 0x1A, leaves.size() - 1);
            put32(header.data(), 0x1E, allBlocks.empty() ? 0 : allBlocks.size() - 1);
            put32(header.data(), 0x22, 0xFFFFFFFFu);
            put32(header.data(), 0x26, allBlocks.size());
            put16(header.data(), 0x2A, static_cast<uint16_t>(treeDepth));
            put32(header.data(), 0x2C, order.size());
            put32(header.data(), 0x30, 1252);
            put32(header.data(), 0x34, lcid);
            put32(header.data(), 0x38, 1);
            put32(header.data(), 0x3C, 10031);
            out.btree.insert(out.btree.begin(), header.begin(), header.end());
            return out;
        }

        // ---------------------------------------------------------------------------
        // $FIftiMain: full-text index
        // ---------------------------------------------------------------------------
        struct HtmlWord {
            std::string text;
            bool isTitle = false;
            uint32_t position = 0;
        };

        std::string decodeEntityRef(const std::string &s) {
            if (s.size() < 3 || s[0] != '&' || s.back() != ';') return " ";
            const std::string name = s.substr(1, s.size() - 2);
            if (!name.empty() && name[0] == '#') {
                unsigned long cp;
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
                if (cp <= 0xFF) r.push_back(static_cast<char>(cp));
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

        bool isIndexWordByte(const unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                   c == '_' || c == 0x01 || c == 0xDE || c == 0xFE;
        }

        void tokenizeIndexedText(const std::string &raw, const bool isTitle, uint32_t &position,
                                 std::vector<HtmlWord> &out) {
            // Replaces entities with a single character (or separator) and lowercase.
            std::string s;
            s.reserve(raw.size());
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
            auto flush = [&] {
                if (word.empty()) return;
                std::string clean;
                for (const char c: word) if (c != '\'' && c != ',') clean.push_back(c);
                if (!clean.empty()) out.push_back({clean, isTitle, position++});
                word.clear();
                inWord = false;
                numberWord = false;
            };
            for (size_t i = 0; i < s.size(); ++i) {
                const auto c = static_cast<unsigned char>(s[i]);
                const unsigned char next = i + 1 < s.size() ? static_cast<unsigned char>(s[i + 1]) : 0;
                bool ends = !isIndexWordByte(c);
                if (ends && numberWord && c == ',') ends = false;
                if (ends && numberWord && c == '.' && next >= '0' && next <= '9') ends = false;
                if (ends && inWord && c == '\'') ends = false;
                if (ends) {
                    flush();
                } else if (!inWord) {
                    inWord = true;
                    numberWord = c >= '0' && c <= '9';
                    word.push_back(static_cast<char>(c));
                } else {
                    word.push_back(static_cast<char>(c));
                }
            }
            flush();
        }

        void extractIndexedWords(const std::vector<uint8_t> &data, std::vector<HtmlWord> &out) {
            const std::string s(reinterpret_cast<const char *>(data.data()), data.size());
            bool inTitle = false, inBody = false, inScript = false, inStyle = false;
            uint32_t position = 0;
            size_t i = 0;
            while (i < s.size()) {
                if (s[i] != '<') {
                    const size_t j = s.find('<', i);
                    const size_t end = j == std::string::npos ? s.size() : j;
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
            std::vector<uint8_t> &out;
            uint8_t buf = 0;
            int used = 0;

            explicit BitWriter(std::vector<uint8_t> &o) : out(o) {
            }

            void write(const uint32_t value, const int bits) {
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

        void writeScaleRoot(BitWriter &bw, const uint32_t value, const int root) {
            uint32_t tmp = value;
            int needed = 0;
            while (tmp) {
                ++needed;
                tmp >>= 1;
            }
            const int prefix = std::max(0, needed - root);
            int rootBits = std::max(needed - 1, root);
            if (rootBits < 0) rootBits = 0;
            const uint32_t mask = rootBits >= 32 ? 0xFFFFFFFFu : (1u << rootBits) - 1u;
            uint32_t bits = (~mask << 1) | (value & mask);
            const int total = prefix + 1 + rootBits;
            if (total >= 32) bits = (~mask << 1) | (value & mask);
            else bits &= (1u << total) - 1u;
            bw.write(bits, total);
        }

        std::string adjustedWord(const std::string &word, uint8_t &offset,
                                 const std::string &previous) {
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

        std::vector<uint8_t> buildFtiMain(const std::vector<FtiWord> &words,
                                          const FtiStats &stats, uint32_t lcid) {
            if (words.empty()) return {};
            std::vector<uint8_t> out(0x400, 0);

            // The FPC writer uses these temporary roots and writes the same values
            // in the final header. The scales stay at 2.
            constexpr int kDocRoot = 1, kCountRoot = 1, kLocRoot = 5;
            constexpr size_t kNodeSize = 4096;
            struct NodeRef {
                uint32_t offset = 0;
                std::string lastWord;
            };
            std::vector<NodeRef> leaves;
            std::vector<uint8_t> block(kNodeSize, 0);
            size_t used = 8; // DWORD nextLeaf + WORD 0 + WORD freeSpace
            std::string lastWord;
            uint32_t lastLeafAt = 0;
            uint32_t lastLeafFree = 0;

            auto flushLeaf = [&] {
                put32(block.data(), 0, 0);
                lastLeafFree = kNodeSize - used;
                put16(block.data(), 6, static_cast<uint16_t>(lastLeafFree));
                if (lastLeafAt != 0) put32(out.data(), lastLeafAt, out.size());
                leaves.push_back({(out.size()), lastWord});
                out.insert(out.end(), block.begin(), block.end());
                lastLeafAt = out.size() - kNodeSize;
                std::fill(block.begin(), block.end(), 0);
                used = 8;
                lastWord.clear();
            };

            for (const FtiWord &w: words) {
                if (w.text.size() > 99) continue;
                uint8_t offset = 0;
                std::string part = adjustedWord(w.text, offset, lastWord);
                if (17 + part.size() >= kNodeSize - used) {
                    flushLeaf();
                    part = adjustedWord(w.text, offset, lastWord);
                }

                const uint32_t wlcAt = out.size();
                {
                    BitWriter bw(out);
                    uint32_t lastDoc = 0;
                    for (const FtiPosting &p: w.docs) {
                        uint32_t lastLoc = 0;
                        writeScaleRoot(bw, p.topic - lastDoc, kDocRoot);
                        lastDoc = p.topic;
                        writeScaleRoot(bw, p.positions.size(), kCountRoot);
                        for (uint32_t pos: p.positions) {
                            writeScaleRoot(bw, pos - lastLoc, kLocRoot);
                            lastLoc = pos;
                        }
                        bw.flush();
                    }
                }
                const uint32_t wlcSize = out.size() - wlcAt;

                block[used++] = static_cast<uint8_t>(part.size() + 1);
                block[used++] = offset;
                std::copy(part.begin(), part.end(), block.begin() + static_cast<ptrdiff_t>(used));
                used += part.size();
                block[used++] = w.isTitle ? 1 : 0;
                encIntLe7At(block, used, w.docs.size());
                put32(block.data(), used, wlcAt);
                used += 4;
                put16(block.data(), used, 0);
                used += 2;
                encIntLe7At(block, used, wlcSize);
                lastWord = w.text;
            }
            flushLeaf();

            // Index nodes: each entry stores the last term of the child block and the
            // absolute offset of that block. The header of an index node has only
            // the WORD of free space; entries start at offset 2.
            std::vector<NodeRef> level = leaves;
            size_t indexLevels = 0;
            while (level.size() > 1) {
                std::vector<NodeRef> next;
                std::vector<uint8_t> ib(kNodeSize, 0);
                put16(ib.data(), 0, kNodeSize - 2); // free space
                size_t iu = 2;
                std::string lastIndexWord;
                auto flushIndex = [&](const bool newBlockNeeded) {
                    put16(ib.data(), 0, static_cast<uint16_t>(kNodeSize - iu));
                    const uint32_t at = out.size();
                    out.insert(out.end(), ib.begin(), ib.end());
                    next.push_back({at, lastIndexWord});
                    std::fill(ib.begin(), ib.end(), 0);
                    iu = 2;
                    lastIndexWord.clear();
                    if (newBlockNeeded) put16(ib.data(), 0, kNodeSize - 2);
                };
                for (const NodeRef &node: level) {
                    uint8_t off = 0;
                    std::string part = adjustedWord(node.lastWord, off, lastIndexWord);
                    const size_t need = 2 + part.size() + 4 + 2;
                    if (iu + need >= kNodeSize) {
                        flushIndex(true);
                        part = adjustedWord(node.lastWord, off, lastIndexWord);
                    }
                    ib[iu++] = static_cast<uint8_t>(part.size() + 1);
                    ib[iu++] = off;
                    std::copy(part.begin(), part.end(), ib.begin() + static_cast<ptrdiff_t>(iu));
                    iu += part.size();
                    put32(ib.data(), iu, node.offset);
                    iu += 4;
                    put16(ib.data(), iu, 0);
                    iu += 2;
                    lastIndexWord = node.lastWord;
                }
                flushIndex(false);
                level = std::move(next);
                ++indexLevels;
            }

            const uint32_t rootOffset = out.size() - kNodeSize;
            const auto depth = static_cast<uint16_t>(1 + indexLevels);

            std::vector<uint8_t> header(0x400, 0);
            header[2] = 0x28;
            put32(header, 4, stats.files);
            put32(header, 8, rootOffset);
            put32(header, 0x10, leaves.size());
            put32(header, 0x14, rootOffset);
            put16(header.data(), 0x18, depth);
            put32(header, 0x1A, 7);
            header[0x1E] = 2;
            header[0x1F] = kDocRoot;
            header[0x20] = 2;
            header[0x21] = kCountRoot;
            header[0x22] = 2;
            header[0x23] = kLocRoot;
            put32(header, 0x2E, kNodeSize);
            put32(header, 0x32, 0);
            put32(header, 0x36, 1);
            put32(header, 0x3A, 5);
            put32(header, 0x3E, stats.longest);
            put32(header, 0x42, static_cast<uint32_t>(stats.totalOccurrences));
            put32(header, 0x46, words.size());
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

        Internal buildInternal(const BuildOptions &opt,
                               const std::vector<InputFile> &files) {
            Internal in;

            // --- #STRINGS -----------------------------------------------------------
            // Order observed in CHMs compiled by HHA: "", window name, title,
            // table of contents, index, starting topic, topic titles (HTML) and titles
            // of table of contents/index files. The font is in code 16 of #SYSTEM.
            // Precompute sorted index for O(log n) file lookup instead of O(n) linear scan.
            std::vector<std::pair<std::string, const InputFile *>> fileIndex;
            fileIndex.reserve(files.size());
            for (const InputFile &f: files) {
                fileIndex.emplace_back(stripSlash(f.internalName), &f);
            }
            std::sort(fileIndex.begin(), fileIndex.end(),
                      [](const auto &a, const auto &b) { return a.first < b.first; });
            auto findFile = [&](const std::string &internal) -> const InputFile * {
                auto it = std::lower_bound(fileIndex.begin(), fileIndex.end(), internal,
                                           [](const auto &pair, const std::string &s) {
                                               return pair.first < s;
                                           });
                if (it != fileIndex.end() && it->first == internal) return it->second;
                return nullptr;
            };

            StringTable st;
            size_t totalStringSize = 0;
            for (const InputFile &f: files) totalStringSize += f.internalName.size() + f.data.size() / 100;
            st.reserve(totalStringSize);
            auto stAddFile = [&](const std::string &s) -> uint32_t {
                if (s.empty()) return 0;
                return st.add(stripSlash(s));
            };

            // Windows: uses [WINDOWS] from .hhp when present; otherwise, generates a
            // default window compatible with hh.exe. Advanced embedded windows are not
            // supported.
            std::vector<WindowDef> windows = opt.windows;
            const bool synthWindow = windows.empty();
            in.synthWindow = synthWindow;
            if (synthWindow) {
                WindowDef w;
                w.name = opt.defaultWindow.empty() ? "main" : opt.defaultWindow;
                w.caption = opt.title;
                w.toc = opt.contentsFile;
                w.index = opt.indexFile;
                w.defaultFile = stripSlash(opt.defaultTopic);
                w.homeFile = stripSlash(opt.defaultTopic);
                windows.push_back(std::move(w));
            }
            for (WindowDef &w: windows) {
                if (w.name.empty()) w.name = opt.defaultWindow.empty() ? "main" : opt.defaultWindow;
                if (w.caption.empty()) w.caption = opt.title;
                if (w.defaultFile.empty()) w.defaultFile = stripSlash(opt.defaultTopic);
                if (w.homeFile.empty()) w.homeFile = w.defaultFile;
                // fsValidMembers: sets bits according to present/explicitly given fields.
                // Semantics based on the HH_WINTYPE table observed in real CHMs.
                uint32_t v = w.validFlags;
                if (v == 0) {
                    if (w.navStyle != 0) v |= 0x00000002;
                    if (w.styles != 0) v |= 0x00000004;
                    if (w.exStyles != 0) v |= 0x00000008;
                    if (w.rect[0] || w.rect[1] || w.rect[2] || w.rect[3]) v |= 0x00000010;
                    if (w.navWidth != 0) v |= 0x00000020;
                    if (w.showState != 0) v |= 0x00000040;
                    if (w.buttons != 0) v |= 0x00000100;
                    if (w.navPaneClosed != 0) v |= 0x00000200;
                    if (w.tabPos != 0) v |= 0x00000400;
                    if (w.defaultPane != 0) v |= 0x00002000;
                    w.validFlags = v;
                }
            }

            // Selects default window by Default Window=.
            size_t defaultWindowIndex = 0;
            for (size_t i = 0; i < windows.size(); ++i) {
                if (lower(windows[i].name) == lower(opt.defaultWindow)) {
                    defaultWindowIndex = i;
                    break;
                }
            }
            const WindowDef &dw = windows[defaultWindowIndex];

            (void) dw;
            (void) stAddFile;

            // --- Topics: HTML from table of contents, standalone HTML, table of contents.hhc and index.hhk -----
            struct Topic {
                std::string local;
                std::string title;
                uint32_t titleOff = 0xFFFFFFFFu;
                bool inToc = false;
                int tocNode = -1;
            };
            std::vector<Topic> topics;
            topics.reserve(opt.toc.size() + files.size());
            std::vector tocTopicIndex(opt.toc.size(), -1);
            std::unordered_map<std::string, int> topicByLocal;
            topicByLocal.reserve(opt.toc.size() + files.size());
            std::unordered_map<std::string, std::string> titleCache;
            titleCache.reserve(files.size());
            for (const InputFile &f: files) {
                const std::string loc = stripSlash(f.internalName);
                if (const std::string lo = lower(loc); lo.find(".htm") == std::string::npos) continue;
                titleCache[normalizeLocal(loc)] = htmlTitle(f.data);
            }
            auto pageTitleCached = [&](const std::string &internal, const std::string &fallback) {
                if (const auto it = titleCache.find(normalizeLocal(internal)); it != titleCache.end() && !it->second.empty()) return it->second;
                return fallback;
            };
            for (size_t i = 0; i < opt.toc.size(); ++i) {
                if (opt.toc[i].local.empty()) continue;
                const std::string loc = stripSlash(opt.toc[i].local);
                const std::string key = normalizeLocal(loc);
                auto it = topicByLocal.find(key);
                if (it != topicByLocal.end()) {
                    tocTopicIndex[i] = it->second;
                    Topic &t = topics[it->second];
                    t.inToc = true;
                    if (t.tocNode < 0) t.tocNode = static_cast<int>(i);
                    continue;
                }
                tocTopicIndex[i] = static_cast<int>(topics.size());
                topicByLocal[key] = tocTopicIndex[i];
                topics.push_back({
                    loc, pageTitleCached(loc, opt.toc[i].name), 0xFFFFFFFFu,
                    true, static_cast<int>(i)
                });
            }
            std::unordered_set<std::string> knownLocals;
            knownLocals.reserve(topics.size() + files.size());
            for (const Topic &t: topics) knownLocals.insert(normalizeLocal(t.local));
            for (const InputFile &f: files) {
                const std::string loc = stripSlash(f.internalName);
                const std::string lo = lower(loc);
                if (lo.find(".htm") == std::string::npos) continue;
                const std::string key = normalizeLocal(loc);
                if (knownLocals.find(key) != knownLocals.end()) continue;
                knownLocals.insert(key);
                topics.push_back({loc, pageTitleCached(loc, loc), 0xFFFFFFFFu, false, -1});
            }

            if (topics.empty()) {
                topics.push_back({
                    stripSlash(opt.defaultTopic), opt.title,
                    0xFFFFFFFFu, false, -1
                });
            }
            const size_t htmlTopicCount = topics.size();
            (void) htmlTopicCount;

            for (Topic &t: topics) t.titleOff = st.add(t.title);

            // --- #URLSTR: 8-byte zero prefix before each URL ----------------------
            in.urlstr.reserve(topics.size() * 20 + 1);
            in.urlstr.push_back(0x42);
            std::vector<uint32_t> urlSlotOff(topics.size(), 0);
            for (size_t i = 0; i < topics.size(); ++i) {
                urlSlotOff[i] = in.urlstr.size();
                zeros(in.urlstr, 8);
                cstr(in.urlstr, topics[i].local);
            }

            // --- #URLTBL: 4096-byte blocks, 341 entries per block --------------
            // Observed in HHC CHMs: one entry per HTML topic; no entries for
            // .hhc/.hhk. Sorting by ID facilitates search.
            struct UrlEntry {
                uint32_t id;
                uint32_t topic;
                uint32_t urlOff;
            };
            std::vector<UrlEntry> urlEntries;
            urlEntries.reserve(topics.size());
            for (size_t i = 0; i < topics.size(); ++i) {
                urlEntries.push_back({
                    makeId(topics[i].local), i,
                    urlSlotOff[i]
                });
            }
            std::sort(urlEntries.begin(), urlEntries.end(),
                      [](const UrlEntry &a, const UrlEntry &b) { return a.id < b.id; });
            std::vector<uint32_t> topicUrlTblOff(topics.size(), 0);
            constexpr size_t perBlock = 341;
            for (size_t i = 0; i < urlEntries.size(); i += perBlock) {
                const size_t n = std::min(perBlock, urlEntries.size() - i);
                const size_t base = in.urltbl.size();
                const bool full = n == perBlock;
                const size_t blockSize = full ? 4096u : n * 12u;
                in.urltbl.resize(base + blockSize, 0);
                for (size_t j = 0; j < n; ++j) {
                    const auto &[id, topic, urlOff] = urlEntries[i + j];
                    const size_t off = base + j * 12;
                    put32(in.urltbl, off + 0, id);
                    put32(in.urltbl, off + 4, topic);
                    put32(in.urltbl, off + 8, urlOff);
                    if (topic < topicUrlTblOff.size())
                        topicUrlTblOff[topic] = static_cast<uint32_t>(off);
                }
                if (full) {
                    // Observed in HHC: full 4KB blocks store 4096 here.
                    put32(in.urltbl, base + 4092, 4096u);
                }
            }

            // --- #TOCIDX ------------------------------------------------------------
            const size_t n = opt.toc.size();
            std::vector<int> parent(n, -1), firstChild(n, -1), nextSibling(n, -1);
            std::vector lastAtDepth(64, -1);
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
                cursor += nodeSize[i];
            }
            const uint32_t dwordListOff = cursor;
            const uint32_t linkOff = dwordListOff + n * 4;

            in.tocidx.assign(4096, 0);
            put32(in.tocidx, 0, 4096);
            put32(in.tocidx, 4, linkOff);
            put32(in.tocidx, 8, n);
            put32(in.tocidx, 0xC, dwordListOff);

            in.tocidx.resize(linkOff + n * 16, 0);

            for (size_t i = 0; i < n; ++i) {
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
                put32(in.tocidx, dwordListOff + i * 4,
                      tocTopicIndex[i] >= 0
                          ? static_cast<uint32_t>(tocTopicIndex[i])
                          : 0xFFFFFFFFu);
            }
            for (size_t i = 0; i < n; ++i) {
                const size_t at = in.tocidx.size();
                in.tocidx.resize(at + 16, 0);
                put32(in.tocidx, at, nodeOffset[i]);
                put32(in.tocidx, at + 4, 666u + i);
                put32(in.tocidx, at + 8,
                      dwordListOff + i * 4);
                put32(in.tocidx, at + 0xC,
                      tocTopicIndex[i] >= 0
                          ? static_cast<uint32_t>(tocTopicIndex[i])
                          : 0xFFFFFFFFu);
            }
            if (n == 0) in.tocidx.resize(4096 + 16, 0);

            // --- #TOPICS ------------------------------------------------------------
            in.topics.reserve(topics.size() * 16);
            for (size_t i = 0; i < topics.size(); ++i) {
                const Topic &t = topics[i];
                uint32_t tocOff = t.tocNode >= 0 ? nodeOffset[t.tocNode] : 0;
                uint32_t urlTblOff = i < topicUrlTblOff.size() ? topicUrlTblOff[i] : 0;
                w32(in.topics, tocOff);
                w32(in.topics, t.titleOff);
                w32(in.topics, urlTblOff);
                w16(in.topics, t.inToc ? 6 : 2);
                w16(in.topics, 0);
            }
            if (topics.empty()) zeros(in.topics, 16);

            // --- #IDXHDR (same bytes as #SYSTEM entry 13) -------------------
            const uint32_t idxChecksum = makeId("IDXHDR") ^ opt.unixTime;
            in.idxhdr.assign(4096, 0);
            put32(in.idxhdr, 0, 0x4D532354u); // "T#SM" in little-endian
            put32(in.idxhdr, 4, idxChecksum);
            put32(in.idxhdr, 8, 1);
            put32(in.idxhdr, 0xC, topics.size());
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
            // Complete HH_WINTYPE format per htmlhelp.h and chmspec.
            // Layout based on the file_wintype structure from ReactOS hhctrl:
            // Pointer fields (HWND, LPCTSTR) are serialized as DWORD offsets
            // in #STRINGS. Advanced fields (rcHTML, tabOrder, cHistory, rcMinSize,
            // cbInfoTypes, pszCustomTabs) support embedded windows and special types
            // of CHM 1.1+.
            //
            // Offset  Type          Field (htmlhelp.h)
            // 0x00    int           cbStruct
            // 0x04    BOOL          fUniCodeStrings
            // 0x08    DWORD         pszType (offset in #STRINGS)
            // 0x0C    DWORD         fsValidMembers
            // 0x10    DWORD         fsWinProperties
            // 0x14    DWORD         pszCaption (offset in #STRINGS)
            // 0x18    DWORD         dwStyles
            // 0x1C    DWORD         dwExStyles
            // 0x20    RECT          rcWindowPos (16 bytes)
            // 0x30    int           nShowState
            // 0x34    RECT          rcHTML (16 bytes) — HTML panel coordinates
            // 0x44    DWORD         hwndHelp (runtime, written as 0)
            // 0x48    DWORD         hwndCaller (runtime, written as 0)
            // 0x50    DWORD         hwndToolBar (runtime, written as 0)
            // 0x54    DWORD         hwndNavigation (runtime, written as 0)
            // 0x58    DWORD         hwndHTML (runtime, written as 0)
            // 0x5C    DWORD         pszToc (offset in #STRINGS)
            // 0x60    DWORD         pszIndex (offset in #STRINGS)
            // 0x64    DWORD         pszFile (offset in #STRINGS)
            // 0x68    DWORD         pszHome (offset in #STRINGS)
            // 0x6C    DWORD         fsToolBarFlags
            // 0x70    BOOL          fNotExpanded
            // 0x74    int           curNavType
            // 0x78    int           tabpos
            // 0x7C    int           idNotify
            // 0x80    BYTE[17]     tabOrder
            // 0x91    int           cHistory
            // 0x95    DWORD         pszJump1 (offset in #STRINGS) — padding to 0x98
            // 0x98    DWORD         pszJump2 (offset in #STRINGS)
            // 0x9C    DWORD         pszUrlJump1 (offset in #STRINGS)
            // 0xA0    DWORD         pszUrlJump2 (offset in #STRINGS)
            // 0xA4    RECT          rcMinSize (16 bytes) — minimum window size
            // 0xB4    int           cbInfoTypes
            // 0xB8    DWORD         pszCustomTabs (offset in #STRINGS)
            // Total: 0xBC (188 bytes) for CHM 1.0, 0xC0 (192) with padding
            // The HTML Help Workshop writes 196 bytes (0xC4) with final padding.
            const uint32_t winEntrySize = 196;
            in.windows.assign(8 + winEntrySize * windows.size(), 0);
            put32(in.windows, 0, windows.size());
            put32(in.windows, 4, winEntrySize);
            for (size_t i = 0; i < windows.size(); ++i) {
                const WindowDef &w = windows[i];
                const size_t off = 8 + i * winEntrySize;
                auto W32 = [&](const size_t fieldOff, const uint32_t v) { put32(in.windows, off + fieldOff, v); };
                auto WSTR = [&](const size_t fieldOff, const std::string &s) {
                    if (!s.empty()) put32(in.windows, off + fieldOff, st.add(s));
                };
                auto WFILE = [&](const size_t fieldOff, const std::string &s) {
                    if (!s.empty()) put32(in.windows, off + fieldOff, st.add(stripSlash(s)));
                };

                // --- Basic fields (present in all versions) ---------------
                W32(0x00, winEntrySize); // cbStruct
                W32(0x04, 0); // fUniCodeStrings
                WSTR(0x08, w.name); // pszType
                W32(0x0C, w.validFlags); // fsValidMembers
                W32(0x10, w.navStyle); // fsWinProperties
                WSTR(0x14, w.caption.empty() ? opt.title : w.caption); // pszCaption
                W32(0x18, w.styles); // dwStyles
                W32(0x1C, w.exStyles); // dwExStyles
                for (int r = 0; r < 4; ++r) {
                    put32(in.windows, off + 0x20 + r * 4, static_cast<uint32_t>(w.rect[r]));
                }
                W32(0x30, w.showState); // nShowState

                // --- rcHTML: topic panel coordinates (embedded windows) --
                for (int r = 0; r < 4; ++r) {
                    put32(in.windows, off + 0x34 + r * 4, static_cast<uint32_t>(w.rcHTML[r]));
                }

                // --- Window handles (runtime, always 0 in the file) -----------
                W32(0x44, 0); // hwndHelp
                W32(0x48, 0); // hwndCaller
                W32(0x4C, w.navWidth); // iNavWidth
                W32(0x50, 0); // hwndToolBar
                W32(0x54, 0); // hwndNavigation
                W32(0x58, 0); // hwndHTML

                // --- Navigation files ---------------------------------------
                WFILE(0x5C, w.toc); // pszToc
                WFILE(0x60, w.index); // pszIndex
                WFILE(0x64, w.defaultFile); // pszFile
                WFILE(0x68, w.homeFile); // pszHome

                // --- Toolbar and navigation -----------------------------------------
                W32(0x6C, w.buttons); // fsToolBarFlags
                W32(0x70, w.navPaneClosed); // fNotExpanded
                W32(0x74, w.defaultPane); // curNavType
                W32(0x78, w.tabPos); // tabpos
                W32(0x7C, w.idNotify); // idNotify

                // --- tabOrder: tab order (17 bytes) -------------------------
                size_t tabOrderOff = off + 0x80;
                for (int t = 0; t < 17; ++t) {
                    in.windows[tabOrderOff + t] = w.tabOrder[t];
                }

                // --- cHistory: number of history items ----------------------
                put32(in.windows, off + 0x91, static_cast<uint32_t>(w.cHistory));

                // --- Jump buttons ------------------------------------------------
                WSTR(0x98, w.jump1); // pszJump1
                WSTR(0x9C, w.jump2); // pszJump2
                WSTR(0xA0, w.urlJump1); // pszUrlJump1
                WSTR(0xA4, w.urlJump2); // pszUrlJump2

                // --- Advanced fields (CHM 1.1+): embedded windows and special types
                // rcMinSize: minimum window size ----------------------------
                for (int r = 0; r < 4; ++r) {
                    put32(in.windows, off + 0xB4 + r * 4, static_cast<uint32_t>(w.rcMinSize[r]));
                }

                // cbInfoTypes: size of paInfoTypes (for info types) --
                W32(0xC4, static_cast<uint32_t>(w.cbInfoTypes)); // cbInfoTypes

                // pszCustomTabs: custom tab labels ------------------
                WSTR(0xC8, w.customTabs); // pszCustomTabs
            }

            // --- Binary keyword index ($WW*) ----------------------------
            std::map<std::string, uint32_t> topicByLocalBin;
            for (size_t i = 0; i < htmlTopicCount; ++i) {
                topicByLocalBin[normalizeLocal(topics[i].local)] = i;
            }
            std::vector<KeywordIndexEntry> keywordEntries;
            keywordEntries.reserve(opt.keywords.size());
            std::unordered_set<std::string> seenKeyword;
            seenKeyword.reserve(opt.keywords.size());
            for (const KeywordEntry &k: opt.keywords) {
                std::string key = k.name;
                size_t a = 0, b = key.size();
                while (a < b && std::isspace(static_cast<unsigned char>(key[a]))) ++a;
                while (b > a && std::isspace(static_cast<unsigned char>(key[b - 1]))) --b;
                key = lower(key.substr(a, b - a));
                if (key.empty() || !seenKeyword.insert(key).second) continue;
                KeywordIndexEntry e;
                e.word = k.name;
                e.local = k.local;
                const auto it = topicByLocalBin.find(normalizeLocal(k.local));
                e.topicIndex = it == topicByLocalBin.end() ? 0 : it->second;
                keywordEntries.push_back(std::move(e));
            }
            const BlobSet kw = buildKeywordLinks(keywordEntries,
                                                 topics.size(), opt.lcid);
            in.kLinksBTree = kw.btree;
            in.kLinksData = kw.data;
            in.kLinksMap = kw.map;
            in.kLinksProperty = kw.prop;
            in.aLinksProperty.assign(4, 0);
            in.objinst = buildObjInst(opt.lcid);

            // --- Full-text index ($FIftiMain) ------------------------------
            std::vector<FtiWord> ftiWords;
            ftiWords.reserve(htmlTopicCount * 100);
            std::map<std::string, size_t> ftiIndex;
            FtiStats stats;
            for (size_t i = 0; i < htmlTopicCount; ++i) {
                const InputFile *f = findFile(topics[i].local);
                if (!f) continue;
                ++stats.files;
                std::vector<HtmlWord> words;
                extractIndexedWords(f->data, words);
                for (const HtmlWord &hw: words) {
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
                    FtiWord &fw = ftiWords[it->second];
                    if (fw.docs.empty() || fw.docs.back().topic != i) {
                        fw.docs.push_back({i, {}});
                    }
                    fw.docs.back().positions.push_back(hw.position);
                }
            }
            // The reader traverses the tree in CompareStr order; the title form comes
            // before the body form when the word is the same.
            std::sort(ftiWords.begin(), ftiWords.end(), [](const FtiWord &a, const FtiWord &b) {
                if (a.text != b.text) return a.text < b.text;
                return a.isTitle && !b.isTitle;
            });
            in.fti = buildFtiMain(ftiWords, stats, opt.lcid);

            // --- #SYSTEM ------------------------------------------------------------
            in.system.reserve(256);
            w32(in.system, 3);
            addSystemU32(in.system, 10, opt.unixTime);
            addSystemString(in.system, 9, "HHA Version 4.74.8702");
            {
                std::vector<uint8_t> info(36, 0);
                put32(info, 0, opt.lcid);
                put32(info, 4, 0); // DBCS / fDBCS
                put32(info, 8, in.fti.empty() ? 0 : 1); // full-text search
                put32(info, 0xC, keywordEntries.empty() ? 0 : 1); // KLinks
                put32(info, 0x10, 0); // ALinks
                put64(info, 0x14, opt.timestamp);
                put32(info, 0x1C, 0);
                put32(info, 0x20, 0);
                addSystemEntry(in.system, 4, info.data(), info.size());
            }
            if (!opt.contentsFile.empty()) addSystemString(in.system, 0, opt.contentsFile);
            if (!opt.indexFile.empty()) addSystemString(in.system, 1, opt.indexFile);
            addSystemString(in.system, 2, stripSlash(opt.defaultTopic));
            addSystemString(in.system, 3, opt.title);
            addSystemString(in.system, 6, lower(opt.compiledName));
            addSystemU32(in.system, 7, 0x62A83769u); // Binary index ID
            addSystemU32(in.system, 12, 0);
            addSystemEntry(in.system, 13, in.idxhdr.data(), in.idxhdr.size());
            addSystemU32(in.system, 15, 0);
            if (!synthWindow) {
                addSystemString(in.system, 5, opt.defaultWindow);
                addSystemString(in.system, 16, opt.font);
            }

            in.strings = st.bytes();
            return in;
        }

        // ---------------------------------------------------------------------------
        // ITSP/PMGL/PMGI Directory
        // ---------------------------------------------------------------------------
        struct DirEntry {
            std::string name;
            uint32_t section = 0;
            uint64_t offset = 0;
            uint64_t length = 0;
        };

        struct Chunk {
            std::array<uint8_t, 4096> data{};
            size_t dataLen = 0; // entry bytes, starting from offset 20
            int count = 0;

            Chunk() { data.fill(0); }
        };

        std::vector<uint8_t> encodeDirEntry(const DirEntry &e) {
            std::vector<uint8_t> v;
            encInt(v, e.name.size());
            raw(v, e.name.data(), e.name.size());
            encInt(v, e.section);
            encInt(v, e.offset);
            encInt(v, e.length);
            return v;
        }

        // Writes a PMGL chunk. 'quickrefs' are offsets of entries 5, 10, 15...
        void finishPmgl(Chunk &c, const int prev, const int next) {
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
                const uint8_t *p = c.data.data() + inChunk;
                uint64_t nameLen = 0;
                int shift = 0;
                while ((*p & 0x80u) != 0) {
                    nameLen |= (uint64_t) (*p & 0x7Fu) << shift;
                    shift += 7;
                    ++p;
                }
                nameLen |= (uint64_t) (*p & 0x7Fu) << shift;
                ++p;

                p += nameLen;
                for (int k = 0; k < 3; ++k) {
                    while ((*p & 0x80u) != 0) ++p;
                    ++p;
                }

                auto entryLen = static_cast<size_t>(p - (c.data.data() + inChunk));
                inChunk += entryLen;
            }
            // The HHC writes quickrefs in reverse order and relative to the start of the
            // entry area (after the 20-byte chunk header).
            int qpos = 4094;
            for (auto it = quickrefs.rbegin(); it != quickrefs.rend(); ++it) {
                qpos -= 2;
                put16(c.data.data(), static_cast<size_t>(qpos), *it);
            }
            // QuickRefSize covers the entire free area after entries, including the
            // quickref table and the final WORD count. inChunk is the offset
            // absolute in the chunk (20 + entry bytes).
            put32(c.data.data(), 4, 4096 - inChunk);
            put16(c.data.data(), 4094, static_cast<uint16_t>(c.count));
        }

        void finishPmgi(Chunk &c) {
            raw(c.data.data(), "PMGI", 4);
            std::vector<uint16_t> quickrefs;
            size_t inChunk = 8;
            for (int i = 0; i < c.count; ++i) {
                if ((i + 1) % 5 == 0) {
                    quickrefs.push_back(static_cast<uint16_t>(inChunk - 8));
                }
                const uint8_t *p = c.data.data() + inChunk;
                uint64_t nameLen = 0;
                int shift = 0;
                while ((*p & 0x80u) != 0) {
                    nameLen |= (uint64_t) (*p & 0x7Fu) << shift;
                    shift += 7;
                    ++p;
                }
                nameLen |= (uint64_t) (*p & 0x7Fu) << shift;
                ++p;
                p += nameLen;
                while ((*p & 0x80u) != 0) ++p;
                ++p;

                const auto entryLen = static_cast<size_t>(p - (c.data.data() + inChunk));
                inChunk += entryLen;
            }
            int qpos = 4094;
            for (auto it = quickrefs.rbegin(); it != quickrefs.rend(); ++it) {
                qpos -= 2;
                put16(c.data.data(), static_cast<size_t>(qpos), *it);
            }
            // inChunk is the absolute offset in the chunk (8 + entry bytes).
            put32(c.data.data(), 4, 4096 - inChunk);
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

        bool buildDirectory(const std::vector<DirEntry> &entries,
                            Directory &dir, std::string &error) {
            std::vector<const DirEntry *> sorted;
            sorted.reserve(entries.size());
            for (const DirEntry &e: entries) sorted.push_back(&e);
            std::sort(sorted.begin(), sorted.end(),
                      [](const DirEntry *a, const DirEntry *b) {
                          const std::string la = lower(a->name);
                          const std::string lb = lower(b->name);
                          if (la != lb) return la < lb;
                          return a->name < b->name;
                      });

            dir.pmgl.clear();
            dir.pmgi.clear();
            Chunk cur;
            for (const DirEntry *e: sorted) {
                const std::vector<uint8_t> enc = encodeDirEntry(*e);
                const int quickAfter = (cur.count + 1) % 5 == 0
                                           ? (cur.count + 1) / 5
                                           : cur.count / 5;
                const size_t limit = 4094u - static_cast<size_t>(2 * quickAfter) - 20u;
                if (cur.dataLen + enc.size() > limit) {
                    finishPmgl(cur,
                               dir.pmgl.empty() ? -1 : static_cast<int>(dir.pmgl.size() - 1),
                               static_cast<int>(dir.pmgl.size() + 1));
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
            dir.lastPmgl = dir.pmgl.size() - 1;
            dir.depth = 1;
            dir.indexRoot = 0xFFFFFFFFu;
            dir.numChunks = dir.pmgl.size();

            if (dir.pmgl.size() > 1) {
                // A single PMGI chunk with the first name of each PMGL chunk.
                Chunk pmgi;
                for (size_t i = 0; i < dir.pmgl.size(); ++i) {
                    const uint8_t *p = pmgi.data.data() + 8 + pmgi.dataLen;
                    (void) p;
                    uint64_t nameLen = 0;
                    int shift = 0;
                    const uint8_t *q = dir.pmgl[i].data.data() + 20;
                    while ((*q & 0x80u) != 0) {
                        nameLen |= (uint64_t) (*q & 0x7Fu) << shift;
                        shift += 7;
                        ++q;
                    }
                    nameLen |= (uint64_t) (*q & 0x7Fu) << shift;
                    ++q;
                    std::string name(reinterpret_cast<const char *>(q), static_cast<size_t>(nameLen));

                    std::vector<uint8_t> enc;
                    encInt(enc, name.size());
                    raw(enc, name.data(), name.size());
                    encInt(enc, i);

                    const int quickAfter = (pmgi.count + 1) % 5 == 0
                                               ? (pmgi.count + 1) / 5
                                               : pmgi.count / 5;
                    if (const size_t limit = 4094u - static_cast<size_t>(2 * quickAfter) - 8u; pmgi.dataLen + enc.size() > limit) {
                        error = "too many PMGL chunks for the implemented PMGI index";
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
                dir.indexRoot = dir.pmgl.size();
                dir.numChunks = dir.pmgl.size() + dir.pmgi.size();
            }
            return true;
        }

        // ---------------------------------------------------------------------------
        // MSCompressed section metadata blocks
        // ---------------------------------------------------------------------------
        std::vector<uint8_t> makeNameList() {
            const char *names[] = {"Uncompressed", "MSCompressed"};

            std::vector<uint8_t> v;
            // The first field is the offset of the terminator of the first string, measured
            // from the start of the file (0x1E for "Uncompressed").
            w16(v, static_cast<uint16_t>(2 + 4 + 2 * std::strlen(names[0])));
            w16(v, 2);
            for (const char *nm: names) {
                const size_t len = std::strlen(nm);
                w16(v, static_cast<uint16_t>(len));
                for (size_t i = 0; i < len; ++i) w16(v, static_cast<uint16_t>(nm[i]));
                w16(v, 0);
            }
            return v;
        }

        std::vector<uint8_t> makeTransformList() {
            const char *s = "{7FC28940-9D31-11D0";
            std::vector<uint8_t> v;
            for (const char *p = s; *p; ++p) w16(v, static_cast<uint16_t>(*p));
            return v;
        }

        std::vector<uint8_t> makeControlData() {
            std::vector<uint8_t> v;
            w32(v, 6); // unknown field (0x06 in HHA CHMs)
            raw(v, "LZXC", 4);
            w32(v, 2); // version
            w32(v, 2); // resetInterval, in 0x8000-byte frames
            w32(v, 2); // windowSize, in 0x8000-byte frames
            w32(v, 1); // windowsPerReset
            w32(v, 0);
            return v;
        }

        std::vector<uint8_t> makeResetTable(const lzx::Result &lz) {
            // The table has one offset per 32 KiB frame. The last offset also
            // points to the end of the stream, as observed in real CHMs.
            std::vector<uint64_t> entries = lz.resetOffsets;
            if (entries.empty()) entries.push_back(0);

            std::vector<uint8_t> v;
            w32(v, 2);
            w32(v, entries.size());
            w32(v, 8);
            w32(v, 0x28);
            w64(v, lz.uncompressedSize);
            w64(v, lz.stream.size());
            w64(v, 0x8000);
            for (const uint64_t off: entries) w64(v, off);
            return v;
        }

        // ---------------------------------------------------------------------------
        // Pending entry: data not yet positioned in a section
        // ---------------------------------------------------------------------------
        struct Pending {
            std::string name;
            uint32_t section = 0;
            std::vector<uint8_t> data;
            uint64_t offset = 0;
            uint64_t length = 0;
        };
    } // namespace

    bool build(const std::string &outPath,
               const std::vector<InputFile> &files,
               const BuildOptions &options,
               std::string &error) {
        error.clear();

        Internal in = buildInternal(options, files);

        // --- Section 1: all files compressed with LZX --------------------
        std::vector<Pending> compressed;
        auto addCompressed = [&](const std::string &name,
                                 const std::vector<uint8_t> &data) {
            compressed.push_back({name, 1, data, 0, 0});
        };
        for (const InputFile &f: files) addCompressed(f.internalName, f.data);
        addCompressed("/#IDXHDR", in.idxhdr);
        if (!in.synthWindow) addCompressed("/#WINDOWS", in.windows);
        addCompressed("/#STRINGS", in.strings);
        addCompressed("/#TOPICS", in.topics);
        addCompressed("/#URLTBL", in.urltbl);
        addCompressed("/#URLSTR", in.urlstr);

        // Internal index files from the HTML Help Workshop.
        addCompressed("/$FIftiMain", in.fti);
        addCompressed("/$OBJINST", in.objinst);
        addCompressed("/$WWAssociativeLinks/Property", in.aLinksProperty);
        addCompressed("/$WWKeywordLinks/BTree", in.kLinksBTree);
        addCompressed("/$WWKeywordLinks/Data", in.kLinksData);
        addCompressed("/$WWKeywordLinks/Map", in.kLinksMap);
        addCompressed("/$WWKeywordLinks/Property", in.kLinksProperty);

        std::vector<uint8_t> sec1;
        for (Pending &p: compressed) {
            p.offset = sec1.size();
            p.length = p.data.size();
            sec1.insert(sec1.end(), p.data.begin(), p.data.end());
        }
        const lzx::Result lz = lzx::compress(sec1);

        // --- Section 0: uncompressed files ---------------------------------
        std::vector<Pending> rawFiles;
        auto addRaw = [&](const std::string &name,
                          const std::vector<uint8_t> &data) {
            rawFiles.push_back({name, 0, data, 0, 0});
        };
        // The HHC places NameList at the beginning of section 0, followed by #SYSTEM.
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
        for (Pending &p: rawFiles) {
            p.offset = sec0.size();
            p.length = p.data.size();
            sec0.insert(sec0.end(), p.data.begin(), p.data.end());
        }

        // Content: pointer to the LZX stream; its data comes after section 0.
        rawFiles.push_back({
            "::DataSpace/Storage/MSCompressed/Content", 0, {},
            static_cast<uint64_t>(sec0.size()), lz.stream.size()
        });

        // Directories (zero-size entries).
        rawFiles.push_back({"/", 0, {}, 0, 0});
        rawFiles.push_back({"/$WWAssociativeLinks/", 0, {}, 0, 0});
        rawFiles.push_back({"/$WWKeywordLinks/", 0, {}, 0, 0});
        rawFiles.push_back({kTransformDir, 0, {}, 0, 0});

        std::vector<DirEntry> entries;
        // HHC registers empty files with offset zero.
        entries.reserve(rawFiles.size());
        for (const Pending &p: rawFiles) {
            entries.push_back({
                p.name, p.section,
                p.length == 0 ? 0 : p.offset, p.length
            });
        }

        for (const Pending &p: compressed) {
            entries.push_back({
                p.name, p.section,
                p.length == 0 ? 0 : p.offset, p.length
            });
        }

        Directory dir;
        if (!buildDirectory(entries, dir, error)) return false;

        const uint64_t dirBytes = static_cast<uint64_t>(dir.pmgl.size() + dir.pmgi.size()) * 4096;
        const uint64_t contentOffset = 0x60 + 0x18 + 0x54 + dirBytes;
        const uint64_t totalLen = contentOffset + sec0.size() + lz.stream.size();

        // --- ITSF + section table -------------------------------------------
        std::vector<uint8_t> out;
        raw(out, "ITSF", 4);
        w32(out, 3);
        w32(out, 0x60);
        w32(out, 1);
        w32(out, static_cast<uint32_t>(options.timestamp));
        w32(out, options.lcid);
        raw(out, kGuidDir, 16);
        raw(out, kGuidData, 16);
        w64(out, 0x60); // OffsetHS0
        w64(out, 0x18); // LengthHS0
        w64(out, 0x78); // OffsetHS1
        w64(out, 0x54 + dirBytes); // LengthHS1 (header + ITSP chunks)
        w64(out, contentOffset); // OffsetCS0

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
        w32(out, 2); // density / block index interval
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
            error = "unexpected CHM header size";
            return false;
        }

        for (const Chunk &c: dir.pmgl) raw(out, c.data.data(), c.data.size());
        for (const Chunk &c: dir.pmgi) raw(out, c.data.data(), c.data.size());
        if (out.size() != contentOffset) {
            error = "inconsistent content offset";
            return false;
        }
        out.insert(out.end(), sec0.begin(), sec0.end());
        out.insert(out.end(), lz.stream.begin(), lz.stream.end());

        if (out.size() != totalLen) {
            error = "inconsistent final size";
            return false;
        }

        std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
        if (!f) {
            error = "could not create: " + outPath;
            return false;
        }
        f.write(reinterpret_cast<const char *>(out.data()), static_cast<std::streamsize>(out.size()));
        if (!f) {
            error = "failed to write: " + outPath;
            return false;
        }
        return true;
    }
} // namespace chm
