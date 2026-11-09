#include "core/proofreadingtext.h"
#include "core/proofreadingtypes.h"
#include "core/translationtypes.h"
#include "ui/mainwindow.h"
#include "ui/proofreadingpanel.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSharedPointer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <algorithm>

namespace {

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

// A modal dialog opened by a MainWindow action runs its own event loop, so the
// click has to come from a timer inside that loop. The flag is asserted after
// the action returns: QVERIFY inside the timer would not fail the test.
struct ModalAction
{
    QString buttonObjectName; // or "escape"
    bool clicked = false;
    QString previewText;
};

using ModalActionPtr = QSharedPointer<ModalAction>;

ModalActionPtr scheduleModalAction(const QString &buttonObjectName)
{
    auto action = QSharedPointer<ModalAction>::create();
    action->buttonObjectName = buttonObjectName;
    QTimer::singleShot(0, qApp, [action] {
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal) {
            // The offscreen platform can miss activeModalWidget(); a single
            // visible top-level dialog is the same thing here.
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (widget->isVisible() && qobject_cast<QDialog *>(widget)) {
                    modal = widget;
                    break;
                }
            }
        }
        if (!modal)
            return;
        if (auto *preview = modal->findChild<QPlainTextEdit *>(
                QStringLiteral("proofreadingPreview"))) {
            action->previewText = preview->toPlainText();
        }
        if (action->buttonObjectName == QLatin1String("escape")) {
            action->clicked = true;
            QTest::keyClick(modal, Qt::Key_Escape);
            return;
        }
        auto *button = modal->findChild<QPushButton *>(action->buttonObjectName);
        if (!button)
            return;
        action->clicked = true;
        button->click();
    });
    return action;
}

} // namespace

class TestProofreadingPanel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<TranslationRequest>();
        qRegisterMetaType<TranslationResult>();
        qRegisterMetaType<ProofreadingResult>();
    }

    void checkButtonVisibility()
    {
        MainWindow window;
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *check = window.findChild<QPushButton *>(QStringLiteral("check"));
        auto *status = window.findChild<QLabel *>(QStringLiteral("status"));
        auto *panel = window.findChild<ProofreadingPanel *>();
        QVERIFY(source);
        QVERIFY(check);
        QVERIFY(status);
        QVERIFY(panel);

        QVERIFY(check->isHidden());
        QVERIFY(panel->isHidden());

        window.setProofreadingAvailable(true);
        QVERIFY(!check->isHidden());
        QVERIFY(!panel->isHidden());

        QSignalSpy checkSpy(&window, &MainWindow::checkRequested);
        check->click();
        QCOMPARE(status->text(), QStringLiteral("Nothing to check"));
        QCOMPARE(checkSpy.count(), 0);

        source->setPlainText(QStringLiteral("Some text"));
        check->click();
        QCOMPARE(checkSpy.count(), 1);
        QCOMPARE(checkSpy.first().at(0).toString(), QStringLiteral("Some text"));
        QCOMPARE(status->text(), QString());

        auto *state = panel->findChild<QLabel *>(QStringLiteral("proofreadingState"));
        QVERIFY(state);
        QCOMPARE(state->text(), QStringLiteral("Checking…"));
    }

    void showsIssuesAndHighlights()
    {
        MainWindow window;
        window.setProofreadingAvailable(true);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *panel = window.findChild<ProofreadingPanel *>();
        QVERIFY(source);
        QVERIFY(panel);
        source->setPlainText(QStringLiteral("abcdefghij"));

        window.showProofreadingResult(
            makeResult({makeIssue(1, 2, QStringLiteral("First message"),
                                  replacements(QStringLiteral("X")), QStringLiteral("R1")),
                        makeIssue(5, 2, QStringLiteral("Second message"),
                                  replacements(QStringLiteral("Y")), QStringLiteral("R2"))}));

        auto *list = panel->findChild<QListWidget *>(QStringLiteral("proofreadingIssues"));
        auto *state = panel->findChild<QLabel *>(QStringLiteral("proofreadingState"));
        QVERIFY(list);
        QVERIFY(state);
        QCOMPARE(list->count(), 2);
        QVERIFY2(list->item(0)->text().contains(QStringLiteral("First message")),
                 qPrintable(list->item(0)->text()));
        QVERIFY2(list->item(0)->text().contains(QStringLiteral("Rule: R1")),
                 qPrintable(list->item(0)->text()));
        QVERIFY2(list->item(1)->text().contains(QStringLiteral("Second message")),
                 qPrintable(list->item(1)->text()));
        QVERIFY(!panel->isHidden());
        QCOMPARE(state->text(), QString());
        QCOMPARE(source->extraSelections().size(), 2);
    }

    void noIssuesState()
    {
        MainWindow window;
        window.setProofreadingAvailable(true);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *panel = window.findChild<ProofreadingPanel *>();
        QVERIFY(source);
        QVERIFY(panel);
        source->setPlainText(QStringLiteral("Clean text"));

        window.showProofreadingResult(makeResult({}));

        auto *list = panel->findChild<QListWidget *>(QStringLiteral("proofreadingIssues"));
        auto *state = panel->findChild<QLabel *>(QStringLiteral("proofreadingState"));
        QVERIFY(list);
        QVERIFY(state);
        QCOMPARE(state->text(), QStringLiteral("No issues found."));
        QCOMPARE(list->count(), 0);
        QCOMPARE(source->extraSelections().size(), 0);
    }

    void errorStates()
    {
        MainWindow window;
        window.setProofreadingAvailable(true);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *panel = window.findChild<ProofreadingPanel *>();
        auto *check = window.findChild<QPushButton *>(QStringLiteral("check"));
        QVERIFY(source);
        QVERIFY(panel);
        QVERIFY(check);
        source->setPlainText(QStringLiteral("abc"));

        auto *list = panel->findChild<QListWidget *>(QStringLiteral("proofreadingIssues"));
        auto *state = panel->findChild<QLabel *>(QStringLiteral("proofreadingState"));
        QVERIFY(list);
        QVERIFY(state);

        const QString gate = QStringLiteral(
            "Enable online text checking in Settings → Text checking.");
        window.showProofreadingResult(makeError(TranslationError::ServiceUnavailable, gate));
        QVERIFY2(state->text().contains(gate), qPrintable(state->text()));
        QCOMPARE(list->count(), 0);
        QCOMPARE(source->extraSelections().size(), 0);
        QVERIFY(!check->isHidden());
        QVERIFY(check->isEnabled());

        window.showProofreadingResult(makeError(TranslationError::RateLimited, {}));
        QVERIFY2(state->text().contains(QStringLiteral("Too many requests")),
                 qPrintable(state->text()));
        QCOMPARE(list->count(), 0);
        QCOMPARE(source->extraSelections().size(), 0);
    }

    void replacementChoicesAndApply()
    {
        MainWindow window;
        window.setProofreadingAvailable(true);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *panel = window.findChild<ProofreadingPanel *>();
        QVERIFY(source);
        QVERIFY(panel);
        source->setPlainText(QStringLiteral("abcdefghij"));

        window.showProofreadingResult(
            makeResult({makeIssue(0, 2, QStringLiteral("First"),
                                  replacements(QStringLiteral("XX"), QStringLiteral("YY")),
                                  QStringLiteral("R1")),
                        makeIssue(5, 2, QStringLiteral("Second"),
                                  replacements(QStringLiteral("ZZ"), QStringLiteral("WW")),
                                  QStringLiteral("R2"))}));

        auto *list = panel->findChild<QListWidget *>(QStringLiteral("proofreadingIssues"));
        auto *combo = panel->findChild<QComboBox *>(QStringLiteral("proofreadingReplacement"));
        auto *replace = panel->findChild<QPushButton *>(QStringLiteral("proofreadingReplace"));
        QVERIFY(list);
        QVERIFY(combo);
        QVERIFY(replace);

        list->setCurrentRow(1);
        QCOMPARE(combo->count(), 2);
        QCOMPARE(combo->currentIndex(), 0);
        QCOMPARE(combo->itemText(0), QStringLiteral("ZZ"));
        QCOMPARE(combo->itemText(1), QStringLiteral("WW"));

        combo->setCurrentIndex(1);
        QEvent languageChange(QEvent::LanguageChange);
        QCoreApplication::sendEvent(panel, &languageChange);
        QCOMPARE(combo->currentIndex(), 1);
        QVERIFY2(list->item(1)->text().contains(QStringLiteral("Rule:")),
                 qPrintable(list->item(1)->text()));

        QSignalSpy checkSpy(&window, &MainWindow::checkRequested);
        replace->click();

        QCOMPARE(source->toPlainText(), QStringLiteral("abcdeWWhij"));
        QCOMPARE(checkSpy.count(), 1);
        QCOMPARE(source->extraSelections().size(), 0);
    }

    void singleUndoAfterReplace()
    {
        MainWindow window;
        window.setProofreadingAvailable(true);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *panel = window.findChild<ProofreadingPanel *>();
        QVERIFY(source);
        QVERIFY(panel);
        source->setPlainText(QStringLiteral("abcdefghij"));

        window.showProofreadingResult(
            makeResult({makeIssue(5, 2, QStringLiteral("Second"),
                                  replacements(QStringLiteral("WW")), QStringLiteral("R2"))}));

        auto *list = panel->findChild<QListWidget *>(QStringLiteral("proofreadingIssues"));
        auto *replace = panel->findChild<QPushButton *>(QStringLiteral("proofreadingReplace"));
        QVERIFY(list);
        QVERIFY(replace);
        list->setCurrentRow(0);
        replace->click();
        QCOMPARE(source->toPlainText(), QStringLiteral("abcdeWWhij"));

        source->undo();
        QCOMPARE(source->toPlainText(), QStringLiteral("abcdefghij"));
    }

    void emptyReplacementEmptiesTextAndPanel()
    {
        MainWindow window;
        window.setProofreadingAvailable(true);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *panel = window.findChild<ProofreadingPanel *>();
        auto *status = window.findChild<QLabel *>(QStringLiteral("status"));
        QVERIFY(source);
        QVERIFY(panel);
        QVERIFY(status);
        source->setPlainText(QStringLiteral("abc"));

        window.showProofreadingResult(
            makeResult({makeIssue(0, 3, QStringLiteral("Whole text"),
                                  replacements(QString()), QStringLiteral("R1"))}));

        auto *list = panel->findChild<QListWidget *>(QStringLiteral("proofreadingIssues"));
        auto *replace = panel->findChild<QPushButton *>(QStringLiteral("proofreadingReplace"));
        QVERIFY(list);
        QVERIFY(replace);
        QCOMPARE(list->count(), 1);
        list->setCurrentRow(0);

        // An empty replacement is applicable: it covers the whole text.
        QVERIFY(replace->isEnabled());
        replace->click();

        QVERIFY(source->toPlainText().isEmpty());
        QCOMPARE(list->count(), 0);
        QCOMPARE(source->extraSelections().size(), 0);
        QCOMPARE(status->text(), QStringLiteral("Nothing to check"));
    }

    void ignoreRemovesRowOnly()
    {
        MainWindow window;
        window.setProofreadingAvailable(true);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *panel = window.findChild<ProofreadingPanel *>();
        QVERIFY(source);
        QVERIFY(panel);
        source->setPlainText(QStringLiteral("abcdefghij"));

        window.showProofreadingResult(
            makeResult({makeIssue(1, 2, QStringLiteral("First"),
                                  replacements(QStringLiteral("X")), QStringLiteral("R1")),
                        makeIssue(5, 2, QStringLiteral("Second"),
                                  replacements(QStringLiteral("Y")), QStringLiteral("R2"))}));

        auto *list = panel->findChild<QListWidget *>(QStringLiteral("proofreadingIssues"));
        auto *ignore = panel->findChild<QPushButton *>(QStringLiteral("proofreadingIgnore"));
        QVERIFY(list);
        QVERIFY(ignore);
        list->setCurrentRow(0);

        QSignalSpy checkSpy(&window, &MainWindow::checkRequested);
        ignore->click();

        QCOMPARE(list->count(), 1);
        QCOMPARE(source->extraSelections().size(), 1);
        QCOMPARE(source->toPlainText(), QStringLiteral("abcdefghij"));
        QCOMPARE(checkSpy.count(), 0);
    }

    void replaceAllAppliesAllAndSkips()
    {
        MainWindow window;
        window.setProofreadingAvailable(true);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *panel = window.findChild<ProofreadingPanel *>();
        QVERIFY(source);
        QVERIFY(panel);
        source->setPlainText(QStringLiteral("abc def ghi"));

        const ProofreadingResult checked =
            makeResult({makeIssue(0, 3, QStringLiteral("First"),
                                  replacements(QStringLiteral("AAA")), QStringLiteral("R1")),
                        makeIssue(4, 3, QStringLiteral("Middle"), {}, QStringLiteral("R2")),
                        makeIssue(8, 3, QStringLiteral("Last"),
                                  replacements(QStringLiteral("CCC")), QStringLiteral("R3"))});
        window.showProofreadingResult(checked);

        auto *replaceAll =
            panel->findChild<QPushButton *>(QStringLiteral("proofreadingReplaceAll"));
        QVERIFY(replaceAll);
        QSignalSpy checkSpy(&window, &MainWindow::checkRequested);

        ModalActionPtr action = scheduleModalAction(QStringLiteral("proofreadingPreviewOk"));
        replaceAll->click();
        QVERIFY(action->clicked);
        QCOMPARE(action->previewText, QStringLiteral("AAA def CCC"));
        QCOMPARE(source->toPlainText(), QStringLiteral("AAA def CCC"));
        QCOMPARE(checkSpy.count(), 0);

        source->undo();
        QCOMPARE(source->toPlainText(), QStringLiteral("abc def ghi"));

        // Cancelling the preview leaves the text and the list untouched.
        window.showProofreadingResult(checked);
        action = scheduleModalAction(QStringLiteral("proofreadingPreviewCancel"));
        replaceAll->click();
        QVERIFY(action->clicked);
        QCOMPARE(source->toPlainText(), QStringLiteral("abc def ghi"));
        QCOMPARE(checkSpy.count(), 0);
    }

    void copyCorrectedText()
    {
        MainWindow window;
        window.setProofreadingAvailable(true);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *panel = window.findChild<ProofreadingPanel *>();
        QVERIFY(source);
        QVERIFY(panel);
        source->setPlainText(QStringLiteral("abcdef"));

        window.showProofreadingResult(
            makeResult({makeIssue(0, 1, QStringLiteral("First"),
                                  replacements(QStringLiteral("A")), QStringLiteral("R1")),
                        makeIssue(3, 1, QStringLiteral("Second"),
                                  replacements(QStringLiteral("D")), QStringLiteral("R2"))}));

        auto *copy = panel->findChild<QPushButton *>(QStringLiteral("proofreadingCopy"));
        QVERIFY(copy);
        QGuiApplication::clipboard()->setText(QStringLiteral("sentinel"));
        copy->click();

        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("AbcDef"));
        QCOMPARE(source->toPlainText(), QStringLiteral("abcdef"));
        QCOMPARE(source->extraSelections().size(), 2);
    }

    void autoReplaceAppliesOnce()
    {
        MainWindow window;
        window.setProofreadingAvailable(true);
        window.setProofreadingOptions(false, true);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *panel = window.findChild<ProofreadingPanel *>();
        QVERIFY(source);
        QVERIFY(panel);
        source->setPlainText(QStringLiteral("abc def"));

        const ProofreadingResult checked =
            makeResult({makeIssue(0, 3, QStringLiteral("First"),
                                  replacements(QStringLiteral("AAA")), QStringLiteral("R1")),
                        makeIssue(4, 3, QStringLiteral("Second"),
                                  replacements(QStringLiteral("BBB")), QStringLiteral("R2"))});
        QSignalSpy checkSpy(&window, &MainWindow::checkRequested);
        window.showProofreadingResult(checked);

        QCOMPARE(source->toPlainText(), QStringLiteral("AAA BBB"));
        QCOMPARE(checkSpy.count(), 0);
        auto *list = panel->findChild<QListWidget *>(QStringLiteral("proofreadingIssues"));
        QVERIFY(list);
        QCOMPARE(list->count(), 0);
        QCOMPARE(source->extraSelections().size(), 0);

        source->undo();
        QCOMPARE(source->toPlainText(), QStringLiteral("abc def"));

        // With the option off nothing is applied without a click.
        window.setProofreadingOptions(false, false);
        window.showProofreadingResult(checked);
        QCOMPARE(source->toPlainText(), QStringLiteral("abc def"));
        QCOMPARE(list->count(), 2);
    }

    void checkBeforeTranslationWithIssues()
    {
        MainWindow window;
        window.setProofreadingAvailable(true);
        window.setProofreadingOptions(true, false);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *translate = window.findChild<QPushButton *>(QStringLiteral("translate"));
        QVERIFY(source);
        QVERIFY(translate);
        source->setPlainText(QStringLiteral("abc def"));

        QSignalSpy checkSpy(&window, &MainWindow::checkRequested);
        QSignalSpy translateSpy(&window, &MainWindow::translateRequested);

        translate->click();
        QCOMPARE(checkSpy.count(), 1);
        QCOMPARE(translateSpy.count(), 0);

        const ProofreadingResult checked =
            makeResult({makeIssue(0, 3, QStringLiteral("First"),
                                  replacements(QStringLiteral("XYZ")), QStringLiteral("R1"))});

        ModalActionPtr action = scheduleModalAction(QStringLiteral("translateAnyway"));
        window.showProofreadingResult(checked);
        QVERIFY(action->clicked);
        QCOMPARE(translateSpy.count(), 1);
        QCOMPARE(translateSpy.at(0).at(0).value<TranslationRequest>().text,
                 QStringLiteral("abc def"));
        QCOMPARE(source->toPlainText(), QStringLiteral("abc def"));

        translate->click();
        QCOMPARE(checkSpy.count(), 2);
        action = scheduleModalAction(QStringLiteral("applyCorrectionsAndTranslate"));
        window.showProofreadingResult(checked);
        QVERIFY(action->clicked);
        QCOMPARE(translateSpy.count(), 2);
        QCOMPARE(source->toPlainText(), QStringLiteral("XYZ def"));
        QCOMPARE(translateSpy.at(1).at(0).value<TranslationRequest>().text,
                 QStringLiteral("XYZ def"));

        translate->click();
        QCOMPARE(checkSpy.count(), 3);
        action = scheduleModalAction(QStringLiteral("escape"));
        window.showProofreadingResult(checked);
        QVERIFY(action->clicked);
        QCOMPARE(translateSpy.count(), 2);
        QCOMPARE(source->toPlainText(), QStringLiteral("XYZ def"));
    }

    void checkBeforeTranslationWithoutIssues()
    {
        MainWindow window;
        window.setProofreadingAvailable(true);
        window.setProofreadingOptions(true, false);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *translate = window.findChild<QPushButton *>(QStringLiteral("translate"));
        auto *panel = window.findChild<ProofreadingPanel *>();
        QVERIFY(source);
        QVERIFY(translate);
        QVERIFY(panel);
        source->setPlainText(QStringLiteral("Clean text"));

        QSignalSpy checkSpy(&window, &MainWindow::checkRequested);
        QSignalSpy translateSpy(&window, &MainWindow::translateRequested);

        translate->click();
        QCOMPARE(checkSpy.count(), 1);
        window.showProofreadingResult(makeResult({}));
        QCOMPARE(translateSpy.count(), 1);
        QCOMPARE(translateSpy.at(0).at(0).value<TranslationRequest>().text,
                 QStringLiteral("Clean text"));

        translate->click();
        QCOMPARE(checkSpy.count(), 2);
        window.showProofreadingResult(
            makeError(TranslationError::Network, QStringLiteral("host not found")));
        QCOMPARE(translateSpy.count(), 2);
        auto *state = panel->findChild<QLabel *>(QStringLiteral("proofreadingState"));
        QVERIFY(state);
        QVERIFY2(state->text().contains(QStringLiteral("Network error")),
                 qPrintable(state->text()));
    }

    void checkBeforeTranslationOff()
    {
        MainWindow window;
        window.setProofreadingAvailable(true);
        window.setProofreadingOptions(false, false);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *translate = window.findChild<QPushButton *>(QStringLiteral("translate"));
        QVERIFY(source);
        QVERIFY(translate);
        source->setPlainText(QStringLiteral("abc"));

        QSignalSpy checkSpy(&window, &MainWindow::checkRequested);
        QSignalSpy translateSpy(&window, &MainWindow::translateRequested);
        translate->click();
        QCOMPARE(translateSpy.count(), 1);
        QCOMPARE(checkSpy.count(), 0);
    }

    void autoTranslateBypassesCheck()
    {
        MainWindow window;
        window.setProofreadingAvailable(true);
        window.setProofreadingOptions(true, false);
        window.setAutoTranslateDelay(10);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        QVERIFY(source);

        QSignalSpy checkSpy(&window, &MainWindow::checkRequested);
        QSignalSpy translateSpy(&window, &MainWindow::translateRequested);
        source->setPlainText(QStringLiteral("Some text"));

        QTRY_COMPARE_WITH_TIMEOUT(translateSpy.count(), 1, 5000);
        QCOMPARE(checkSpy.count(), 0);
    }

    void autoTranslateDoesNotCancelCheck()
    {
        MainWindow window;
        window.setProofreadingAvailable(true);
        window.setAutoTranslateDelay(10);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *check = window.findChild<QPushButton *>(QStringLiteral("check"));
        auto *panel = window.findChild<ProofreadingPanel *>();
        QVERIFY(source);
        QVERIFY(check);
        QVERIFY(panel);

        QSignalSpy checkSpy(&window, &MainWindow::checkRequested);
        QSignalSpy translateSpy(&window, &MainWindow::translateRequested);

        source->setPlainText(QStringLiteral("Some text"));
        check->click();
        QCOMPARE(checkSpy.count(), 1);

        // The typing timer would fire here and cancel the explicit check.
        QTest::qWait(100);
        QCOMPARE(checkSpy.count(), 1);
        QCOMPARE(translateSpy.count(), 0);

        // A replacement must not start the automatic translation either.
        window.showProofreadingResult(
            makeResult({makeIssue(0, 4, QStringLiteral("First"),
                                  replacements(QStringLiteral("This")), QStringLiteral("R1"))}));
        auto *list = panel->findChild<QListWidget *>(QStringLiteral("proofreadingIssues"));
        auto *replace = panel->findChild<QPushButton *>(QStringLiteral("proofreadingReplace"));
        QVERIFY(list);
        QVERIFY(replace);
        QCOMPARE(list->count(), 1);
        list->setCurrentRow(0);
        replace->click();
        QCOMPARE(checkSpy.count(), 2);

        QTest::qWait(100);
        QCOMPARE(translateSpy.count(), 0);
    }

    void translationAndDisableClearPanel()
    {
        MainWindow window;
        window.setProofreadingAvailable(true);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *panel = window.findChild<ProofreadingPanel *>();
        auto *check = window.findChild<QPushButton *>(QStringLiteral("check"));
        QVERIFY(source);
        QVERIFY(panel);
        QVERIFY(check);
        source->setPlainText(QStringLiteral("Some text"));

        auto *list = panel->findChild<QListWidget *>(QStringLiteral("proofreadingIssues"));
        QVERIFY(list);
        const ProofreadingResult checked =
            makeResult({makeIssue(0, 4, QStringLiteral("First"),
                                  replacements(QStringLiteral("This")), QStringLiteral("R1"))});
        QSignalSpy cancelSpy(&window, &MainWindow::checkCancelled);
        QSignalSpy translateSpy(&window, &MainWindow::translateRequested);

        window.showProofreadingResult(checked);
        QCOMPARE(list->count(), 1);
        QCOMPARE(source->extraSelections().size(), 1);
        window.showTranslationStarted();
        QCOMPARE(list->count(), 0);
        QCOMPARE(source->extraSelections().size(), 0);
        QCOMPARE(cancelSpy.count(), 1);

        window.showProofreadingResult(checked);
        QCOMPARE(list->count(), 1);
        window.translateText(QStringLiteral("Some text"));
        QCOMPARE(list->count(), 0);
        QCOMPARE(source->extraSelections().size(), 0);
        QCOMPARE(cancelSpy.count(), 2);
        QCOMPARE(translateSpy.count(), 1);

        window.showProofreadingResult(checked);
        QCOMPARE(list->count(), 1);
        window.showTranslation(TranslationRequest{QStringLiteral("Some text"),
                                                  QStringLiteral("en"), QStringLiteral("ru")},
                               TranslationResult{});
        QCOMPARE(list->count(), 0);
        QCOMPARE(source->extraSelections().size(), 0);
        QCOMPARE(cancelSpy.count(), 3);

        window.showProofreadingResult(checked);
        QCOMPARE(list->count(), 1);
        window.setProofreadingAvailable(false);
        QVERIFY(panel->isHidden());
        QVERIFY(check->isHidden());
        QCOMPARE(list->count(), 0);
        QCOMPARE(source->extraSelections().size(), 0);
        QCOMPARE(cancelSpy.count(), 4);
    }

    void editingClearsAndCancels()
    {
        MainWindow window;
        window.setProofreadingAvailable(true);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *panel = window.findChild<ProofreadingPanel *>();
        QVERIFY(source);
        QVERIFY(panel);
        source->setPlainText(QStringLiteral("abcdef"));
        const ProofreadingResult checked =
            makeResult({makeIssue(1, 2, QStringLiteral("First"),
                                  replacements(QStringLiteral("X")), QStringLiteral("R1"))});
        window.showProofreadingResult(checked);

        auto *list = panel->findChild<QListWidget *>(QStringLiteral("proofreadingIssues"));
        QVERIFY(list);
        QCOMPARE(list->count(), 1);
        QCOMPARE(source->extraSelections().size(), 1);

        QSignalSpy cancelSpy(&window, &MainWindow::checkCancelled);
        source->insertPlainText(QStringLiteral("x"));
        QCOMPARE(list->count(), 0);
        QCOMPARE(source->extraSelections().size(), 0);
        QCOMPARE(cancelSpy.count(), 1);

        window.showProofreadingResult(checked);
        QCOMPARE(list->count(), 1);
        window.show();
        window.close();
        QCOMPARE(cancelSpy.count(), 2);
    }

    void correctedTextRules()
    {
        const QString text = QStringLiteral("abcdef");

        // Two non-overlapping issues, applied from the end.
        QCOMPARE(ProofreadingText::corrected(
                     text, {makeIssue(0, 1, {}, replacements(QStringLiteral("A")),
                                      QStringLiteral("R1")),
                            makeIssue(3, 1, {}, replacements(QStringLiteral("D")),
                                      QStringLiteral("R2"))}),
                 QStringLiteral("AbcDef"));

        // At the same offset the longer range wins, the shorter overlaps it.
        QCOMPARE(ProofreadingText::corrected(
                     text, {makeIssue(1, 1, {}, replacements(QStringLiteral("S")),
                                      QStringLiteral("R2")),
                            makeIssue(1, 3, {}, replacements(QStringLiteral("LONG")),
                                      QStringLiteral("R1"))}),
                 QStringLiteral("aLONGef"));

        // Out of range and empty replacements are skipped.
        QCOMPARE(ProofreadingText::corrected(
                     text, {makeIssue(100, 1, {}, replacements(QStringLiteral("X")),
                                      QStringLiteral("R1")),
                            makeIssue(0, 1, {}, {}, QStringLiteral("R2"))}),
                 text);

        // A zero-length issue inserts text.
        QCOMPARE(ProofreadingText::corrected(
                     text, {makeIssue(3, 0, {}, replacements(QStringLiteral("X")),
                                      QStringLiteral("R1"))}),
                 QStringLiteral("abcXdef"));

        // Nothing applies: the original text comes back.
        QCOMPARE(ProofreadingText::corrected(
                     text, {makeIssue(-1, 1, {}, replacements(QStringLiteral("X")),
                                      QStringLiteral("R1"))}),
                 text);

        QVERIFY(ProofreadingText::isApplicable(
            text, makeIssue(3, 3, {}, replacements(QStringLiteral("X")),
                            QStringLiteral("R1"))));
        QVERIFY(!ProofreadingText::isApplicable(
            text, makeIssue(3, 4, {}, replacements(QStringLiteral("X")),
                            QStringLiteral("R1"))));
        QVERIFY(!ProofreadingText::isApplicable(
            text, makeIssue(0, 1, {}, {}, QStringLiteral("R1"))));

        // A range must not split a surrogate pair.
        const QString pair = QString::fromUtf8("\xF0\x9F\x98\x80");
        QCOMPARE(pair.size(), 2);
        QVERIFY(!ProofreadingText::isApplicable(
            pair, makeIssue(1, 0, {}, replacements(QStringLiteral("X")),
                            QStringLiteral("R1"))));
        QVERIFY(ProofreadingText::isApplicable(
            pair, makeIssue(0, 2, {}, replacements(QStringLiteral("X")),
                            QStringLiteral("R1"))));
    }
};

QTEST_MAIN(TestProofreadingPanel)
#include "tst_proofreadingpanel.moc"