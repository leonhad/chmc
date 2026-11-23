# `.hhk` File Format — HTML Help Keyword Index (Index)

> Part of the **Microsoft HTML Help 1.x** source format family, used to generate
> `.chm` files. See also `chm.md`, `hhc.md`, and `hhp.md`.

---

## 1. What Is It

The `.hhk` file (**HTML Help Keyword Index**) is the source file that defines the entries in the
**Index** tab of the CHM viewer. It is a list of keywords, each pointing to
an HTML topic.

- Like the `.hhc`, it is a **sitemap file**: HTML used as a data container
  (`<OBJECT type="text/sitemap">` + `<PARAM>`).
- It is compiled into the `.chm` by `hhc.exe` and referenced by `Index file=` in the `.hhp`.
- Unlike the `.hhc`, the `.hhk` is a **flat list** — there is no hierarchy via nested `<UL>`;
  alphabetical ordering is done by the viewer.
- Encoding: traditionally the system ANSI/DBCS; UTF-8 is used by modern generators.

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
  <!-- Global properties (optional) -->
  <OBJECT type="text/site properties">
      <param name="FrameName" value="main">
      <param name="WindowName" value="main">
      <param name="Font" value="Arial,9,0">
  </OBJECT>

  <!-- Index entries -->
  <UL>
    <LI><OBJECT type="text/sitemap">
        <param name="Name" value="Installation">
        <param name="Local" value="installation.htm">
    </OBJECT></LI>
    <LI><OBJECT type="text/sitemap">
        <param name="Name" value="Installation">
        <param name="Name" value="System Requirements">
        <param name="Local" value="installation.htm#requirements">
    </OBJECT></LI>
    <LI><OBJECT type="text/sitemap">
        <param name="Name" value="Keyboard Shortcuts">
        <param name="Local" value="shortcuts.htm">
    </OBJECT></LI>
  </UL>
</BODY>
</HTML>
```

### Elements

| Element | Function |
|----------|--------|
| `<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">` | doctype required by the sitemap format |
| `<!-- Sitemap 1.0 -->` | marks the format version |
| `<meta name="GENERATOR" ...>` | identifies who generated the file |
| `<OBJECT type="text/site properties">` | global properties of the index (optional) |
| `<UL>` | single list containing all entries |
| `<LI>` | one index entry |
| `<OBJECT type="text/sitemap">` | entry data |
| `<param name="Name" ...>` | keyword (can appear more than once) |
| `<param name="Local" ...>` | destination topic |

---

## 3. Global Properties (`text/site properties`)

The canonical order according to the CHM Spec is:

```
FrameName, WindowName, Font
```

| Parameter | Example | Meaning |
|-----------|---------|-------------|
| `FrameName` | `main` | HTML frame where the topic should open |
| `WindowName` | `main` | Window defined in `[WINDOWS]` of `.hhp` |
| `Font` | `Tahoma,9,0` | Font used in the index panel (name, size, charset) |

---

## 4. Entry Parameters (`text/sitemap`)

| Parameter | Required? | Meaning |
|-----------|--------------|-------------|
| `Name` | yes | Keyword displayed in the Index tab |
| `Local` | yes | Relative path to the destination HTML topic |
| `URL` | no | External alternative (e.g., `https://...`) |
| `FrameName` | no | Destination frame |
| `WindowName` | no | Destination window |
| `Comment` | no | Associated comment |
| `ImageNumber` | no | Entry icon (same numbering as `.hhc`) |

### 4.1 Multiple `Name` Parameters

An `<OBJECT>` can contain **more than one `Name`**. The first is the main keyword
(displayed in the left column); the following ones function as **sub-entries** displayed indented
below the keyword, pointing to the same `Local`.

```html
<LI><OBJECT type="text/sitemap">
    <param name="Name" value="Window">
    <param name="Name" value="Resize Window">
    <param name="Name" value="Close Window">
    <param name="Local" value="windows.htm">
</OBJECT></LI>
```

Entries with the same keyword are **automatically merged** by the viewer; the order
of appearance in the file is not preserved — the index is sorted alphabetically
(case-insensitive) at compile/display time.

### 4.2 Paths and Anchors

- Paths relative to the project directory (`.hhp`), with `\` or `/`.
- Anchors: `Local="options.htm#language"`.
- External destination: `Local="https://example.com/faq"` or `URL="..."`.

---

## 5. Formatting Rules

The same restrictions from the HTML Help compiler 4.74.8702 apply:

1. `<LI>` and `<OBJECT>` on the **same line**.
2. Each `<param ...>` on a **separate line**.
3. Escape `&`, `<`, `>`, and `"` in values.
4. Do not use nested `<UL>` — the index is flat.

---

## 6. Complete Example

```html
<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">
<HTML>
<HEAD>
<meta name="GENERATOR" content="Microsoft&reg; HTML Help Workshop 4.1">
<!-- Sitemap 1.0 -->
</HEAD><BODY>
<OBJECT type="text/site properties">
    <param name="WindowName" value="main">
    <param name="Font" value="Tahoma,9,0">
</OBJECT>
<UL>
  <LI><OBJECT type="text/sitemap">
      <param name="Name" value="Backup">
      <param name="Name" value="Restore Backup">
      <param name="Local" value="backup.htm">
  </OBJECT></LI>
  <LI><OBJECT type="text/sitemap">
      <param name="Name" value="Configuration">
      <param name="Name" value="Language">
      <param name="Local" value="config.htm#language">
  </OBJECT></LI>
  <LI><OBJECT type="text/sitemap">
      <param name="Name" value="Configuration">
      <param name="Name" value="Password">
      <param name="Local" value="config.htm#password">
      <param name="ImageNumber" value="19">
  </OBJECT></LI>
  <LI><OBJECT type="text/sitemap">
      <param name="Name" value="Technical Support">
      <param name="URL" value="mailto:support@example.com">
  </OBJECT></LI>
</UL>
</BODY></HTML>
```

---

## 7. Relationship with `Auto Index` and `Binary Index`

- **`Auto Index=Yes`** in the `.hhp` makes the compiler automatically collect keywords contained
  in the HTML pages (via the HTML Help ActiveX control / metatags) and add them to the index.
- **`Binary Index=Yes`** generates a binary representation of the index inside the CHM; it is the default and
  necessary for some features, but can affect CHM merging.
- The **Search** tab (full-text search) is independent of the `.hhk` — it indexes the full text of the
  pages, controlled by `Full-text search=Yes` in the `.hhp`.

---

## 8. Notes

- Like the `.hhc`, the `.hhk` is a data format: opened directly in a browser, it displays only
  bullet points, not a formatted list.
- Tools such as HelpNDoc, FAR HTML, Doxygen, and Sphinx generate `.hhk` automatically from
  annotations in the source code or from the pages.
- Duplicate keywords with different destinations are allowed; the viewer groups the
  entries under the same term.

---

## 9. References

- CHM Spec — Sitemap formats (section 5.7.3, HHK): https://chmspec.nongnu.org/latest/Sitemap.html
- Microsoft Learn — Create an Index File (HTML Help 1.x)
- Microsoft HTML Help Workshop 4.1 — online documentation
- Helpware / FAR HTML — index item properties
