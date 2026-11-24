# CHM Compiler

This directory contains a self-contained implementation of:

- **LZX** compressor in the profile used by CHM (`chmc` / `lzx.cpp`);
- **ITSF v3 / ITSP v1** container builder with PMGL/PMGI directory (`chmc` / `chm.cpp`);
- CLI that reads a `.hhp` project and pre-existing `.hhc` / `.hhk` files (`chmc`).

## Prerequisites

Before running the builder, **all** files listed in the `[FILES]` section of the `.hhp`, in addition to
the `.hhc`, the `.hhk`, and the starting topic, must already exist on disk. The builder does not create HTML,
images, or project files — it only packages them.

A minimal project consists of:

```text
project.hhp
summary.hhc
index.hhk
index.htm
topic1.htm
style.css
logo.png
```

See the complete example in `example`.

## Compilation

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    main.cpp chm.cpp lzx.cpp -o src
```

On Windows with LLVM-MinGW, the same command works using `g++` from PATH.

There is also a handwritten `Makefile.win` in the repository root:

```bash
make -f Makefile.win         # builds chmc.exe
make -f Makefile.win run     # generates example/project.chm
make -f Makefile.win clean   # removes objects and executables
make -f Makefile.win test    # runs the tests when libmspack is available
```

## Usage

```bash
src <project.hhp> [output_file.chm]
```

If the second argument is omitted, the output name comes from the `Compiled file=` field in the `.hhp`.

Example:

```bash
cd example
../src.exe project.hhp
```

The command above generates `project.chm`.

## Recognized `.hhp` Options

| Section       | Key                   | Use                                                   |
|---------------|-----------------------|-------------------------------------------------------|
| `[OPTIONS]`   | `Title`               | CHM title                                             |
| `[OPTIONS]`   | `Compiled file`       | Name of the generated `.chm`                          |
| `[OPTIONS]`   | `Contents file`       | Path to `.hhc` (table of contents)                    |
| `[OPTIONS]`   | `Index file`          | Path to `.hhk` (index)                                |
| `[OPTIONS]`   | `Default topic`       | Topic displayed when opening the CHM                  |
| `[OPTIONS]`   | `Default Window`      | Window name in `#WINDOWS`                             |
| `[OPTIONS]`   | `Default Font`        | Default font in `#STRINGS`/`#IDXHDR`                 |
| `[OPTIONS]`   | `Language`            | LCID (`0x416` = pt-BR, `0x409` = en-US)              |
| `[FILES]`     | `file=...`            | Files to include in the compressed section            |

References `src=`, `value=`, and `url(...)` found in HTML/CSS are added
automatically when the referenced file exists.

## Generated LZX Profile

| Parameter                    | Value                                      |
|------------------------------|--------------------------------------------|
| Window                       | 64 KiB (`windowBits = 16`, `windowSize = 2`) |
| Frame size                   | 32768 bytes (`0x8000`)                     |
| `resetInterval`              | 2 frames (0x10000 bytes)                   |
| E8 Preprocessing             | disabled (bit 0 at the start of the interval) |
| Block type                   | VERBATIM, one block per 32 KiB frame       |
| Main tree                    | fixed: 16 symbols of 8 bits + 480 of 9 bits |
| Length tree                  | fixed: 7 symbols of 7 bits + 242 of 8 bits |
| Aligned offset tree          | not written (only used in ALIGNED blocks)  |

Each frame is zero-padded to 32768 bytes and realigned to a 16-bit word boundary. The trees
are transmitted in all blocks, but the deltas are relative to the previous block; the state
is reset at each reset interval. At the end, 4 zero bytes satisfy the extra frame read
by the decoder.

## Validation

The test suite uses [libmspack](https://www.cabextract.org.uk/libmspack/) (GitHub mirror:
[kyz/libmspack](https://github.com/kyz/libmspack)) as an independent decoder. libmspack is
an **optional** dependency: `configure` detects it with `pkg-config`, and the tests are
built only when it is found. No copy of libmspack is compiled into this project.

```bash
./configure
make
make check        # builds test_chm when libmspack is available
```

With the handwritten `Makefile.win`, the flags can also be supplied manually:

```bash
make -f Makefile.win test MSPACK_CFLAGS=-I/path/include MSPACK_LIBS=-lmspack
```

`test_chm` validates the generated CHM by extracting every file with libmspack's public
CHM decompressor API, which also exercises the LZX stream. The low-level `test_lzx`
helper is kept as `src/test_lzx.cpp` for reference only: a system libmspack does not
export the internal `lzxd_*` API it needs.

The expected output is `OK <file>` for each file in the example, with `failures=0` at
the end. Additionally, the generated CHM was successfully opened in the Windows Help
Viewer (`hh.exe`), displaying the `HH Parent` window and the title `Example CHM`.

> **Important:** the quickrefs of PMGL/PMGI chunks must be written in reverse order and
> relative to the start of the entry area. `libmspack` ignores this table, but `hh.exe`
> uses it; an incorrectly inverted table causes the viewer to reject the file.

## Binary Index and Full-Text Search

The builder fills in the real internal blobs:

- `$OBJINST` contains the 256 × 10 byte table from `chmc` and the two COM objects;
  in the example the result is byte-identical to the reference HHC blob (2,715 bytes).
- `$WWKeywordLinks/{BTree,Data,Map,Property}` is built from the `Name`/`Local` pairs in the `.hhk`.
  BTree with 2,048-byte blocks, 13 bytes per Data entry, Map and Property with eight
  DWORDs. `$WWAssociativeLinks/Property` receives four zero bytes.
- `$FIftiMain` indexes `<title>` and `<body>` from the HTML files. The trees use 4,096-byte nodes,
  WLC with scales `2` and roots `1`, `1`, `5`, MSB-first bits, and 7-bit little-endian ENCINT.
  Entry 4 of `#SYSTEM` announces `full-text` and `KLinks` when applicable.

The `.hhk` parser captures `Name`, `Local`, `Target`, and `ID`, and topic indices are resolved via `#TOPICS`.
As additional validation, the `$WW*` and `$OBJINST` blobs from the example were compared byte by byte
with those from the HHC, and the `$FIftiMain` was decoded using the FPC reader semantics.

## ActiveX Controls and Navigation Scripts

The compiler detects and records HTML Help ActiveX commands and navigation scripts:

- **ActiveX controls**: `<OBJECT>` tags with `classid="clsid:adb880a6-d8ff-11cf-9377-00aa003b7a11"`
  (HHCtrl.ocx) are parsed. Supported commands include `Contents`, `Index`, `KLink`, `Popup`,
  `Button`, `Button_IT`, `HTML`, and `Close`. All `<PARAM>` attributes (`Command`, `Item1`–`Item5`,
  `Button`, `Flags`, `Window`, `Text`) are extracted.
- **Navigation scripts**: JavaScript calls to `window.helpObj` are detected. Supported functions
  include `navigate`, `addHistory`, `removeHistory`, `popup`, `setInfoType`, `getInfoType`,
  `getHistoryCount`, `getHistoryItem`, `setLocation`, `getLocation`, `addFavorite`,
  `removeFavorite`, `getFavoriteCount`, and `getFavoriteItem`.
- **Advanced `.hhc`/`.hhk` parameters**: The parser now recognizes `Target` (window target),
  `Merge` (external CHM reference), and `ID` (topic identifier) in addition to `Name` and `Local`.

The compile output reports the number of ActiveX commands and navigation scripts found.

## Known Limitations

- The IDs in `#URLTBL` are deterministic (FNV-1a), different from those in the HHC; navigation uses the
  table index and is not affected.
- The `.hhc` / `.hhk` parser recognizes `<UL>`, `<LI>`, and `<OBJECT type="text/sitemap">`
  with `Name`, `Local`, `Target`, `Merge`, and `ID` parameters; it is not a complete HTML parser.
- ActiveX commands and navigation scripts are detected but not validated against the CHM content;
  references to external `.hhc`/`.hhk` files via `Merge` are recorded but not resolved.

For binary details, see `doc/chm.md` and `doc/lzx.md`.
