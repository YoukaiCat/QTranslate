#pragma once

#include "core/translationtypes.h"

#include <QElapsedTimer>
#include <QPoint>
#include <QStringList>
#include <QWidget>

class QLabel;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTimer;
class QToolButton;
class PopupSpinner;

// Frameless always-on-top window next to the cursor for the Ctrl+Q scenario
// (see doc/architecture.md, §4.13). It knows nothing about services and the
// network: it emits translateRequested() and AppController feeds the result
// back through showResult().
class PopupWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PopupWindow(QWidget *parent = nullptr);
    ~PopupWindow() override;

    // Target languages of the active service, shown in the language menu.
    void setLanguages(const QStringList &targetCodes);
    // Changes the target without starting a translation (used for defaults).
    void setTargetLanguage(const QString &code);
    QString targetLanguage() const { return m_request.targetLang; }

    void setServiceName(const QString &name);
    void setAutoCloseOnFocusLoss(bool enabled);
    void setPreferredWidth(int width);

    bool isPinned() const { return m_pinned; }
    void setPinned(bool pinned);

    // Shows the window at `anchor` (global cursor position) in the loading
    // state and emits translateRequested(). The request is remembered for
    // retry and for changing the target language.
    void showLoading(const TranslationRequest &request, const QPoint &anchor);

    // Switches to the result or error state. Ignored if the window is hidden.
    void showResult(const TranslationResult &result);

    // Short "nothing selected" hint that closes itself after 1.5 s.
    void showNoSelection(const QPoint &anchor);

signals:
    void translateRequested(const TranslationRequest &request);
    void openInMainWindowRequested(const TranslationRequest &request,
                                   const TranslationResult &result);
    // The window was hidden (Esc, ✕, focus loss, no-selection timeout, ...).
    void closed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    enum class State { Loading, Result, Error, NoSelection };

    void buildUi();
    void setState(State state);
    void startTranslation(const TranslationRequest &request);
    void updateLanguageButton();
    void updateLanguageMenu();
    void chooseTargetLanguage(const QString &code);
    void copyResult();
    void openInMainWindow();
    void scheduleFocusLossCheck(int delayMs = 0);
    void closeIfFocusLost();
    void updateGeometryAndPlacement();

    QWidget *m_topBar = nullptr;
    QToolButton *m_languageButton = nullptr;
    QMenu *m_languageMenu = nullptr;
    QLabel *m_serviceLabel = nullptr;
    QToolButton *m_pinButton = nullptr;
    QToolButton *m_closeButton = nullptr;

    QStackedWidget *m_stack = nullptr;
    QWidget *m_loadingPage = nullptr;
    QWidget *m_resultPage = nullptr;
    QWidget *m_errorPage = nullptr;
    QWidget *m_noSelectionPage = nullptr;
    PopupSpinner *m_spinner = nullptr;
    QPlainTextEdit *m_resultEdit = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_retryButton = nullptr;

    QWidget *m_bottomBar = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_openMainButton = nullptr;

    QTimer *m_noSelectionTimer = nullptr;
    QTimer *m_focusLossTimer = nullptr;
    QElapsedTimer m_shownAt;

    QStringList m_targetLanguages;
    TranslationRequest m_request;
    TranslationResult m_result;
    QString m_detectedLang;
    QPoint m_anchor;
    int m_width = 460;
    State m_state = State::Loading;
    bool m_pinned = false;
    bool m_autoCloseOnFocusLoss = true;
    bool m_languageMenuOpen = false;
};
