# LZX Compression Format (Microsoft LZX / LZXD)

Detailed technical documentation of the LZX algorithm and stream format, as used in CAB, CHM,
WIM, NTFS (WOF), and the LZX DELTA extension (MS-PATCH).

---

## 1. Overview

| Property | Value |
|-------------|-------|
| Name | LZX (Microsoft LZX); extended variant: **LZXD / LZX DELTA** |
| Creators | Jonathan Forbes and Tomi Poutanen; adapted and proprietary-ized by Microsoft |
| Family | LZ77 + canonical Huffman coding (similar in spirit to Deflate, but incompatible) |
| Sliding window | Classic: 2¹⁵ to 2²¹ bytes (32 KiB – 2 MiB); LZXD: 2¹⁷ to 2²⁵ (128 KiB – 32 MiB) |
| Match size | 2 to 257 bytes (classic); up to 33,024 bytes in LZX DELTA |
| Frame | 32,768 bytes (0x8000) in LZX with reset (CHM) |
| Bit order | Bits are read from **MSB** to LSB; bytes grouped in **16-bit little-endian words** |
| Used in | `.cab`, `.chm`, `.wim`, NTFS "System Compressed" (WOF), `.lit`, MS-PATCH patches |

LZX is **not** Deflate, LZMA, or LZW. It is a proprietary scheme with three block types, multiple
adaptive Huffman trees, and optional pre-processing for x86 code.

> The format is partially proprietary and was never fully published by Microsoft in the past.
> The most complete and corrected specification is **[MS-PATCH] LZX DELTA Compression and
> Decompression**; the most widespread reference implementation is `lzxd.c` from libmspack
> (Stuart Caie), which documents divergences between the old CAB SDK spec and the real implementation.

---

## 2. Stream Layers

```
Original data
   │
   ├─ (optional) E8 call translation → converts relative x86 CALLs to "absolute" offsets
   │
   ├─ Sliding window (dictionary) → DAGMA: literals + matches (offset, length)
   │
   ├─ Tokens encoded with 4 canonical Huffman trees:
   │      Main Tree, Length Tree, Aligned Offset Tree, Pre-Trees
   │
   └─ Bitstream: 16-bit little-endian words, MSB-first bits
         │
         └─ (in CHM) fragmented into 32 KiB frames with realignment and state reset
```

### 2.1 External Parameters (not stored in the stream)

The decoder **must receive from the container**:

| Parameter | Typical source |
|-----------|---------------|
| Window size (`window_bits`) | CAB: `typeCompress >> 8 & 31` from CFHEADER/FOLDER; CHM: `ControlData` (32 or 64 KiB); WIM: resource header |
| Reset interval | CHM: `ControlData` (in 0x8000 blocks); 0 = no reset (CAB) |
| File size for E8 | CAB: `cbFile` from CFFILE; LZXD: 33-bit header of the stream |
| Total output size | CAB: `cbFile`; CHM: directory/ResetTable |

---

## 3. Bitstream

- Bits are packed **from most significant to least significant** within each
  16-bit word.
- Words are written in **little-endian** (low byte first).
- In streams with reset (CHM), at the end of each 32 KiB frame the stream is **realigned to a
  16-bit boundary**. The CHM `ResetTable` stores the offsets of each boundary.
- *Uncompressed* blocks also require alignment before the R0/R1/R2 fields.

Example (from MS-PATCH spec): bit sequence `a,b,c,...,A,B,C,D` generates bytes with the word
`0x...` whose bit 15 is the first bit read.

---

## 4. General Structure

```
[ E8 header (optional, 1 bit + 32 bits) ]
[ block 1 ][ block 2 ] ... [ block N ]

Block:
  ┌─ 3 bits  : type (1=verbatim, 2=aligned, 3=uncompressed)
  ├─ 24 bits : size of DECOMPRESSED data represented (max. 0xFFFFFF)
  └─ type-specific data
```

### 4.1 Block Types

| Value | Name | Description |
|-------|------|-----------|
| 0 | invalid | — |
| **1** | `VERBATIM` | Huffman tokens; offsets written directly |
| **2** | `ALIGNED` | Same as verbatim, with extra aligned offset tree |
| **3** | `UNCOMPRESSED` | Literal data + R0/R1/R2 states |
| 4–7 | invalid | — |

### 4.2 Note on the Size Field

There is a documented historical discrepancy:

- **[MS-PATCH]/LZXD:** 3 type bits + **24 bits** of size (MSB first).
- **Old CAB SDK spec / libyal:** 3 type bits + 1 "default block size" bit (32 KiB)
  + 16 bits of size, with an extra byte when the window > 32 KiB.

The real CAB implementation (libmspack) uses the 24-bit field. Check the container's reference
tool in case of doubt.

---

## 5. Compressed Blocks (VERBATIM and ALIGNED)

### 5.1 Block Header

Exact field order (as implemented):

1. **Aligned Offset Tree** (type 2 only): 8 codes of **3 bits** each = 24 bits.
2. **Main Tree, part 1** (literals): Pre-tree of 20×4 bits + encoded lengths for
   symbols 0–255.
3. **Main Tree, part 2** (matches): Pre-tree of 20×4 bits + lengths for symbols
   `256 .. 255 + 8*NUM_POSITION_SLOTS` (e.g., 256 + 8×30 = **496 symbols** with a 32 KiB window).
4. **Length Tree**: Pre-tree of 20×4 bits + lengths for **249 symbols**.
5. **Token sequence** (literals and matches) until the block size is complete.

> The aligned tree comes **before** the others in the implementation, although the old CAB spec said
> after — another divergence pointed out by libmspack.

### 5.2 Pre-Trees and Length Encoding

Each tree is transmitted indirectly: first a **pre-tree** of 20 symbols with
fixed 4-bit lengths; then the real lengths are encoded with that pre-tree.

Pre-tree symbols:

| Symbol | Meaning |
|---------|-------------|
| 0–16 | Direct length: `(previous_length - symbol + 17) mod 17` |
| 17 | RLE of zeros: read 4 extra bits → repeat `4 + value` zeros |
| 18 | RLE of zeros: read 5 extra bits → repeat `20 + value` zeros |
| 19 | RLE of one length: read 1 bit (4 or 5 repetitions) + another pre-tree symbol (4 bits) that gives the absolute value |

The lengths are **differential relative to the previous block** — the tree state
persists between blocks (in the first block, the "previous" is zero). This is one of the sources
of table compression.

A tree with all zero lengths is allowed for the **Length Tree** (when the block does not
contain long matches), but not for the Main Tree.

### 5.3 Canonical Huffman Trees

Given the vector of lengths (in bits) per symbol:

- Sort by increasing length; on tie, by increasing symbol number.
- Codes are assigned sequentially (the code of length *n+1* is code *n*
  incremented and shifted left).
- Validity rule: if a node has children, all nodes to its right with the same length
  must also have children (avoids "hole" codes).

---

## 6. Tokens: Literals and Matches

### 6.1 Main Tree — Symbol Decoding

```
main_element = main_tree.decode()

if main_element < 256:
    → LITERAL: emit byte main_element
else:
    m = main_element - 256
    length_header = m & 7          ; 3 bits
    position_slot = m >> 3         ; offset slot number
```

### 6.2 Match Length

| `length_header` | Length |
|-----------------|-------------|
| 0 | 2 |
| 1 | 3 |
| 2 | 4 |
| 3 | 5 |
| 4 | 6 |
| 5 | 7 |
| 6 | 8 |
| **7** | `9 + symbol_read_from_Length_Tree` (9 … 257) |

The **Length Tree** has 249 symbols; symbol `i` represents length `i + 9`
(so, 248 → 257). Matches of 2 to 8 bytes use only the header — no extra bits.

> In LZX DELTA, length 257 signals an extra step allowing matches up to 33,024 bytes.

### 6.3 Offsets: Slots, Footer, and Repeated Displacements

The match offset is converted to a **formatted offset**:

```
if offset == R0 → 0
if offset == R1 → 1
if offset == R2 → 2
else           → offset + 2
```

The formatted offset is divided into **position slot** (base) + **position footer** (extra bits):

```
extra_bits(slot) = 0                       if slot < 4
                 = min(17, slot/2 - 1)     otherwise
offset = position_base[slot] + footer - 2
```

`position_base` table for a 32 KiB window (30 slots):

| Slot | Base | Extra | Slot | Base | Extra |
|------|------|-------|------|------|-------|
| 0 | 0 | 0 | 15 | 192 | 6 |
| 1 | 1 | 0 | 16 | 256 | 7 |
| 2 | 2 | 0 | 17 | 384 | 7 |
| 3 | 3 | 0 | 18 | 512 | 8 |
| 4 | 4 | 1 | 19 | 768 | 8 |
| 5 | 6 | 1 | 20 | 1024 | 9 |
| 6 | 8 | 2 | 21 | 1536 | 9 |
| 7 | 12 | 2 | 22 | 2048 | 10 |
| 8 | 16 | 3 | 23 | 3072 | 10 |
| 9 | 24 | 3 | 24 | 4096 | 11 |
| 10 | 32 | 4 | 25 | 6144 | 11 |
| 11 | 48 | 4 | 26 | 8192 | 12 |
| 12 | 64 | 5 | 27 | 12288 | 12 |
| 13 | 96 | 5 | 28 | 16384 | 13 |
| 14 | 128 | 6 | 29 | 24576 | 13 |

Slots ≥ 30 exist for larger windows (32, 34, 36, 38, 42, 50 slots for 2¹⁶…2²¹).
The real rule is "first slot whose base ≥ window size", which explains values like 42/50
in implementations, despite the old SDK table saying 40/42.

### 6.4 Repeated Offsets R0, R1, R2

- Slots **0, 1, and 2** reuse the three most recent offsets.
- After using R1 or R2, there is a **swap with R0** (not a complete LRU queue — it is a deliberate quirk):

```
slot 0 → offset = R0                    (swap R0↔R0 = no-op)
slot 1 → offset = R1;  swap R0 ↔ R1
slot 2 → offset = R2;  swap R0 ↔ R2
```

- After an **explicit** offset (slot ≥ 3): `R2 = R1; R1 = R0; R0 = offset`.
- Slot 3 (formatted offset 3) corresponds to the real offset **1**.
- R0/R1/R2 values are reset in *uncompressed* blocks (they come written in the header).

### 6.5 Aligned Offset Tree (ALIGNED blocks)

When `extra_bits ≥ 3`, the extra bits are split:

- The **3 least significant bits** come from the **Aligned Offset Tree** (8 symbols, 3 bits of length
  each in the header).
- The remaining bits (`extra_bits - 3`) are read directly from the stream, MSB first.

For `extra_bits = 1` or `2`, the bits are read directly (no aligned tree).
This split improves compression because offsets in x86 code tend to be multiples of 8.

---

## 7. UNCOMPRESSED Blocks

When the data does not compress, the block stores raw bytes:

1. After the 3 type bits and the size, the stream is padded with zeros to a **16-bit** boundary.
2. If it was already aligned, a **complete zero word** is inserted (1–16 bits of padding).
3. **R0, R1, and R2** are written as little-endian DWORDs (12 bytes).
4. The literal bytes of the block follow; if the total number of bytes is odd, 1 zero byte is added.

Example from the specification (compression of `"abc"` with R0=R1=R2=1):

```
Bits/value                                   Hex
16: 0x0014  chunk size = 20 bytes            14 00
 1: 0       E8 disabled                  --
 3: 3       uncompressed block               --
24: 0x000003 block size = 3 bytes            00 30 00  (bits)
 4: 0000    padding for word-align            --
 4 bytes: R0 = 1                             01 00 00 00
 4 bytes: R1 = 1                             01 00 00 00
 4 bytes: R2 = 1                             01 00 00 00
 3 bytes: "abc"                              61 62 63
 1 byte : padding                            00

Sequence: 14 00 00 30 30 00 01 00 00 00 01 00 00 00 01 00 00 00 61 62 63 00
```

---

## 8. E8 Call Translation (x86 Pre-processing)

Objective: improve compression of 32-bit x86 code, where the opcode `0xE8` (relative CALL) is
followed by a 32-bit displacement. Since the displacement changes if the code is relocated, LZX
converts this value to something close to an absolute address — which tends to repeat more.

### 8.1 Activation

In the **first bit of the first frame/chunk** of the stream:

| Bit | Meaning |
|-----|-------------|
| 0 | E8 translation disabled |
| 1 | Enabled; two 16-bit fields follow (high word, low word) with `E8_file_size` |

Translation is automatically disabled after chunk 32,768 (1 GiB of data).
Do not translate occurrences of 0xE8 in the last 6–10 bytes of a frame.

### 8.2 Pre-processing (compression)

Pseudocode per [MS-PATCH]:

```
if chunk_offset < 0x40000000 and chunk_size > 10:
  for i from 0 to chunk_size - 11:
    if chunk_byte[i] == 0xE8:
      current_pointer = chunk_offset + i
      displacement    = LE32(chunk_byte[i+1 .. i+4])
      target          = current_pointer + displacement
      if 0 <= target < E8_file_size + current_pointer:
        if target >= E8_file_size:
          target = displacement - E8_file_size
        write LE32(chunk_byte[i+1..i+4], target)
      i += 4
```

### 8.3 Post-processing (decompression — inverse)

```
value = LE32(chunk_byte[i+1 .. i+4])
if value >= -current_pointer and value < E8_file_size:
  if value >= 0:
    displacement = value - current_pointer
  else:
    displacement = value + E8_file_size
  write LE32(chunk_byte[i+1..i+4], displacement)
```

---

## 9. Persistent State and Frames

| State | Behavior |
|--------|---------------|
| `window[]` (sliding window) | Persists between blocks; offsets can reference previous data |
| R0, R1, R2 | Persist; replaced by written values in uncompressed blocks |
| Tree lengths | Persist between blocks (differential encoding) |
| Reconstructed trees | Each compressed block, from the lengths |
| Reset (CHM) | Every `reset_interval` frames of 32 KiB (2 in the implemented profile): realigns bitstream, **resets all LZX state** (Huffman tables and LZ history), re-reads E8 header |

This complete reset is the main difference between CAB LZX (no reset) and CHM LZX (with reset),
and is what allows **semi-random access** to compressed sections within a CHM.

---

## 10. Decoding Pseudocode (Compressed Blocks)

```
while remaining_block_bytes > 0:
    main = main_tree.decode()

    if main < 256:
        window[pos++] = main
        continue

    m = main - 256
    len_header = m & 7
    slot       = m >> 3

    if len_header == 7:
        length = 9 + length_tree.decode()
    else:
        length = len_header + 2

    if slot < 3:
        offset = [R0,R1,R2][slot]
        swap([R0,R1,R2][slot], R0)
    else:
        extra  = extra_bits(slot)
        base   = position_base[slot] - 2
        if type == ALIGNED and extra >= 3:
            footer = (aligned_tree.decode()) | (read_bits(extra-3) << 3)
        else:
            footer = read_bits(extra)
        offset = base + footer
        R2 = R1; R1 = R0; R0 = offset

    copy `length` bytes from window[pos-offset] to window[pos]
    pos += length
```

---

## 11. Variants by Container

| Container | Window | Reset | E8 | Notes |
|-----------|--------|-------|----|-------------|
| **CAB** | 2¹⁵–2²¹ (`typeCompress`) | no | optional (file size in CFFILE) | Blocks can cross CFDATA; trees persist per folder |
| **CHM** | 2¹⁵ or 2¹⁶ (32/64 KiB) | yes, interval in `ControlData` | usually disabled (HTML) | `ResetTable` stores frame offsets; padded to 0x8000 |
| **WIM** | 2¹⁵–2²¹ | no | yes, with `E8_file_size = 12000000` for WIM files | Uses `IMAGE_RESOURCE` with `WIM_COMPRESSED` |
| **NTFS WOF** | 32 KiB | — | — | "System Compressed" from Windows 10+ |
| **LZX DELTA (LZXD)** | 2¹⁷–2²⁵ | chunked | yes, with 33-bit header | Supports external reference data and long matches |

### 11.1 LZX DELTA Differences

- Stream divided into **chunks** with a 16-bit prefix (chunk size).
- First bit of the first chunk activates E8 and brings `E8_file_size` (2×16 bits).
- Minimum window 128 KiB, maximum 32 MiB.
- Match of length 257 activates extra field → up to 33,024 bytes.
- Offsets can point to **reference data** provided by the caller (in addition to already decoded data).

---

## 12. Main Constants

```c
LZX_MIN_MATCH              = 2
LZX_MAX_MATCH              = 257
LZX_NUM_CHARS              = 256
LZX_NUM_PRIMARY_LENGTHS    = 7      /* lengths 2..8 direct */
LZX_NUM_SECONDARY_LENGTHS  = 249    /* Length Tree symbols */
LZX_PRETREE_NUM_ELEMENTS   = 20
LZX_ALIGNED_NUM_ELEMENTS   = 8
LZX_BLOCKTYPE_VERBATIM     = 1
LZX_BLOCKTYPE_ALIGNED      = 2
LZX_BLOCKTYPE_UNCOMPRESSED = 3
LZX_FRAME_SIZE             = 32768
```

---

## 13. Practical Application in CHM

In a CHM (see `chm.md`):

1. `::DataSpace/Storage/MSCompressed/ControlData` reports `LZXC`, version, `resetInterval`,
   `windowSize` (in 0x8000 blocks in v2), and `windowSize`/cache.
2. `.../Transform/{7FC28940-...}/InstanceData/ResetTable` provides the offsets of each frame
   boundary in the compressed stream and the compressed/decompressed sizes.
3. Every `resetInterval` frames of 32 KiB the LZX state is **reset** — that's why
   a single HTML topic can be extracted without decompressing the entire section.

---

## 14. Profile Implemented in `../chmc`

The compressor included in this repository uses the most restrictive and interoperable profile for CHM:

| Item                         | Adopted value                                                   |
|------------------------------|-----------------------------------------------------------------|
| Window                       | `2^16` = 65536 bytes (`ControlData.windowSize = 2`)             |
| Frame                        | 32768 bytes (`0x8000`), always zero-padded                      |
| `resetInterval`              | 2 frames (0x10000 bytes between resets)                         |
| Reset                        | LZX state zeroed at the start of each 2-frame interval          |
| E8                           | disabled; bit 0 at the start of each interval                   |
| Block                        | VERBATIM (`1`), one per 32 KiB frame                            |
| Trees                        | transmitted in each block; deltas relative to the previous block|
| Pretree                      | fixed: 12 codes of 4 bits + 8 of 5 bits                        |
| Main tree                    | fixed: 16 codes of 8 bits + 480 of 9 bits                      |
| Length tree                  | fixed: 7 codes of 7 bits + 242 of 8 bits                       |
| Aligned offset tree          | absent (only exists in ALIGNED blocks)                          |
| Tokenization                 | matches can cross frames, but never the interval boundary       |
| Alignment                    | `align16()` at the end of each 32 KiB frame                     |
| Bit ordering                 | MSB-first within 16-bit words, written little-endian            |

Some details confirmed with `lzxd.c` are easy to get wrong:

1. Bits are not written as direct bytes. The reader builds 16-bit words in
   little-endian and consumes bits from MSB to LSB; writing bytes MSB-first produces
   `bad block type` already in the first block.
2. `lzxd.c` realigns the input at the end of **each 32 KiB frame**. Without `align16()` per
   frame, the next block starts at a shifted bit and decoding fails with
   `ERR11` / `bad block type`.
3. Each frame starts with its own VERBATIM block and transmits the trees, but the deltas
   are relative to the previous block. The tree and LZ history state is zeroed at the
   start of the reset interval (2 frames).
4. A single 65536-byte block with a 64 KiB window is rejected with `error=11`; that's why
   the section is divided into 32768-byte blocks.
5. After the last data frame, the decoder still processes a 32 KiB frame with
   zero size. The builder appends 4 zero bytes for that final header. Without them,
   `lzxd_decompress()` ends with `MSPACK_ERR_DECRUNCH` even though it produced all the
   expected bytes.
6. `resetOffsets` contains offsets of **interval starts** and a sentinel pointing to
   the end of the compressed stream. Using end-of-interval offsets makes `chmd.c` lose
   synchronization when extracting files positioned after a reset boundary.

### 14.1 Validation

The compressor was tested against `lzxd_init(..., 16, 2, ...)` / `lzxd_decompress()` from libmspack:

- `make test-lzx` generates and decodes 131,072 bytes (4 frames), with `error=0` and
  `decoded=131072 expected=131072`;
- the example CHM is extracted by `test_chm.exe` and `dump_all.exe` without errors;
- `hh.exe` opens the generated CHM, confirming that the LZX profile and the `ResetTable` are accepted
  by the native Windows viewer.

---

## 15. References

- **[MS-PATCH] LZX DELTA Compression and Decompression** (Microsoft Open Specs):
  https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-patch/
- **E8 Call Translation** (MS-PATCH):
  https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-patch/e6c8366d-a8c3-4f34-aa5d-56f44f938a09
- **libmspack — lzxd.c** (Stuart Caie): https://github.com/kyz/libmspack/blob/master/libmspack/mspack/lzxd.c
- **libyal — Compression methods**: https://github.com/libyal/libfwnt/blob/main/documentation/Compression%20methods.asciidoc
- **LZXFMT — Microsoft LZX Data Compression Format** (archived copy)
- **ReactOS `dll/win32/itss/lzx.c`** — decoder used by ITSS.DLL

---

*Document generated on 22/11/2026. Details derived from public specification and reverse engineering;
historical divergences between the CAB SDK spec and the real implementation were recorded when
relevant.*
