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

// Palavra-chave do .hhk, com o topico que deve ser aberto no índice binário.
struct KeywordEntry {
    std::string name;
    std::string local;
    int depth = 0;
};

// Definicao de janela do visualizador (seção [WINDOWS] do .hhp).
// Mapeia os campos mais comuns de HH_WINTYPE; embedded windows avançadas
// (tipos/integrações especiais) não são suportadas.
struct WindowDef {
    std::string name;          // Arg 0
    std::string caption;       // Arg 1
    std::string toc;           // Arg 2
    std::string index;         // Arg 3
    std::string defaultFile;   // Arg 4
    std::string homeFile;      // Arg 5
    std::string jump1;         // Arg 6
    std::string jump2;         // Arg 7
    std::string urlJump1;      // Arg 8
    std::string urlJump2;      // Arg 9
    uint32_t navStyle = 0;     // Arg 10 / fsWinProperties
    uint32_t navWidth = 0;     // Arg 11
    uint32_t buttons = 0;      // Arg 12 / fsToolBarFlags
    int32_t rect[4] = {0, 0, 0, 0}; // Arg 13 [left,top,right,bottom]
    uint32_t styles = 0;       // Arg 14 / dwStyles
    uint32_t exStyles = 0;     // Arg 15 / dwExStyles
    uint32_t showState = 0;    // Arg 16
    uint32_t navPaneClosed = 0;// Arg 17
    uint32_t defaultPane = 0;  // Arg 18
    uint32_t tabPos = 0;       // Arg 19
    uint32_t idNotify = 0;     // Arg 20
    uint32_t validFlags = 0;   // fsValidMembers; 0 = calculado pelo gerador
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

    std::string contentsTitle = "Contents";  // titulo do arquivo .hhc
    std::string indexTitle = "Index";        // titulo do arquivo .hhk

    std::vector<TocEntry> toc;
    std::vector<KeywordEntry> keywords;
    std::vector<WindowDef> windows;
};

// Monta um CHM ITSF v3 com seção 0 (não comprimida) e seção 1 (LZX).
bool build(const std::string& outPath,
           const std::vector<InputFile>& files,
           const BuildOptions& options,
           std::string& error);

} // namespace chm
