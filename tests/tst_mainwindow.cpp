#include "core/translationtypes.h"
#include "ui/mainwindow.h"
#include "ui/proofreadingpanel.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

// The translation window must stay a translation window: these tests prove the
// checking surface is gone and that the translation paths still work. The
// checking-window coverage lives in tst_proofreadingwindow.
class TestMainWindow : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<TranslationRequest>();
        qRegisterMetaType<TranslationResult>();
    }

    void init() { QGuiApplication::clipboard()->clear(); }

    void translationWindowHasNoCheckingUi()
    {
        MainWindow window;
        QVERIFY(!window.findChild<ProofreadingPanel *>());
        QVERIFY(!window.findChild<QPushButton *>(QStringLiteral("check")));
        QVERIFY(!window.findChild<QWidget *>(QStringLiteral("proofreadingPanel")));
        QVERIFY(window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText")));
        QVERIFY(window.findChild<QPushButton *>(QStringLiteral("translate")));
        QVERIFY(window.findChild<QPushButton *>(QStringLiteral("copy")));
    }

    void translatingEmitsTheRequest()
    {
        MainWindow window;
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        auto *translate = window.findChild<QPushButton *>(QStringLiteral("translate"));
        QVERIFY(source);
        QVERIFY(translate);
        source->setPlainText(QStringLiteral("Some text"));

        QSignalSpy spy(&window, &MainWindow::translateRequested);
        translate->click();

        QCOMPARE(spy.count(), 1);
        const TranslationRequest request = spy.first().at(0).value<TranslationRequest>();
        QCOMPARE(request.text, QStringLiteral("Some text"));
    }

    void autoTranslateRequestsTranslation()
    {
        MainWindow window;
        window.setAutoTranslateDelay(10);
        auto *source = window.findChild<QPlainTextEdit *>(QStringLiteral("sourceText"));
        QVERIFY(source);

        QSignalSpy spy(&window, &MainWindow::translateRequested);
        source->setPlainText(QStringLiteral("Some text"));

        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    }

    void copyTranslation()
    {
        MainWindow window;
        window.showTranslationStarted();

        TranslationResult result;
        result.translatedText = QStringLiteral("Translated text");
        window.showResult(result);

        auto *copy = window.findChild<QPushButton *>(QStringLiteral("copy"));
        QVERIFY(copy);
        QVERIFY(copy->isEnabled());
        copy->click();

        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("Translated text"));
    }
};

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
