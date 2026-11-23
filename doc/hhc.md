# `.hhc` File Format — HTML Help Contents (Table of Contents)

> Part of the **Microsoft HTML Help 1.x** source format family, used to generate
> `.chm` files. See also `chm.md`, `hhk.md`, and `hhp.md`.

---

## 1. What Is It

The `.hhc` file (**HTML Help Contents**) is an **HTML file used as a data source**, not as a
browser page. It defines the topic tree displayed in the **Contents** tab of the
CHM viewer's navigation panel.

- It is one of the so-called **"sitemap files"** — a proprietary Microsoft format (proposed to W3C,
  never standardized) based on HTML with `<OBJECT>`/`<PARAM>`.
- It is **compiled into the `.chm`** by the compiler (`hhc.exe`);
  after compilation, the `.hhc` normally does not need to be distributed.
- Character encoding: usually the system code page (ANSI/DBCS); modern
  versions may use UTF-8 with `<meta charset>`.
- One `.hhc` per project is normal; it is possible to merge tables of contents from other CHMs with the syntax
  `file.chm::/other.hhc`.

---

## 2. General Structure

```html
<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">
<HTML>
<HEAD>
<meta name="GENERATOR" content="Microsoft&reg; HTML Help Workshop 4.1">
<!-- Sitemap 1.0 -->
</HEAD>
<BODY>
  <!-- 1) GLOBAL properties of the table of contents -->
  <OBJECT type="text/site properties">
      <param name="Window Styles" value="0x800627">
      <param name="ImageType" value="Folder">
      <param name="Font" value="Arial,9,0">
  </OBJECT>

  <!-- 2) Entry tree -->
  <UL>
    <LI><OBJECT type="text/sitemap">
        <param name="Name" value="Chapter Title">
        <param name="Local" value="chapter1.htm">
        <param name="ImageNumber" value="1">
    </OBJECT>
      <UL>
        <LI><OBJECT type="text/sitemap">
            <param name="Name" value="Child Topic">
            <param name="Local" value="chapter1\topic.htm">
        </OBJECT></LI>
      </UL>
    </LI>
  </UL>
</BODY>
</HTML>
```

### Elements

| Element | Function |
|----------|--------|
| `<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">` | classic doctype required by the sitemap format |
| `<!-- Sitemap 1.0 -->` | version comment; identifies the format for the compiler |
| `<meta name="GENERATOR" ...>` | generator information (HTML Help Workshop, HelpNDoc, Doxygen, Sphinx, etc.) |
| `<HEAD>` / `<BODY>` | standard HTML structure |
| `<OBJECT type="text/site properties">` | **once**, at the beginning of `<BODY>`: global properties |
| `<UL>` / `<LI>` | hierarchy: a child `<UL>` inside an `<LI>` creates an expandable node |
| `<OBJECT type="text/sitemap">` | **one per entry** in the tree; contains the entry's `<param>` elements |
| `<param name="..." value="...">` | entry attributes (name, file, icon, etc.) |

> **Important:** the hierarchy is purely structural. An `<LI>` that contains a nested `<UL>`
> becomes a "chapter"/folder; an `<LI>` without children is a leaf topic.

---

## 3. Global Properties (`text/site properties`)

Observed canonical order (CHM Spec):

```
FrameName, WindowName, Information Types, ImageList, Image Width, Color Mask,
Background, Foreground, ExWindow Styles, Window Styles, ImageType, Font
```

| Parameter | Example | Meaning |
|-----------|---------|-------------|
| `FrameName` | `left` | HTML frame where topics should open |
| `WindowName` | `main` | Window defined in `[WINDOWS]` of `.hhp` where to open |
| `Information Types` | — | Audience/type metadata (SaveType, Type, TypeDesc, etc.) |
| `ImageList` | `icons.bmp` | BMP with several icons of the same size in a horizontal strip |
| `Image Width` | `16` | Width of each icon in `ImageList` (HHW ignores the value on read and shows 20) |
| `Color Mask` | `0xFF` | Color mask (used by tools like FAR; affects Background/Foreground) |
| `Background` | `0xffffff` | Tree background color (default white) |
| `Foreground` | `0x0` | Tree text color (default black) |
| `ExWindow Styles` | `0x6300` | Win32 extended styles DWORD (`WS_EX_*`) |
| `Window Styles` | `0x800627` | Win32 window styles DWORD (`WS_*`) |
| `ImageType` | `Folder` | `Folder` uses folder icons; without it, uses book/page with `?` |
| `Font` | `Arial,9,0` | Font, size, charset |
| `Auto Generated` | `Yes` | Emitted when HHW generates the table of contents automatically (`Auto TOC` in HHP) |

---

## 4. Entry Parameters (`text/sitemap`)

Canonical order: `Merge`, `Name`, `Type`, `Local`, `URL`, `FrameName`, `WindowName`, `Comment`, `New`, `ImageNumber`.

| Parameter | Required? | Meaning |
|-----------|--------------|-------------|
| `Name` | yes | Text displayed in the Contents tree |
| `Local` | yes* | Relative path to the topic file (e.g., `htm\topic.htm`) |
| `URL` | no | Alternative to `Local` to open an external URL (e.g., `http://...`) |
| `ImageNumber` | no | Entry icon (see table below) |
| `New` | `Yes` | Marks the entry as "new" (adds the *New* suffix to the icon) |
| `Comment` | no | Comment associated with the entry |
| `FrameName` | no | Opens the topic in a specific frame |
| `WindowName` | no | Opens the topic in a specific window |
| `Type` | no | Information type (audience) of the entry |
| `Merge` | no | Reference to the table of contents of another CHM |

\* `Local` or `URL` must be present for the click to go somewhere; entries with only `Name`
function as "chapter" (node without destination).

### 4.1 `ImageNumber` Values (Standard Icons)

1-based numbering (extractable from `hxvz.dll`):

| # | Icon | # | Icon |
|----|-------|----|-------|
| 1 | Closed book | 2 | Open book |
| 3 | Closed book "New" | 4 | Open book "New" |
| 5 | Closed folder | 6 | Open folder |
| 7 | Closed folder "New" | 8 | Open folder "New" |
| 9 | Page with `?` (query) | 10 | Query "New" |
| 11 | Text | 12 | Text "New" |
| 13 | Web document | 14 | Web document "New" |
| 15 | Web link | 16 | Web link "New" |
| 17 | Info | 18 | Info "New" |
| 19+ | Link, Booklet, E-mail, Image, Audio, Video, Index, Idea, Note, Tool... | | |

When `ImageType=Folder` is set globally, folders/books use folder icons.

### 4.2 Paths and Anchors

- Paths are **relative to the project directory** (`.hhp`), using `\` or `/`.
- HTML anchors are allowed: `Local="topic.htm#section2"`.
- External links: `Local="http://www.example.com/"` or via `URL`.
- Merge another CHM: `Merge="other.chm::/index.hhc"`.

---

## 5. Compilation Rules / Compatibility

The HTML Help compiler 4.74.8702 is sensitive to formatting:

1. `<LI>` and `<OBJECT>` must be **on the same line**.
2. Each `<param ...>` must be on **a separate line**.
3. `<OBJECT>` of an entry must be closed before the child `<UL>`.
4. Attribute values must have `&`, `<`, `>`, and `"` escaped (`&amp;`, `&lt;`, `&gt;`, `&quot;`).
5. Very long lines are problematic in HHW (the `.hhp` has a 1024 character limit;
   similar caution applies here).

Automatic generators (Doxygen, Sphinx, HelpNDoc, FAR HTML) follow these conventions.

---

## 6. Complete Example (with Hierarchy and Icons)

```html
<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">
<HTML>
<HEAD>
<meta name="GENERATOR" content="Microsoft&reg; HTML Help Workshop 4.1">
<!-- Sitemap 1.0 -->
</HEAD><BODY>
<OBJECT type="text/site properties">
    <param name="Window Styles" value="0x800627">
    <param name="ImageType" value="Folder">
    <param name="Font" value="Tahoma,9,0">
</OBJECT>
<UL>
  <LI><OBJECT type="text/sitemap">
      <param name="Name" value="User Guide">
      <param name="Local" value="index.htm">
      <param name="ImageNumber" value="5">
  </OBJECT>
    <UL>
      <LI><OBJECT type="text/sitemap">
          <param name="Name" value="Installation">
          <param name="Local" value="installation.htm">
          <param name="ImageNumber" value="11">
      </OBJECT></LI>
      <LI><OBJECT type="text/sitemap">
          <param name="Name" value="Getting Started">
          <param name="Local" value="start.htm#steps">
          <param name="ImageNumber" value="11">
          <param name="New" value="Yes">
      </OBJECT></LI>
    </UL>
  </LI>
  <LI><OBJECT type="text/sitemap">
      <param name="Name" value="Online Reference">
      <param name="URL" value="https://example.com/docs">
      <param name="ImageNumber" value="15">
  </OBJECT></LI>
</UL>
</BODY></HTML>
```

---

## 7. Notes

- A common browser **does not render** the `<param>` elements — that's why a `.hhc` opened directly
  shows only bullet points. It is a data format, not a page.
- `Binary TOC=Yes` in the `.hhp` generates a binary representation of the table of contents in the CHM (necessary for
  certain features, but incompatible with merging and custom icons).
- The `.hhc` is referenced by the `Contents file=` key in `[OPTIONS]` of the `.hhp`.

---

## 8. References

- CHM Spec — Sitemap formats: https://chmspec.nongnu.org/latest/Sitemap.html
- Microsoft Learn — Create a Table of Contents File
- Microsoft HTML Help Workshop 4.1 — online documentation (table of contents, index, project)
- Helpware / FAR HTML — HHC item properties
