# ADR 0006: Note body format

- **Status:** Accepted
- **Date:** 2026-11-02
- **Task:** 3.1

## Context

The editor is a `QTextEdit` on a `QTextDocument` (Plan §5). Phase 3 adds
paragraph styles, lists, checklists, images, tables and, later, collapsed
headings. Every note body must be saved and loaded without losing any of it,
and saving the same note twice must give the same bytes: the round-trip
harness (TC-EDT-001) requires `serialize(deserialize(serialize(doc))) ==
serialize(doc)`, and sync (Phase 9) compares and merges stored bodies.

Phase 2 stored `QTextDocument::toHtml()` in `notes.body_html`.

Notes should also work with Markdown: pasting answers from AI chats, which
copy Markdown, and exporting notes as Markdown (tasks 3.8 and 8.3).

## The spike

A fixture note with every planned feature (`tests/common/EditorFixture.h`):
headings 1 to 3 (one collapsed), bold, italic, underline, strikethrough,
highlight, text color, a link, a monospaced line, bulleted, square ("dashed"),
numbered and nested lists, a checklist with a checked and an open item, an
image (`attachment://<uuid>`, 240 × 120), and a table directly after a list.
Each block was saved and loaded through each candidate and compared.

**Qt HTML** (`toHtml()` / `setHtml()`, Qt 6.10):

- Keeps headings, all inline formats, links, the monospace font, list styles
  and nesting, checklist markers, image name and size, and table structure.
- **Drops our own properties**: the collapsed heading and an attachment id on
  the image (custom `QTextFormat` properties are never written).
- **Drops the code block's no-wrap setting** (`nonBreakableLines`).
- **Adds noise**: an explicit 9 pt font size on every fragment, and a blue
  color on links.
- **Is not stable**: a table directly after a list is written *inside* the
  list's `<ul>`. Loading it puts the table in the list, which indents it
  40 px; each further save adds another 40 px (40, 80, 120, ...). The
  round-trip harness can't pass.

**Markdown** (`toMarkdown()` / `setMarkdown()`, GitHub dialect):

- Keeps headings, lists, checklists, links, code blocks and tables.
- Loses underline, highlight, text color, image size and our properties.
- **Garbles adjacent inline formats**: `**bold ***italic *_underline ~~`
  (spaces end up inside the markers, so they don't parse back).
- Leaks checklist markers onto the next blocks (the image and the paragraph
  after the table came back as checklist items), and adds blank lines.

## Measurements

Release build (`bench_noteformat`, preset `bench`), AMD Ryzen 5 PRO 7540U,
medians. "Prose" is a realistic note (paragraphs of ~150 characters with a
little formatting, a heading every few paragraphs, short lists). "Dense" is the
fixture repeated: ~10 characters per formatted block, a stress case.

| Note | Format | Stored size | Save | Load |
|---|---|---:|---:|---:|
| prose 5 KB | **JSON (ours)** | 8.7 KB | 0.2 ms | 0.2 ms |
| | Qt HTML | 17 KB | 0.1 ms | 0.9 ms |
| | Markdown | 5.6 KB | 0.3 ms | 0.2 ms |
| prose 100 KB | **JSON (ours)** | 169 KB | 4.2 ms | 5.1 ms |
| | Qt HTML | 333 KB | 2.8 ms | 19 ms |
| | Markdown | 109 KB | 7.1 ms | 4.0 ms |
| prose 1 MB | **JSON (ours)** | 1.7 MB | 52 ms | 62 ms |
| | Qt HTML | 3.4 MB | 33 ms | 221 ms |
| | Markdown | 1.1 MB | 72 ms | 56 ms |
| dense 100 KB | **JSON (ours)** | 635 KB | 33 ms | 47 ms |
| | Qt HTML | 2.0 MB | 23 ms | 173 ms |
| | Markdown | 195 KB | 24 ms | 34 ms |

Opening a note must take < 50 ms (Plan §10): JSON opens a 100 KB prose note in
about 5 ms. Loading is mostly Qt building the document; parsing JSON is a
small part (Markdown, which builds the same document, is about as fast).
Saving runs on autosave, off the UI thread's critical path (task 2.9).

## Options

- **A: Qt HTML + a post-processor** adding `data-*` attributes for lost
  properties. Fails on stability: the table drift is a Qt exporter bug, so A
  would need repairs for Qt's output as well as additions to it, and every
  Qt update could change both.
- **B: Our own versioned JSON** for the document's meaning.
- **C: Markdown as storage.** Loses formatting users can apply, and changes a
  note slightly on every save (see the spike).

## Decision

**Option B: our own JSON, `editor/NoteFormat`.**

- The body describes **what the note means**, not how Qt draws it: blocks
  (paragraphs and tables), each with its heading level, list (style and
  indent, shared by its items), checklist state, indent, alignment, no-wrap,
  collapsed and code-block language; and runs of text with bold, italic,
  underline, strikethrough, monospace, highlight, text color and link, or an
  image reference with its display size. Table cells hold paragraphs.
  The exact shape is documented at the top of `NoteFormat.cpp`.
- **No fonts or sizes are stored.** They come from the paragraph styles when a
  note is shown (task 3.2), so changing a style changes every note, and the
  stored body has no per-run noise.
- Keys appear only when they differ from the default, and adjacent runs with
  the same formats are merged: **every document has exactly one
  serialization**, which TC-EDT-001 checks.
- Readers **ignore unknown keys**, so later versions can add properties that
  older apps skip. A change older apps would misread raises the format's
  version, carried in the JSON as `"v"` (absent while it is 1); readers reject
  versions they don't know, and the note's plain text shows instead. Values out of range are clamped, references to missing
  lists are ignored, and malformed JSON fails cleanly (the document stays
  empty and the caller shows the note's plain text instead).
- Stored in `notes.body` (renamed from `body_html` by migration 002).
  **JSON is the only stored format**: the app has no Qt HTML reader. Phase 2
  bodies (Qt HTML of plain-text demo notes) are dropped by migration 003; their
  text is in `body_text`, which the editor shows when a note has no body, and
  the next save writes JSON. `body_text` stays the plain text for the title,
  preview and search.

  *Amended 2026-11-03:* migration 002 had added a `notes.format_version`
  column (1 = Qt HTML, 2 = JSON) and the editor read both. Before release,
  with only demo data stored, one format and one code path were simpler and
  lighter, so migration 003 removed the column and the HTML path; future
  versions use `"v"` in the JSON.
- **Markdown stays at the edges**: pasted Markdown (task 3.8) and Qt's
  Markdown import both produce a `QTextDocument`, which this format stores like
  any other; a pasted AI answer with headings, emphasis, nested lists, a
  checklist, a code block with its language, a table and a link survives
  (`tst_noteformat`). Export (task 8.3) uses `toMarkdown()` and must work
  around its inline-format and marker bugs found above.

## Consequences

- We maintain a small codec (about 500 lines) and extend it with each editor
  feature; every extension gets a fixture case, and the harness proves the
  round trip.
- Bodies are about 1.6 × the plain text for normal notes (half the size of Qt
  HTML), which also keeps sync uploads small.
- Not in the format yet: nested tables, merged cells, and images that aren't
  attachments. Pasted content that uses them is flattened (task 3.8 decides
  how).
- Sync (Phase 9) can compare bodies byte by byte, since equal documents
  serialize equally.

## Verification

- `tst_noteformat`: TC-EDT-001 (exact round trip, also over repeated saves),
  block-by-block equality with the fixture, what Qt HTML loses is kept, bad
  input of many kinds is rejected without crashing, out-of-range values are
  clamped, unknown keys are ignored, newer versions (`"v"`) are rejected, an
  empty or unreadable body shows the plain text, and an AI answer pasted as
  Markdown survives.
- `tst_migrator`: Phase 2 HTML bodies are dropped and their text kept; no
  `format_version` or `body_html` column remains.
- `tst_autosave`: typing saves a JSON body that reads back to the typed text.
- `bench_noteformat`: the table above; prose notes up to 100 KB, and the dense
  5 KB note, must open in < 50 ms.
