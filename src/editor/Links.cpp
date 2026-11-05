#include "editor/Links.h"

#include <QRegularExpression>

namespace notes::editor {

namespace {

constexpr QChar kObjectReplacement(0xFFFC); // an image in the text

bool isWordBreak(QChar character)
{
    return character.isSpace() || character == kObjectReplacement;
}

// Whether `token` ends with a bracket it doesn't open, as in
// "(see https://example.com)".
bool unmatchedClose(QStringView token)
{
    const QChar close = token.back();
    QChar open;
    if (close == u')') {
        open = u'(';
    } else if (close == u']') {
        open = u'[';
    } else {
        return close == u'>' || close == u'}';
    }
    return token.count(open) < token.count(close);
}

// `token` without the punctuation around it that belongs to the sentence.
QStringView trimmed(QStringView token, qsizetype& offset)
{
    static const QString kLeading = QStringLiteral("([<{\"'“‘");
    static const QString kTrailing = QStringLiteral(".,;:!?\"'”’");
    while (!token.isEmpty() && kLeading.contains(token.front())) {
        token = token.sliced(1);
        ++offset;
    }
    while (!token.isEmpty() && (kTrailing.contains(token.back()) || unmatchedClose(token))) {
        token.chop(1);
    }
    return token;
}

QString hrefFor(QStringView token)
{
    static const QRegularExpression kEmail(
        QStringLiteral("^[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\\.[A-Za-z0-9-]+)*\\.[A-Za-z]{2,}$"));
    const auto startsWith = [token](const char* prefix) {
        return token.startsWith(QLatin1StringView(prefix), Qt::CaseInsensitive);
    };
    if (startsWith("https://") || startsWith("http://") || startsWith("mailto:")) {
        return token.toString();
    }
    if (startsWith("www.") && token.size() > 4) {
        return QStringLiteral("https://") + token;
    }
    if (kEmail.matchView(token).hasMatch()) {
        return QStringLiteral("mailto:") + token;
    }
    return {};
}

} // namespace

bool Links::isAllowed(const QUrl& url)
{
    if (!url.isValid()) {
        return false;
    }
    const QString scheme = url.scheme().toLower();
    if (scheme == u"http" || scheme == u"https") {
        return !url.host().isEmpty();
    }
    if (scheme == u"mailto") {
        return url.path().contains(u'@');
    }
    return false;
}

bool Links::isAllowed(const QString& href)
{
    return !href.isEmpty() && isAllowed(QUrl(href));
}

std::optional<Links::Match> Links::endingAt(QStringView text)
{
    qsizetype start = text.size();
    while (start > 0 && !isWordBreak(text.at(start - 1))) {
        --start;
    }
    qsizetype offset = start;
    const QStringView token = trimmed(text.sliced(start), offset);
    if (token.isEmpty()) {
        return std::nullopt;
    }
    QString href = hrefFor(token);
    if (!isAllowed(href)) {
        return std::nullopt;
    }
    return Match{.start = offset, .length = token.size(), .href = std::move(href)};
}

} // namespace notes::editor
