# CHM File Format (Compiled HTML Help)

Detailed technical documentation of the `.chm` format, including binary structure, internal directory,
compression, constituent files, and security considerations.

---

## 1. Overview

| Property           | Value                                                                 |
|--------------------|-----------------------------------------------------------------------|
| Full name          | Compiled HTML Help                                                    |
| Extension          | `.chm`                                                                |
| MIME type          | `application/vnd.ms-htmlhelp`                                         |
| Creator            | Microsoft (introduced with Windows 98 / HTML Help 1.x)                |
| Successor to       | `.hlp` (Windows Help 3.x / 4.x)                                       |
| Magic signature    | `ITSF` (bytes `49 54 53 46` at the beginning of the file)             |
| Byte order         | Little-endian (Intel), except where indicated                         |
| Compression        | LZX (LZ77 family algorithm, also used in CAB files)                   |
| Viewer             | `hh.exe` / `hhctrl.ocx` (HTML Help Viewer, native to Windows)         |

A CHM file is, in essence, **a small compressed file system** that contains HTML pages,
images, scripts, and navigation metadata (index, table of contents, search), held in "sections"
of content. Compression is transparent to the viewer.

> **Historical note:** it is speculated that the format derives from `IStorage` (storage structures
> documented in the Platform SDK), although public documentation does not confirm this. The format
> was reverse-engineered; the classic reference is the work of Matthew T. Russotto (2001–2003).

---

## 2. Overall Physical Structure

```
+--------------------------------------------------+  offset 0x0000
|  ITSF initial header (0x38 = 56 bytes)           |
|  signature, version, timestamp, LCID, 2 GUIDs    |
+--------------------------------------------------+  offset 0x0038
|  Header Section Table (2 entries of 16 bytes)    |
|  [offset, length] for section 0 and for the dir. |
+--------------------------------------------------+  offset 0x0058
|  8 extra bytes: offset of content section 0      |
|  (absent in version 2 files)                     |
+--------------------------------------------------+  offset 0x0060 (v3)
|  Header Section 0: general information           |
|  (total file size, unknown data)                |
+--------------------------------------------------+
|  Header Section 1: directory (ITSP + PMGL /     |
|  PMGI chunks)                                    |
+--------------------------------------------------+
|  Content Section 0 (data + nested sections)      |
|  - HTML, images, ::DataSpace/* etc.              |
+--------------------------------------------------+  end of file
```

The entire header (initial header + section table + content field) measures **0x60 = 96 bytes**
in version 3. The two "header sections" are pointed to by absolute offsets in the table.

---

## 3. Initial Header (ITSF)

Located at offset `0x0000`. Size: `0x38` bytes (56 bytes) before the section table.

| Offset | Size | Type       | Description                                                                 |
|--------|------|------------|---------------------------------------------------------------------------|
| 0x00   | 4    | char[4]    | Signature: `"ITSF"`                                                      |
| 0x04   | 4    | DWORD      | Version (normally `3`; versions 1 and 2 exist)                            |
| 0x08   | 4    | DWORD      | Total header length, including section table and extra data               |
| 0x0C   | 4    | DWORD      | Unknown (`1` in many files)                                              |
| 0x10   | 4    | DWORD      | Timestamp (derived from the file's `GetFileTime`, with a +42 applied)     |
| 0x14   | 4    | DWORD      | LCID of the user who compiled the CHM (e.g., `0x0409` = English US)       |
| 0x18   | 16   | GUID       | Directory GUID: `{7C01FD10-7BAA-11D0-9E0C-00A0-C922-E6EC}`             |
| 0x28   | 16   | GUID       | Data stream GUID: `{7C01FD11-7BAA-11D0-9E0C-00A0-C922-E6EC}`           |

### Section Table (offset 0x38)

Two 16-byte entries (`QWORD absolute offset`, `QWORD length`):

| Entry | Offset in table | Points to                          |
|-------|-----------------|--------------------------------------|
| Section 0 | 0x38          | Header Section 0 (general info)      |
| Section 1 | 0x48          | Header Section 1 (ITSP directory)    |

### Extra field (offset 0x58, version 3 only)

| Offset | Size | Description                                              |
|--------|------|--------------------------------------------------------|
| 0x58   | 8    | `QWORD`: offset of Content Section 0 within the file   |

In version 2 this field does not exist and the content starts immediately after the directory.

---

## 4. Header Section 0

Contains basically the total file size:

| Offset | Size | Type  | Description                              |
|--------|------|-------|----------------------------------------|
| 0x00   | 4    | DWORD | Unknown (typically `0x01FE`)           |
| 0x04   | 4    | DWORD | Unknown (`0`)                          |
| 0x08   | 8    | QWORD | Total CHM file size in bytes            |
| 0x10   | 4    | DWORD | Unknown (`0`)                          |
| 0x14   | 4    | DWORD | Unknown (`0`)                          |

---

## 5. Header Section 1 — the Directory (ITSP)

This is the central structure of the CHM: an index of all internal "files".

### 5.1 Directory Header

| Offset | Size | Type    | Description                                                                 |
|--------|------|---------|---------------------------------------------------------------------------|
| 0x00   | 4    | char[4] | Signature: `"ITSP"`                                                      |
| 0x04   | 4    | DWORD   | Version (`1`)                                                             |
| 0x08   | 4    | DWORD   | Length of this header (`0x54` = 84 bytes)                                 |
| 0x0C   | 4    | DWORD   | Unknown (`0x0A`)                                                          |
| 0x10   | 4    | DWORD   | Size of each directory chunk (`0x1000` = 4096 bytes, typical)             |
| 0x14   | 4    | DWORD   | Quickref area density (usually `2`)                                       |
| 0x18   | 4    | DWORD   | Index tree depth (`1` = PMGL only; `2` = one level of PMGI)              |
| 0x1C   | 4    | DWORD   | Index root chunk number (`-1` if none)                                    |
| 0x20   | 4    | DWORD   | First PMGL chunk number                                                   |
| 0x24   | 4    | DWORD   | Last PMGL chunk number                                                    |
| 0x28   | 4    | DWORD   | Unknown (`-1`)                                                            |
| 0x2C   | 4    | DWORD   | Total number of directory chunks                                          |
| 0x30   | 4    | DWORD   | Compiler LCID (from ITSS.DLL, not from the .hhp file)                     |
| 0x34   | 16   | GUID    | `{5D02926A-212E-11D0-9DF9-00A0C922E6EC}`                                   |
| 0x44   | 4    | DWORD   | Length again (`0x54`)                                                     |
| 0x48   | 4    | DWORD   | Unknown (`-1`)                                                            |
| 0x4C   | 4    | DWORD   | Unknown (`-1`)                                                            |
| 0x50   | 4    | DWORD   | Unknown (`-1`)                                                            |

### 5.2 Listing Chunks (PMGL)

Immediately after the ITSP header come the chunks. Each chunk occupies the size defined at
`0x10` of the ITSP (normally 4096 bytes).

| Offset | Size | Description                                                                 |
|--------|------|---------------------------------------------------------------------------|
| 0x00   | 4    | Signature: `"PMGL"`                                                      |
| 0x04   | 4    | Length of free/quickref area at the end of the chunk                      |
| 0x08   | 4    | Always `0`                                                                |
| 0x0C   | 4    | Previous PMGL chunk number (`-1` if first)                                |
| 0x10   | 4    | Next PMGL chunk number (`-1` if last)                                     |
| 0x14   | ...  | Listing entries, sorted by name (case-insensitive ordering)               |

Each **listing entry** has the format:

```
ENCINT  name length
BYTEs   name (UTF-8)
ENCINT  content section number
ENCINT  offset within the section (after decompression)
ENCINT  file length (after decompression)
```

**Quickref:** written backward at the end of the chunk. There is one quickref entry every
`n = 1 + (1 << density)` entries (with density 2, `n = 5`). At the end of the chunk:

```
ChunkLen-0x02: WORD  number of entries in the chunk
ChunkLen-0x04: WORD  offset of the last indexed entry (n, 2n, ...), relative to the start of the entry area
ChunkLen-0x06: WORD  offset of the previous group
... (reverse order; the first quickref written is for entries 4, 9, 14, ...)
```

The offset is measured from byte `0x14` of the chunk (20 bytes of header), not from the beginning
of the file. This table is ignored by `libmspack`, but required by `hh.exe`.

### 5.3 Index Chunks (PMGI)

Optional; omitted when there is only one PMGL chunk.

| Offset | Size | Description                                                    |
|--------|------|--------------------------------------------------------------|
| 0x00   | 4    | Signature: `"PMGI"`                                         |
| 0x04   | 4    | Length of quickref/free area at the end                      |
| 0x08   | ...  | Index entries                                                |

Index entry:

```
ENCINT  name length
BYTEs   name (UTF-8)
ENCINT  number of the listing chunk that starts with this name
```

With depth ≥ 3, the upper levels point to lower PMGI chunks instead of PMGL.

### 5.4 ENCINT — Encoded Integers

Variable-length integer. The high bit (bit 7) of each byte indicates "continues in the next
byte". The 7-bit groups are stored from most significant to least significant
(big-endian by group). Example:

```
EA 15  ->  ((0xEA & 0x7F) << 7) | 0x15  =  0x3515
```

### 5.5 Naming Convention in the Directory

- Names starting with `/` → **user data** (HTML pages, images, CSS, scripts, etc.).
- Names starting with `::` → **internal format files** (system metadata).

---

## 6. Content Sections and Compression

### 6.1 Content Sections

- The **Content Section 0** starts at the offset indicated in the ITSF header (v3) and contains all
  "normal" data (HTML, images, etc.) **and** the other nested sections within it.
- The **remaining sections** are stored inside section 0, in special files
  `::DataSpace/Storage/<SectionName>/Content`.

The known section names are:

| Name           | Meaning                                  |
|----------------|----------------------------------------------|
| `Uncompressed` | Raw data, no compression                  |
| `MSCompressed` | Data compressed with LZX                     |

### 6.2 `::DataSpace/NameList`

File in section 0 that lists the names of all sections:

```
0000: WORD  file length, in words
0002: WORD  number of entries

For each entry:
0000: WORD  name length in words (without the null terminator)
0002: WORD  UTF-16LE characters
xxxx: WORD  0  (terminator)
```

### 6.3 `ControlData` — LZX Parameters

File `::DataSpace/Storage/<Section>/ControlData` (0x1C = 28 bytes):

| Offset | Size | Description                                                          |
|--------|------|--------------------------------------------------------------------|
| 0x00   | 4    | Number of DWORDs following `"LZXC"` (must be 6 in v2)              |
| 0x04   | 4    | ASCII `"LZXC"` — compression type identifier                       |
| 0x08   | 4    | Version (≤ 2)                                                      |
| 0x0C   | 4    | LZX reset interval                                                 |
| 0x10   | 4    | Window size                                                        |
| 0x14   | 4    | Cache size                                                         |
| 0x18   | 4    | Unknown (`0`)                                                      |

In version 2, reset interval, window, and cache are expressed in blocks of `0x8000` bytes;
in version 1, in bytes. The CHM compiled by the HTML Help Workshop that served as reference uses
a **64 KiB** window (`windowSize = 2` blocks of 0x8000, equivalent to `windowBits = 16` in
LZX notation) and `resetInterval = 2` frames, i.e., one reset every 0x10000 generated bytes.

### 6.4 `ResetTable` — Reset Table

File:
`::DataSpace/Storage/<Section>/Transform/{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}/InstanceData/ResetTable`

| Offset | Size | Description                                                       |
|--------|------|-----------------------------------------------------------------|
| 0x00   | 4    | `2` (likely version number)                                      |
| 0x04   | 4    | Number of entries in the table                                    |
| 0x08   | 4    | Size of each entry (`8`)                                          |
| 0x0C   | 4    | Table header length (`0x28`)                                      |
| 0x10   | 8    | Length of decompressed data                                       |
| 0x18   | 8    | Length of compressed data                                         |
| 0x20   | 8    | Block size (`0x8000`)                                             |
| 0x28   | 8    | Entry 0 (zeroth)                                                  |
| 0x30   | 8    | Position in the compressed stream of the 1st block boundary      |
| ...    | 8    | ...remaining boundaries                                           |

### 6.5 LZX Particularities in CHM

- The bit stream is **realigned to 16-bit boundaries** every `0x8000` output bytes.
- The decompressed stream is padded to a multiple of `0x8000` bytes.
- After each reset interval, **all LZX state is reset** (Huffman tables *and*
  Lempel-Ziv history), unlike CAB LZX. This allows semi-random access:
  it is possible to start reading at any interval boundary using the ResetTable.

### 6.6 Other Internal Section Files

| File                                             | Content                                                         |
|--------------------------------------------------|-----------------------------------------------------------------|
| `::DataSpace/Storage/<Section>/Content`          | Raw/compressed data of the section                               |
| `::DataSpace/Storage/<Section>/ControlData`      | Compression parameters (LZXC)                                    |
| `::DataSpace/Storage/<Section>/SpanInfo`         | QWORD with the decompressed size of the section                  |
| `::DataSpace/Storage/<Section>/Transform/List`   | Attempt at a list of transformation GUIDs (usually broken)       |
| `::DataSpace/Storage/<Section>/Transform/{GUID}/InstanceData/ResetTable` | LZX reset table (see 6.4)    |

---

## 7. Typical Files Inside a CHM

In addition to `::DataSpace/*` files, a CHM generated by HTML Help Workshop contains:

| Internal file            | Function                                                                 |
|--------------------------|------------------------------------------------------------------------|
| `/default.htm` (or `index.htm`) | Starting page displayed when opening the CHM                          |
| `/*.htm`, `/*.html`      | Help topics (content pages)                                            |
| `/*.hhc`                 | **HTML Help Contents** — table of contents/navigation tree (HTML format with `<OBJECT>`, `<PARAM>`, and `<UL>/<LI>` tags) |
| `/*.hhk`                 | **HTML Help Keyword Index** — keyword index                            |
| `/*.hhp`                 | Project file (generally **not** compiled into the CHM)                 |
| `/*.gif`, `/*.png`, `/*.jpg` | Images                                                               |
| `/*.css`                 | Style sheets                                                           |
| `/*.js`                  | Scripts (use with caution — attack vector)                             |
| `::DataSpace/*`          | Format metadata (see section 6)                                        |

The viewer uses the `.hhc`/`.hhk` files to build the **Contents**, **Index**, and **Search** tabs,
while the full-text index is built from the page content.

### 7.1 How the CHM is Generated

1. The author writes HTML pages + a `.hhp` project file (settings: title, starting page,
   included files, compilation options).
2. The compiler (HTML Help Workshop, HelpNDoc, Help & Manual, Sandcastle, etc.) generates `.hhc` and `.hhk`.
3. The compiler builds the ITSF/ITSP structure, creates the content sections, compresses with LZX, and
   writes the PMGL/PMGI directory — producing the final `.chm`.

---

## 8. Programmatic Reading and Manipulation

| Tool / Library           | Language     | Use                                                    |
|--------------------------|--------------|--------------------------------------------------------|
| `chmlib` (jedwing)       | C            | Reading/extraction; basis of many free readers         |
| [`libmspack`](https://www.cabextract.org.uk/libmspack/) (CHM decoder)| C | CHM decoder from the libmspack project (Samba)         |
| Apache Tika (`chm` parser)| Java        | Text and metadata extraction                           |
| `pychm`                  | Python       | chmlib binding for Python                              |
| `xCHM`, `KchmViewer`, `ChmSee` | GUI apps | Viewing on Linux/macOS                                 |
| `hh.exe -decompile`      | Windows      | Decompiles CHM via native tool: `hh.exe -decompile <dir> <file.chm>` |
| 7-Zip / PeaZip           | multi        | Opens CHM as archive (does not reconstruct navigation)  |

Example (Windows):

```cmd
hh.exe -decompile C:\tmp\mychm myhelp.chm
```

---

## 9. Security and Limitations

- **Mark of the Web (MOTW):** Windows blocks CHMs downloaded from the internet. To open, you must
  unblock the file (right-click → Properties → Unblock) or remove the ADS stream
  `Zone.Identifier`.
- **Scripts/ActiveX:** HTML pages inside the CHM can execute scripts and ActiveX controls;
  they have historically been used for malware. That's why the security block exists.
- The native viewer `hh.exe` is Windows-specific; on other systems third-party readers are needed,
  with partial support for navigation features.
- Full-text search and the table of contents depend on `.hhc`/`.hhk` and indexes generated at
  compile time; decompiling loses these navigation metadata if only the raw files
  are extracted.

---

## 10. Illustrative Hex Dump Example (Start of a CHM v3)

```
00000000  49 54 53 46 03 00 00 00  60 00 00 00 01 00 00 00  |ITSF....`.......|
00000010  xx xx xx xx 09 04 00 00  10 FD 01 7C AA 7B D0 11  |...........|.{...|
00000020  9E 0C 00 A0 C9 22 E6 EC  11 FD 01 7C AA 7B D0 11  |.....".....|.{...|
00000030  9E 0C 00 A0 C9 22 E6 EC  58 00 00 00 00 00 00 00  |....."..X.......|
00000040  18 00 00 00 00 00 00 00  70 00 00 00 00 00 00 00  |........p.......|
00000050  80 02 00 00 00 00 00 00  88 02 00 00 00 00 00 00  |................|
00000060  54 49 54 50 01 00 00 00  54 00 00 00 0A 00 00 00  |ITSP....T.......|
00000070  00 10 00 00 02 00 00 00  02 00 00 00 FF FF FF FF  |................|
...
```

Legend: `ITSF` → version 3 → header size 0x60 → LCID 0x0409 → GUIDs →
section table (section 0 @0x58 len0x18; directory @0x70 len0x280) → content offset
@0x288 → `ITSP` with chunk size 0x1000, density 2, depth 2.

---

## 11. Layered Summary

```
CHM (ITSF)
├── Header (ITSF + section table + content offset)
├── Header Section 0 ............ file size
├── Header Section 1 (ITSP) ..... directory
│   ├── PMGL (listing chunks) → entries (name/section/offset/size)
│   └── PMGI (index chunks)   → name search tree
└── Content Section 0
    ├── /files.htm, /images, /default.htm ...
    ├── /.hhc (table of contents), /.hhk (index)
    └── ::DataSpace/... (NameList, Storage/<Section>/{Content,ControlData,
                         SpanInfo,Transform/.../ResetTable})
```

---

## 12. Reference Implementation (`../src`)

The `../src` directory contains a complete C++17 implementation that generates an ITSF v3 CHM
from pre-existing files. The flow is:

1. read `.hhp`, `.hhc`, and `.hhk`;
2. build the internal metadata (`#SYSTEM`, `#STRINGS`, `#TOCIDX`, `#TOPICS`, `#URLTBL`,
   `#URLSTR`, `#IDXHDR`, `#WINDOWS`);
3. compress section 1 in LZX with a 64 KiB window and `resetInterval = 2` frames;
4. write section 0 (`NameList`, `ControlData`, `SpanInfo`, `ResetTable`, `Content`);
5. build the PMGL/PMGI directory and the ITSF/ITSP headers.

The `[WINDOWS]` section of the `.hhp` is interpreted for standard windows (`HH_WINTYPE`): name,
title, TOC/index, default/home topic, panel styles, width, buttons, position,
Win32/extended styles, display state, and common navigation fields. The builder
calculates `fsValidMembers` when not provided. The `#WINDOWS` structure now includes
all fields of `HH_WINTYPE`, including advanced *embedded windows* and special
types: `rcHTML` (HTML panel coordinates), `tabOrder` (tab order),
`cHistory` (history items), `rcMinSize` (minimum size), `cbInfoTypes`
(info types), and `pszCustomTabs` (custom tabs).

### 12.1 Compilation and Usage

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    src/main.cpp src/chm.cpp src/lzx.cpp -o src

src example/project.hhp          # uses Compiled file= from .hhp
src example/project.hhp output.chm
```

### 12.2 Validation

The flow was validated with the real libmspack decoders (`lzxd.c` and `chmd.c`) and with
Windows `hh.exe`:

- `make test-lzx` decodes 131,072 bytes (4 frames) without errors;
- `make test-chm` recovers all 7 files from the example (7,002 bytes of compressed section);
- `dump_all.exe` extracts the generated CHM with `err=0` for all entries;
- `hh.exe` opens `../example`, displaying the `HH Parent` window with the title `Example CHM`;
- the `ResetTable` offsets point to the **start** of each reset interval, with a
  final sentinel in the stream;
- the LZX stream ends with 4 zero bytes, required for the decoder to process the extra
  32 KiB frame and align the last word.

The decisive fix for `hh.exe` was writing the **PMGL/PMGI chunk quickrefs in reverse
order and relative to the start of the entry area**. libmspack ignores this table, but the
native viewer uses it; with wrong quickrefs the file was rejected with
`Cannot open the file`, even though all internal bytes were valid.

### 12.3 Internal Index Blobs

The internal index files are no longer stubs. The builder produces:

- `$OBJINST`: two COM objects and the 256 × 10 byte table from `../src`.
  The result is 2,715 bytes and is **byte-identical** to the reference HHC blob.
- `$WWKeywordLinks/{BTree,Data,Map,Property}`: binary index built from the
  `Name`/`Local` pairs in the `.hhk`. The BTree uses 2,048-byte blocks with ordered UTF-16LE words,
  each data entry is 13 bytes, the Map records the listing blocks, and the Property
  contains the eight expected DWORDs when links exist.
- `$WWAssociativeLinks/Property`: four zero bytes, as in the reference HHC.
- `$FIftiMain`: full-text index generated from the `<title>` and `<body>` sections of the HTML files.
  Words are lowercase, limited to 99 bytes, and sorted by `CompareStr`, with the title
  form ahead of the body form. The leaf and index nodes are 4,096 bytes; the
  occurrence lists use WLC with scales `2` and roots `1`, `1`, and `5`, written in
  MSB-first bits. The compressed integers in the leaves use 7-bit groups in little-endian order.
- Entry 4 of `#SYSTEM` now announces `full-text` and `KLinks` when the corresponding blobs
  exist.

In the example, the four `$WWKeywordLinks` blobs and `$OBJINST` are identical to those in the
reference HHC, and the generated `$FIftiMain` was traversed with a decoder that follows the
FPC reader semantics: 32 words, topics 0..2, and consistent positions.

### 12.4 ActiveX Controls and Navigation Scripts

The builder now detects and records HTML Help ActiveX commands and navigation scripts from HTML
files:

- **ActiveX controls** (`HHCtrl.ocx`): `<OBJECT>` tags with
  `classid="clsid:adb880a6-d8ff-11cf-9377-00aa003b7a11"` are parsed. The compiler extracts all
  `<PARAM>` attributes including `Command`, `Item1` through `Item5`, `Button`, `Flags`, `Window`,
  and `Text`. Supported commands include `Contents`, `Index`, `KLink`, `Popup`, `Button`,
  `Button_IT`, `HTML`, and `Close`.
- **Navigation scripts** (`window.helpObj`): JavaScript calls to the HTML Help viewer API are
  detected. Supported functions include `navigate`, `addHistory`, `removeHistory`, `popup`,
  `setInfoType`, `getInfoType`, `getHistoryCount`, `getHistoryItem`, `setLocation`, `getLocation`,
  `addFavorite`, `removeFavorite`, `getFavoriteCount`, and `getFavoriteItem`.
- **Advanced `.hhc`/`.hhk` parameters**: The parser recognizes `Target` (window target),
  `Merge` (external CHM reference for merging help contents), and `ID` (topic identifier for
  context-sensitive help) in addition to `Name` and `Local`.

The compile output reports the count of ActiveX commands and navigation scripts found. These are
stored in the `BuildOptions` structure for potential use by future features (e.g., context-sensitive
help mapping, external CHM merging).

### 12.5 Current Limitations

- The IDs written in `#URLTBL` are deterministic (FNV-1a), different from those generated by the HHC.
  Navigation uses the table index, so this does not prevent opening.
- The `.hhc`/`.hhk` parser covers the usual structure with `<UL>`, `<LI>`, and
  `<OBJECT type="text/sitemap">` with `Name`, `Local`, `Target`, `Merge`, and `ID`.
- The Huffman trees are fixed and complete, not frequency-adaptive. This was a
  deliberate decision: they are valid for the CHM profile and eliminated divergences in
  pretree delta encoding in multi-frame files.
- ActiveX commands and navigation scripts are detected but not validated against the CHM content;
  references to external `.hhc`/`.hhk` files via `Merge` are recorded but not resolved.
  - The `[WINDOWS]` section of the `.hhp` supports standard and advanced windows, including
    all fields of `HH_WINTYPE` (rcHTML, tabOrder, cHistory, rcMinSize,
    cbInfoTypes, pszCustomTabs) per the CHM 1.1+ standard.

---

## 13. References

- Matthew T. Russotto — *Microsoft's HTML Help (.chm) format*: http://www.russotto.net/chm/chmformat.html
- CHM Spec (GNU Savannah / nongnu): https://chmspec.nongnu.org/latest/ITSF.html
- Apache Tika — `org.apache.tika.parser.microsoft.chm` (structures `ChmItsfHeader`, `ChmItspHeader`, etc.)
- ReactOS `sdk/tools/hhpcomp/chmc/chm.h` (ITSF, PMGL, PMGI, LZXC definitions)
- libchm / chmlib — https://github.com/jedwing/chmlib
- libmspack — https://www.cabextract.org.uk/libmspack/

---

*Document generated on 22/11/2026. Content based on publicly documented reverse engineering;
fields marked as "unknown" remain without known public meaning.*
