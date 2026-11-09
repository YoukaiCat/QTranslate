#pragma once

#include "core/proofreadingtypes.h"
#include "core/translationtypes.h"

#include <QList>
#include <QMainWindow>

class LanguageComboBox;
class ProofreadingPanel;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QToolButton;

// Main translator window: source text on the left, translation on the right.
// Knows nothing about services and the network: it emits translateRequested()
// and checkRequested() and AppController feeds the outcomes back (see
// doc/architecture.md, §4.13). It never talks to a manager or a service.
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

    // The checking mode is on, and a provider is registered.
    void setProofreadingAvailable(bool available);
    void setProofreadingOptions(bool checkBeforeTranslation, bool autoReplace);

    // A translation requested through translateRequested() has started / finished.
    void showTranslationStarted();
    void showResult(const TranslationResult &result);

    // A check requested through checkRequested() has finished (or failed).
    void showProofreadingResult(const ProofreadingResult &result);

    // Puts the text grabbed by the Ctrl+W scenario into the source field and
    // starts a translation with the current languages.
    void translateText(const QString &text);

    // 0 disables automatic translation while typing.
    void setAutoTranslateDelay(int ms);

    // Shows a translation transferred from the popup: sets the languages and
    // both texts without starting a new request.
    void showTranslation(const TranslationRequest &request, const TranslationResult &result);

    // Re-applies translatable texts after a language change.
    void retranslateUi();

signals:
    void translateRequested(const TranslationRequest &request);
    void checkRequested(const QString &text);
    void checkCancelled();
    void serviceChanged(const QString &id);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    enum class BeforeTranslationChoice { Dismissed, ApplyCorrections, TranslateAnyway };

    void buildUi();
    void requestTranslation(bool userInitiated = true);
    void swapLanguages();
    void copyResult();
    void setStatus(const QString &text, bool isError = false);
    void updateSwapButton();

    void startCheck();
    void onProofreadingResult(const ProofreadingResult &result);
    void updateHighlights();
    void selectIssue(int row);
    void applyReplacement(int row, int replacementIndex);
    void ignoreIssue(int row);
    void replaceAll(bool preview);
    void copyCorrected();
    BeforeTranslationChoice askBeforeTranslation(int issueCount);
    bool previewCorrections(const QString &corrected);
    void applyIssueEdits(const QVector<ProofreadingIssue> &issues);

    LanguageComboBox *m_sourceLang = nullptr;
    QToolButton *m_swapButton = nullptr;
    LanguageComboBox *m_targetLang = nullptr;
    QLabel *m_serviceLabel = nullptr;
    QComboBox *m_service = nullptr;
    QPlainTextEdit *m_sourceEdit = nullptr;
    QPlainTextEdit *m_resultEdit = nullptr;
    ProofreadingPanel *m_proofreadingPanel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_checkButton = nullptr;
    QPushButton *m_translateButton = nullptr;
    QPushButton *m_copyButton = nullptr;
    QTimer *m_autoTranslateTimer = nullptr;
    int m_autoTranslateDelayMs = 0;

    QString m_detectedLang; // of the last translation; lets ⇄ work with "Auto-detect"
    bool m_closeHidesToTray = true;
    bool m_proofreadingAvailable = false;
    bool m_checkBeforeTranslation = false;
    bool m_autoReplace = false;
    bool m_pendingTranslationCheck = false;
    bool m_applyingEdit = false;
};