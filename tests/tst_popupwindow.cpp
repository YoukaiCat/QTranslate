#include "core/languages.h"
#include "ui/popupwindow.h"

#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

namespace {

TranslationRequest request(const QString &text, const QString &target = QStringLiteral("ru"))
{
    return {text, Languages::autoCode(), target};
}

TranslationResult success(const QString &text)
{
    TranslationResult result;
    result.translatedText = text;
    result.detectedSourceLang = QStringLiteral("en");
    result.serviceId = QStringLiteral("fake");
    return result;
}

TranslationResult failure(TranslationError error, const QString &message)
{
    TranslationResult result;
    result.error = error;
    result.errorMessage = message;
    result.serviceId = QStringLiteral("fake");
    return result;
}

} // namespace

class TestPopupWindow : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<TranslationRequest>();
        qRegisterMetaType<TranslationResult>();
    }

    void loadingEmitsRequest()
    {
        PopupWindow popup;
        popup.setLanguages({QStringLiteral("de"), QStringLiteral("ru")});
        popup.setTargetLanguage(QStringLiteral("ru"));

        QSignalSpy requested(&popup, &PopupWindow::translateRequested);
        popup.showLoading(request(QStringLiteral("Hello")), QPoint(100, 100));

        QVERIFY(popup.isVisible());
        QCOMPARE(requested.size(), 1);
        const auto actual = requested.first().first().value<TranslationRequest>();
        QCOMPARE(actual.text, QStringLiteral("Hello"));
        QCOMPARE(actual.sourceLang, Languages::autoCode());
        QCOMPARE(actual.targetLang, QStringLiteral("ru"));

        popup.hide();
    }

    void resultIsShownAndCopied()
    {
        PopupWindow popup;
        popup.setLanguages({QStringLiteral("ru")});
        popup.setTargetLanguage(QStringLiteral("ru"));
        popup.showLoading(request(QStringLiteral("Hello")), QPoint(100, 100));

        popup.showResult(success(QStringLiteral("Привет")));

        auto *result = popup.findChild<QPlainTextEdit *>(QStringLiteral("popupResult"));
        QVERIFY(result);
        QCOMPARE(result->toPlainText(), QStringLiteral("Привет"));

        auto *copy = popup.findChild<QPushButton *>(QStringLiteral("popupCopy"));
        QVERIFY(copy);
        QVERIFY(copy->isEnabled());
        copy->click();
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("Привет"));

        popup.hide();
    }

    void openInMainWindowCarriesBothTexts()
    {
        PopupWindow popup;
        popup.setLanguages({QStringLiteral("ru")});
        popup.setTargetLanguage(QStringLiteral("ru"));
        const TranslationRequest source = request(QStringLiteral("Hello"));
        popup.showLoading(source, QPoint(100, 100));
        const TranslationResult translated = success(QStringLiteral("Привет"));
        popup.showResult(translated);

        QSignalSpy opened(&popup, &PopupWindow::openInMainWindowRequested);
        auto *openButton = popup.findChild<QPushButton *>(QStringLiteral("popupOpenMain"));
        QVERIFY(openButton);
        QVERIFY(openButton->isEnabled());
        openButton->click();

        QCOMPARE(opened.size(), 1);
        QCOMPARE(opened.first().at(0).value<TranslationRequest>().text,
                 QStringLiteral("Hello"));
        QCOMPARE(opened.first().at(1).value<TranslationResult>().translatedText,
                 QStringLiteral("Привет"));
        QVERIFY(!popup.isVisible());
    }

    void errorIsShownAndRetryRepeatsTheRequest()
    {
        PopupWindow popup;
        popup.setLanguages({QStringLiteral("ru")});
        popup.setTargetLanguage(QStringLiteral("ru"));
        popup.showLoading(request(QStringLiteral("Hello")), QPoint(100, 100));

        QSignalSpy requested(&popup, &PopupWindow::translateRequested);
        popup.showResult(failure(TranslationError::Network,
                                 QStringLiteral("Host not found")));

        auto *error = popup.findChild<QLabel *>(QStringLiteral("popupError"));
        QVERIFY(error);
        QVERIFY(error->text().contains(QStringLiteral("Network error")));
        QVERIFY(error->text().contains(QStringLiteral("Host not found")));

        auto *copy = popup.findChild<QPushButton *>(QStringLiteral("popupCopy"));
        QVERIFY(copy);
        QVERIFY(!copy->isEnabled());

        auto *retry = popup.findChild<QPushButton *>(QStringLiteral("popupRetry"));
        QVERIFY(retry);
        retry->click();

        QCOMPARE(requested.size(), 1);
        const auto actual = requested.first().first().value<TranslationRequest>();
        QCOMPARE(actual.text, QStringLiteral("Hello"));
        QCOMPARE(actual.targetLang, QStringLiteral("ru"));

        popup.hide();
    }

    void changingLanguageTranslatesAgain()
    {
        PopupWindow popup;
        popup.setLanguages({QStringLiteral("de"), QStringLiteral("ru")});
        popup.setTargetLanguage(QStringLiteral("ru"));
        popup.showLoading(request(QStringLiteral("Hello")), QPoint(100, 100));

        auto *language = popup.findChild<QToolButton *>(QStringLiteral("popupLanguages"));
        QVERIFY(language);
        QVERIFY(language->menu());
        QAction *german = nullptr;
        for (QAction *action : language->menu()->actions()) {
            if (action->data().toString() == QStringLiteral("de"))
                german = action;
        }
        QVERIFY(german);

        QSignalSpy requested(&popup, &PopupWindow::translateRequested);
        german->trigger();

        QCOMPARE(popup.targetLanguage(), QStringLiteral("de"));
        QCOMPARE(requested.size(), 1);
        const auto actual = requested.first().first().value<TranslationRequest>();
        QCOMPARE(actual.text, QStringLiteral("Hello"));
        QCOMPARE(actual.targetLang, QStringLiteral("de"));

        popup.hide();
    }

    void pinIsPerInvocation()
    {
        PopupWindow popup;
        popup.setLanguages({QStringLiteral("ru")});
        popup.setTargetLanguage(QStringLiteral("ru"));

        popup.showLoading(request(QStringLiteral("one")), QPoint(100, 100));
        popup.setPinned(true);
        QVERIFY(popup.isPinned());

        popup.showLoading(request(QStringLiteral("two")), QPoint(200, 200));
        QVERIFY(!popup.isPinned());

        popup.hide();
    }

    void noSelectionHintClosesItself()
    {
        PopupWindow popup;
        popup.setLanguages({QStringLiteral("ru")});
        popup.setTargetLanguage(QStringLiteral("ru"));

        QSignalSpy requested(&popup, &PopupWindow::translateRequested);
        popup.showNoSelection(QPoint(100, 100));

        QVERIFY(popup.isVisible());
        auto *hint = popup.findChild<QLabel *>(QStringLiteral("popupNoSelection"));
        QVERIFY(hint);
        QVERIFY(!hint->text().isEmpty());
        QCOMPARE(requested.size(), 0);

        QTRY_VERIFY_WITH_TIMEOUT(!popup.isVisible(), 3000);
    }
};

QTEST_MAIN(TestPopupWindow)
#include "tst_popupwindow.moc"
