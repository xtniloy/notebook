#include "services/NoteText.h"

#include <QList>
#include <QRegularExpression>

namespace notes::services {

namespace {

bool isLineBreak(QChar c)
{
    // \r for CRLF text; QTextDocument uses the Unicode separators internally.
    return c == u'\n' || c == u'\r' || c == QChar::ParagraphSeparator || c == QChar::LineSeparator;
}

// The trimmed, non-empty lines of `text`, in order.
QList<QStringView> lines(QStringView text)
{
    QList<QStringView> result;
    qsizetype start = 0;
    for (qsizetype i = 0; i <= text.size(); ++i) {
        if (i == text.size() || isLineBreak(text.at(i))) {
            const QStringView line = text.sliced(start, i - start).trimmed();
            if (!line.isEmpty()) {
                result.append(line);
            }
            start = i + 1;
        }
    }
    return result;
}

// At most `max` UTF-16 units, without splitting a surrogate pair (an emoji cut
// in half would show as a replacement character).
QString truncated(QStringView text, qsizetype max)
{
    if (text.size() <= max) {
        return text.toString();
    }
    qsizetype end = max;
    if (text.at(end - 1).isHighSurrogate()) {
        --end;
    }
    return text.first(end).trimmed().toString();
}

} // namespace

QString NoteText::title(QStringView plainText)
{
    const QList<QStringView> all = lines(plainText);
    return all.isEmpty() ? QString() : truncated(all.first(), kMaxTitleLength);
}

QString NoteText::snippet(QStringView plainText)
{
    const QList<QStringView> all = lines(plainText);
    QString result;
    for (qsizetype i = 1; i < all.size() && result.size() < kMaxSnippetLength; ++i) {
        if (!result.isEmpty()) {
            result += u' ';
        }
        result += all.at(i);
    }
    return truncated(result, kMaxSnippetLength);
}

QStringList NoteText::attachmentUuids(const QString& body)
{
    static const QRegularExpression kImage(
        QStringLiteral(R"re("image"\s*:\s*"attachment://([0-9A-Fa-f-]{36})")re"));
    QStringList uuids;
    for (const QRegularExpressionMatch& match : kImage.globalMatch(body)) {
        const QString uuid = match.captured(1);
        if (!uuids.contains(uuid)) {
            uuids.append(uuid);
        }
    }
    return uuids;
}

} // namespace notes::services
