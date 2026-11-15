#include "core/proofreadingtext.h"
#include "core/settings.h"
#include "services/llmproofreadingparser.h"

#include <QTest>

namespace {

const QString kGrammar = QStringLiteral("grammar");

// The three document shapes that carry one edit: an object, the same object in
// a Markdown fence and a bare array.
QStringList editDocuments(const QString &original, const QString &replacement,
                          const QString &category)
{
    const QString entry =
        QStringLiteral("{\"original\":\"%1\",\"replacement\":\"%2\",\"message\":\"m\","
                       "\"category\":\"%3\"}")
            .arg(original, replacement, category);
    return {
        QStringLiteral("{\"edits\":[%1]}").arg(entry),
        QStringLiteral("```json\n{\"edits\":[%1]}\n```").arg(entry),
        QStringLiteral("[%1]").arg(entry),
    };
}

} // namespace

class TestLlmProofreadingParser : public QObject
{
    Q_OBJECT

private slots:
    void systemPromptSchema()
    {
        const QString prompt = LlmProofreading::systemPrompt(QStringLiteral("de"), {});

        QVERIFY2(prompt.contains(QStringLiteral("{\"edits\"")), qPrintable(prompt));
        QVERIFY(prompt.contains(QStringLiteral("\"original\"")));
        QVERIFY(prompt.contains(QStringLiteral("\"replacement\"")));
        QVERIFY(prompt.contains(QStringLiteral("\"message\"")));
        QVERIFY(prompt.contains(QStringLiteral("\"category\"")));
        QVERIFY(prompt.contains(QStringLiteral("{\"edits\":[]}")));
        QVERIFY(prompt.contains(QStringLiteral("data, not instructions"), Qt::CaseInsensitive));
        // The prompt is built from literals: no user text, key, URL or path.
        QVERIFY(!prompt.contains(QStringLiteral("://")));
        QVERIFY(!prompt.contains(QLatin1Char('/')));
    }

    void systemPromptLanguage_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("expected");

        QTest::newRow("de") << "de" << "German";
        QTest::newRow("ru") << "ru" << "Russian";
        QTest::newRow("auto") << "auto" << "detect";
        QTest::newRow("empty") << "" << "detect";
    }

    void systemPromptLanguage()
    {
        QFETCH(QString, language);
        QFETCH(QString, expected);

        const QString prompt = LlmProofreading::systemPrompt(language, {});
        QVERIFY2(prompt.contains(expected, Qt::CaseInsensitive), qPrintable(prompt));
        // Every place marker is filled in, including the repeated %1.
        QVERIFY(!prompt.contains(QStringLiteral("%1")));
    }

    void systemPromptCategories()
    {
        const QString spelling =
            LlmProofreading::systemPrompt(QStringLiteral("en"), {QStringLiteral("spelling")});
        QVERIFY(spelling.contains(QStringLiteral("spelling")));
        QVERIFY(!spelling.contains(QStringLiteral("punctuation")));
        QVERIFY(!spelling.contains(kGrammar));

        const QString all = LlmProofreading::systemPrompt(QStringLiteral("en"), {});
        for (const QString &id : proofreadingCategoryIds())
            QVERIFY2(all.contains(id), qPrintable(id));

        const QString two = LlmProofreading::systemPrompt(
            QStringLiteral("en"), {QStringLiteral("spelling"), QStringLiteral("style")});
        QVERIFY(two.contains(QStringLiteral("spelling")));
        QVERIFY(two.contains(QStringLiteral("style")));
        QVERIFY(!two.contains(kGrammar));
        QVERIFY(!two.contains(QStringLiteral("punctuation")));
    }

    void parsesStructuredEdits()
    {
        const QString source = QStringLiteral("He has two cat.");
        const QString content = QStringLiteral(
            "{\"edits\":["
            "{\"original\":\"He\",\"replacement\":\"She\",\"message\":\" Use she. \","
            "\"category\":\"grammar\"},"
            "{\"original\":\"cat\",\"replacement\":\"cats\",\"message\":\"Plural.\","
            "\"category\":\"grammar\"}]}");

        const LlmProofreading::Parsed parsed =
            LlmProofreading::parseResponse(source, content, {});
        QCOMPARE(parsed.status, LlmProofreading::Status::Edits);
        QCOMPARE(parsed.droppedEdits, 0);
        QVERIFY(parsed.correctedText.isEmpty());
        QCOMPARE(parsed.issues.size(), 2);

        QCOMPARE(parsed.issues.at(0).offset, 0);
        QCOMPARE(parsed.issues.at(0).length, 2);
        QCOMPARE(parsed.issues.at(0).replacements, QStringList{QStringLiteral("She")});
        QCOMPARE(parsed.issues.at(0).message, QStringLiteral("Use she."));
        QCOMPARE(parsed.issues.at(0).ruleId, QStringLiteral("llm"));
        QCOMPARE(parsed.issues.at(0).category, kGrammar);
        QCOMPARE(parsed.issues.at(0).severity, 0);

        QCOMPARE(parsed.issues.at(1).offset, 11);
        QCOMPARE(parsed.issues.at(1).length, 3);
        QCOMPARE(parsed.issues.at(1).replacements, QStringList{QStringLiteral("cats")});
        QCOMPARE(parsed.issues.at(1).message, QStringLiteral("Plural."));

        QCOMPARE(ProofreadingText::corrected(source, parsed.issues),
                 QStringLiteral("She has two cats."));
    }

    void parsesFencedObject()
    {
        const QString source = QStringLiteral("teh cat");
        const QString content = QStringLiteral(
            "```json\n"
            "{\"edits\":[{\"original\":\"teh\",\"replacement\":\"the\",\"message\":\"Typo.\","
            "\"category\":\"spelling\"}]}\n"
            "```");

        const LlmProofreading::Parsed parsed =
            LlmProofreading::parseResponse(source, content, {});
        QCOMPARE(parsed.status, LlmProofreading::Status::Edits);
        QCOMPARE(parsed.issues.size(), 1);
        QCOMPARE(parsed.issues.first().offset, 0);
        QCOMPARE(parsed.issues.first().length, 3);
        QCOMPARE(parsed.issues.first().category, QStringLiteral("spelling"));
    }

    void parsesObjectWithProse()
    {
        const QString source = QStringLiteral("Hello wrold");
        const QString content = QStringLiteral(
            "Sure! Here is the answer:\n"
            "{\"edits\":[{\"original\":\"wrold\",\"replacement\":\"world\","
            "\"message\":\"Spelling.\",\"category\":\"spelling\"}]}\n"
            "Hope it helps.");

        const LlmProofreading::Parsed parsed =
            LlmProofreading::parseResponse(source, content, {});
        QCOMPARE(parsed.status, LlmProofreading::Status::Edits);
        QCOMPARE(parsed.issues.size(), 1);
        QCOMPARE(parsed.issues.first().offset, 6);
        QCOMPARE(parsed.issues.first().length, 5);
        QCOMPARE(ProofreadingText::corrected(source, parsed.issues),
                 QStringLiteral("Hello world"));
    }

    void parsesBareArray()
    {
        const QString source = QStringLiteral("Hello wrold");
        const QString content = QStringLiteral(
            "[{\"original\":\"wrold\",\"replacement\":\"world\",\"message\":\"\","
            "\"category\":\"spelling\"}]");

        const LlmProofreading::Parsed parsed =
            LlmProofreading::parseResponse(source, content, {});
        QCOMPARE(parsed.status, LlmProofreading::Status::Edits);
        QCOMPARE(parsed.issues.size(), 1);
        QCOMPARE(parsed.issues.first().offset, 6);
        QCOMPARE(parsed.issues.first().length, 5);
        // An empty model message gets the one fixed literal.
        QCOMPARE(parsed.issues.first().message,
                 QStringLiteral("the model suggested a change here"));
    }

    void parsesDeletion()
    {
        // The prompt allows a deletion: an empty `replacement` removes `original`.
        const QString source = QStringLiteral("Hello world");
        const QString content = QStringLiteral(
            "{\"edits\":[{\"original\":\" world\",\"replacement\":\"\",\"message\":\"m\","
            "\"category\":\"grammar\"}]}");

        const LlmProofreading::Parsed parsed =
            LlmProofreading::parseResponse(source, content, {});
        QCOMPARE(parsed.status, LlmProofreading::Status::Edits);
        QCOMPARE(parsed.issues.size(), 1);
        QCOMPARE(parsed.issues.first().offset, 5);
        QCOMPARE(parsed.issues.first().length, 6);
        QCOMPARE(parsed.issues.first().replacements, QStringList{QString()});
        QCOMPARE(ProofreadingText::corrected(source, parsed.issues), QStringLiteral("Hello"));
    }

    void parsesCyrillicWithEmoji()
    {
        // The emoji is two UTF-16 units, so the edit starts at 3, not at 1.
        const QString source = QStringLiteral("\U0001F642 Он пашол");
        const QString content = QStringLiteral(
            "{\"edits\":[{\"original\":\"Он па\",\"replacement\":\"Он по\","
            "\"message\":\"m\",\"category\":\"grammar\"}]}");

        const LlmProofreading::Parsed parsed =
            LlmProofreading::parseResponse(source, content, {});
        QCOMPARE(parsed.status, LlmProofreading::Status::Edits);
        QCOMPARE(parsed.issues.size(), 1);
        QCOMPARE(parsed.issues.first().offset, 3);
        QCOMPARE(parsed.issues.first().length, 5);
    }

    void parsesEmojiOriginal()
    {
        const QString source = QStringLiteral("I like \U0001F600 it");
        const QString content = QStringLiteral(
            "{\"edits\":[{\"original\":\"\U0001F600\",\"replacement\":\"\U0001F601\","
            "\"message\":\"m\",\"category\":\"style\"}]}");

        const LlmProofreading::Parsed parsed =
            LlmProofreading::parseResponse(source, content, {});
        QCOMPARE(parsed.issues.size(), 1);
        QCOMPARE(parsed.issues.first().offset, 7);
        QCOMPARE(parsed.issues.first().length, 2);
        QCOMPARE(ProofreadingText::corrected(source, parsed.issues),
                 QStringLiteral("I like \U0001F601 it"));
    }

    void parsesRepeatedFragment()
    {
        const QString source = QStringLiteral("да да");
        const QString content = QStringLiteral(
            "{\"edits\":["
            "{\"original\":\"да\",\"replacement\":\"нет\",\"message\":\"m\","
            "\"category\":\"grammar\"},"
            "{\"original\":\"да\",\"replacement\":\"нет\",\"message\":\"m\","
            "\"category\":\"grammar\"}]}");

        const LlmProofreading::Parsed parsed =
            LlmProofreading::parseResponse(source, content, {});
        QCOMPARE(parsed.issues.size(), 2);
        QCOMPARE(parsed.issues.at(0).offset, 0);
        QCOMPARE(parsed.issues.at(0).length, 2);
        QCOMPARE(parsed.issues.at(1).offset, 3);
        QCOMPARE(parsed.issues.at(1).length, 2);
        QCOMPARE(ProofreadingText::corrected(source, parsed.issues),
                 QStringLiteral("нет нет"));
    }

    void dropsOverlappingEdit()
    {
        // "bc" lies inside "abc": corrected() would skip the second edit, so it
        // is dropped and counted instead of vanishing without a trace.
        const QString source = QStringLiteral("abc");
        const QString content = QStringLiteral(
            "{\"edits\":["
            "{\"original\":\"abc\",\"replacement\":\"X\",\"message\":\"m\","
            "\"category\":\"grammar\"},"
            "{\"original\":\"bc\",\"replacement\":\"Y\",\"message\":\"m\","
            "\"category\":\"grammar\"}]}");

        const LlmProofreading::Parsed parsed =
            LlmProofreading::parseResponse(source, content, {});
        QCOMPARE(parsed.status, LlmProofreading::Status::Edits);
        QCOMPARE(parsed.issues.size(), 1);
        QCOMPARE(parsed.issues.first().offset, 0);
        QCOMPARE(parsed.issues.first().length, 3);
        QCOMPARE(parsed.issues.first().replacements, QStringList{QStringLiteral("X")});
        QCOMPARE(parsed.droppedEdits, 1);
        QCOMPARE(ProofreadingText::corrected(source, parsed.issues), QStringLiteral("X"));
    }

    void acceptsAdjacentEdits()
    {
        // Adjacent ranges do not overlap: a strict intersection must keep both.
        const QString source = QStringLiteral("ab");
        const QString content = QStringLiteral(
            "{\"edits\":["
            "{\"original\":\"a\",\"replacement\":\"X\",\"message\":\"m\","
            "\"category\":\"grammar\"},"
            "{\"original\":\"b\",\"replacement\":\"Y\",\"message\":\"m\","
            "\"category\":\"grammar\"}]}");

        const LlmProofreading::Parsed parsed =
            LlmProofreading::parseResponse(source, content, {});
        QCOMPARE(parsed.status, LlmProofreading::Status::Edits);
        QCOMPARE(parsed.issues.size(), 2);
        QCOMPARE(parsed.issues.at(0).offset, 0);
        QCOMPARE(parsed.issues.at(0).length, 1);
        QCOMPARE(parsed.issues.at(0).replacements, QStringList{QStringLiteral("X")});
        QCOMPARE(parsed.issues.at(1).offset, 1);
        QCOMPARE(parsed.issues.at(1).length, 1);
        QCOMPARE(parsed.issues.at(1).replacements, QStringList{QStringLiteral("Y")});
        QCOMPARE(parsed.droppedEdits, 0);
        QCOMPARE(ProofreadingText::corrected(source, parsed.issues), QStringLiteral("XY"));
    }

    void dropsMissingOriginal()
    {
        const QString content = QStringLiteral(
            "{\"edits\":[{\"original\":\"xyz\",\"replacement\":\"abc\",\"message\":\"m\","
            "\"category\":\"grammar\"}]}");

        const LlmProofreading::Parsed parsed =
            LlmProofreading::parseResponse(QStringLiteral("Hallo"), content, {});
        QCOMPARE(parsed.status, LlmProofreading::Status::Edits);
        QVERIFY(parsed.issues.isEmpty());
        QCOMPARE(parsed.droppedEdits, 1);
    }

    void dropsEmptyOriginalAndUnchangedReplacement()
    {
        const QString content = QStringLiteral(
            "{\"edits\":["
            "{\"original\":\"\",\"replacement\":\"x\",\"message\":\"m\",\"category\":\"grammar\"},"
            "{\"original\":\"Hallo\",\"replacement\":\"Hallo\",\"message\":\"m\","
            "\"category\":\"grammar\"}]}");

        const LlmProofreading::Parsed parsed =
            LlmProofreading::parseResponse(QStringLiteral("Hallo"), content, {});
        QCOMPARE(parsed.status, LlmProofreading::Status::Edits);
        QVERIFY(parsed.issues.isEmpty());
        QCOMPARE(parsed.droppedEdits, 2);
    }

    void dropsNonObjectEntry()
    {
        const QString content = QStringLiteral("{\"edits\":[1]}");

        const LlmProofreading::Parsed parsed =
            LlmProofreading::parseResponse(QStringLiteral("Hallo"), content, {});
        QCOMPARE(parsed.status, LlmProofreading::Status::Edits);
        QVERIFY(parsed.issues.isEmpty());
        QCOMPARE(parsed.droppedEdits, 1);
    }

    void dropsSurrogateSplitEdit()
    {
        // The source is "a" + U+1F600 + "b"; an edit that starts on the low half
        // or ends on the high half of the pair cannot be applied.
        const QString source = QStringLiteral("a\U0001F600b");

        const QString startsInside = QStringLiteral(
            "{\"edits\":[{\"original\":\"\\ude00b\",\"replacement\":\"x\",\"message\":\"m\","
            "\"category\":\"grammar\"}]}");
        const LlmProofreading::Parsed first =
            LlmProofreading::parseResponse(source, startsInside, {});
        QCOMPARE(first.status, LlmProofreading::Status::Edits);
        QVERIFY(first.issues.isEmpty());
        QCOMPARE(first.droppedEdits, 1);

        const QString endsInside = QStringLiteral(
            "{\"edits\":[{\"original\":\"a\\ud83d\",\"replacement\":\"x\",\"message\":\"m\","
            "\"category\":\"grammar\"}]}");
        const LlmProofreading::Parsed second =
            LlmProofreading::parseResponse(source, endsInside, {});
        QCOMPARE(second.status, LlmProofreading::Status::Edits);
        QVERIFY(second.issues.isEmpty());
        QCOMPARE(second.droppedEdits, 1);
    }

    void findsOutOfOrderEdit()
    {
        // The forward cursor misses "a", so the search restarts from 0.
        const QString source = QStringLiteral("abc");
        const QString content = QStringLiteral(
            "{\"edits\":["
            "{\"original\":\"c\",\"replacement\":\"C\",\"message\":\"m\",\"category\":\"grammar\"},"
            "{\"original\":\"a\",\"replacement\":\"A\",\"message\":\"m\","
            "\"category\":\"grammar\"}]}");

        const LlmProofreading::Parsed parsed =
            LlmProofreading::parseResponse(source, content, {});
        QCOMPARE(parsed.issues.size(), 2);
        QCOMPARE(parsed.issues.at(0).offset, 2);
        QCOMPARE(parsed.issues.at(1).offset, 0);
        QCOMPARE(ProofreadingText::corrected(source, parsed.issues), QStringLiteral("AbC"));
    }

    void keepsEmptyEditsDocument()
    {
        // An edits document with no edit is still Edits: the service reports it
        // as a bad response instead of "the text is clean".
        for (const QString &content :
             {QStringLiteral("{\"edits\":[]}"), QStringLiteral("[]")}) {
            const LlmProofreading::Parsed parsed =
                LlmProofreading::parseResponse(QStringLiteral("Hallo"), content, {});
            QCOMPARE(parsed.status, LlmProofreading::Status::Edits);
            QVERIFY(parsed.issues.isEmpty());
            QCOMPARE(parsed.droppedEdits, 0);
        }
    }

    void filtersCategories()
    {
        const QString source = QStringLiteral("teh cat");
        const QStringList spelling = {QStringLiteral("spelling")};

        // A known but unrequested category drops the edit in every shape.
        const QStringList unrequested =
            editDocuments(QStringLiteral("teh"), QStringLiteral("the"), kGrammar);
        for (const QString &document : unrequested) {
            const LlmProofreading::Parsed parsed =
                LlmProofreading::parseResponse(source, document, spelling);
            QCOMPARE(parsed.status, LlmProofreading::Status::Edits);
            QVERIFY(parsed.issues.isEmpty());
            QCOMPARE(parsed.droppedEdits, 1);
        }

        // An unknown category is kept with the grammar fallback.
        const QStringList unknown =
            editDocuments(QStringLiteral("teh"), QStringLiteral("the"), QStringLiteral("meaning"));
        for (const QString &document : unknown) {
            const LlmProofreading::Parsed parsed =
                LlmProofreading::parseResponse(source, document, spelling);
            QCOMPARE(parsed.issues.size(), 1);
            QCOMPARE(parsed.issues.first().category, kGrammar);
            QCOMPARE(parsed.droppedEdits, 0);
        }

        // An empty category is kept the same way.
        const QStringList empty =
            editDocuments(QStringLiteral("teh"), QStringLiteral("the"), QString());
        for (const QString &document : empty) {
            const LlmProofreading::Parsed parsed =
                LlmProofreading::parseResponse(source, document, spelling);
            QCOMPARE(parsed.issues.size(), 1);
            QCOMPARE(parsed.issues.first().category, kGrammar);
        }

        // A requested id matches trimmed and case-insensitively.
        const QStringList mixedCase = editDocuments(QStringLiteral("teh"), QStringLiteral("the"),
                                                    QStringLiteral("  SPELLING "));
        for (const QString &document : mixedCase) {
            const LlmProofreading::Parsed parsed =
                LlmProofreading::parseResponse(source, document, spelling);
            QCOMPARE(parsed.issues.size(), 1);
            QCOMPARE(parsed.issues.first().category, QStringLiteral("spelling"));
        }

        // An empty request means all four categories.
        const QStringList style =
            editDocuments(QStringLiteral("teh"), QStringLiteral("the"), QStringLiteral("style"));
        for (const QString &document : style) {
            const LlmProofreading::Parsed parsed =
                LlmProofreading::parseResponse(source, document, {});
            QCOMPARE(parsed.issues.size(), 1);
            QCOMPARE(parsed.issues.first().category, QStringLiteral("style"));
        }
    }

    void malformedForEmptyAnswer()
    {
        for (const QString &content : {QString(), QStringLiteral("   \n\t ")}) {
            const LlmProofreading::Parsed parsed =
                LlmProofreading::parseResponse(QStringLiteral("Hallo"), content, {});
            QCOMPARE(parsed.status, LlmProofreading::Status::Malformed);
            QVERIFY(parsed.issues.isEmpty());
            QVERIFY(parsed.correctedText.isEmpty());
            QCOMPARE(parsed.droppedEdits, 0);
        }
    }

    void malformedForNonEditsJson()
    {
        for (const QString &content : {QStringLiteral("{\"foo\":1}"), QStringLiteral("[1,2]"),
                                       QStringLiteral("{\"edits\":\"nope\"}")}) {
            const LlmProofreading::Parsed parsed =
                LlmProofreading::parseResponse(QStringLiteral("Hallo"), content, {});
            QCOMPARE(parsed.status, LlmProofreading::Status::Malformed);
            QVERIFY(parsed.issues.isEmpty());
            QVERIFY(parsed.correctedText.isEmpty());
        }
    }

    void plainTextFallback()
    {
        const QString source = QStringLiteral("Das ist ein Hund.");
        const QString content = QStringLiteral("  Das ist ein Hund! \n");

        const LlmProofreading::Parsed parsed =
            LlmProofreading::parseResponse(source, content, {});
        QCOMPARE(parsed.status, LlmProofreading::Status::PlainText);
        QCOMPARE(parsed.correctedText, QStringLiteral("Das ist ein Hund!"));
        QCOMPARE(parsed.issues.size(), 1);

        const ProofreadingIssue issue = parsed.issues.first();
        QCOMPARE(issue.offset, 16);
        QCOMPARE(issue.length, 1);
        QCOMPARE(issue.replacements, QStringList{QStringLiteral("!")});
        QCOMPARE(issue.ruleId, QStringLiteral("llm-diff"));
        QCOMPARE(issue.category, kGrammar);
        QCOMPARE(issue.severity, 0);
        QCOMPARE(issue.message, QStringLiteral("the corrected text differs here"));
        QCOMPARE(ProofreadingText::corrected(source, parsed.issues),
                 QStringLiteral("Das ist ein Hund!"));
    }

    void plainTextEqualSource()
    {
        const QString source = QStringLiteral("Das ist ein Hund.");

        const LlmProofreading::Parsed parsed =
            LlmProofreading::parseResponse(source, source, {});
        QCOMPARE(parsed.status, LlmProofreading::Status::PlainText);
        QCOMPARE(parsed.correctedText, source);
        QVERIFY(parsed.issues.isEmpty());
    }

    void plainTextWhitespaceOnlyChange()
    {
        // stripCodeFence() trims the answer, so surrounding whitespace is not a
        // change: the corrected text stays the source and no issue is invented.
        for (const QString &source :
             {QStringLiteral("    Zeile"), QStringLiteral("Zeile ")}) {
            const LlmProofreading::Parsed parsed =
                LlmProofreading::parseResponse(source, source, {});
            QCOMPARE(parsed.status, LlmProofreading::Status::PlainText);
            QCOMPARE(parsed.correctedText, source);
            QVERIFY(parsed.issues.isEmpty());
        }
    }

    void plainTextWithJsonSlices()
    {
        // A slice of prose that happens to parse as JSON must not turn a
        // corrected text into Malformed or into an empty edits document.
        for (const QString &text : {QStringLiteral("Die Menge {} ist leer."),
                                    QStringLiteral("Wie in [1] gezeigt."),
                                    QStringLiteral("Die Liste [] ist leer.")}) {
            const LlmProofreading::Parsed parsed =
                LlmProofreading::parseResponse(text, text, {});
            QCOMPARE(parsed.status, LlmProofreading::Status::PlainText);
            QCOMPARE(parsed.correctedText, text);
            QVERIFY(parsed.issues.isEmpty());
        }
    }

    void parsesProseAroundEmptyDocument()
    {
        // A model may wrap the document in prose; an empty `edits` array is the
        // normal "no changes" answer, not a corrected text.
        const QString source = QStringLiteral("Das ist ein Hund.");
        const QString content = QStringLiteral("The text is fine: {\"edits\":[]}");

        const LlmProofreading::Parsed parsed =
            LlmProofreading::parseResponse(source, content, {});
        QCOMPARE(parsed.status, LlmProofreading::Status::Edits);
        QVERIFY(parsed.issues.isEmpty());
        QCOMPARE(parsed.droppedEdits, 0);
        QVERIFY(parsed.correctedText.isEmpty());
    }

    void diffEqualTexts()
    {
        const QString source = QStringLiteral("gleich");
        QVERIFY(LlmProofreading::diffIssues(source, source).isEmpty());
        QVERIFY(LlmProofreading::diffIssues(QString(), QString()).isEmpty());
    }

    void diffChangedLines()
    {
        const QString source = QStringLiteral("eins\nzwei\ndrei");
        const QVector<ProofreadingIssue> issues =
            LlmProofreading::diffIssues(source, QStringLiteral("EINS\nZWEI\ndrei"));

        QCOMPARE(issues.size(), 2);
        QCOMPARE(issues.at(0).offset, 0);
        QCOMPARE(issues.at(0).length, 4);
        QCOMPARE(issues.at(0).replacements, QStringList{QStringLiteral("EINS")});
        QCOMPARE(issues.at(1).offset, 5);
        QCOMPARE(issues.at(1).length, 4);
        QCOMPARE(issues.at(1).replacements, QStringList{QStringLiteral("ZWEI")});
        for (const ProofreadingIssue &issue : issues) {
            QCOMPARE(issue.ruleId, QStringLiteral("llm-diff"));
            QCOMPARE(issue.category, kGrammar);
            QCOMPARE(issue.severity, 0);
            QCOMPARE(issue.message, QStringLiteral("the corrected text differs here"));
        }
    }

    void diffDifferentLineCounts()
    {
        const QVector<ProofreadingIssue> issues = LlmProofreading::diffIssues(
            QStringLiteral("eins\nzwei"), QStringLiteral("eins\nzwei\ndrei"));

        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().offset, 9);
        QCOMPARE(issues.first().length, 0);
        QCOMPARE(issues.first().replacements, QStringList{QStringLiteral("\ndrei")});
    }

    void diffInsertedLine()
    {
        const QVector<ProofreadingIssue> issues =
            LlmProofreading::diffIssues(QStringLiteral("a\nb"), QStringLiteral("a\nb\n"));

        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().offset, 3);
        QCOMPARE(issues.first().length, 0);
        QCOMPARE(issues.first().replacements, QStringList{QStringLiteral("\n")});
    }

    void diffDeletion()
    {
        const QVector<ProofreadingIssue> issues =
            LlmProofreading::diffIssues(QStringLiteral("Hello world"), QStringLiteral("Hello"));

        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().offset, 5);
        QCOMPARE(issues.first().length, 6);
        QCOMPARE(issues.first().replacements, QStringList{QString()});
    }

    void diffSurrogateBoundary()
    {
        // The cut must stay outside the emoji: offset 1 / length 2, not 1 / 3.
        const QVector<ProofreadingIssue> issues = LlmProofreading::diffIssues(
            QStringLiteral("a\U0001F600b"), QStringLiteral("a\U0001F601b"));

        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().offset, 1);
        QCOMPARE(issues.first().length, 2);
        QCOMPARE(issues.first().replacements, QStringList{QStringLiteral("\U0001F601")});
    }

    void diffCorrectedInvariant_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("corrected");

        QTest::newRow("two changed lines")
            << "eins\nzwei\ndrei" << "EINS\nZWEI\ndrei";
        QTest::newRow("inserted line") << "eins\nzwei" << "eins\nzwei\ndrei";
        QTest::newRow("trailing newline") << "a\nb" << "a\nb\n";
        QTest::newRow("deletion") << "Hello world" << "Hello";
        QTest::newRow("surrogate boundary") << "a\U0001F600b" << "a\U0001F601b";
        QTest::newRow("empty source") << "" << "Hallo";
        QTest::newRow("empty correction") << "Hallo" << "";
        QTest::newRow("punctuation") << "Das ist ein Hund." << "Das ist ein Hund!";
        QTest::newRow("line count and content")
            << "a\nb\nc" << "a\nB\nc\nd";
    }

    void diffCorrectedInvariant()
    {
        QFETCH(QString, source);
        QFETCH(QString, corrected);

        QCOMPARE(
            ProofreadingText::corrected(source, LlmProofreading::diffIssues(source, corrected)),
            corrected);
    }
};

QTEST_GUILESS_MAIN(TestLlmProofreadingParser)
#include "tst_llmproofreadingparser.moc"
