#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chm {
    // Arquivo que será colocado dentro do CHM. 'internalName' comeca com '/'.
    struct InputFile {
        std::string internalName;
        std::vector<uint8_t> data;
    };

    struct TocEntry {
        std::string name; // texto exibido no sumário
        std::string local; // nome interno ("/topic.htm"), vazio para no sem destino
        int depth = 0; // profundidade no .hhc (0 = raiz)
        bool hasChildren = false;
    };

    // Palavra-chave do .hhk, com o topico que deve ser aberto no índice binário.
    struct KeywordEntry {
        std::string name;
        std::string local;
        int depth = 0;
    };

    // Definicao de janela do visualizador (seção [WINDOWS] do .hhp).
    // Mapeia todos os campos de HH_WINTYPE, incluindo embedded windows
    // e tipos especiais (rcHTML, tabOrder, cHistory, rcMinSize, infoTypes,
    // customTabs) conforme o padrão CHM 1.1+.
    struct WindowDef {
        std::string name; // Arg 0 / pszType
        std::string caption; // Arg 1 / pszCaption
        std::string toc; // Arg 2 / pszToc
        std::string index; // Arg 3 / pszIndex
        std::string defaultFile; // Arg 4 / pszFile
        std::string homeFile; // Arg 5 / pszHome
        std::string jump1; // Arg 6 / pszJump1
        std::string jump2; // Arg 7 / pszJump2
        std::string urlJump1; // Arg 8 / pszUrlJump1
        std::string urlJump2; // Arg 9 / pszUrlJump2
        uint32_t navStyle = 0; // Arg 10 / fsWinProperties
        uint32_t navWidth = 0; // Arg 11 / iNavWidth
        uint32_t buttons = 0; // Arg 12 / fsToolBarFlags
        int32_t rect[4] = {0, 0, 0, 0}; // Arg 13 / rcWindowPos [left,top,right,bottom]
        uint32_t styles = 0; // Arg 14 / dwStyles
        uint32_t exStyles = 0; // Arg 15 / dwExStyles
        uint32_t showState = 0; // Arg 16 / nShowState
        uint32_t navPaneClosed = 0; // Arg 17 / fNotExpanded
        uint32_t defaultPane = 0; // Arg 18 / curNavType
        uint32_t tabPos = 0; // Arg 19 / tabpos
        uint32_t idNotify = 0; // Arg 20 / idNotify
        uint32_t validFlags = 0; // fsValidMembers; 0 = calculado pelo gerador

        // --- Campos avançados / embedded windows (CHM 1.1+) --------------------
        int32_t rcHTML[4] = {0, 0, 0, 0}; // rcHTML: coordenadas do painel de tópicos
        uint8_t tabOrder[17] = {}; // tabOrder: ordem das abas (Contents, Index, Search, History, Favorites, Reserved 1-5, Custom)
        int32_t cHistory = 30; // cHistory: número de itens de histórico (padrão 30)
        std::string customTabs; // pszCustomTabs: rótulos das abas personalizadas (strings null-separated)
        int32_t rcMinSize[4] = {0, 0, 0, 0}; // rcMinSize: tamanho mínimo da janela (left,top,right,bottom)
        int32_t cbInfoTypes = 0; // cbInfoTypes: tamanho de paInfoTypes
    };

    struct BuildOptions {
        std::string title = " untitled";
        std::string defaultTopic = "/index.htm";
        std::string contentsFile = "toc.hhc";
        std::string indexFile = "index.hhk";
        std::string compiledName = "output";
        std::string defaultWindow = "main";
        std::string font = "Arial,9,0";
        uint32_t lcid = 0x0416; // pt-BR; 0x0409 = en-US
        uint64_t timestamp = 0; // FILETIME completo (QWORD)
        uint32_t unixTime = 0; // time_t usado no codigo 10 de #SYSTEM

        std::string contentsTitle = "Contents"; // titulo do arquivo .hhc
        std::string indexTitle = "Index"; // titulo do arquivo .hhk

        std::vector<TocEntry> toc;
        std::vector<KeywordEntry> keywords;
        std::vector<WindowDef> windows;
    };

    // Monta um CHM ITSF v3 com seção 0 (não comprimida) e seção 1 (LZX).
    bool build(const std::string &outPath,
               const std::vector<InputFile> &files,
               const BuildOptions &options,
               std::string &error);
} // namespace chm
