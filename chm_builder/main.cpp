#include "chm.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string decodeEntities(std::string s) {
    const char* from[] = {"&amp;", "&lt;", "&gt;", "&quot;", "&#39;", "&apos;", "&nbsp;"};
    const char* to[]   = {"&",     "<",    ">",    "\"",     "'",      "'",       " "};
    for (size_t i = 0; i < sizeof(from) / sizeof(from[0]); ++i) {
        size_t at;
        while ((at = s.find(from[i])) != std::string::npos) {
            s.replace(at, std::strlen(from[i]), to[i]);
        }
    }
    return s;
}

std::string percentDecode(std::string s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            const auto hex = [](char c) {
                return static_cast<unsigned>(std::isdigit(static_cast<unsigned char>(c))
                                                 ? c - '0' : (std::tolower(c) - 'a' + 10));
            };
            out.push_back(static_cast<char>((hex(s[i + 1]) << 4) | hex(s[i + 2])));
            i += 2;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// Nome interno usado no diretorio do CHM: sempre com '/' e sem query/fragmento.
std::string internalName(std::string s) {
    s = trim(s);
    const size_t cut = s.find_first_of("?#");
    if (cut != std::string::npos) s.resize(cut);
    for (char& c : s) if (c == '\\') c = '/';
    s = percentDecode(s);
    while (s.rfind("./", 0) == 0) s.erase(0, 2);
    while (s.size() > 1 && s[0] == '/' && s[1] == '/') s.erase(0, 1);
    if (s.empty()) return "/";
    if (s[0] != '/') s.insert(0, "/");
    return s;
}

bool readBytes(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool readText(const fs::path& p, std::string& out) {
    std::vector<uint8_t> b;
    if (!readBytes(p, b)) return false;
    out.assign(reinterpret_cast<const char*>(b.data()), b.size());
    return true;
}

uint64_t fileTime() {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
#else
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
    return static_cast<uint64_t>(ms * 10000) + 116444736000000000ULL;
#endif
}

uint32_t parseLcid(const std::string& raw, uint32_t fallback) {
    const std::string s = trim(raw);
    if (s.empty()) return fallback;
    try {
        size_t used = 0;
        unsigned long value;
        if (lower(s).rfind("0x", 0) == 0) value = std::stoul(s, &used, 16);
        else value = std::stoul(s, &used, 10);
        if (used == 0 || used != s.size()) return fallback;
        if (value < 0x100) value |= 0x0400; // LANGID -> LCID
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
};

bool parseHhp(const fs::path& path, Hhp& hhp, std::string& error) {
    std::string text;
    if (!readText(path, text)) {
        error = "nao foi possivel ler: " + path.string();
        return false;
    }
    std::istringstream in(text);
    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.empty() || t[0] == ';') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = lower(trim(t.substr(1, t.size() - 2)));
            continue;
        }
        if (section == "options") {
            const size_t eq = t.find('=');
            if (eq == std::string::npos) continue;
            hhp.options[lower(trim(t.substr(0, eq)))] = trim(t.substr(eq + 1));
        } else if (section == "files") {
            const size_t eq = t.find('=');
            const std::string value = trim(eq == std::string::npos ? t : t.substr(eq + 1));
            if (!value.empty()) hhp.files.push_back(value);
        }
    }
    return true;
}

std::string option(const Hhp& h, const char* key, const std::string& fallback = {}) {
    const auto it = h.options.find(lower(key));
    return it == h.options.end() ? fallback : it->second;
}

// ---------------------------------------------------------------------------
// HHC / HHK
// ---------------------------------------------------------------------------
bool findAttr(const std::string& line, const std::string& name, std::string& value) {
    size_t pos = 0;
    const std::string needle = lower(name);
    while ((pos = lower(line).find(needle, pos)) != std::string::npos) {
        size_t p = pos + needle.size();
        while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) ++p;
        if (p >= line.size() || line[p] != '=') { ++pos; continue; }
        ++p;
        while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) ++p;
        if (p >= line.size() || (line[p] != '"' && line[p] != '\'')) { ++pos; continue; }
        const char quote = line[p++];
        const size_t end = line.find(quote, p);
        if (end == std::string::npos) { ++pos; continue; }
        value = decodeEntities(line.substr(p, end - p));
        return true;
    }
    return false;
}

bool paramValue(const std::string& line, const std::string& paramName, std::string& value) {
    // Uma linha do .hhc pode conter varios <param>: é preciso localizar o
    // parametro pedido em vez de usar sempre o primeiro "name=".
    const std::string wanted = lower(trim(paramName));
    size_t pos = 0;
    const std::string key = "name";
    while ((pos = lower(line).find(key, pos)) != std::string::npos) {
        size_t p = pos + key.size();
        while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) ++p;
        if (p < line.size() && line[p] == '=') {
            ++p;
            while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) ++p;
            if (p < line.size() && (line[p] == '"' || line[p] == '\'')) {
                const char quote = line[p++];
                const size_t end = line.find(quote, p);
                if (end != std::string::npos) {
                    const std::string name = decodeEntities(line.substr(p, end - p));
                    if (lower(trim(name)) == wanted) {
                        const size_t rest = end + 1;
                        return findAttr(line.substr(rest), "value", value);
                    }
                    pos = end + 1;
                    continue;
                }
            }
        }
        ++pos;
    }
    return false;
}

void markChildren(std::vector<chm::TocEntry>& toc) {
    for (size_t i = 0; i < toc.size(); ++i) {
        for (size_t j = i + 1; j < toc.size(); ++j) {
            if (toc[j].depth > toc[i].depth) {
                toc[i].hasChildren = true;
                break;
            }
        }
    }
}

bool parseHhc(const fs::path& path, std::vector<chm::TocEntry>& toc, std::string& error) {
    std::string text;
    if (!readText(path, text)) {
        error = "nao foi possivel ler: " + path.string();
        return false;
    }
    std::istringstream in(text);
    std::string line;
    int ulDepth = 0;
    chm::TocEntry current;
    bool inLi = false;
    auto flush = [&]() {
        if (!inLi) return;
        current.name = trim(current.name);
        if (!current.local.empty() || !current.name.empty()) toc.push_back(current);
        current = chm::TocEntry();
        inLi = false;
    };

    while (std::getline(in, line)) {
        const std::string low = lower(line);
        for (size_t pos = 0; (pos = low.find('<', pos)) != std::string::npos;) {
            const size_t end = low.find('>', pos);
            if (end == std::string::npos) break;
            const std::string tag = trim(low.substr(pos + 1, end - pos - 1));
            if (tag.rfind("/ul", 0) == 0) {
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
        if (!inLi) continue;
        std::string v;
        if (paramValue(line, "Name", v)) current.name = v;
        if (paramValue(line, "Local", v)) current.local = internalName(v);
    }
    flush();
    markChildren(toc);
    return true;
}

bool parseHhk(const fs::path& path, std::vector<chm::KeywordEntry>& keywords,
              std::string& error) {
    std::string text;
    if (!readText(path, text)) {
        error = "nao foi possivel ler: " + path.string();
        return false;
    }

    std::vector<chm::KeywordEntry> stack;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t open = lower(text.substr(pos)).find("<li");
        const size_t close = lower(text.substr(pos)).find("</li");
        const bool hasOpen = open != std::string::npos;
        const bool hasClose = close != std::string::npos;

        if (hasClose && (!hasOpen || close < open)) {
            if (!stack.empty()) {
                chm::KeywordEntry e = stack.back();
                stack.pop_back();
                if (!e.name.empty()) {
                    e.depth = static_cast<int>(stack.size());
                    keywords.push_back(std::move(e));
                }
            }
            pos += close + 4;
            continue;
        }
        if (!hasOpen) break;

        const size_t at = pos + open;
        size_t end = lower(text.substr(at)).find('>');
        if (end == std::string::npos) break;
        end += at + 1;
        chm::KeywordEntry item;
        // Os <param> do OBJECT costumam vir depois do <li>, na mesma linha.
        size_t paramEnd = lower(text.substr(end)).find("</li");
        if (paramEnd == std::string::npos) paramEnd = text.size();
        else paramEnd += end;
        const std::string block = text.substr(end, paramEnd - end);
        std::string v;
        if (paramValue(block, "Name", v)) item.name = trim(v);
        if (paramValue(block, "Local", v)) item.local = v;
        stack.push_back(std::move(item));
        pos = end;
    }
    while (!stack.empty()) {
        chm::KeywordEntry e = stack.back();
        stack.pop_back();
        if (!e.name.empty()) {
            e.depth = static_cast<int>(stack.size());
            keywords.push_back(std::move(e));
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Arquivos e referencias
// ---------------------------------------------------------------------------
bool isAsset(const std::string& name) {
    const std::string ext = lower(fs::path(name).extension().string());
    return ext == ".gif" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
           ext == ".bmp" || ext == ".css" || ext == ".js" || ext == ".svg";
}

std::vector<std::string> assetReferences(const std::vector<uint8_t>& data) {
    const std::string s(reinterpret_cast<const char*>(data.data()), data.size());
    const char* keys[] = {"src=", "value=", "url("};
    std::vector<std::string> result;
    for (const char* key : keys) {
        size_t pos = 0;
        while ((pos = s.find(key, pos)) != std::string::npos) {
            size_t p = pos + std::strlen(key);
            while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
            if (p < s.size() && (s[p] == '"' || s[p] == '\'')) {
                const char quote = s[p++];
                const size_t end = s.find(quote, p);
                if (end != std::string::npos) result.push_back(s.substr(p, end - p));
            } else {
                size_t end = p;
                while (end < s.size() && !std::isspace(static_cast<unsigned char>(s[end])) &&
                       s[end] != '>' && s[end] != ')') ++end;
                if (end > p) result.push_back(s.substr(p, end - p));
            }
            pos += std::strlen(key);
        }
    }
    return result;
}

void printUsage() {
    std::cerr << "Uso: chm_builder <projeto.hhp> [arquivo_saida.chm]\n"
              << "\n"
              << "Le o .hhp, o .hhc e o .hhk indicados no projeto e gera um CHM ITSF v3\n"
              << "com a secao de conteudo comprimida em LZX. Se a saida nao for informada,\n"
              << "usa-se o campo 'Compiled file' do .hhp.\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    const fs::path hhpPath = fs::absolute(fs::path(argv[1]));
    const fs::path base = hhpPath.parent_path();
    std::string error;
    Hhp hhp;
    if (!parseHhp(hhpPath, hhp, error)) {
        std::cerr << "Erro: " << error << "\n";
        return 1;
    }

    const std::string contentsRel = option(hhp, "Contents file");
    const std::string indexRel = option(hhp, "Index file");
    const std::string defaultRel = option(hhp, "Default topic", "index.htm");
    const std::string compiledRel = option(hhp, "Compiled file", "output.chm");

    chm::BuildOptions opt;
    opt.title = option(hhp, "Title", "Sem titulo");
    opt.defaultTopic = internalName(defaultRel);
    opt.contentsFile = fs::path(contentsRel).filename().string();
    opt.indexFile = fs::path(indexRel).filename().string();
    opt.defaultWindow = option(hhp, "Default Window", "main");
    opt.font = option(hhp, "Default Font", "Arial,9,0");
    opt.lcid = parseLcid(option(hhp, "Language"), 0x0416);
    opt.timestamp = fileTime();
    opt.unixTime = static_cast<uint32_t>(std::time(nullptr));

    fs::path outPath = argc > 2 ? fs::path(argv[2]) : base / fs::path(compiledRel);
    if (!outPath.is_absolute()) outPath = fs::absolute(outPath);
    opt.compiledName = fs::path(compiledRel).stem().string();

    std::vector<chm::InputFile> files;
    std::set<std::string> names;
    auto addFile = [&](const std::string& rel, bool required) -> bool {
        if (rel.empty()) return true;
        const std::string name = internalName(rel);
        if (name == "/" || names.count(name)) return true;
        fs::path abs = fs::path(rel);
        if (!abs.is_absolute()) abs = base / abs;
        std::error_code ec;
        if (!fs::is_regular_file(abs, ec)) {
            if (required) std::cerr << "Aviso: arquivo nao encontrado: " << abs << "\n";
            return false;
        }
        std::vector<uint8_t> data;
        if (!readBytes(abs, data)) {
            if (required) std::cerr << "Aviso: falha ao ler: " << abs << "\n";
            return false;
        }
        names.insert(name);
        files.push_back({name, std::move(data)});
        return true;
    };

    // Ordem: [FILES] do HHP, sumario, indice, topico inicial e hhc/hhk.
    for (const std::string& f : hhp.files) addFile(f, true);
    if (!contentsRel.empty()) addFile(contentsRel, true);
    if (!indexRel.empty()) addFile(indexRel, true);
    addFile(defaultRel, true);

    if (!parseHhc(base / fs::path(contentsRel), opt.toc, error)) {
        std::cerr << "Erro: " << error << "\n";
        return 1;
    }
    if (!parseHhk(base / fs::path(indexRel), opt.keywords, error)) {
        std::cerr << "Erro: " << error << "\n";
        return 1;
    }
    for (const chm::TocEntry& e : opt.toc) addFile(e.local, true);

    // Imagens/CSS referenciados por HTML e pelo proprio hhc/hhk.
    std::vector<std::string> pendingAssets;
    for (const chm::InputFile& f : files) {
        for (const std::string& ref : assetReferences(f.data)) {
            if (isAsset(ref)) pendingAssets.push_back(ref);
        }
    }
    for (const std::string& ref : pendingAssets) addFile(ref, false);

    if (!chm::build(outPath.string(), files, opt, error)) {
        std::cerr << "Erro ao gerar CHM: " << error << "\n";
        return 1;
    }

    std::cout << "CHM gerado: " << outPath << "\n"
              << "  arquivos: " << files.size()
              << ", sumario: " << opt.toc.size()
              << ", palavras-chave: " << opt.keywords.size() << "\n";
    return 0;
}
