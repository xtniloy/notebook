#pragma once

#include <QString>
#include <QStringView>
#include <QUrl>

#include <optional>

namespace notes::editor {

// Links in notes (task 3.9): which ones may be opened, and finding the one
// just typed.
class Links
{
public:
    // Whether a link may be opened (or kept when pasted): http and https with
    // a host, and mailto with an address. Everything else (file, javascript,
    // data, smb, custom app schemes...) is never opened, whatever the note
    // says: a note can come from anywhere (paste, import, sync).
    [[nodiscard]] static bool isAllowed(const QUrl& url);
    [[nodiscard]] static bool isAllowed(const QString& href);

    // A link found in text: where it is and what it opens.
    struct Match
    {
        qsizetype start = 0;
        qsizetype length = 0;
        QString href;
    };
    // The link that ends `text` (what was typed before a space or Enter):
    // `https://...` or `http://...`, `www.example.com` (opened as https),
    // `mailto:...`, or an e-mail address (as mailto). Punctuation after it
    // isn't part of it ("see https://example.com." links without the
    // period), nor is a closing bracket it doesn't open. Nothing for
    // anything else, or for a link that couldn't be opened.
    [[nodiscard]] static std::optional<Match> endingAt(QStringView text);
};

} // namespace notes::editor
