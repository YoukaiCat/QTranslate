#include "core/proofreadingtext.h"
#include "core/proofreadingtypes.h"
#include "core/translationtypes.h"
#include "ui/proofreadingpanel.h"
#include "ui/proofreadingpopupwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

namespace {

const QPoint kAnchor(200, 200);

ProofreadingIssue makeIssue(int offset, int length, const QString &message,
                            const QStringList &replacements, const QString &ruleId)
{
    ProofreadingIssue issue;
    issue.offset = offset;
    issue.length = length;
    issue.message = message;
    issue.replacements = replacements;
    issue.ruleId = ruleId;
    return issue;
}

ProofreadingResult makeResult(const QVector<ProofreadingIssue> &issues)
{
    ProofreadingResult result;
    result.issues = issues;
    result.serviceId = QStringLiteral("fake");
    return result;
}

ProofreadingResult makeError(TranslationError error, const QString &message)
{
    ProofreadingResult result;
    result.error = error;
    result.errorMessage = message;
    result.serviceId = QStringLiteral("fake");
    return result;
}

QStringList replacements(const QString &first, const QString &second = {})
{
    QStringList values{first};
    if (!second.isNull())
        values.append(second);
    return values;
}

} // namespace

class TestProofreadingPopup : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<ProofreadingResult>();
        qRegisterMetaType<TranslationRequest>();
        qRegisterMetaType<TranslationResult>();
    }

    void nonActivatingLayout()
    {
        ProofreadingPopupWindow window;
        QVERIFY(window.windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
        QVERIFY(window.testAttribute(Qt::WA_ShowWithoutActivating));

        window.showChecking(QStringLiteral("Some text"), kAnchor);
        QTRY_VERIFY(window.isVisible());

        auto *replace = window.findChild<QPushButton *>(QStringLiteral("proofreadingReplace"));
        auto *ignore = window.findChild<QPushButton *>(QStringLiteral("proofreadingIgnore"));
        auto *combo =
            window.findChild<QComboBox *>(QStringLiteral("proofreadingReplacement"));
        auto *replaceAll =
            window.findChild<QPushButton *>(QStringLiteral("proofreadingReplaceAll"));
        auto *copy = window.findChild<QPushButton *>(QStringLiteral("proofreadingCopy"));
        QVERIFY(replace);
        QVERIFY(ignore);
        QVERIFY(combo);
        QVERIFY(replaceAll);
        QVERIFY(copy);

        // A synthetic paste replaces the whole selection: no fragment actions.
        QVERIFY(replace->isHidden());
        QVERIFY(ignore->isHidden());
        QVERIFY(combo->isHidden());
        QVERIFY(!replaceAll->isHidden());
        QVERIFY(!copy->isHidden());
        QCOMPARE(replaceAll->text(), QStringLiteral("Replace selection"));
        QCOMPARE(copy->text(), QStringLiteral("Copy corrected text"));

        window.hide();
    }

    void checkingState()
    {
        ProofreadingPopupWindow window;
        window.showChecking(QStringLiteral("Some text"), kAnchor);
        QTRY_VERIFY(window.isVisible());

        auto *state = window.findChild<QLabel *>(QStringLiteral("proofreadingState"));
        auto *list = window.findChild<QListWidget *>(QStringLiteral("proofreadingIssues"));
        auto *preview =
            window.findChild<QPlainTextEdit *>(QStringLiteral("proofreadingCorrected"));
        auto *replaceAll =
            window.findChild<QPushButton *>(QStringLiteral("proofreadingReplaceAll"));
        auto *copy = window.findChild<QPushButton *>(QStringLiteral("proofreadingCopy"));
        QVERIFY(state);
        QVERIFY(list);
        QVERIFY(preview);
        QVERIFY(replaceAll);
        QVERIFY(copy);

        QCOMPARE(state->text(), QStringLiteral("Checking…"));
        QCOMPARE(list->count(), 0);
        QVERIFY(preview->toPlainText().isEmpty());
        QVERIFY(!replaceAll->isEnabled());
        QVERIFY(!copy->isEnabled());

        window.hide();
    }

    void issuesAndPreview()
    {
        ProofreadingPopupWindow window;
        window.setPreferredWidth(500);
        window.showChecking(QStringLiteral("abcdef"), kAnchor);
        QTRY_VERIFY(window.isVisible());

        window.showResult(
            makeResult({makeIssue(0, 1, QStringLiteral("First"),
                                  replacements(QStringLiteral("A")), QStringLiteral("R1")),
                        makeIssue(3, 1, QStringLiteral("Second"),
                                  replacements(QStringLiteral("D")), QStringLiteral("R2"))}));

        auto *panel = window.findChild<ProofreadingPanel *>();
        auto *list = window.findChild<QListWidget *>(QStringLiteral("proofreadingIssues"));
        auto *state = window.findChild<QLabel *>(QStringLiteral("proofreadingState"));
        auto *preview =
            window.findChild<QPlainTextEdit *>(QStringLiteral("proofreadingCorrected"));
        auto *replaceAll =
            window.findChild<QPushButton *>(QStringLiteral("proofreadingReplaceAll"));
        auto *copy = window.findChild<QPushButton *>(QStringLiteral("proofreadingCopy"));
        QVERIFY(panel);
        QVERIFY(list);
        QVERIFY(state);
        QVERIFY(preview);
        QVERIFY(replaceAll);
        QVERIFY(copy);

        QCOMPARE(list->count(), 2);
        QVERIFY2(list->item(0)->text().contains(QStringLiteral("First")),
                 qPrintable(list->item(0)->text()));
        QVERIFY2(list->item(1)->text().contains(QStringLiteral("Second")),
                 qPrintable(list->item(1)->text()));
        QCOMPARE(state->text(), QString());

        // The preview and correctedText() are the shared core rule, not a
        // second implementation in the window.
        QCOMPARE(preview->toPlainText(), QStringLiteral("AbcDef"));
        QCOMPARE(preview->toPlainText(), ProofreadingText::corrected(QStringLiteral("abcdef"),
                                                                    panel->issues()));
        QCOMPARE(window.correctedText(), QStringLiteral("AbcDef"));

        QVERIFY(replaceAll->isEnabled());
        QVERIFY(copy->isEnabled());
        QCOMPARE(window.width(), 500);

        window.hide();
    }

    void noIssuesAndErrorStates()
    {
        ProofreadingPopupWindow window;
        window.showChecking(QStringLiteral("Clean text"), kAnchor);
        QTRY_VERIFY(window.isVisible());

        auto *state = window.findChild<QLabel *>(QStringLiteral("proofreadingState"));
        auto *list = window.findChild<QListWidget *>(QStringLiteral("proofreadingIssues"));
        auto *preview =
            window.findChild<QPlainTextEdit *>(QStringLiteral("proofreadingCorrected"));
        auto *replaceAll =
            window.findChild<QPushButton *>(QStringLiteral("proofreadingReplaceAll"));
        auto *copy = window.findChild<QPushButton *>(QStringLiteral("proofreadingCopy"));
        QVERIFY(state);
        QVERIFY(list);
        QVERIFY(preview);
        QVERIFY(replaceAll);
        QVERIFY(copy);

        window.showResult(makeResult({}));
        QCOMPARE(state->text(), QStringLiteral("No issues found."));
        QCOMPARE(list->count(), 0);
        QVERIFY(preview->toPlainText().isEmpty());
        QVERIFY(!replaceAll->isEnabled());
        QVERIFY(!copy->isEnabled());

        window.showResult(makeError(TranslationError::RateLimited, {}));
        QVERIFY2(state->text().contains(QStringLiteral("Too many requests")),
                 qPrintable(state->text()));
        QCOMPARE(list->count(), 0);
        QVERIFY(!replaceAll->isEnabled());
        QVERIFY(preview->toPlainText().isEmpty());

        // The manager's online gate arrives as errorMessage and is shown as is.
        const QString gate =
            QStringLiteral("Enable online text checking in Settings → Text checking.");
        window.showResult(makeError(TranslationError::ServiceUnavailable, gate));
        QVERIFY2(state->text().contains(gate), qPrintable(state->text()));
        QVERIFY2(state->text().contains(QStringLiteral("The text checking service is unavailable")),
                 qPrintable(state->text()));
        QCOMPARE(list->count(), 0);
        QVERIFY(!replaceAll->isEnabled());
        QVERIFY(!copy->isEnabled());
        QVERIFY(preview->toPlainText().isEmpty());

        window.hide();
    }

    void signalsAndClosedOnce()
    {
        ProofreadingPopupWindow window;
        QSignalSpy replaceSpy(&window, &ProofreadingPopupWindow::replaceRequested);
        QSignalSpy copySpy(&window, &ProofreadingPopupWindow::copyRequested);
        QSignalSpy closedSpy(&window, &ProofreadingPopupWindow::closed);

        auto *replaceAll =
            window.findChild<QPushButton *>(QStringLiteral("proofreadingReplaceAll"));
        auto *copy = window.findChild<QPushButton *>(QStringLiteral("proofreadingCopy"));
        auto *cancel = window.findChild<QPushButton *>(QStringLiteral("proofreadingCancel"));
        QVERIFY(replaceAll);
        QVERIFY(copy);
        QVERIFY(cancel);

        window.showChecking(QStringLiteral("abcdef"), kAnchor);
        QTRY_VERIFY(window.isVisible());
        QCOMPARE(closedSpy.count(), 0);

        // The confirmation is disabled while checking, so a click means nothing.
        replaceAll->click();
        QCOMPARE(replaceSpy.count(), 0);

        window.showResult(
            makeResult({makeIssue(0, 1, QStringLiteral("First"),
                                  replacements(QStringLiteral("A")), QStringLiteral("R1"))}));
        QVERIFY(replaceAll->isEnabled());
        replaceAll->click();
        QCOMPARE(replaceSpy.count(), 1);

        copy->click();
        QCOMPARE(copySpy.count(), 1);
        QCOMPARE(closedSpy.count(), 0);

        cancel->click();
        QTRY_VERIFY(!window.isVisible());
        QCOMPARE(closedSpy.count(), 1);

        // A second hide is not a second close.
        window.hide();
        QCOMPARE(closedSpy.count(), 1);

        // A new showChecking() resets the flag for the next close.
        window.showChecking(QStringLiteral("abcdef"), kAnchor);
        QTRY_VERIFY(window.isVisible());
        QCOMPARE(closedSpy.count(), 1);
        window.close();
        QTRY_VERIFY(!window.isVisible());
        QCOMPARE(closedSpy.count(), 2);
    }

    void languageChangePreservesList()
    {
        ProofreadingPopupWindow window;
        window.showChecking(QStringLiteral("abcdef"), kAnchor);
        QTRY_VERIFY(window.isVisible());
        window.showResult(
            makeResult({makeIssue(0, 1, QStringLiteral("First"),
                                  replacements(QStringLiteral("A"), QStringLiteral("B")),
                                  QStringLiteral("R1")),
                        makeIssue(3, 1, QStringLiteral("Second"),
                                  replacements(QStringLiteral("D")), QStringLiteral("R2"))}));

        auto *panel = window.findChild<ProofreadingPanel *>();
        auto *title =
            window.findChild<QLabel *>(QStringLiteral("proofreadingPopupTitle"));
        auto *caveat = window.findChild<QLabel *>(QStringLiteral("proofreadingCaveat"));
        auto *list = window.findChild<QListWidget *>(QStringLiteral("proofreadingIssues"));
        auto *preview =
            window.findChild<QPlainTextEdit *>(QStringLiteral("proofreadingCorrected"));
        auto *replaceAll =
            window.findChild<QPushButton *>(QStringLiteral("proofreadingReplaceAll"));
        auto *cancel = window.findChild<QPushButton *>(QStringLiteral("proofreadingCancel"));
        QVERIFY(panel);
        QVERIFY(title);
        QVERIFY(caveat);
        QVERIFY(list);
        QVERIFY(preview);
        QVERIFY(replaceAll);
        QVERIFY(cancel);

        QCOMPARE(title->text(), QStringLiteral("Check the selection"));
        QVERIFY2(caveat->text().contains(QStringLiteral("replaced only after you confirm")),
                 qPrintable(caveat->text()));
        QVERIFY2(caveat->text().contains(QStringLiteral("Undo")), qPrintable(caveat->text()));
        QCOMPARE(cancel->text(), QStringLiteral("Cancel"));

        const QString previewText = preview->toPlainText();
        QCOMPARE(previewText, QStringLiteral("AbcDef"));

        QEvent languageChange(QEvent::LanguageChange);
        QCoreApplication::sendEvent(&window, &languageChange);
        QCoreApplication::sendEvent(panel, &languageChange);

        QCOMPARE(list->count(), 2);
        QCOMPARE(preview->toPlainText(), previewText);
        QCOMPARE(replaceAll->text(), QStringLiteral("Replace selection"));
        QCOMPARE(cancel->text(), QStringLiteral("Cancel"));
        QCOMPARE(title->text(), QStringLiteral("Check the selection"));

        window.hide();
    }

    void clipboardUntouched()
    {
        QGuiApplication::clipboard()->setText(QStringLiteral("sentinel-proofreading-popup"));

        ProofreadingPopupWindow window;
        window.showChecking(QStringLiteral("abcdef"), kAnchor);
        QTRY_VERIFY(window.isVisible());
        window.showResult(
            makeResult({makeIssue(0, 1, QStringLiteral("First"),
                                  replacements(QStringLiteral("A")), QStringLiteral("R1"))}));

        auto *replaceAll =
            window.findChild<QPushButton *>(QStringLiteral("proofreadingReplaceAll"));
        auto *copy = window.findChild<QPushButton *>(QStringLiteral("proofreadingCopy"));
        QVERIFY(replaceAll);
        QVERIFY(copy);

        // The window emits signals; it never copies by itself.
        replaceAll->click();
        copy->click();
        window.correctedText();
        window.hide();

        QCOMPARE(QGuiApplication::clipboard()->text(),
                 QStringLiteral("sentinel-proofreading-popup"));
    }

    void resultIgnoredWhenHiddenOrCancelled()
    {
        ProofreadingPopupWindow window;
        window.showChecking(QStringLiteral("abcdef"), kAnchor);
        QTRY_VERIFY(window.isVisible());
        window.showResult(
            makeResult({makeIssue(0, 1, QStringLiteral("First"),
                                  replacements(QStringLiteral("A")), QStringLiteral("R1"))}));

        auto *state = window.findChild<QLabel *>(QStringLiteral("proofreadingState"));
        auto *list = window.findChild<QListWidget *>(QStringLiteral("proofreadingIssues"));
        auto *preview =
            window.findChild<QPlainTextEdit *>(QStringLiteral("proofreadingCorrected"));
        auto *replaceAll =
            window.findChild<QPushButton *>(QStringLiteral("proofreadingReplaceAll"));
        QVERIFY(state);
        QVERIFY(list);
        QVERIFY(preview);
        QVERIFY(replaceAll);
        QCOMPARE(list->count(), 1);

        window.hide();
        QTRY_VERIFY(!window.isVisible());

        // A result for a closed window must not leak into the next invocation.
        window.showResult(
            makeResult({makeIssue(0, 1, QStringLiteral("Hidden"),
                                  replacements(QStringLiteral("X")), QStringLiteral("R1")),
                        makeIssue(3, 1, QStringLiteral("Hidden too"),
                                  replacements(QStringLiteral("Y")), QStringLiteral("R2"))}));
        QCOMPARE(list->count(), 1);

        window.showChecking(QStringLiteral("abcdef"), kAnchor);
        QTRY_VERIFY(window.isVisible());
        QCOMPARE(state->text(), QStringLiteral("Checking…"));
        QCOMPARE(list->count(), 0);
        QVERIFY(preview->toPlainText().isEmpty());
        QVERIFY(!replaceAll->isEnabled());

        // Cancelled results keep the checking state instead of showing an error.
        window.showResult(makeError(TranslationError::Cancelled, {}));
        QCOMPARE(state->text(), QStringLiteral("Checking…"));
        QCOMPARE(list->count(), 0);
        QVERIFY(!replaceAll->isEnabled());

        window.hide();
    }
};

QTEST_MAIN(TestProofreadingPopup)
#include "tst_proofreadingpopup.moc"
