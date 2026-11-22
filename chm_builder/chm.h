#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chm {

// Arquivo que sera colocado dentro do CHM. 'internalName' comeca com '/'.
struct InputFile {
    std::string internalName;
    std::vector<uint8_t> data;
};

struct TocEntry {
    std::string name;     // texto exibido no sumario
    std::string local;    // nome interno ("/topic.htm"), vazio para no sem destino
    int depth = 0;        // profundidade no .hhc (0 = raiz)
    bool hasChildren = false;
};

struct BuildOptions {
    std::string title = " untitled";
    std::string defaultTopic = "/index.htm";
    std::string contentsFile = "toc.hhc";
    std::string indexFile = "index.hhk";
    std::string compiledName = "output";
    std::string defaultWindow = "main";
    std::string font = "Arial,9,0";
    uint32_t lcid = 0x0416;    // pt-BR; 0x0409 = en-US
    uint64_t timestamp = 0;    // FILETIME completo (QWORD)
    uint32_t unixTime = 0;     // time_t usado no codigo 10 de #SYSTEM

    // Campos da primeira entrada de #WINDOWS (HH_WINTYPE).
    uint32_t winValidFlags = 0;  // mascaras HHWIN_PARAM_*
    uint32_t winArg10 = 0;       // estilos da janela
    uint32_t winArg12 = 0;       // estilos estendidos
    std::string winArg8;         // argumento textual extra, quando presente

    std::string contentsTitle = "Contents";  // titulo do arquivo .hhc
    std::string indexTitle = "Index";        // titulo do arquivo .hhk

    std::vector<TocEntry> toc;
    std::vector<std::string> keywords;
};

// Monta um CHM ITSF v3 com secao 0 (nao comprimida) e secao 1 (LZX).
bool build(const std::string& outPath,
           const std::vector<InputFile>& files,
           const BuildOptions& options,
           std::string& error);

} // namespace chm
