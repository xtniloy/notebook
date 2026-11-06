// Task 3.11: the editor with a 50,000-word note holding every kind of content
// (headings, lists, checklists, inline code, links, code blocks, images).
// TC-PERF-003 opening it (loaded, first screen painted) takes < 50 ms;
// TC-PERF-002 each keystroke, from the key to the repainted editor, < 16 ms
// (Plan §10). Headless (offscreen, raster painting), release flags.

#include "models/Attachment.h"
#include "ui/NoteTextEdit.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTextBlock>

#include <algorithm>
#include <numeric>
#include <vector>

using notes::models::Attachment;
using notes::ui::NoteTextEdit;

namespace {

constexpr int kWords = 50'000;
constexpr int kWordsPerParagraph = 20;
constexpr int kImages = 20;
constexpr qint64 kOpenBudgetNs = 50'000'000;
constexpr qint64 kKeystrokeBudgetNs = 16'000'000;

double ms(qint64 ns)
{
    return static_cast<double>(ns) / 1e6;
}

// A note of kWords words, kWordsPerParagraph a paragraph, with every kind of
// content spread through it, and its images.
struct Note
{
    QString body;
    QString plainText;
    QList<Attachment> attachments;
    int words = 0;
};

QJsonObject text(const QString& value)
{
    return QJsonObject{{QStringLiteral("text"), value}};
}

Note makeNote()
{
    static const QStringList kVocabulary =
        QStringLiteral("the note keeps plans for a trip to Rome with "
                       "flights hotels museums dinners and a list of "
                       "things to pack before leaving on Friday morning")
            .split(u' ');
    Note note;
    QImage picture(800, 600, QImage::Format_RGB32);
    picture.fill(QColor(0x4A, 0x90, 0xD9));
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    picture.save(&buffer, "png");

    QJsonArray blocks;
    QStringList plain;
    int word = 0;
    const auto sentence = [&](int count) {
        QStringList words;
        for (int i = 0; i < count; ++i, ++word) {
            words.append(kVocabulary.at(word % kVocabulary.size()));
        }
        return words.join(u' ');
    };
    const auto paragraph = [&](const QJsonArray& runs, QJsonObject block = {}) {
        block.insert(QStringLiteral("runs"), runs);
        blocks.append(block);
        QString line;
        for (const auto& run : runs) {
            line += run.toObject().value(QStringLiteral("text")).toString();
        }
        plain.append(line);
    };
    for (int index = 0; word < kWords; ++index) {
        if (index % 25 == 0) {
            paragraph({text(sentence(4))}, {{QStringLiteral("h"), 2}});
        }
        if (index % 50 == 10) {
            for (int item = 0; item < 5; ++item) {
                paragraph({text(sentence(6))}, {{QStringLiteral("list"), 0}});
            }
        }
        if (index % 50 == 30) {
            for (int item = 0; item < 3; ++item) {
                paragraph({text(sentence(5))},
                          {{QStringLiteral("list"), 1}, {QStringLiteral("check"), item == 0}});
            }
        }
        if (index % 100 == 60) {
            for (int line = 0; line < 4; ++line) {
                paragraph({QJsonObject{{QStringLiteral("text"), sentence(5)},
                                       {QStringLiteral("mono"), true}}});
            }
        }
        if (index % 125 == 70 && note.attachments.size() < kImages) {
            Attachment image;
            image.uuid = QStringLiteral("00000000-0000-4000-8000-%1")
                             .arg(note.attachments.size(), 12, 10, QLatin1Char('0'));
            image.fileName = QStringLiteral("picture.png");
            image.mimeType = QStringLiteral("image/png");
            image.data = png;
            image.sizeBytes = png.size();
            note.attachments.append(image);
            blocks.append(
                QJsonObject{{QStringLiteral("runs"),
                             QJsonArray{QJsonObject{{QStringLiteral("image"),
                                                     QStringLiteral("attachment://") + image.uuid},
                                                    {QStringLiteral("w"), 400},
                                                    {QStringLiteral("h"), 300}}}}});
            plain.append(QString());
        }
        // A paragraph with inline code, and every tenth a link.
        QJsonArray runs{text(sentence(kWordsPerParagraph / 2) + u' '),
                        QJsonObject{{QStringLiteral("text"), QStringLiteral("make")},
                                    {QStringLiteral("mono"), true}},
                        text(u' ' + sentence((kWordsPerParagraph / 2) - 2))};
        if (index % 10 == 5) {
            runs.append(text(QStringLiteral(" see ")));
            runs.append(
                QJsonObject{{QStringLiteral("text"), QStringLiteral("example.com")},
                            {QStringLiteral("href"), QStringLiteral("https://example.com")}});
        }
        paragraph(runs);
    }
    note.words = word;
    const QJsonArray lists{QJsonObject{{QStringLiteral("style"), QStringLiteral("disc")},
                                       {QStringLiteral("indent"), 1}},
                           QJsonObject{{QStringLiteral("style"), QStringLiteral("check")},
                                       {QStringLiteral("indent"), 1}}};
    note.body = QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("lists"), lists},
                                                            {QStringLiteral("blocks"), blocks}})
                                      .toJson(QJsonDocument::Compact));
    note.plainText = plain.join(u'\n');
    return note;
}

struct Stats
{
    qint64 median = 0;
    double mean = 0;
    qint64 p99 = 0;
    qint64 worst = 0;
};

Stats statsOf(std::vector<qint64> times)
{
    std::ranges::sort(times);
    return {.median = times.at(times.size() / 2),
            .mean = ms(std::accumulate(times.begin(), times.end(), qint64{0})) /
                    static_cast<double>(times.size()),
            .p99 = times.at((times.size() * 99) / 100),
            .worst = times.back()};
}

} // namespace

class bench_Editor : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void openingANote() const;
    void typing() const;

private:
    Note m_note;
};

void bench_Editor::initTestCase()
{
    m_note = makeNote();
    qInfo("Note: %d words, %lld paragraphs, %lld images, %lld KB stored", m_note.words,
          m_note.plainText.count(u'\n') + 1, m_note.attachments.size(), m_note.body.size() / 1024);
}

// TC-PERF-003
void bench_Editor::openingANote() const
{
    NoteTextEdit edit;
    edit.resize(700, 900); // the editor pane on a 1080p screen
    edit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&edit));

    // Loading the note and showing its first screen, as when it's selected,
    // then what that queued (the link look). Qt lays out the rest of a long
    // note in the background, afterwards. The first run warms caches (fonts).
    std::vector<qint64> times;
    QElapsedTimer timer;
    for (int run = 0; run < 6; ++run) {
        edit.setNoteBody({}, {}); // another note
        QCoreApplication::processEvents();
        timer.start();
        edit.setNoteBody(m_note.body, m_note.plainText, m_note.attachments);
        edit.viewport()->repaint();
        QCoreApplication::sendPostedEvents();
        if (run > 0) {
            times.push_back(timer.nsecsElapsed());
        }
    }
    QCOMPARE(edit.attachments().size(), m_note.attachments.size());
    const Stats stats = statsOf(times);
    qInfo("Opening: median %.2f ms, mean %.2f ms, max %.2f ms (budget 50 ms)", ms(stats.median),
          stats.mean, ms(stats.worst));
    // The median of five: one run slowed by something else on the machine
    // doesn't make it fail.
    QVERIFY2(stats.median < kOpenBudgetNs,
             qPrintable(QStringLiteral("median open %1 ms").arg(ms(stats.median), 0, 'f', 2)));
}

// TC-PERF-002
void bench_Editor::typing() const
{
    NoteTextEdit edit;
    edit.resize(700, 900);
    edit.setNoteBody(m_note.body, m_note.plainText, m_note.attachments);
    edit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&edit));
    edit.setFocus();
    // In the middle of the note, at the end of a paragraph.
    QTextCursor cursor(edit.document()->findBlockByNumber(edit.document()->blockCount() / 2));
    cursor.movePosition(QTextCursor::EndOfBlock);
    edit.setTextCursor(cursor);
    edit.ensureCursorVisible();

    // A sentence and a new paragraph, again and again: each key, what it
    // queues (code spacing, link look) and the repaint.
    const QString typed = QStringLiteral("Book the train to Florence for Sunday.\n");
    const auto press = [&](QChar character) {
        if (character == u'\n') {
            QTest::keyClick(&edit, Qt::Key_Return);
        } else {
            QTest::keyClick(&edit, character.toLatin1());
        }
        QCoreApplication::processEvents();
        edit.viewport()->repaint();
    };
    for (const QChar character : typed) {
        press(character); // warm-up
    }
    std::vector<qint64> times;
    QElapsedTimer timer;
    for (int round = 0; round < 8; ++round) {
        for (const QChar character : typed) {
            timer.start();
            press(character);
            times.push_back(timer.nsecsElapsed());
        }
    }
    const Stats stats = statsOf(times);
    qInfo("%zu keystrokes: mean %.2f ms, p99 %.2f ms, max %.2f ms (budget 16 ms)", times.size(),
          stats.mean, ms(stats.p99), ms(stats.worst));
    QVERIFY2(stats.worst < kKeystrokeBudgetNs,
             qPrintable(QStringLiteral("slowest keystroke %1 ms").arg(ms(stats.worst), 0, 'f', 2)));
}

QTEST_MAIN(bench_Editor)
#include "bench_editor.moc"
