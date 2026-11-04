#include "ui/popupwindow.h"

#include "core/appinfo.h"
#include "core/languages.h"
#include "core/popupplacement.h"
#include "ui/translationerrortext.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QStackedWidget>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kDefaultWidth = 460;
constexpr int kMinWidth = 320;
constexpr int kMaxWidth = 800;
constexpr int kMargin = 12;
constexpr int kSpacing = 8;
constexpr int kAnchorOffset = 14;
constexpr int kNoSelectionTimeoutMs = 1500;
constexpr int kFocusLossGuardMs = 250;

} // namespace

// A small rotating indicator; QProgressBar's busy mode looks too heavy in the
// compact popup.
class PopupSpinner final : public QWidget
{
public:
    explicit PopupSpinner(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(26, 26);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        m_timer.setInterval(70);
        QObject::connect(&m_timer, &QTimer::timeout, this, [this] {
            m_phase = (m_phase + 1) % kSpokes;
            update();
        });
    }

    void start()
    {
        m_phase = 0;
        m_timer.start();
        update();
    }

    void stop() { m_timer.stop(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(width() / 2.0, height() / 2.0);

        const QColor base = palette().color(QPalette::Highlight);
        for (int i = 0; i < kSpokes; ++i) {
            const int distance = (i - m_phase + kSpokes) % kSpokes;
            QColor color = base;
            color.setAlphaF(0.15 + 0.85 * (kSpokes - distance) / kSpokes);
            painter.save();
            painter.rotate(i * 360.0 / kSpokes);
            painter.setPen(QPen(color, 2.5, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(0, -10), QPointF(0, -4));
            painter.restore();
        }
    }

private:
    static constexpr int kSpokes = 8;
    QTimer m_timer;
    int m_phase = 0;
};

PopupWindow::PopupWindow(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("popupWindow"));
    setWindowTitle(AppInfo::name());
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(true);
    setFixedWidth(kDefaultWidth);
    setStyleSheet(QStringLiteral(
        "QToolButton { border: 0; padding: 2px 6px; border-radius: 4px; }"
        "QToolButton:hover { background-color: palette(midlight); }"
        "QToolButton:checked { background-color: palette(highlight);"
        " color: palette(highlighted-text); }"
        "QPlainTextEdit { border: 0; background: transparent; }"
        "QPushButton { padding: 3px 10px; }"));

    buildUi();

    m_noSelectionTimer = new QTimer(this);
    m_noSelectionTimer->setSingleShot(true);
    connect(m_noSelectionTimer, &QTimer::timeout, this, [this] { hide(); });

    m_focusLossTimer = new QTimer(this);
    m_focusLossTimer->setSingleShot(true);
    connect(m_focusLossTimer, &QTimer::timeout, this, &PopupWindow::closeIfFocusLost);

    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { hide(); });

    setState(State::Loading);
    updateLanguageButton();
    resize(m_width, 180);
}

PopupWindow::~PopupWindow() = default;

void PopupWindow::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
    root->setSpacing(kSpacing);

    // Top bar: language, service, pin, close.
    m_topBar = new QWidget(this);
    auto *topLayout = new QHBoxLayout(m_topBar);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(6);

    m_languageButton = new QToolButton(m_topBar);
    m_languageButton->setObjectName(QStringLiteral("popupLanguages"));
    m_languageButton->setPopupMode(QToolButton::InstantPopup);
    m_languageButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_languageButton->setAutoRaise(true);
    m_languageButton->setCursor(Qt::PointingHandCursor);
    m_languageButton->setToolTip(tr("Change the translation language"));
    m_languageButton->setAccessibleName(tr("Target language"));
    m_languageMenu = new QMenu(m_languageButton);
    m_languageButton->setMenu(m_languageMenu);
    connect(m_languageMenu, &QMenu::aboutToShow, this, [this] {
        m_languageMenuOpen = true;
        updateLanguageMenu();
    });
    connect(m_languageMenu, &QMenu::aboutToHide, this, [this] {
        m_languageMenuOpen = false;
        // Activation returns to the popup after the menu closes; give it a
        // moment before deciding that the focus went elsewhere.
        scheduleFocusLossCheck(120);
    });

    m_serviceLabel = new QLabel(m_topBar);
    m_serviceLabel->setObjectName(QStringLiteral("popupService"));
    m_serviceLabel->setStyleSheet(QStringLiteral("color: palette(placeholderText);"));

    m_pinButton = new QToolButton(m_topBar);
    m_pinButton->setObjectName(QStringLiteral("popupPin"));
    m_pinButton->setText(QStringLiteral("📌"));
    m_pinButton->setCheckable(true);
    m_pinButton->setToolTip(tr("Keep the window open when focus is lost"));
    m_pinButton->setAccessibleName(m_pinButton->toolTip());
    connect(m_pinButton, &QToolButton::toggled, this, &PopupWindow::setPinned);

    m_closeButton = new QToolButton(m_topBar);
    m_closeButton->setObjectName(QStringLiteral("popupClose"));
    m_closeButton->setText(QStringLiteral("✕"));
    m_closeButton->setToolTip(tr("Close (Esc)"));
    m_closeButton->setAccessibleName(m_closeButton->toolTip());
    connect(m_closeButton, &QToolButton::clicked, this, [this] { hide(); });

    topLayout->addWidget(m_languageButton);
    topLayout->addStretch();
    topLayout->addWidget(m_serviceLabel);
    topLayout->addWidget(m_pinButton);
    topLayout->addWidget(m_closeButton);

    // States.
    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("popupState"));

    m_loadingPage = new QWidget(m_stack);
    auto *loadingLayout = new QHBoxLayout(m_loadingPage);
    loadingLayout->setContentsMargins(4, 8, 4, 8);
    loadingLayout->setSpacing(10);
    m_spinner = new PopupSpinner(m_loadingPage);
    auto *loadingLabel = new QLabel(tr("Translating…"), m_loadingPage);
    loadingLabel->setObjectName(QStringLiteral("popupLoading"));
    loadingLayout->addWidget(m_spinner);
    loadingLayout->addWidget(loadingLabel);
    loadingLayout->addStretch();

    m_resultPage = new QWidget(m_stack);
    auto *resultLayout = new QVBoxLayout(m_resultPage);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    m_resultEdit = new QPlainTextEdit(m_resultPage);
    m_resultEdit->setObjectName(QStringLiteral("popupResult"));
    m_resultEdit->setAccessibleName(tr("Translation"));
    m_resultEdit->setReadOnly(true);
    m_resultEdit->setFrameShape(QFrame::NoFrame);
    m_resultEdit->setTabChangesFocus(true);
    resultLayout->addWidget(m_resultEdit);

    m_errorPage = new QWidget(m_stack);
    auto *errorLayout = new QVBoxLayout(m_errorPage);
    errorLayout->setContentsMargins(4, 4, 4, 4);
    errorLayout->setSpacing(8);
    m_errorLabel = new QLabel(m_errorPage);
    m_errorLabel->setObjectName(QStringLiteral("popupError"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_retryButton = new QPushButton(tr("Retry"), m_errorPage);
    m_retryButton->setObjectName(QStringLiteral("popupRetry"));
    errorLayout->addWidget(m_errorLabel);
    errorLayout->addWidget(m_retryButton, 0, Qt::AlignLeft);
    errorLayout->addStretch();
    connect(m_retryButton, &QPushButton::clicked, this, [this] {
        if (!m_request.text.isEmpty())
            startTranslation(m_request);
    });

    m_noSelectionPage = new QWidget(m_stack);
    auto *noSelectionLayout = new QVBoxLayout(m_noSelectionPage);
    noSelectionLayout->setContentsMargins(4, 4, 4, 4);
    auto *noSelectionLabel = new QLabel(tr("Nothing is selected"), m_noSelectionPage);
    noSelectionLabel->setObjectName(QStringLiteral("popupNoSelection"));
    noSelectionLabel->setWordWrap(true);
    noSelectionLayout->addWidget(noSelectionLabel);
    noSelectionLayout->addStretch();

    m_stack->addWidget(m_loadingPage);
    m_stack->addWidget(m_resultPage);
    m_stack->addWidget(m_errorPage);
    m_stack->addWidget(m_noSelectionPage);

    // Bottom actions shown for a successful translation only.
    m_bottomBar = new QWidget(this);
    auto *bottomLayout = new QHBoxLayout(m_bottomBar);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(8);
    m_copyButton = new QPushButton(tr("⧉ Copy"), m_bottomBar);
    m_copyButton->setObjectName(QStringLiteral("popupCopy"));
    m_copyButton->setEnabled(false);
    connect(m_copyButton, &QPushButton::clicked, this, &PopupWindow::copyResult);
    m_openMainButton = new QPushButton(tr("⇄ Open in Main Window"), m_bottomBar);
    m_openMainButton->setObjectName(QStringLiteral("popupOpenMain"));
    m_openMainButton->setEnabled(false);
    connect(m_openMainButton, &QPushButton::clicked, this,
            &PopupWindow::openInMainWindow);
    bottomLayout->addWidget(m_copyButton);
    bottomLayout->addWidget(m_openMainButton);
    bottomLayout->addStretch();

    root->addWidget(m_topBar);
    root->addWidget(m_stack);
    root->addWidget(m_bottomBar);
}

void PopupWindow::setLanguages(const QStringList &targetCodes)
{
    m_targetLanguages = targetCodes;
    std::sort(m_targetLanguages.begin(), m_targetLanguages.end(),
              [](const QString &a, const QString &b) {
                  return QString::localeAwareCompare(Languages::displayName(a),
                                                     Languages::displayName(b))
                      < 0;
              });

    m_languageMenu->clear();
    for (const QString &code : std::as_const(m_targetLanguages)) {
        QAction *action = m_languageMenu->addAction(Languages::displayName(code));
        action->setData(code);
        action->setCheckable(true);
        connect(action, &QAction::triggered, this,
                [this, code] { chooseTargetLanguage(code); });
    }

    if (!m_targetLanguages.contains(m_request.targetLang) && !m_targetLanguages.isEmpty())
        m_request.targetLang = m_targetLanguages.first();

    m_languageButton->setEnabled(!m_targetLanguages.isEmpty());
    updateLanguageMenu();
    updateLanguageButton();
}

void PopupWindow::setTargetLanguage(const QString &code)
{
    if (code.isEmpty())
        return;
    m_request.targetLang = code;
    m_detectedLang.clear();
    updateLanguageMenu();
    updateLanguageButton();
}

void PopupWindow::setServiceName(const QString &name)
{
    m_serviceLabel->setText(name);
    m_serviceLabel->setVisible(!name.isEmpty());
}

void PopupWindow::setAutoCloseOnFocusLoss(bool enabled)
{
    m_autoCloseOnFocusLoss = enabled;
}

void PopupWindow::setPreferredWidth(int width)
{
    const int bounded = qBound(kMinWidth, width, kMaxWidth);
    if (m_width == bounded)
        return;
    m_width = bounded;
    setFixedWidth(m_width);
    if (isVisible())
        updateGeometryAndPlacement();
}

void PopupWindow::setPinned(bool pinned)
{
    if (m_pinned == pinned)
        return;
    m_pinned = pinned;
    m_pinButton->setChecked(pinned);
    m_pinButton->setToolTip(pinned ? tr("Unpin (close the window when focus is lost)")
                                   : tr("Keep the window open when focus is lost"));
    m_pinButton->setAccessibleName(m_pinButton->toolTip());
}

void PopupWindow::showLoading(const TranslationRequest &request, const QPoint &anchor)
{
    m_anchor = anchor;
    m_shownAt.start();
    setPinned(false); // the pin is per invocation
    startTranslation(request);
}

void PopupWindow::showResult(const TranslationResult &result)
{
    if (!isVisible() || result.error == TranslationError::Cancelled)
        return;

    m_result = result;
    m_detectedLang = result.detectedSourceLang;
    m_spinner->stop();
    updateLanguageButton();

    if (!result.ok()) {
        m_errorLabel->setText(TranslationErrorText::forResult(result));
        m_copyButton->setEnabled(false);
        m_openMainButton->setEnabled(false);
        setState(State::Error);
    } else {
        m_resultEdit->setPlainText(result.translatedText);
        m_copyButton->setEnabled(!result.translatedText.isEmpty());
        m_openMainButton->setEnabled(!result.translatedText.isEmpty());
        setState(State::Result);
    }
    updateGeometryAndPlacement();
}

void PopupWindow::showNoSelection(const QPoint &anchor)
{
    m_anchor = anchor;
    m_shownAt.start();
    setPinned(false);
    m_request.text.clear();
    m_detectedLang.clear();
    m_result = TranslationResult{};
    m_spinner->stop();
    m_resultEdit->clear();
    m_copyButton->setEnabled(false);
    m_openMainButton->setEnabled(false);
    setState(State::NoSelection);
    updateLanguageButton();
    updateGeometryAndPlacement();
    show();
    raise();
    activateWindow();
    setFocus(Qt::ActiveWindowFocusReason);
    m_noSelectionTimer->start(kNoSelectionTimeoutMs);
}

void PopupWindow::startTranslation(const TranslationRequest &request)
{
    m_request = request;
    m_detectedLang.clear();
    m_result = TranslationResult{};
    m_resultEdit->clear();
    m_copyButton->setText(tr("⧉ Copy"));
    m_copyButton->setEnabled(false);
    m_openMainButton->setEnabled(false);
    m_noSelectionTimer->stop();
    m_spinner->start();
    setState(State::Loading);
    updateLanguageButton();
    updateGeometryAndPlacement();
    show();
    raise();
    activateWindow();
    setFocus(Qt::ActiveWindowFocusReason);
    emit translateRequested(m_request);
}

void PopupWindow::setState(State state)
{
    m_state = state;
    switch (state) {
    case State::Loading:
        m_stack->setCurrentWidget(m_loadingPage);
        break;
    case State::Result:
        m_stack->setCurrentWidget(m_resultPage);
        break;
    case State::Error:
        m_stack->setCurrentWidget(m_errorPage);
        break;
    case State::NoSelection:
        m_stack->setCurrentWidget(m_noSelectionPage);
        break;
    }
    m_bottomBar->setVisible(state == State::Result);
}

void PopupWindow::updateLanguageButton()
{
    QString source;
    if (m_request.sourceLang.isEmpty()
        || m_request.sourceLang == Languages::autoCode()) {
        source = m_detectedLang.isEmpty() ? tr("Auto")
                                          : Languages::displayName(m_detectedLang);
    } else {
        source = Languages::displayName(m_request.sourceLang);
    }
    const QString target = m_request.targetLang.isEmpty()
        ? tr("Language")
        : Languages::displayName(m_request.targetLang);

    m_languageButton->setText(tr("%1 → %2").arg(source, target));
}

void PopupWindow::updateLanguageMenu()
{
    for (QAction *action : m_languageMenu->actions())
        action->setChecked(action->data().toString() == m_request.targetLang);
}

void PopupWindow::chooseTargetLanguage(const QString &code)
{
    if (code.isEmpty() || code == m_request.targetLang)
        return;
    m_request.targetLang = code;
    m_detectedLang.clear();
    updateLanguageMenu();
    updateLanguageButton();
    if (!m_request.text.isEmpty())
        startTranslation(m_request);
}

void PopupWindow::copyResult()
{
    if (!m_result.ok() || m_result.translatedText.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(m_result.translatedText);
    m_copyButton->setText(tr("✓ Copied"));
    QTimer::singleShot(1500, this, [this] { m_copyButton->setText(tr("⧉ Copy")); });
}

void PopupWindow::openInMainWindow()
{
    if (!m_result.ok() || m_request.text.isEmpty())
        return;
    const TranslationRequest request = m_request;
    const TranslationResult result = m_result;
    hide();
    emit openInMainWindowRequested(request, result);
}

void PopupWindow::scheduleFocusLossCheck(int delayMs)
{
    if (m_focusLossTimer)
        m_focusLossTimer->start(delayMs);
}

void PopupWindow::closeIfFocusLost()
{
    if (!isVisible() || !m_autoCloseOnFocusLoss || m_pinned || m_languageMenuOpen)
        return;
    if (isActiveWindow())
        return;
    if (QApplication::activePopupWidget() || QApplication::activeModalWidget())
        return;

    // activateWindow() is asynchronous on Windows; do not hide the window in
    // the short moment between show() and the activation event.
    if (m_shownAt.isValid() && m_shownAt.elapsed() < kFocusLossGuardMs) {
        scheduleFocusLossCheck(int(kFocusLossGuardMs - m_shownAt.elapsed()) + 10);
        return;
    }

    // The language menu temporarily takes activation away from the popup.
    // While the application is still active and no other of our windows has
    // become active, this is just the menu closing: keep the popup. A click in
    // another application makes the whole application inactive, and a click in
    // the main window gives it activation; both must close the popup.
    if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
        hide();
        return;
    }
    QWidget *activeWindow = QApplication::activeWindow();
    if (activeWindow && activeWindow != this)
        hide();
}

void PopupWindow::updateGeometryAndPlacement()
{
    QScreen *screen = QGuiApplication::screenAt(m_anchor);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
    const int textWidth = m_width - 2 * kMargin - 28;

    int contentHeight = 60;
    switch (m_state) {
    case State::Loading:
        contentHeight = 56;
        break;
    case State::NoSelection:
        contentHeight = 48;
        break;
    case State::Error:
        contentHeight = qMax(72, m_errorLabel->heightForWidth(textWidth)
                                     + m_retryButton->sizeHint().height() + 16);
        break;
    case State::Result: {
        QTextDocument document;
        document.setDefaultFont(m_resultEdit->font());
        document.setPlainText(m_result.translatedText);
        document.setTextWidth(textWidth);
        contentHeight = qMax(56, int(std::ceil(document.size().height())) + 12);
        break;
    }
    }

    const int maxWindowHeight = qMin(600, qMax(180, available.height() * 7 / 10));
    int chrome = 2 * kMargin + 2; // window border
    chrome += m_topBar->sizeHint().height() + kSpacing;
    if (m_state == State::Result)
        chrome += kSpacing + m_bottomBar->sizeHint().height();

    contentHeight = qMin(contentHeight, qMax(40, maxWindowHeight - chrome));
    m_stack->setFixedHeight(contentHeight);
    setFixedSize(m_width, qMin(maxWindowHeight, chrome + contentHeight));
    layout()->activate();

    move(PopupPlacement::positionNear(m_anchor, size(), available, kAnchorOffset));
}

void PopupWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void PopupWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && !isActiveWindow() && isVisible())
        scheduleFocusLossCheck();
}

void PopupWindow::hideEvent(QHideEvent *event)
{
    m_spinner->stop();
    m_noSelectionTimer->stop();
    m_focusLossTimer->stop();
    m_languageMenuOpen = false;
    QWidget::hideEvent(event);
    emit closed();
}
