# Bundled third-party assets

Everything here is compiled into the binary as Qt resources (see `src/CMakeLists.txt`).
License texts are in `licenses/` and shown in **Help → About → Licenses**.

## Fonts (`fonts/`, SIL Open Font License 1.1)

Static TTFs, not the variable fonts: Qt 6.10 on Linux registers a variable font as
Regular/Italic only and fakes bold by thickening, so `setWeight(Bold)` doesn't use
the real Bold design (checked on 2026-10-29 by comparing glyph advances).

| File | Source | SHA-256 |
|---|---|---|
| `Inter-Regular.ttf` | [Inter v4.1](https://github.com/rsms/inter/releases/tag/v4.1) `extras/ttf/` | `40d692fce188e4471e2b3cba937be967878f631ad3ebbbdcd587687c7ebe0c82` |
| `Inter-Italic.ttf` | 〃 | `bbc051dd204b5019a1aa0bc0ae2aa8a05ab13e7a3f979fa357631dc7feb6833a` |
| `Inter-SemiBold.ttf` | 〃 | `78a843fade9d4612a5567302fb595b56976eb5fcebf4fea5a5912d638bafcde3` |
| `Inter-Bold.ttf` | 〃 | `288316099b1e0a47a4716d159098005eef7c0066921f34e3200393dbdb01947f` |
| `Inter-BoldItalic.ttf` | 〃 | `948405a16cdc62701da5f4005ed068ca5f4d27061d98f7974ccfc37831d9581d` |
| `JetBrainsMono-Regular.ttf` | [JetBrains Mono v2.304](https://github.com/JetBrains/JetBrainsMono/releases/tag/v2.304) `fonts/ttf/` | `a0bf60ef0f83c5ed4d7a75d45838548b1f6873372dfac88f71804491898d138f` |
| `JetBrainsMono-Bold.ttf` | 〃 | `5590990c82e097397517f275f430af4546e1c45cff408bde4255dad142479dcb` |

Only these weights are bundled, to stay inside the binary-size budget (Plan §10).
Other weights fall back to the nearest bundled one.

## Icons (`icons/`, ISC; some derived from Feather, MIT)

[Lucide 1.57.0](https://github.com/lucide-icons/lucide/releases/tag/1.57.0) (`lucide-icons-1.57.0.zip`), copied unmodified.

**Adding an icon:**

1. Copy `<name>.svg` from the same Lucide release into `icons/`.
2. Add an enum value to `notes::ui::Icon` and its file to `iconFileName()` in `src/ui/Icons.cpp`.

`tst_icons` fails if an icon file is missing or if an SVG in `icons/` isn't referenced
in `Icons.cpp`, so unused icons don't bloat the binary.
