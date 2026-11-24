#include "chm.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {
    std::string trim(const std::string &s)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
            ++a;
        }
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
            --b;
        }
        return s.substr(a, b - a);
    }

    std::string lower(const std::string &s)
    {
        std::string r;
        r.reserve(s.size());
        for (const char c: s) {
            r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }

        return r;
    }

    std::string decodeEntities(std::string s)
    {
        const char *from[] = {"&amp;", "&lt;", "&gt;", "&quot;", "&#39;", "&apos;", "&nbsp;"};
        for (size_t i = 0; i < std::size(from); ++i) {
            size_t at;
            while ((at = s.find(from[i])) != std::string::npos) {
                const char *to[] = {"&", "<", ">", "\"", "'", "'", " "};
                s.replace(at, std::strlen(from[i]), to[i]);
            }
        }

        return s;
    }

    std::string percentDecode(const std::string &s)
    {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
                const auto hex = [](const char c) { return static_cast<unsigned>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::tolower(c) - 'a' + 10); };
                out.push_back(static_cast<char>((hex(s[i + 1]) << 4) | hex(s[i + 2])));
                i += 2;
            } else {
                out.push_back(s[i]);
            }
        }
        return out;
    }

    // Internal name used in the CHM directory: always with '/' and without query/fragment.
    std::string internalName(std::string s)
    {
        s = trim(s);
        if (const size_t cut = s.find_first_of("?#"); cut != std::string::npos) {
            s.resize(cut);
        }
        for (char &c: s) {
            if (c == '\\') {
                c = '/';
            }
        }
        s = percentDecode(s);
        while (s.rfind("./", 0) == 0) {
            s.erase(0, 2);
        }
        while (s.size() > 1 && s[0] == '/' && s[1] == '/') {
            s.erase(0, 1);
        }
        if (s.empty()) {
            return "/";
        }
        if (s[0] != '/') {
            s.insert(0, "/");
        }
        return s;
    }

    bool readBytes(const fs::path &p, std::vector<uint8_t> &out)
    {
        std::ifstream f(p, std::ios::binary | std::ios::ate);
        if (!f) {
            return false;
        }
        const auto size = f.tellg();
        f.seekg(0, std::ios::beg);
        out.resize(size);
        f.read(reinterpret_cast<char *>(out.data()), size);
        return static_cast<bool>(f);
    }

    bool readText(const fs::path &p, std::string &out)
    {
        std::vector<uint8_t> b;
        if (!readBytes(p, b)) {
            return false;
        }
        out.assign(reinterpret_cast<const char *>(b.data()), b.size());
        return true;
    }

    uint64_t fileTime()
    {
#ifdef _WIN32
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
#else
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        return static_cast<uint64_t>(ms * 10000) + 116444736000000000ULL;
#endif
    }

    uint32_t parseLcid(const std::string &raw, const uint32_t fallback)
    {
        std::string s = trim(raw);
        if (s.empty()) {
            return fallback;
        }
        if (const size_t sp = s.find_first_of(" \t"); sp != std::string::npos) {
            s = s.substr(0, sp);
        }

        try {
            size_t used = 0;
            unsigned long value;
            if (lower(s).rfind("0x", 0) == 0) {
                value = std::stoul(s, &used, 16);
            } else {
                value = std::stoul(s, &used, 10);
            }
            if (used == 0 || used != s.size()) {
                return fallback;
            }
            if (value < 0x100) {
                value |= 0x0400; // LANGID -> LCID
            }
            return static_cast<uint32_t>(value);
        } catch (...) {
            return fallback;
        }
    }

    // ---------------------------------------------------------------------------
    // HHP
    // ---------------------------------------------------------------------------
    struct Hhp {
        std::map<std::string, std::string> options;
        std::vector<std::string> files;
        std::vector<std::string> windows;
    };

    bool parseHhp(const fs::path &path, Hhp &hhp, std::string &error)
    {
        std::string text;
        if (!readText(path, text)) {
            error = "could not read: " + path.string();
            return false;
        }
        std::istringstream in(text);
        std::string line;
        std::string section;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const std::string t = trim(line);
            if (t.empty() || t[0] == ';') {
                continue;
            }
            if (t.front() == '[' && t.back() == ']') {
                section = lower(trim(t.substr(1, t.size() - 2)));
                continue;
            }
            if (section == "options") {
                const size_t eq = t.find('=');
                if (eq == std::string::npos) {
                    continue;
                }
                hhp.options[lower(trim(t.substr(0, eq)))] = trim(t.substr(eq + 1));
            } else if (section == "files") {
                const size_t eq = t.find('=');
                if (const std::string value = trim(eq == std::string::npos ? t : t.substr(eq + 1)); !value.empty()) {
                    hhp.files.push_back(value);
                }
            } else if (section == "windows") {
                hhp.windows.push_back(t);
            }
        }
        return true;
    }

    std::string option(const Hhp &h, const char *key, const std::string &fallback = {})
    {
        const auto it = h.options.find(lower(key));
        return it == h.options.end() ? fallback : it->second;
    }

    bool parseUint32Field(const std::string &raw, uint32_t &out)
    {
        const std::string s = trim(raw);
        if (s.empty()) {
            return false;
        }
        try {
            size_t used = 0;
            const unsigned long v = lower(s).rfind("0x", 0) == 0 ? std::stoul(s, &used, 16) : std::stoul(s, &used, 10);
            if (used != s.size()) {
                return false;
            }
            out = static_cast<uint32_t>(v);
            return true;
        } catch (...) {
            return false;
        }
    }

    bool parseInt32Field(const std::string &raw, int32_t &out)
    {
        const std::string s = trim(raw);
        if (s.empty()) {
            return false;
        }
        try {
            size_t used = 0;
            const long v = lower(s).rfind("0x", 0) == 0 || lower(s).rfind("-0x", 0) == 0 ? std::stol(s, &used, 0) : std::stol(s, &used, 10);
            if (used != s.size()) {
                return false;
            }
            out = static_cast<int32_t>(v);
            return true;
        } catch (...) {
            return false;
        }
    }

    std::vector<std::string> splitWindowFields(const std::string &line)
    {
        std::vector<std::string> fields;
        std::string cur;
        bool inQuote = false;
        char quote = 0;
        int bracketDepth = 0;
        for (const char c: line) {
            if (inQuote) {
                cur.push_back(c);
                if (c == quote) {
                    inQuote = false;
                }
                continue;
            }
            if (c == '"' || c == '\'') {
                inQuote = true;
                quote = c;
                cur.push_back(c);
                continue;
            }
            if (c == '[') {
                ++bracketDepth;
                cur.push_back(c);
                continue;
            }
            if (c == ']') {
                if (bracketDepth > 0) {
                    --bracketDepth;
                }
                cur.push_back(c);
                continue;
            }
            if (c == ',' && bracketDepth == 0) {
                fields.push_back(trim(cur));
                cur.clear();
                continue;
            }
            cur.push_back(c);
        }
        fields.push_back(trim(cur));
        return fields;
    }

    std::string unquoteField(const std::string &raw)
    {
        std::string s = trim(raw);
        if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
            return s.substr(1, s.size() - 2);
        }

        return s;
    }

    bool parseRectField(const std::string &raw, int32_t rect[4])
    {
        std::string s = trim(raw);
        if (s.empty()) {
            return false;
        }
        if (s.front() == '[' && s.back() == ']') {
            s = s.substr(1, s.size() - 2);
        }
        std::vector<int32_t> vals;
        size_t p = 0;
        while (p < s.size()) {
            while (p < s.size() && (std::isspace(static_cast<unsigned char>(s[p])) || s[p] == ',')) {
                ++p;
            }
            if (p >= s.size()) {
                break;
            }
            size_t e = p;
            if (s[e] == '-' || s[e] == '+') {
                ++e;
            }
            if (e + 1 < s.size() && s[e] == '0' && (s[e + 1] == 'x' || s[e + 1] == 'X')) {
                e += 2;
                while (e < s.size() && std::isxdigit(static_cast<unsigned char>(s[e]))) {
                    ++e;
                }
            } else {
                while (e < s.size() && std::isdigit(static_cast<unsigned char>(s[e]))) {
                    ++e;
                }
            }
            int32_t v = 0;
            if (!parseInt32Field(s.substr(p, e - p), v)) {
                return false;
            }
            vals.push_back(v);
            p = e;
        }
        if (vals.size() != 4) {
            return false;
        }
        for (int i = 0; i < 4; ++i) {
            rect[i] = vals[i];
        }
        return true;
    }

    bool parseWindowLine(const std::string &line, chm::WindowDef &w, std::string &error)
    {
        std::string s = trim(line);
        const size_t eq = s.find('=');
        if (eq == std::string::npos) {
            error = "invalid [WINDOWS] line: " + line;
            return false;
        }
        w.name = trim(s.substr(0, eq));
        const std::vector<std::string> f = splitWindowFields(s.substr(eq + 1));
        auto strField = [&](const size_t i) -> std::string { return i < f.size() ? unquoteField(f[i]) : std::string(); };
        auto u32Field = [&](const size_t i, uint32_t &dst) {
            if (i < f.size()) {
                parseUint32Field(f[i], dst);
            }
        };
        w.caption = strField(0);
        w.toc = strField(1);
        w.index = strField(2);
        w.defaultFile = strField(3);
        w.homeFile = strField(4);
        w.jump1 = strField(5);
        w.jump2 = strField(6);
        w.urlJump1 = strField(7);
        w.urlJump2 = strField(8);
        u32Field(9, w.navStyle);
        u32Field(10, w.navWidth);
        u32Field(11, w.buttons);
        if (f.size() > 12 && !trim(f[12]).empty()) {
            parseRectField(f[12], w.rect);
        }
        u32Field(13, w.styles);
        u32Field(14, w.exStyles);
        u32Field(15, w.showState);
        u32Field(16, w.navPaneClosed);
        u32Field(17, w.defaultPane);
        u32Field(18, w.tabPos);
        u32Field(19, w.idNotify);
        return !w.name.empty();
    }

    // ---------------------------------------------------------------------------
    // HHC / HHK
    // ---------------------------------------------------------------------------
    bool findAttr(const std::string &line, const std::string &name, std::string &value)
    {
        size_t pos = 0;
        const std::string needle = lower(name);
        const std::string lowLine = lower(line);
        while ((pos = lowLine.find(needle, pos)) != std::string::npos) {
            size_t p = pos + needle.size();
            while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) {
                ++p;
            }
            if (p >= line.size() || line[p] != '=') {
                pos += needle.size();
                continue;
            }
            ++p;
            while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) {
                ++p;
            }
            if (p >= line.size() || (line[p] != '"' && line[p] != '\'')) {
                pos += needle.size();
                continue;
            }
            const char quote = line[p++];
            const size_t end = line.find(quote, p);
            if (end == std::string::npos) {
                pos += needle.size();
                continue;
            }
            value = decodeEntities(line.substr(p, end - p));
            return true;
        }
        return false;
    }

    size_t findCi(const std::string &hay, const char *needle, const size_t pos = 0)
    {
        const size_t nlen = std::strlen(needle);
        if (nlen == 0) {
            return pos;
        }
        for (size_t i = pos; i + nlen <= hay.size(); ++i) {
            bool ok = true;
            for (size_t j = 0; j < nlen; ++j) {
                if (std::tolower(static_cast<unsigned char>(hay[i + j])) != std::tolower(static_cast<unsigned char>(needle[j]))) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                return i;
            }
        }
        return std::string::npos;
    }

    bool ieq(const std::string &a, const char *b)
    {
        size_t i = 0;
        while (i < a.size() && std::isspace(static_cast<unsigned char>(a[i]))) {
            ++i;
        }
        size_t j = a.size();
        while (j > i && std::isspace(static_cast<unsigned char>(a[j - 1]))) {
            --j;
        }
        const size_t bl = std::strlen(b);
        if (j - i != bl) {
            return false;
        }
        for (size_t k = 0; k < bl; ++k) {
            if (std::tolower(static_cast<unsigned char>(a[i + k])) != std::tolower(static_cast<unsigned char>(b[k]))) {
                return false;
            }
        }
        return true;
    }

    bool paramValue(const std::string &line, const std::string &paramName, std::string &value)
    {
        const char *wanted = paramName.c_str();
        size_t pos = 0;
        while ((pos = findCi(line, "name", pos)) != std::string::npos) {
            size_t p = pos + 4;
            while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) {
                ++p;
            }
            if (p >= line.size() || line[p] != '=') {
                ++pos;
                continue;
            }
            ++p;
            while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) {
                ++p;
            }
            if (p >= line.size() || (line[p] != '"' && line[p] != '\'')) {
                ++pos;
                continue;
            }
            const char quote = line[p++];
            const size_t end = line.find(quote, p);
            if (end == std::string::npos) {
                ++pos;
                continue;
            }
            if (const std::string name = decodeEntities(line.substr(p, end - p)); ieq(name, wanted)) {
                return findAttr(line.substr(end + 1), "value", value);
            }

            pos = end + 1;
        }
        return false;
    }

    void markChildren(std::vector<chm::TocEntry> &toc)
    {
        for (size_t i = 0; i < toc.size(); ++i) {
            for (size_t j = i + 1; j < toc.size(); ++j) {
                if (toc[j].depth > toc[i].depth) {
                    toc[i].hasChildren = true;
                    break;
                }
            }
        }
    }

    bool parseHhc(const fs::path &path, std::vector<chm::TocEntry> &toc, std::string &error)
    {
        std::string text;
        if (!readText(path, text)) {
            error = "could not read: " + path.string();
            return false;
        }
        std::istringstream in(text);
        std::string line;
        int ulDepth = 0;
        chm::TocEntry current;
        bool inLi = false;
        auto flush = [&] {
            if (!inLi) {
                return;
            }
            current.name = trim(current.name);
            if (!current.local.empty() || !current.name.empty()) {
                toc.push_back(current);
            }
            current = chm::TocEntry();
            inLi = false;
        };

        while (std::getline(in, line)) {
            const std::string low = lower(line);
            for (size_t pos = 0; (pos = low.find('<', pos)) != std::string::npos;) {
                const size_t end = low.find('>', pos);
                if (end == std::string::npos) {
                    break;
                }
                if (const std::string tag = trim(low.substr(pos + 1, end - pos - 1)); tag.rfind("/ul", 0) == 0) {
                    ulDepth = std::max(0, ulDepth - 1);
                } else if (tag.rfind("ul", 0) == 0) {
                    ++ulDepth;
                } else if (tag.rfind("/li", 0) == 0) {
                    flush();
                } else if (tag.rfind("li", 0) == 0) {
                    flush();
                    current.depth = std::max(0, ulDepth - 1);
                    inLi = true;
                }
                pos = end + 1;
            }
            if (!inLi) {
                continue;
            }
            std::string v;
            if (paramValue(line, "Name", v)) {
                current.name = v;
            }
            if (paramValue(line, "Local", v)) {
                current.local = internalName(v);
            }
            if (paramValue(line, "Target", v)) {
                current.target = v;
            }
            if (paramValue(line, "Merge", v)) {
                current.merge = v;
            }
            if (paramValue(line, "ID", v)) {
                current.id = v;
            }
        }
        flush();
        markChildren(toc);
        return true;
    }

    bool parseHhk(const fs::path &path, std::vector<chm::KeywordEntry> &keywords, std::string &error)
    {
        std::string text;
        if (!readText(path, text)) {
            error = "could not read: " + path.string();
            return false;
        }
        std::istringstream in(text);
        std::string line;
        int ulDepth = 0;
        chm::KeywordEntry current;
        bool inLi = false;
        auto flush = [&] {
            if (!inLi) {
                return;
            }
            current.name = trim(current.name);
            if (!current.local.empty() || !current.name.empty()) {
                keywords.push_back(current);
            }
            current = chm::KeywordEntry();
            inLi = false;
        };

        while (std::getline(in, line)) {
            const std::string low = lower(line);
            for (size_t pos = 0; (pos = low.find('<', pos)) != std::string::npos;) {
                const size_t end = low.find('>', pos);
                if (end == std::string::npos) {
                    break;
                }
                if (const std::string tag = trim(low.substr(pos + 1, end - pos - 1)); tag.rfind("/ul", 0) == 0) {
                    ulDepth = std::max(0, ulDepth - 1);
                } else if (tag.rfind("ul", 0) == 0) {
                    ++ulDepth;
                } else if (tag.rfind("/li", 0) == 0) {
                    flush();
                } else if (tag.rfind("li", 0) == 0) {
                    flush();
                    current.depth = std::max(0, ulDepth - 1);
                    inLi = true;
                }
                pos = end + 1;
            }
            if (!inLi) {
                continue;
            }
            std::string v;
            if (paramValue(line, "Name", v)) {
                current.name = v;
            }
            if (paramValue(line, "Local", v)) {
                current.local = internalName(v);
            }
            if (paramValue(line, "Target", v)) {
                current.target = v;
            }
            if (paramValue(line, "ID", v)) {
                current.id = v;
            }
        }
        flush();
        return true;
    }

    // ---------------------------------------------------------------------------
    // ActiveX and Navigation Scripts
    // ---------------------------------------------------------------------------
    bool parseActiveXCommands(const std::vector<chm::InputFile> &files, std::vector<chm::ActiveXCommand> &commands, std::vector<chm::NavigationScript> &scripts)
    {
        // HTML Help ActiveX control classid
        const std::string hhClassid = "adb880a6-d8ff-11cf-9377-00aa003b7a11";

        for (const auto &[internalName, data] : files) {
            const std::string ext = lower(fs::path(internalName).extension().string());
            if (ext != ".htm" && ext != ".html") {
                continue;
            }

            const std::string s(reinterpret_cast<const char *>(data.data()), data.size());
            const std::string sl = lower(s);

            // --- ActiveX Commands (HHCtrl.ocx) ---
            size_t pos = 0;
            while ((pos = sl.find("classid=", pos)) != std::string::npos) {
                size_t p = pos + 8;
                while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) { ++p; }

                std::string classid;
                if (p < s.size() && (s[p] == '"' || s[p] == '\'')) {
                    const char quote = s[p++];
                    if (const size_t end = s.find(quote, p); end != std::string::npos) {
                        classid = lower(s.substr(p, end - p));
                    }
                }

                // Check if this is the HTML Help ActiveX control
                if (classid.find(hhClassid) == std::string::npos) {
                    pos += 8;
                    continue;
                }

                // Find the closing </object> tag
                size_t objStart = 0;
                size_t searchBack = pos;
                while (searchBack > 0) {
                    --searchBack;
                    if (s[searchBack] == '<') {
                        size_t tagEnd = s.find('>', searchBack);
                        if (tagEnd != std::string::npos) {
                            std::string tag = lower(s.substr(searchBack, tagEnd - searchBack + 1));
                            if (tag.find("object") != std::string::npos) {
                                objStart = searchBack;
                                break;
                            }
                        }
                    }
                }

                size_t objEnd = sl.find("</object>", pos);
                if (objEnd == std::string::npos) {
                    pos += 8;
                    continue;
                }

                std::string objContent = s.substr(objStart, objEnd - objStart + 9);
                std::string objContentLower = lower(objContent);

                chm::ActiveXCommand cmd;

                // Extract OBJECT id
                {
                    size_t idPos = objContentLower.find("id=", objStart);
                    if (idPos != std::string::npos && idPos < objEnd) {
                        size_t ip = idPos + 3;
                        while (ip < objContent.size() && std::isspace(static_cast<unsigned char>(objContent[ip]))) { ++ip; }
                        if (ip < objContent.size() && (objContent[ip] == '"' || objContent[ip] == '\'')) {
                            const char quote = objContent[ip++];
                            if (const size_t end = objContent.find(quote, ip); end != std::string::npos) {
                                cmd.id = objContent.substr(ip, end - ip);
                            }
                        }
                    }
                }

                // Extract PARAM name/value pairs
                size_t paramPos = 0;
                while ((paramPos = objContentLower.find("<param", paramPos)) != std::string::npos && paramPos < objEnd) {
                    size_t paramEnd = objContent.find('>', paramPos);
                    if (paramEnd == std::string::npos) break;

                    std::string paramTag = objContent.substr(paramPos, paramEnd - paramPos + 1);
                    std::string paramTagLower = lower(paramTag);

                    std::string paramName, paramValue;
                    if (findAttr(paramTag, "name", paramName)) {
                        paramName = lower(paramName);
                        findAttr(paramTag.substr(paramTag.find("value", paramName.size())), "value", paramValue);
                    } else {
                        paramPos += 6;
                        continue;
                    }

                    if (paramName == "command") {
                        cmd.command = paramValue;
                    } else if (paramName == "item1") {
                        cmd.item1 = paramValue;
                    } else if (paramName == "item2") {
                        cmd.item2 = paramValue;
                    } else if (paramName == "item3") {
                        cmd.item3 = paramValue;
                    } else if (paramName == "item4") {
                        cmd.item4 = paramValue;
                    } else if (paramName == "item5") {
                        cmd.item5 = paramValue;
                    } else if (paramName == "button") {
                        cmd.button = paramValue;
                    } else if (paramName == "flags") {
                        cmd.flags = paramValue;
                    } else if (paramName == "window") {
                        cmd.window = paramValue;
                    } else if (paramName == "text") {
                        cmd.text = paramValue;
                    }

                    paramPos += 6;
                }

                if (!cmd.command.empty()) {
                    cmd.sourceFile = internalName;
                    commands.push_back(std::move(cmd));
                }

                pos = objEnd + 9;
            }

            // --- Navigation Scripts (window.helpObj API) ---
            pos = 0;
            while ((pos = sl.find("helpobj", pos)) != std::string::npos) {
                // Find the function call context
                size_t funcStart = pos;
                while (funcStart > 0 && (std::isalpha(static_cast<unsigned char>(s[funcStart - 1])) || s[funcStart - 1] == '_')) {
                    --funcStart;
                }

                // Look for the dot and function name after helpObj
                size_t dotPos = sl.find('.', pos + 7);
                if (dotPos == std::string::npos || dotPos > pos + 20) {
                    pos += 7;
                    continue;
                }

                size_t funcNameStart = dotPos + 1;
                while (funcNameStart < s.size() && std::isspace(static_cast<unsigned char>(s[funcNameStart]))) { ++funcNameStart; }

                std::string funcName;
                while (funcNameStart < s.size() && (std::isalpha(static_cast<unsigned char>(s[funcNameStart])) || s[funcNameStart] == '_')) {
                    funcName += s[funcNameStart++];
                }

                if (funcName.empty()) {
                    pos += 7;
                    continue;
                }

                // Check if this is a known helpObj function
                const char *knownFuncs[] = {"navigate", "addhistory", "removehistory", "popup", "setinfotype",
                                             "getinfotype", "gethistorycount", "gethistoryitem",
                                             "setlocation", "getlocation", "addfavorite", "removefavorite",
                                             "getfavoritecount", "getfavoriteitem"};
                bool isKnown = false;
                for (const char *kf : knownFuncs) {
                    if (lower(funcName) == kf) {
                        isKnown = true;
                        break;
                    }
                }

                if (!isKnown) {
                    pos += 7;
                    continue;
                }

                // Extract arguments (find the opening paren and matching closing paren)
                size_t parenStart = s.find('(', funcNameStart);
                if (parenStart != std::string::npos) {
                    int depth = 1;
                    size_t parenEnd = parenStart + 1;
                    while (parenEnd < s.size() && depth > 0) {
                        if (s[parenEnd] == '(') ++depth;
                        else if (s[parenEnd] == ')') --depth;
                        ++parenEnd;
                    }
                    if (depth == 0) {
                        std::string args = s.substr(parenStart + 1, parenEnd - parenStart - 2);
                        // Trim whitespace
                        size_t aStart = 0, aEnd = args.size();
                        while (aStart < aEnd && std::isspace(static_cast<unsigned char>(args[aStart]))) ++aStart;
                        while (aEnd > aStart && std::isspace(static_cast<unsigned char>(args[aEnd - 1]))) --aEnd;
                        args = args.substr(aStart, aEnd - aStart);

                        chm::NavigationScript script;
                        script.function = funcName;
                        script.arguments = args;
                        script.sourceFile = internalName;
                        scripts.push_back(std::move(script));
                    }
                }

                pos += 7;
            }
        }

        return true;
    }

    // ---------------------------------------------------------------------------
    // Files and references
    // ---------------------------------------------------------------------------
    bool isAsset(const std::string &name)
    {
        const std::string ext = lower(fs::path(name).extension().string());
        return ext == ".gif" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".css" || ext == ".js" || ext == ".svg";
    }

    std::vector<std::string> assetReferences(const std::vector<uint8_t> &data)
    {
        const std::string s(reinterpret_cast<const char *>(data.data()), data.size());
        const std::string sl = lower(s);
        const char *keys[] = {"src=", "href=", "value=", "url("};
        std::vector<std::string> result;
        for (const char *key: keys) {
            const size_t klen = std::strlen(key);
            size_t pos = 0;
            while ((pos = sl.find(key, pos)) != std::string::npos) {
                size_t p = pos + klen;
                while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) {
                    ++p;
                }
                if (p < s.size() && (s[p] == '"' || s[p] == '\'')) {
                    const char quote = s[p++];
                    if (const size_t end = s.find(quote, p); end != std::string::npos) {
                        result.push_back(s.substr(p, end - p));
                    }
                } else {
                    size_t end = p;
                    while (end < s.size() && !std::isspace(static_cast<unsigned char>(s[end])) && s[end] != '>' && s[end] != ')') {
                        ++end;
                    }
                    if (end > p) {
                        result.push_back(s.substr(p, end - p));
                    }
                }
                pos += klen;
            }
        }
        return result;
    }

    void printUsage()
    {
        std::cerr << "Usage: src <project.hhp> [output_file.chm]\n"
                  << "\n"
                  << "Reads the .hhp, .hhc, and .hhk files specified in the project and generates\n"
                  << "an ITSF v3 CHM with the content section compressed using LZX. If the output\n"
                  << "is not specified, it uses the 'Compiled file' field from the .hhp.\n";
    }
} // namespace

int main(int argc, char **argv)
{
    std::cout << "CHM Compiler\n\n";

    if (argc < 2) {
        printUsage();
        return 1;
    }

    const fs::path hhpPath = fs::absolute(fs::path(argv[1]));
    const fs::path base = hhpPath.parent_path();
    std::string error;
    Hhp hhp;
    if (!parseHhp(hhpPath, hhp, error)) {
        std::cerr << "Error: " << error << "\n";
        return 1;
    }

    const std::string contentsRel = option(hhp, "Contents file");
    const std::string indexRel = option(hhp, "Index file");
    const std::string defaultRel = option(hhp, "Default topic", "index.htm");
    const std::string compiledRel = option(hhp, "Compiled file", "output.chm");

    chm::BuildOptions opt;
    opt.title = option(hhp, "Title", "Untitled");
    opt.defaultTopic = internalName(defaultRel);
    opt.contentsFile = fs::path(contentsRel).filename().string();
    opt.indexFile = fs::path(indexRel).filename().string();
    opt.defaultWindow = option(hhp, "Default Window", "main");
    opt.font = option(hhp, "Default Font", "Arial,9,0");
    opt.lcid = parseLcid(option(hhp, "Language"), 0x0416);
    opt.timestamp = fileTime();
    opt.unixTime = static_cast<uint32_t>(std::time(nullptr));
    opt.compiledName = fs::path(compiledRel).stem().string();

    auto startTime = std::chrono::steady_clock::now();

    fs::path outPath = argc > 2 ? fs::path(argv[2]) : base / fs::path(compiledRel);
    if (!outPath.is_absolute()) {
        outPath = fs::absolute(outPath);
    }
    outPath = fs::weakly_canonical(outPath);

    std::cout << "Compiling: " << outPath.string() << "\n";

    for (const std::string &wline: hhp.windows) {
        chm::WindowDef w;
        if (std::string werr; !parseWindowLine(wline, w, werr)) {
            std::cerr << "Warning: " << werr << "\n";
            continue;
        }

        opt.windows.push_back(std::move(w));
    }
    std::vector<chm::InputFile> files;
    std::set<std::string> names;
    auto addFile = [&](const std::string &rel, const bool required) -> bool {
        if (rel.empty()) {
            return true;
        }
        const std::string name = internalName(rel);
        if (name == "/" || names.count(name)) {
            return true;
        }
        auto abs = fs::path(rel);
        if (!abs.is_absolute()) {
            abs = base / abs;
        }
        if (std::error_code ec; !fs::is_regular_file(abs, ec)) {
            if (required) {
                std::cerr << "Warning: file not found: " << abs << "\n";
            }
            return false;
        }
        std::vector<uint8_t> data;
        if (!readBytes(abs, data)) {
            if (required) {
                std::cerr << "Warning: failed to read: " << abs << "\n";
            }
            return false;
        }
        names.insert(name);
        files.push_back({name, std::move(data)});
        return true;
    };

    // Order: [FILES] from HHP, table of contents, index, starting topic, and hhc/hhk.
    for (const std::string &f: hhp.files) {
        addFile(f, true);
    }
    if (!contentsRel.empty()) {
        addFile(contentsRel, true);
    }
    if (!indexRel.empty()) {
        addFile(indexRel, true);
    }
    addFile(defaultRel, true);

    if (!parseHhc(base / fs::path(contentsRel), opt.toc, error)) {
        std::cerr << "Error: " << error << "\n";
        return 1;
    }
    if (!parseHhk(base / fs::path(indexRel), opt.keywords, error)) {
        std::cerr << "Error: " << error << "\n";
        return 1;
    }
    for (const chm::TocEntry &e: opt.toc) {
        addFile(e.local, true);
    }

    // Images/CSS referenced by HTML and by hhc/hhk itself.
    std::vector<std::string> pendingAssets;
    for (const auto &[internalName, data]: files) {
        for (const std::string &ref: assetReferences(data)) {
            if (isAsset(ref)) {
                pendingAssets.push_back(ref);
            }
        }
    }
    for (const std::string &ref: pendingAssets) {
        addFile(ref, false);
    }

    // Ensure that files referenced by [WINDOWS] are considered.
    for (const chm::WindowDef &w: opt.windows) {
        if (!w.toc.empty()) {
            addFile(w.toc, false);
        }
        if (!w.index.empty()) {
            addFile(w.index, false);
        }
        if (!w.defaultFile.empty()) {
            addFile(w.defaultFile, false);
        }
        if (!w.homeFile.empty()) {
            addFile(w.homeFile, false);
        }
    }

    // Parse ActiveX commands and navigation scripts from HTML files.
    parseActiveXCommands(files, opt.activeXCommands, opt.navigationScripts);

    if (!chm::build(outPath.string(), files, opt, error)) {
        std::cerr << "Error generating CHM: " << error << "\n";
        return 1;
    }

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();
    int minutes = static_cast<int>(duration / 60);
    int seconds = static_cast<int>(duration % 60);

    // Count topics (HTML/HTM files)
    size_t nTopics = 0;
    for (const auto &[internalName, data]: files) {
        if (const std::string ext = lower(fs::path(internalName).extension().string()); ext == ".htm" || ext == ".html") {
            ++nTopics;
        }
    }

    // Count graphics (image files)
    size_t nGraphics = 0;
    for (const auto &[internalName, data]: files) {
        if (const std::string ext = lower(fs::path(internalName).extension().string()); ext == ".gif" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".svg") {
            ++nGraphics;
        }
    }

    // Count local and internet links from all HTML files
    size_t nLocalLinks = 0;
    size_t nInternetLinks = 0;
    std::set<std::string> seenLinks;
    for (const auto &[internalName, data]: files) {
        if (const std::string ext = lower(fs::path(internalName).extension().string()); ext == ".htm" || ext == ".html") {
            const std::string s(reinterpret_cast<const char *>(data.data()), data.size());
            const std::string sl = lower(s);
            size_t pos = 0;
            while ((pos = sl.find("href=", pos)) != std::string::npos) {
                size_t p = pos + 5;
                while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) {
                    ++p;
                }

                std::string href;
                if (p < s.size() && (s[p] == '"' || s[p] == '\'')) {
                    const char quote = s[p++];
                    if (const size_t end = s.find(quote, p); end != std::string::npos) {
                        href = s.substr(p, end - p);
                    }
                } else {
                    size_t end = p;
                    while (end < s.size() && !std::isspace(static_cast<unsigned char>(s[end])) && s[end] != '>') {
                        ++end;
                    }

                    if (end > p) {
                        href = s.substr(p, end - p);
                    }
                }
                pos += 5;
                if (href.empty()) {
                    continue;
                }

                // Skip anchor links (#id)
                if (href[0] == '#') {
                    continue;
                }

                // Use lowercase for dedup
                const std::string hrefLower = lower(href);
                if (seenLinks.count(hrefLower)) {
                    continue;
                }
                seenLinks.insert(hrefLower);

                if (hrefLower.rfind("http://", 0) == 0 || hrefLower.rfind("https://", 0) == 0) {
                    ++nInternetLinks;
                } else {
                    ++nLocalLinks;
                }
            }
        }
    }

    std::cout << "\n"
              << "Compile time: " << minutes << " minutes, " << seconds << " seconds\n"
              << nTopics << "\tTopics\n"
              << nLocalLinks << "\tLocal links\n"
              << nInternetLinks << "\tInternet links\n"
              << nGraphics << "\tGraphics\n"
              << files.size() << "\tFiles\n"
              << opt.toc.size() << "\tTable of contents\n"
              << opt.keywords.size() << "\tKeywords\n"
              << opt.windows.size() << "\tWindows\n"
              << opt.activeXCommands.size() << "\tActiveX commands\n"
              << opt.navigationScripts.size() << "\tNavigation scripts\n";

    return 0;
}
