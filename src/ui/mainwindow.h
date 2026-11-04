#pragma once

#include "core/translationtypes.h"

#include <QList>
#include <QMainWindow>

class LanguageComboBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

// Main translator window: source text on the left, translation on the right.
// Knows nothing about services and the network: it emits translateRequested()
// and AppController feeds the outcome back (see doc/architecture.md, §4.13).
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    struct ServiceEntry
    {
        QString id;
        QString name;
    };

    explicit MainWindow(QWidget *parent = nullptr);

    // Shows the window, restores it from minimized state and brings it to front.
    void showAndActivate();

    // When true (default), closing the window hides it to the tray instead of
    // closing it. Turned off when there is no tray or the application quits.
    void setCloseHidesToTray(bool hide);

    // Languages of the active service (internal codes); "Auto-detect" is added
    // to the source list.
    void setLanguages(const QStringList &sourceCodes, const QStringList &targetCodes);
    void setSourceLanguage(const QString &code);
    void setTargetLanguage(const QString &code);

    // The service selector is enabled only when there is a choice.
    void setServices(const QList<ServiceEntry> &services, const QString &activeId);

    // A translation requested through translateRequested() has started / finished.
    void showTranslationStarted();
    void showResult(const TranslationResult &result);

signals:
    void translateRequested(const TranslationRequest &request);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void requestTranslation();
    void swapLanguages();
    void copyResult();
    void setStatus(const QString &text, bool isError = false);
    void updateSwapButton();

    LanguageComboBox *m_sourceLang = nullptr;
    QToolButton *m_swapButton = nullptr;
    LanguageComboBox *m_targetLang = nullptr;
    QComboBox *m_service = nullptr;
    QPlainTextEdit *m_sourceEdit = nullptr;
    QPlainTextEdit *m_resultEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_translateButton = nullptr;
    QPushButton *m_copyButton = nullptr;

    QString m_detectedLang; // of the last translation; lets ⇄ work with "Auto-detect"
    bool m_closeHidesToTray = true;
};
