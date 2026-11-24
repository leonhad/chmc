#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chm {
    // File that will be placed inside the CHM. 'internalName' starts with '/'.
    struct InputFile {
        std::string internalName;
        std::vector<uint8_t> data;
    };

    struct TocEntry {
        std::string name; // text displayed in the table of contents
        std::string local; // internal name ("/topic.htm"), empty for node without destination
        std::string target; // Target window name (e.g., "_main", "_blank")
        std::string merge; // Merge attribute for external CHM references (e.g., "other.chm::/toc.hhc")
        std::string id; // Topic identifier for context-sensitive help
        int depth = 0; // depth in .hhc (0 = root)
        bool hasChildren = false;
    };

    // Keyword from .hhk, with the topic that should be opened in the binary index.
    struct KeywordEntry {
        std::string name;
        std::string local;
        std::string target; // Target window name
        std::string id; // Topic identifier for context-sensitive help
        int depth = 0;
    };

    // HTML Help ActiveX control command (HHCtrl.ocx).
    // Represents <OBJECT> tags with classid adb880a6-d8ff-11cf-9377-00aa003b7a11.
    struct ActiveXCommand {
        std::string command; // e.g., "Contents", "Index", "KLink", "Popup", "Button", "Button_IT", "HTML", "Close"
        std::string item1; // First item parameter (e.g., .hhc/.hhk path, keyword, URL)
        std::string item2; // Second item parameter (e.g., window name, flags)
        std::string item3; // Third item parameter (e.g., button text)
        std::string item4; // Fourth item parameter (e.g., popup title)
        std::string item5; // Fifth item parameter (e.g., popup flags)
        std::string button; // Button text for Button/Button_IT commands
        std::string flags; // Flags for Contents/Index commands
        std::string window; // Target window name
        std::string text; // Display text for KLink/Button commands
        std::string id; // OBJECT id attribute
        std::string sourceFile; // HTML file containing this ActiveX command
    };

    // Navigation script reference (window.helpObj API).
    // Represents JavaScript calls to the HTML Help viewer.
    struct NavigationScript {
        std::string function; // e.g., "navigate", "addHistory", "removeHistory", "popup", "setInfoType"
        std::string arguments; // Comma-separated arguments
        std::string sourceFile; // File containing the script
    };

    // Viewer window definition ([WINDOWS] section of .hhp).
    // Maps all fields of HH_WINTYPE, including embedded windows
    // and special types (rcHTML, tabOrder, cHistory, rcMinSize, infoTypes,
    // customTabs) per the CHM 1.1+ standard.
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
        uint32_t validFlags = 0; // fsValidMembers; 0 = calculated by the builder

        // --- Advanced fields / embedded windows (CHM 1.1+) --------------------
        int32_t rcHTML[4] = {0, 0, 0, 0}; // rcHTML: topic panel coordinates
        uint8_t tabOrder[17] = {}; // tabOrder: tab order (Contents, Index, Search, History, Favorites, Reserved 1-5, Custom)
        int32_t cHistory = 30; // cHistory: number of history items (default 30)
        std::string customTabs; // pszCustomTabs: custom tab labels (null-separated strings)
        int32_t rcMinSize[4] = {0, 0, 0, 0}; // rcMinSize: minimum window size (left,top,right,bottom)
        int32_t cbInfoTypes = 0; // cbInfoTypes: size of paInfoTypes
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
        std::vector<ActiveXCommand> activeXCommands;
        std::vector<NavigationScript> navigationScripts;
    };

    // Builds an ITSF v3 CHM with section 0 (uncompressed) and section 1 (LZX).
    bool build(const std::string &outPath, const std::vector<InputFile> &files, const BuildOptions &options, std::string &error);
} // namespace chm
