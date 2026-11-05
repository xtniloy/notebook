#include "editor/PasteCleaner.h"

#include "editor/Links.h"
#include "editor/NoteFormat.h"
#include "editor/StyleManager.h"

#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextList>
#include <QUrl>

#include <algorithm>

namespace notes::editor {

namespace {

// A document that never loads anything: QTextDocument would otherwise read
// the images and style sheets HTML names from disk (and an app-wide resource
// provider could fetch them), which for pasted HTML means tracking pixels.
class SealedDocument : public QTextDocument
{
public:
    SealedDocument() = default;

protected:
    QVariant loadResource(int type, const QUrl& /*name*/) override
    {
        if (type == QTextDocument::ImageResource) {
            return QImage();
        }
        return QByteArray();
    }
};

// Elements whose content is never text to keep. Qt's HTML reader would show
// some of it (a script's code) or act on it (a style sheet).
const QRegularExpression& hiddenElements()
{
    static const QRegularExpression kHidden(
        QStringLiteral(R"(<(script|style|noscript|template|iframe|object|embed|svg|math|head)\b)"
                       R"([^>]*>.*?</\1\s*>)"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    return kHidden;
}

// Pasted headings become ours: levels 1 and 2 Heading, deeper ones
// Subheading. Title stays for a note's first line.
int headingFor(int level)
{
    if (level <= 0) {
        return 0;
    }
    const int heading = StyleManager::headingLevel(ParagraphStyle::Heading);
    return level <= heading ? heading : StyleManager::headingLevel(ParagraphStyle::Subheading);
}

QJsonArray cleanRuns(const QJsonArray& runs)
{
    QJsonArray clean;
    for (const auto& value : runs) {
        const QJsonObject run = value.toObject();
        QString text = run.value(QStringLiteral("text")).toString();
        if (text.isEmpty()) {
            continue; // images and empty runs
        }
        text.replace(QChar::Nbsp, u' ');
        QJsonObject kept{{QStringLiteral("text"), text}};
        const QString href = run.value(QStringLiteral("href")).toString();
        if (Links::isAllowed(href)) {
            kept.insert(QStringLiteral("href"), href);
        }
        for (const char* key : {"b", "i", "s", "mono"}) {
            if (run.value(QLatin1StringView(key)).toBool()) {
                kept.insert(QLatin1StringView(key), true);
            }
        }
        // Links come underlined from HTML: that's the link's look, not the
        // text's format.
        if (run.value(QStringLiteral("u")).toBool() && href.isEmpty()) {
            kept.insert(QStringLiteral("u"), true);
        }
        // Runs that differed only in what was dropped are one run now.
        if (!clean.isEmpty()) {
            QJsonObject previous = clean.last().toObject();
            const QString previousText = previous.take(QStringLiteral("text")).toString();
            QJsonObject formats = kept;
            formats.remove(QStringLiteral("text"));
            if (previous == formats) {
                previous.insert(QStringLiteral("text"), previousText + text);
                clean.replace(clean.size() - 1, previous);
                continue;
            }
        }
        clean.append(kept);
    }
    return clean;
}

QJsonObject cleanParagraph(const QJsonObject& node)
{
    QJsonObject clean;
    if (const QJsonArray runs = cleanRuns(node.value(QStringLiteral("runs")).toArray());
        !runs.isEmpty()) {
        clean.insert(QStringLiteral("runs"), runs);
    }
    if (const int level = headingFor(node.value(QStringLiteral("h")).toInt()); level > 0) {
        clean.insert(QStringLiteral("h"), level);
    }
    for (const char* key : {"list", "check", "code"}) {
        if (node.contains(QLatin1StringView(key))) {
            clean.insert(QLatin1StringView(key), node.value(QLatin1StringView(key)));
        }
    }
    return clean;
}

QJsonObject cleanTable(const QJsonObject& table)
{
    QJsonArray cells;
    for (const auto& cell : table.value(QStringLiteral("cells")).toArray()) {
        QJsonArray content;
        for (const auto& paragraph : cell.toArray()) {
            content.append(cleanParagraph(paragraph.toObject()));
        }
        cells.append(content);
    }
    QJsonObject clean = table;
    clean.insert(QStringLiteral("cells"), cells);
    return clean;
}

// No list, no table, no text but spaces and line breaks.
bool isEmptyParagraph(const QJsonObject& node)
{
    if (node.contains(QStringLiteral("table")) || node.contains(QStringLiteral("list"))) {
        return false;
    }
    const QJsonArray runs = node.value(QStringLiteral("runs")).toArray();
    return std::ranges::all_of(runs, [](const QJsonValue& run) {
        return run.toObject().value(QStringLiteral("text")).toString().trimmed().isEmpty();
    });
}

// Our list kinds and nesting, from whatever lists the reader made.
void normalizeLists(QTextDocument& document)
{
    QSet<QTextList*> done;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        QTextList* list = block.textList();
        if (list == nullptr || done.contains(list)) {
            continue;
        }
        done.insert(list);
        const int indent = std::clamp(list->format().indent(), 1, StyleManager::kMaxListIndent);
        list->setFormat(StyleManager::listFormat(StyleManager::listStyleOf(*list), indent));
    }
}

// The document's meaning, cleaned (see the class comment).
QString cleanBody(QTextDocument& document)
{
    StyleManager::adoptTaskLists(document);
    normalizeLists(document);
    const QJsonObject body =
        QJsonDocument::fromJson(NoteFormat::serialize(document).toUtf8()).object();

    QJsonArray blocks;
    for (const auto& value : body.value(QStringLiteral("blocks")).toArray()) {
        const QJsonObject node = value.toObject();
        if (const QJsonValue table = node.value(QStringLiteral("table")); table.isObject()) {
            blocks.append(QJsonObject{{QStringLiteral("table"), cleanTable(table.toObject())}});
        } else {
            blocks.append(cleanParagraph(node));
        }
    }
    // Browsers wrap a copied selection in empty lines.
    while (!blocks.isEmpty() && isEmptyParagraph(blocks.first().toObject())) {
        blocks.removeFirst();
    }
    while (!blocks.isEmpty() && isEmptyParagraph(blocks.last().toObject())) {
        blocks.removeLast();
    }
    if (blocks.isEmpty()) {
        return {};
    }
    QJsonObject clean{{QStringLiteral("blocks"), blocks}};
    if (const QJsonValue lists = body.value(QStringLiteral("lists")); lists.isArray()) {
        clean.insert(QStringLiteral("lists"), lists);
    }
    return QString::fromUtf8(QJsonDocument(clean).toJson(QJsonDocument::Compact));
}

int countMatches(const QRegularExpression& pattern, QStringView text)
{
    int count = 0;
    for (auto it = pattern.globalMatchView(text); it.hasNext(); it.next()) {
        ++count;
    }
    return count;
}

} // namespace

QString PasteCleaner::fromHtml(const QString& html)
{
    SealedDocument document;
    document.setHtml(QString(html).remove(hiddenElements()));
    return cleanBody(document);
}

QString PasteCleaner::fromMarkdown(const QString& markdown)
{
    SealedDocument document;
    document.setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
    return cleanBody(document);
}

bool PasteCleaner::looksLikeMarkdown(QStringView text)
{
    using Options = QRegularExpression::PatternOption;
    const auto pattern = [](const char* expression) {
        return QRegularExpression(QLatin1StringView(expression), Options::MultilineOption);
    };
    // Each strong sign alone is enough; weak ones need a second, different one.
    static const QRegularExpression kFence = pattern(R"(^ {0,3}(```|~~~))");
    static const QRegularExpression kTableRule =
        pattern(R"(^ *\|? *:?-{3,}:? *(\| *:?-{3,}:? *)+\|? *$)");
    static const QRegularExpression kHeading = pattern(R"(^ {0,3}#{1,6} +\S)");
    static const QRegularExpression kListItem = pattern(R"(^ *([-*+]|\d{1,9}[.)]) +\S)");
    static const QRegularExpression kTask = pattern(R"(^ *[-*+] +\[[ xX]\] )");
    static const QRegularExpression kQuote = pattern(R"(^ {0,3}> ?\S)");
    static const QRegularExpression kBold = pattern(R"((\*\*|__)\S([^\n]*?\S)?\1)");
    static const QRegularExpression kLink =
        pattern(R"(\[[^\]\n]+\]\((https?://|mailto:)[^)\s]+\))");
    static const QRegularExpression kCode = pattern(R"(`[^`\n]+`)");

    if (kFence.matchView(text).hasMatch() || kTableRule.matchView(text).hasMatch()) {
        return true;
    }
    int signs = 0;
    for (const QRegularExpression* sign : {&kHeading, &kQuote, &kBold, &kLink, &kCode, &kTask}) {
        if (sign->matchView(text).hasMatch()) {
            ++signs;
        }
    }
    if (countMatches(kListItem, text) >= 2) {
        ++signs;
    }
    return signs >= 2;
}

} // namespace notes::editor
