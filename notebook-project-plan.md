# Project Plan: Secure Notes App for Ubuntu

> **Working name:** `NotesApp` (replace with your own brand name)
> **Goal:** A native Ubuntu notes app that matches the macOS Notes app in layout, workflow and features, while being fast, small, and secure by default.
> **Stack:** C++20 · Qt 6 Widgets · SQLCipher · libsodium · QtKeychain · Google Drive (appDataFolder)

---

## Table of Contents

1. [Vision & Principles](#1-vision--principles)
2. [Design Rules (What to Copy, What Not To)](#2-design-rules-what-to-copy-what-not-to)
3. [UI Specification](#3-ui-specification)
4. [Feature List (macOS Notes Parity)](#4-feature-list-macos-notes-parity)
5. [Technical Stack](#5-technical-stack)
6. [Architecture](#6-architecture)
7. [Database Design](#7-database-design)
8. [Security Design](#8-security-design)
9. [Google Sync & Recovery](#9-google-sync--recovery)
10. [Performance & Size Budgets](#10-performance--size-budgets)
11. [Project Structure](#11-project-structure)
12. [Milestones & Timeline](#12-milestones--timeline)
13. [Testing & Quality](#13-testing--quality)
14. [Packaging & Release](#14-packaging--release)
15. [Risks & Mitigations](#15-risks--mitigations)
16. [Definition of Done (v1.0)](#16-definition-of-done-v10)

---

## 1. Vision & Principles

**Vision:** The notes app Ubuntu users wish they had: as simple as macOS Notes, private enough for passwords and personal records, and fast enough that it never feels like it loads.

**Principles**

| Principle | Meaning in practice |
|---|---|
| **Familiar** | Anyone who has used macOS Notes knows how to use it in 10 seconds. |
| **Private by default** | Every note is encrypted on disk and before upload. No setting to turn it on; it is always on. |
| **Instant** | Start, open, type, and search without visible delay. |
| **Small** | A tiny `.deb`, low RAM, no bundled browser engine. |
| **Recoverable** | Users can restore everything on a new machine with Google sign-in + their recovery key. |

---

## 2. Design Rules (What to Copy, What Not To)

The goal is **the same layout, workflow and features** as macOS Notes. Copy the *behavior*; create your *own* visual identity.

**✅ Copy freely (functionality and layout)**
- Three-pane layout: folders → note list → editor
- Toolbar actions and their order
- Keyboard shortcuts and behaviors (e.g., first line becomes the title)
- Features: checklists, tables, pinning, locking, gallery view, search, etc.

**❌ Do not copy (Apple's protected assets)**
- The name "Notes" with Apple branding, Apple logos, or the Notes app icon
- **SF Pro font** (its license only permits use on Apple platforms) → use **Inter** instead
- **SF Symbols** icons → use **Lucide** or **Phosphor** (open-source, MIT-licensed)
- Apple's exact yellow color, textures, and screenshots → choose your own accent color

---

## 3. UI Specification

### 3.1 Main Window Layout

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ ● ● ●   [≡]  [▦ ☰]  [🗑]      [Aa] [☑] [⊞] [📎] [🔒] [⇪]        [🔍 Search  ] │  ← Toolbar
├──────────────┬────────────────────────┬──────────────────────────────────────┤
│ iCloud-like  │ Pinned                 │ October 28, 2026 at 9:41 PM          │
│ ─ Account ─  │ ┌────────────────────┐ │                                      │
│ 📁 All Notes │ │ Shopping List   📌 │ │ Shopping List                        │  ← Title (1st line)
│ 📁 Notes     │ │ 9:41 PM  Milk, eggs│ │                                      │
│ 📁 Work      │ └────────────────────┘ │ ☑ Milk                               │
│ 📁 Personal  │ Today                  │ ☐ Eggs                               │
│ 📁 Passwords │ ┌────────────────────┐ │ ☐ Bread                              │
│              │ │ 🔒 Bank Accounts   │ │                                      │
│ ─ Tags ─     │ │ 8:15 PM  Locked    │ │                                      │
│ #work #home  │ └────────────────────┘ │                                      │
│              │ Previous 7 Days        │                                      │
│ 🗑 Recently  │ ...                    │                                      │
│   Deleted    │                        │                                      │
│ [+ Folder]   │                        │                                      │
├──────────────┴────────────────────────┴──────────────────────────────────────┤
  Sidebar (220px)   Note List (300px)         Editor (flexible)
```

### 3.2 Toolbar (left to right)

| Button | Action | Shortcut |
|---|---|---|
| Sidebar toggle | Show/hide folder sidebar | `Ctrl+Alt+S` |
| List / Gallery | Switch note-list view | `Ctrl+1` / `Ctrl+2` |
| Delete | Move note to Recently Deleted | `Delete` |
| New Note | Create note in current folder | `Ctrl+N` |
| **Aa** Format | Title / Heading / Subheading / Body / Monospaced / Lists | — |
| Checklist | Insert/toggle checklist | `Ctrl+Shift+L` |
| Table | Insert table | `Ctrl+Alt+T` |
| Attachment | Add image / file | `Ctrl+Shift+A` |
| Lock | Lock / unlock note | — |
| Share / Export | Export as PDF, Markdown, plain text | — |
| Search | Search all notes | `Ctrl+F` (note) / `Ctrl+Alt+F` (all) |

### 3.3 Sidebar

- Account section header (shows the Google account when sync is on, otherwise "On My Computer")
- **All Notes**, **Notes** (default folder), user folders, nested subfolders
- **Smart Folders** (filters by tag, date, checklist, attachments, locked)
- **Tags** browser
- **Recently Deleted** (auto-purge after 30 days)
- Folder count badges; drag & drop notes onto folders; right-click → Rename / Delete / New Subfolder

### 3.4 Note List

- Grouped by date: **Pinned**, Today, Yesterday, Previous 7 Days, Previous 30 Days, months, years
- Each row: **title** (bold), time + first line of preview, thumbnail if note has an image
- Locked notes show 🔒 and "Locked" instead of a preview (title stays visible, like macOS)
- Sort options: Date Edited / Date Created / Title
- **Gallery view:** grid of note cards with previews
- Right-click: Pin, Lock, Move to, Duplicate, Delete

### 3.5 Editor

- First line automatically styled as **Title** and used as the note name
- Date/time header at the top (centered, light gray)
- Paragraph styles: Title, Heading, Subheading, Body, Monospaced
- Lists: bulleted, dashed, numbered, checklist (checked items can auto-move to bottom)
- Inline: bold, italic, underline, strikethrough, highlight, text color, links
- Tables with add/remove rows & columns
- Images inline (drag & drop or paste), file attachments as cards
- Collapsible headings (click arrow to fold a section)
- Links to other notes (`>>` typing to search a note title)
- Smooth typing with autosave (debounced 500 ms, no "Save" button)

### 3.6 Visual Style

- Light and dark themes following the Ubuntu system setting
- Font: **Inter** (UI) + **JetBrains Mono** (monospaced blocks)
- Your own accent color (e.g., amber `#F2A900` or something unique to your brand)
- Rounded selection highlights, subtle separators, generous whitespace
- Animations limited to 150 ms fades (fast, never in the way)

---

## 4. Feature List (macOS Notes Parity)

Priority: **P0** = MVP, **P1** = v1.0, **P2** = after v1.0

### Organization
| Feature | Priority |
|---|---|
| Folders + nested subfolders | P0 |
| All Notes view | P0 |
| Pin notes to top | P0 |
| Recently Deleted (30-day recovery) | P0 |
| Move notes via drag & drop | P0 |
| Sort by edited / created / title | P0 |
| Tags (`#tag` in note text) | P1 |
| Smart Folders | P1 |
| Gallery view | P1 |

### Editing
| Feature | Priority |
|---|---|
| Rich text: bold, italic, underline, strikethrough | P0 |
| Title / Heading / Subheading / Body / Monospaced | P0 |
| Bulleted, dashed, numbered lists | P0 |
| Checklists | P0 |
| Autosave | P0 |
| Undo / redo | P0 |
| Images (paste, drag & drop) | P0 |
| Tables | P1 |
| File attachments | P1 |
| Highlight & text colors | P1 |
| Links to other notes | P1 |
| Collapsible sections | P1 |
| Find & replace in note | P1 |
| Sketch / drawing | P2 |
| Audio recording + transcription | P2 |
| Math results (type `25*4=` → shows result) | P2 |

### Search
| Feature | Priority |
|---|---|
| Instant full-text search across all notes | P0 |
| Search highlights in results | P1 |
| Search inside attachments' file names | P1 |

### Security
| Feature | Priority |
|---|---|
| Encrypted database (always on) | P0 |
| Lock individual notes with password | P0 |
| Auto-relock after inactivity | P0 |
| Lock all notes now (`Ctrl+Shift+Alt+L`) | P1 |
| Change notes password | P1 |
| Clipboard auto-clear for copied content from locked notes | P1 |

### Sync & Backup
| Feature | Priority |
|---|---|
| Sign in with Google | P1 |
| End-to-end encrypted sync via Google Drive | P1 |
| Recovery key (24 words) | P1 |
| Restore on new device | P1 |
| Conflict handling | P1 |
| Export all notes (Markdown / PDF) | P1 |
| Import from Markdown / plain text | P2 |

### System Integration
| Feature | Priority |
|---|---|
| Dark / light mode | P0 |
| Keyboard shortcuts | P0 |
| Quick Note (global shortcut opens a small note window) | P2 |
| Print / export to PDF | P1 |

---

## 5. Technical Stack

| Layer | Choice | Why |
|---|---|---|
| Language | **C++20** | Native speed, small binary |
| UI framework | **Qt 6 Widgets** | Mature, fast startup, lighter than QML |
| Rich text editor | **QTextDocument / QTextEdit** (extended) | Built-in tables, lists, images, undo |
| Database | **SQLCipher** (SQLite + AES-256) | Whole-database encryption |
| Full-text search | **SQLite FTS5** | Millisecond search across 10,000+ notes |
| Cryptography | **libsodium** | Argon2id, XChaCha20-Poly1305, secure memory |
| Key storage | **QtKeychain** → GNOME Keyring (libsecret) | OS-protected storage for the device key |
| Networking / OAuth | **Qt Network + Qt Network Authorization** | OAuth2 with PKCE, HTTPS |
| Cloud | **Google Drive API v3, `appDataFolder`** | Hidden, app-only storage |
| Build | **CMake + Ninja** | Standard, fast |
| Tests | **Qt Test** or **GoogleTest** | Unit + integration tests |
| Static analysis | **clang-tidy, cppcheck** | Catch bugs early |
| Runtime checks | **AddressSanitizer, UBSan** | Catch memory bugs in debug builds |
| Packaging | **.deb** + **Flatpak** | Small size + sandboxing |

> **Qt license note:** Use Qt under **LGPL v3** and link Qt **dynamically** (the default). This allows a closed or open-source app without buying a commercial license.

---

## 6. Architecture

```
┌───────────────────────────────────────────────────────────────┐
│                         UI Layer (Qt Widgets)                  │
│   MainWindow · Sidebar · NoteListView · EditorView · Dialogs   │
└──────────────────────────────┬────────────────────────────────┘
                               │ signals / slots
┌──────────────────────────────▼────────────────────────────────┐
│                      Application Layer                         │
│   NoteService · FolderService · SearchService · LockService    │
└───────┬───────────────────────┬───────────────────────┬───────┘
        │                       │                       │
┌───────▼────────┐   ┌──────────▼─────────┐   ┌─────────▼────────┐
│  Storage Layer │   │   Crypto Layer     │   │    Sync Layer    │
│  SQLCipher DB  │   │  libsodium wrapper │   │  SyncEngine      │
│  FTS5 index    │   │  KeyManager        │   │  DriveClient     │
│  Repositories  │   │  QtKeychain        │   │  OAuth (PKCE)    │
└────────────────┘   └────────────────────┘   └──────────────────┘
```

**Rules**
- UI never touches the database or crypto directly; it only calls services.
- All crypto lives in one module (`src/crypto/`) so it can be reviewed and tested in isolation.
- Sync runs on a background thread (`QThread`) so typing never stutters.
- Model/View: note list uses `QAbstractListModel` with lazy loading for large note counts.

---

## 7. Database Design

**Note body format:** store Qt's rich-text HTML (`QTextDocument::toHtml()`) plus a plain-text copy for search. Images and files are stored in the `attachments` table (inside the encrypted DB), not as loose files.

```sql
-- Folders (nested via parent_id)
CREATE TABLE folders (
    id           INTEGER PRIMARY KEY,
    uuid         TEXT UNIQUE NOT NULL,
    parent_id    INTEGER REFERENCES folders(id),
    name         TEXT NOT NULL,
    sort_order   INTEGER DEFAULT 0,
    created_at   INTEGER NOT NULL,
    updated_at   INTEGER NOT NULL,
    deleted_at   INTEGER,                 -- tombstone for sync
    version      INTEGER DEFAULT 1,
    sync_state   INTEGER DEFAULT 0        -- 0 = dirty, 1 = synced
);

-- Notes
CREATE TABLE notes (
    id            INTEGER PRIMARY KEY,
    uuid          TEXT UNIQUE NOT NULL,
    folder_id     INTEGER REFERENCES folders(id),
    title         TEXT NOT NULL DEFAULT '',
    snippet       TEXT,                   -- preview line (NULL if locked)
    body_html     TEXT,                   -- NULL if locked
    body_text     TEXT,                   -- for FTS (NULL if locked)
    is_pinned     INTEGER DEFAULT 0,
    is_locked     INTEGER DEFAULT 0,
    locked_blob   BLOB,                   -- encrypted body when locked
    locked_nonce  BLOB,
    wrapped_key   BLOB,                   -- per-note key, wrapped by lock key
    created_at    INTEGER NOT NULL,
    updated_at    INTEGER NOT NULL,
    trashed_at    INTEGER,                -- Recently Deleted
    deleted_at    INTEGER,                -- permanent tombstone for sync
    version       INTEGER DEFAULT 1,
    sync_state    INTEGER DEFAULT 0
);

-- Attachments (images, files)
CREATE TABLE attachments (
    id          INTEGER PRIMARY KEY,
    uuid        TEXT UNIQUE NOT NULL,
    note_id     INTEGER REFERENCES notes(id) ON DELETE CASCADE,
    file_name   TEXT,
    mime_type   TEXT,
    size_bytes  INTEGER,
    sha256      TEXT,
    data        BLOB,                     -- encrypted separately if note is locked
    thumbnail   BLOB,
    created_at  INTEGER NOT NULL
);

-- Tags
CREATE TABLE tags      (id INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL);
CREATE TABLE note_tags (note_id INTEGER, tag_id INTEGER, PRIMARY KEY (note_id, tag_id));

-- Full-text search (locked notes: title only)
CREATE VIRTUAL TABLE notes_fts USING fts5(
    title, body_text, content='notes', content_rowid='id'
);

-- App settings & sync cursor
CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT);

CREATE INDEX idx_notes_folder  ON notes(folder_id, updated_at DESC);
CREATE INDEX idx_notes_pinned  ON notes(is_pinned, updated_at DESC);
CREATE INDEX idx_notes_sync    ON notes(sync_state);
```

**Migrations:** store `schema_version` in `meta`; run numbered migration scripts at startup.

---

## 8. Security Design

### 8.1 Threat Model

| Threat | Protected? | How |
|---|---|---|
| Someone copies the database file | ✅ | SQLCipher AES-256; key is not in the file |
| Stolen laptop, disk removed | ✅ | DB encrypted; key in keyring, which is tied to the user's login |
| Another user on the same PC | ✅ | DB encrypted + file permissions `0600` |
| Google / anyone reading synced data | ✅ | End-to-end encryption before upload |
| Someone opens the app on an unlocked PC | ✅ for locked notes | Locked notes need the notes password |
| Brute-forcing the notes password | ✅ mitigated | Argon2id with high memory cost + attempt delays |
| Malware running as the same user while logged in | ⚠️ partial | Flatpak sandbox, locked notes, auto-relock. No app can fully defend here. |

### 8.2 Key Hierarchy

```
                    ┌──────────────────────────────┐
                    │  Account Master Key (AMK)    │  random 32 bytes
                    │  encrypts synced data        │
                    └──────┬────────────────┬──────┘
             wrapped by    │                │  wrapped by
     ┌─────────────────────▼──┐      ┌──────▼──────────────────┐
     │ Sync Password          │      │ Recovery Key (24 words) │
     │ → Argon2id → KEK       │      │ → KEK                   │
     └────────────────────────┘      └─────────────────────────┘

     ┌──────────────────────────────┐
     │ Device Key (DK)              │  random 32 bytes, stored in GNOME Keyring
     │ → opens local SQLCipher DB   │
     └──────────────────────────────┘

     ┌──────────────────────────────┐
     │ Notes Password               │  set by user, never stored
     │ → Argon2id → Lock Key        │
     │ → unwraps each locked note's │
     │   random per-note key        │
     └──────────────────────────────┘
```

**Why wrap keys instead of encrypting directly with passwords?** Users can change their password without re-encrypting every note; only the small wrapped key is updated.

### 8.3 Layer 1: Encrypted Local Database
1. First launch: generate DK with `randombytes_buf()`.
2. Store DK in GNOME Keyring via QtKeychain.
3. Open DB with `PRAGMA key` (SQLCipher, AES-256, 256,000 PBKDF2 iterations default).
4. DB file permissions: `chmod 600`.
5. If keyring is unavailable, ask the user for a startup password instead (fallback mode).

### 8.4 Layer 2: Locked Notes
1. User sets one **notes password** (same model as macOS Notes).
2. Derive Lock Key: `crypto_pwhash` (Argon2id, `OPSLIMIT_MODERATE`, `MEMLIMIT_MODERATE`, random 16-byte salt stored in `meta`).
3. Each locked note gets a random note key; content encrypted with `crypto_aead_xchacha20poly1305_ietf_encrypt`.
4. Note key wrapped by Lock Key and stored in `wrapped_key`.
5. Store a verifier (encrypted known value) to check the password without storing it.
6. When locked: `body_html`, `body_text`, `snippet` are NULL; FTS indexes title only.
7. Unlocked content kept in memory only, cleared with `sodium_memzero` on relock.
8. Auto-relock after 5 minutes idle (configurable), on screen lock, and on app close.
9. Failed attempts: exponential delay (1s, 2s, 4s…).
10. Optional password hint (like macOS).

### 8.5 Memory & Runtime Hygiene
- Keys held in `sodium_malloc` buffers (guarded, `mlock`ed, not swapped to disk)
- Wipe plaintext buffers after use
- Disable crash dumps containing memory (`prctl(PR_SET_DUMPABLE, 0)`)
- Clear clipboard 30 s after copying from a locked note
- No logging of note content, ever

### 8.6 Rules for the Crypto Code
- **Never write your own crypto algorithms.** Only call libsodium.
- One `crypto/` module with a small, reviewed API.
- Unit tests with known test vectors.
- Get an external security review before v1.0 if possible.

---

## 9. Google Sync & Recovery

### 9.1 Google Setup
1. Create a project in **Google Cloud Console**.
2. Enable **Google Drive API**.
3. OAuth client type: **Desktop app**.
4. Scope: `https://www.googleapis.com/auth/drive.appdata` (app's hidden folder only; cannot see user's other files).
5. Before public release, complete Google's **OAuth app verification** (privacy policy page required).

### 9.2 Sign-In Flow
```
User clicks "Sign in with Google"
   → App opens system browser (OAuth2 + PKCE, loopback redirect on 127.0.0.1)
   → User approves
   → App receives code → exchanges for access + refresh token
   → Refresh token stored in GNOME Keyring (never in the DB or config files)
```

### 9.3 First-Time Sync Setup
1. User chooses a **sync password**.
2. App generates AMK and a **24-word recovery key**; shows it once with "Print / Save / I've written it down".
3. Upload `keys.bin` = AMK wrapped by sync password + AMK wrapped by recovery key.
4. Begin uploading notes.

### 9.4 Remote File Layout (inside appDataFolder)
```
appDataFolder/
├── keys.bin                    # wrapped AMK (no plaintext keys)
├── folders/<uuid>.bin          # encrypted folder records
├── notes/<uuid>.bin            # encrypted note records
└── attachments/<uuid>.bin      # encrypted attachments
```
Each `.bin` = `version | nonce | XChaCha20-Poly1305(ciphertext)`, encrypted with a per-item subkey derived from AMK (`crypto_kdf_derive_from_key`). File names are UUIDs only, so no titles leak.

### 9.5 Sync Algorithm
1. **Push:** find rows with `sync_state = 0`, encrypt, upload/update file, set `sync_state = 1`.
2. **Pull:** use Drive `changes.list` with a saved `pageToken` (stored in `meta`) to fetch only changed files.
3. **Merge:**
   - Remote version > local and local not dirty → apply remote.
   - Both changed → keep the newer one, save the other as a **"Conflicted copy"** note (no data lost).
4. **Deletes:** tombstones (`deleted_at`) synced, purged after 30 days.
5. **Triggers:** on app start, 5 s after an edit stops, every 5 minutes, and on network reconnect.
6. **Offline:** app works fully offline; sync catches up later.
7. **Backoff:** retry with exponential backoff on network/HTTP 429/5xx errors.

### 9.6 Restore on a New Device
```
Install app → Sign in with Google → App finds keys.bin
   → "Enter your sync password" (or "Use recovery key")
   → AMK unlocked → download & decrypt all notes → done
```

> ⚠️ **Must be shown clearly to users:** If they lose **both** the sync password and the recovery key, their synced data cannot be recovered by anyone, including the developer. This is what makes it secure.

---

## 10. Performance & Size Budgets

| Metric | Target |
|---|---|
| Cold start to usable window | **< 500 ms** |
| Open a note | **< 50 ms** |
| Search across 10,000 notes | **< 30 ms** |
| Typing latency | **< 16 ms** (one frame) |
| Idle RAM | **< 80 MB** |
| `.deb` package size | **< 5 MB** (uses system Qt) |
| Binary size (stripped) | **< 3 MB** |

**Techniques**
- Load only note titles/snippets for the list; full body on click
- Virtualized list (`QListView` with uniform item sizes)
- Generate image thumbnails once, store in DB
- Debounced autosave (500 ms) in a background thread
- SQLite WAL mode + prepared statements
- Build with `-O2`, LTO, `strip`; link Qt dynamically
- Profile with `perf`, `heaptrack`, and Qt Creator's profiler each milestone

---

## 11. Project Structure

```
notesapp/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── cmake/                       # find modules, compiler flags
├── resources/
│   ├── icons/                   # Lucide/Phosphor SVGs
│   ├── fonts/                   # Inter, JetBrains Mono
│   ├── themes/                  # light.qss, dark.qss
│   └── resources.qrc
├── src/
│   ├── main.cpp
│   ├── app/                     # Application, settings, shortcuts
│   ├── ui/
│   │   ├── MainWindow.{h,cpp}
│   │   ├── Sidebar.{h,cpp}
│   │   ├── NoteListView.{h,cpp}
│   │   ├── NoteListDelegate.{h,cpp}
│   │   ├── GalleryView.{h,cpp}
│   │   ├── EditorView.{h,cpp}
│   │   ├── FormatMenu.{h,cpp}
│   │   └── dialogs/             # LockDialog, SyncSetupDialog, RecoveryKeyDialog
│   ├── editor/                  # checklist, table, image, collapsible-section logic
│   ├── models/                  # Note, Folder, Attachment, Tag + Qt models
│   ├── services/                # NoteService, FolderService, SearchService, LockService
│   ├── storage/                 # Database, migrations, repositories
│   ├── crypto/                  # Sodium wrapper, KeyManager, SecureBuffer
│   └── sync/                    # SyncEngine, DriveClient, OAuth, ConflictResolver
├── tests/
│   ├── unit/                    # crypto, storage, services
│   └── integration/             # sync against mock Drive server
├── packaging/
│   ├── debian/
│   └── flatpak/                 # com.yourname.NotesApp.yml
└── docs/
    ├── architecture.md
    ├── security.md
    └── privacy-policy.md        # required for Google verification
```

---

## 12. Milestones & Timeline

Estimates assume **one developer learning C++/Qt** while building. Experienced Qt developers can roughly halve these.

| Phase | Weeks | Deliverable |
|---|---|---|
| **0. Learning & setup** | 1–3 | C++20 basics, Qt Widgets tutorial, CMake project builds on Ubuntu, CI running |
| **1. Skeleton UI** | 4–6 | Three-pane window, toolbar, sidebar, note list, plain editor, light/dark theme |
| **2. Storage (MVP)** | 7–9 | SQLite DB, folders, notes CRUD, autosave, pin, Recently Deleted, sorting |
| **3. Rich editor** | 10–15 | Paragraph styles, lists, checklists, inline formatting, images, undo/redo |
| **4. Encryption** | 16–18 | SQLCipher + keyring, migration from plain DB, file permissions |
| **5. Search** | 19–20 | FTS5, instant search UI, highlighted results |
| **6. Locked notes** | 21–23 | Notes password, lock/unlock, auto-relock, secure memory |
| **🎯 MVP release (private beta)** | 24 | All P0 features, tested by 5–10 users |
| **7. Editor P1** | 25–29 | Tables, attachments, highlights, note links, collapsible sections, tags |
| **8. Gallery & smart folders** | 30–31 | Gallery view, smart folders, export to PDF/Markdown |
| **9. Google sync** | 32–38 | OAuth, key setup, recovery key, push/pull, conflicts, restore flow |
| **10. Hardening** | 39–42 | Security review, fuzzing, performance tuning, accessibility |
| **11. Packaging** | 43–44 | `.deb`, Flatpak, Google OAuth verification, website + privacy policy |
| **🚀 v1.0 public release** | 45 | All P0 + P1 features |

---

## 13. Testing & Quality

| Area | Approach |
|---|---|
| Crypto | Unit tests with libsodium test vectors; wrong-password and tamper tests (must fail safely) |
| Storage | Tests for every repository method; migration tests from each old schema |
| Editor | Round-trip tests: save → load → content identical |
| Sync | Integration tests against a **mock Drive server**; conflict and offline scenarios |
| Memory safety | Every debug build runs with **AddressSanitizer + UBSan** |
| Fuzzing | **libFuzzer** on note HTML import, attachment parsing, sync file decoding |
| Static analysis | `clang-tidy` + `cppcheck` in CI; treat warnings as errors |
| Performance | Benchmark suite with 10,000 generated notes; fail CI if budgets regress |
| Manual QA | Checklist per release: fresh install, upgrade, restore on new machine, lock/unlock |

**CI pipeline (GitHub Actions):** build → unit tests → sanitizer build → clang-tidy → package `.deb`.

---

## 14. Packaging & Release

| Format | Purpose | Notes |
|---|---|---|
| **.deb** | Ubuntu users, smallest size | Depends on system Qt 6 (Ubuntu 24.04+ ships Qt 6) |
| **Flatpak** | Sandboxed, works on any distro | Uses KDE runtime (shared, so the app stays small); restrict permissions to network + keyring |
| **Snap** (optional) | Ubuntu Software store | Consider after v1.0 |

**Release checklist**
- [ ] Version bump + changelog
- [ ] All tests green, sanitizer build clean
- [ ] Performance budgets met
- [ ] Privacy policy published (required by Google)
- [ ] Google OAuth app verified
- [ ] Signed release artifacts + checksums

---

## 15. Risks & Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Rich-text editor is harder than expected (tables, checklists) | High | Start editor early (Phase 3); extend `QTextEdit` rather than writing from scratch |
| C++ learning curve causes memory bugs | High | Smart pointers only, no raw `new`/`delete`; ASan in every debug build; code reviews |
| Users lose recovery key and password | High | Clear warnings, force user to confirm recovery key, offer print/save |
| Google OAuth verification delays release | Medium | Start verification early (Phase 9), prepare privacy policy in advance |
| Sync conflicts lose data | High | Never overwrite silently; always keep a conflicted copy |
| Keyring unavailable on some systems | Medium | Fallback to a startup password |
| Design too close to Apple's assets | Medium | Own name, icons (Lucide/Phosphor), fonts (Inter), colors |
| Scope creep (P2 features) | Medium | Freeze scope per milestone; P2 only after v1.0 |

---

## 16. Definition of Done (v1.0)

- [ ] Three-pane UI matching macOS Notes layout and workflow
- [ ] All **P0** and **P1** features working
- [ ] Database encrypted at rest; file unreadable when copied elsewhere
- [ ] Locked notes require password; auto-relock works
- [ ] Google sync is end-to-end encrypted; Google stores only ciphertext
- [ ] Full restore on a new machine with Google sign-in + sync password or recovery key
- [ ] All performance budgets met on a mid-range laptop
- [ ] `.deb` under 5 MB; Flatpak builds and runs sandboxed
- [ ] No ASan/UBSan errors; fuzzers run 24 h without crashes
- [ ] Privacy policy published; Google OAuth verified
