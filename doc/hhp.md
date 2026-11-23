# `.hhp` File Format — HTML Help Project (CHM Project)

> Part of the **Microsoft HTML Help 1.x** source format family, used to generate
> `.chm` files. See also `chm.md`, `hhc.md`, and `hhk.md`.

---

## 1. What Is It

The `.hhp` file (**HTML Help Project**) is the **project file** used by the Microsoft compiler
(`hhc.exe`, with `hha.dll`) to produce a `.chm`.

- It is a **plain-text INI file**: `[NAME]` sections, `key=value` lines, comments with `;`.
- Line break encoding: **CRLF** (Windows INI standard).
- All sections are optional in theory, but `[OPTIONS]` and `[FILES]` appear in every real
  project. `[WINDOWS]` is required for custom windows.
- Paths are **relative to the directory containing the `.hhp`**.
- The HTML Help Workshop (HHW) fails if any line has **1024 or more characters** (excluding CRLF).

Typical compilation:

```cmd
"C:\Program Files (x86)\HTML Help Workshop\hhc.exe" myProject.hhp
```

The result is `Compiled file=` (e.g., `myProject.chm`) and a log (if `Error log file=` is defined).

---

## 2. Sections

| Section | Purpose |
|---------|---------|
| `[OPTIONS]` | General project and compiler settings |
| `[WINDOWS]` | Viewer window definitions (size, buttons, tabs, panel) |
| `[FILES]` | List of files to include in the CHM (HTML, CSS, images, etc.) |
| `[MERGE FILES]` | List of other `.chm` files whose index/search will be merged at runtime |
| `[ALIAS]` | Mapping of symbolic names (IDH_*) to topics |
| `[MAP]` | Definition of context numbers (`#define IDH_* 1001`) |
| `[TEXT POPUPS]` | Text/header files with texts for ActiveX pop-ups |
| `[INFOTYPES]` | Information/audience types (little support; HHW does not generate) |
| `[SUBSETS]` | Subsets for MSDN Collections (not used in normal CHM) |

---

## 3. `[OPTIONS]` Section

Most common keys (alphabetical order is used by HHW):

| Key | Example | Meaning |
|-----|---------|---------|
| `Compatibility` | `1.1 or later` | Compatible HTML Help version |
| `Compiled file` | `my.chm` | Output `.chm` name |
| `Contents file` | `my.hhc` | Table of contents file (leave empty for no Contents tab) |
| `Index file` | `my.hhk` | Index file (empty = no Index tab) |
| `Default Window` | `main` | Window defined in `[WINDOWS]` used by default |
| `Default topic` | `index.htm` | Page displayed when opening the CHM |
| `Default Font` | `Tahoma,9,0` | Default font (name, size, charset) |
| `Language` | `0x409 English (US)` | Language LCID (e.g., `0x416` Portuguese BR, `0x415` PT-PT) |
| `Title` | `My App Help` | Window/help title |
| `Full-text search` | `Yes` / `No` | Enables Search tab (full-text index) |
| `Full text search stop list file` | `stopwords.stp` | List of words ignored in search |
| `Binary TOC` | `Yes` / `No` | Binary table of contents (necessary for certain features; breaks merging/icons) |
| `Binary Index` | `Yes` / `No` | Binary index (default `Yes`) |
| `Auto Index` | `Yes` / `No` | Automatically collects keywords from topics |
| `Display compile progress` | `Yes` | Shows progress during compilation |
| `Display compile notes` | `Yes` | Shows compilation notes |
| `Error log file` | `my.log` | Error/warning log file |
| `Create CHI file` | `No` | Generates `.chi` file (for MSDN collections) |
| `Flat` | `No` | Flattens the directory structure |
| `ImageType` | `Folder` / `Picture` | Default table of contents icon style |

> Useful LCIDs: `0x409` en-US, `0x416` pt-BR, `0x415` pt-PT, `0x804` zh-CN, `0x411` ja-JP.

---

## 4. `[WINDOWS]` Section

Each line defines a window and has the positional format (arguments separated by commas,
quoted when necessary):

```ini
[WINDOWS]
main="My App Help","my.hhc","my.hhk","index.htm","index.htm",,,,,0x23520,222,0x104E,[10,10,640,480],0xB0000,,,,,,0
```

### Arguments (index 0 to 19)

| # | Example | Meaning |
|---|---------|---------|
| 0 | `main` | Window name/type |
| 1 | `"Help..."` | Title bar text |
| 2 | `"my.hhc"` | Table of contents file |
| 3 | `"my.hhk"` | Index file |
| 4 | `"index.htm"` | Main topic |
| 5 | `"index.htm"` | Alternative starting topic |
| 6–9 | empty | Reserved / additional parameters |
| 10 | `0x63520` | Navigation panel styles bit field |
| 11 | `220` | Navigation panel width (pixels) |
| 12 | `0x10384e` | Toolbar buttons bit field |
| 13 | `[10,10,640,480]` | Initial position `[left, top, right, bottom]` |
| 14 | `0xB0000` | Win32 styles (`SetWindowLong` / `CreateWindow`) |
| 15 | `0x0` | Win32 extended styles (`CreateWindowEx`) |
| 16–18 | empty | Reserved |
| 19 | `0` | Final state/parameter (various flags) |

### 4.1 Button Bit Field (argument 12)

Values defined in `htmlhelp.h` (not all work):

| Value | Button |
|-------|--------|
| `0x000002` | Show/hide panel |
| `0x000004` | Back |
| `0x000008` | Forward |
| `0x000010` | Stop |
| `0x000020` | Refresh |
| `0x000040` | Home |
| `0x000200` | Notes |
| `0x000400` | Contents |
| `0x000800` | Find |
| `0x001000` | Options |
| `0x002000` | Print |
| `0x004000` | Index |
| `0x008000` | Search |
| `0x010000` | History |
| `0x020000` | Favorites |
| `0x040000` / `0x080000` | Jump 1 / Jump 2 |
| `0x100000` | Zoom/Font |
| `0x200000` / `0x400000` | Next topic / Previous topic |

Example: `0x104E` = `0x1000` + `0x40` + `0x8` + `0x4` + `0x2` → Options + Contents + Forward + Back + Panel.

### 4.2 Navigation Panel Styles (argument 10)

| Value | Meaning |
|-------|---------|
| `0x00000001` | Auto-hide/show panel according to focus |
| `0x00000002` | Keep window always on top |
| `0x00000004` | No title bar |
| `0x00000008` | Do not use default styles (`dwStyles`) |
| `0x00000010` | Do not use default extended styles (`dwExStyles`) |
| `0x00000020` | Use three-panel (tri-pane) window — recommended |

---

## 5. `[FILES]` Section

List of files (one per line) that should be compiled into the CHM:

```ini
[FILES]
index.htm
installation.htm
htm\options.htm
css\style.css
img\logo.png
my.hhc
my.hhk
```

- The compiler also automatically discovers files referenced by links in the table of contents,
  index, and HTML pages — but including them explicitly is safer (especially CSS/images).
- Paths relative to the `.hhp` directory.

---

## 6. `[MAP]` and `[ALIAS]` Sections — Context-Sensitive Help

Used when an application calls `HtmlHelp(..., HH_HELP_CONTEXT, 1001)` to open a topic
from a numeric ID associated with a control/dialog.

```ini
[ALIAS]
IDH_HomePage=default.htm
IDH_Installation=htm\installation.htm
IDH_Options=mychm::htm\options.htm

[MAP]
#define IDH_HomePage 1000
#define IDH_Installation 1001
#define IDH_Options 1002
```

It is also possible to use external files:

```ini
[ALIAS]
#include myhelp.ali

[MAP]
#include myhelp.h
```

> When using `#include`, remember to add the included files to `[FILES]` as well.

---

## 7. Other Sections

### `[MERGE FILES]`
List of CHMs that will be merged at runtime (combined index and search):

```ini
[MERGE FILES]
contacts.chm
procedures.chm
```

Practical limit: it is known that HHW 4.74.8702 crashes with more than ~1148 entries; the binary
internal format stores a maximum of 1004.

### `[TEXT POPUPS]`
List of text/header files with the texts displayed by the `Popup` command of the
HTML Help ActiveX control.

### `[INFOTYPES]` / `[SUBSETS]`
Poorly documented; the current HHW does not generate them. Related to audience types and MSDN collections.

---

## 8. Complete and Minimal Example

```ini
;-----------------------------------------------------------
; HTML Help 1.x project example
;-----------------------------------------------------------
[OPTIONS]
Compatibility=1.1 or later
Compiled file=MyApp.chm
Contents file=MyApp.hhc
Index file=MyApp.hhk
Default Window=main
Default topic=index.htm
Default Font=Tahoma,9,0
Language=0x416 Portuguese (Brazil)
Title=MyApp Help
Full-text search=Yes
Full text search stop list file=stopwords.stp
Binary TOC=No
Binary Index=Yes
Auto Index=Yes
Display compile progress=Yes
Error log file=MyApp.log

[WINDOWS]
main="MyApp Help","MyApp.hhc","MyApp.hhk","index.htm","index.htm",,,,,0x63520,220,0x10384E,[10,10,1024,768],0xB0000,,,,,,0

[FILES]
index.htm
installation.htm
options.htm
css\style.css
img\logo.png
MyApp.hhc
MyApp.hhk
stopwords.stp

[MAP]
#define IDH_Start 1000
#define IDH_Installation 1001

[ALIAS]
IDH_Start=index.htm
IDH_Installation=installation.htm
```

---

## 9. Compilation Flow

```
.htm/.html pages ─┐
.hhc file (TOC) ──┼──> hhc.exe + hha.dll ──> .chm file
.hhk file (index) ─┤         (uses the .hhp)
images/CSS/scripts ─┘
```

The compiler:
1. reads `[OPTIONS]` and `[WINDOWS]`;
2. opens `.hhc` and `.hhk` and resolves the topics;
3. includes the files from `[FILES]` and those referenced by links;
4. builds the ITSF/ITSP directory (see `chm.md`);
5. compresses the content with LZX and writes the `.chm`;
6. logs warnings/errors to `Error log file`.

---

## 10. References

- CHM Spec — INI formats / HHP: https://www.gnu.org/non-gnu/chmspec/latest/INI.html
- Helpware — .HHP File Sections: http://www.helpware.net/FAR/help/dlg_hhpedit_sec.htm
- Microsoft Learn — Create a Help Project / Compile a Help Project (HTML Help 1.x)
- `htmlhelp.h` — `HHWIN_*` constants, buttons, and styles
- Sphinx `htmlhelp` builder — `.hhp` template and button bit fields
