#include "ui/proofreadingpopupwindow.h"

#include "core/popupplacement.h"
#include "core/proofreadingtext.h"
#include "ui/proofreadingpanel.h"

#include <QCloseEvent>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

namespace {

constexpr int kDefaultWidth = 460;
constexpr int kMinWidth = 340;
constexpr int kMaxWidth = 800;
constexpr int kMargin = 12;
constexpr int kSpacing = 8;
constexpr int kAnchorOffset = 14;

} // namespace

ProofreadingPopupWindow::ProofreadingPopupWindow(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("proofreadingPopup"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setAutoFillBackground(true);
    setFixedWidth(kDefaultWidth);
    setStyleSheet(QStringLiteral("QPlainTextEdit { border: 0; background: transparent; }"
                                 "QPushButton { padding: 3px 10px; }"));

    buildUi();
}

void ProofreadingPopupWindow::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
    root->setSpacing(kSpacing);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("proofreadingPopupTitle"));
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);

    // The panel owns the issue list, the states and the whole-selection
    // actions; this window only adds the corrected-text preview and Cancel.
    m_panel = new ProofreadingPanel(this);
    m_panel->setExternalMode(true);

    m_correctedLabel = new QLabel(this);
    m_correctedLabel->setObjectName(QStringLiteral("proofreadingCorrectedLabel"));

    m_preview = new QPlainTextEdit(this);
    m_preview->setObjectName(QStringLiteral("proofreadingCorrected"));
    m_preview->setReadOnly(true);
    m_preview->setFrameShape(QFrame::NoFrame);
    m_preview->setTabChangesFocus(true);
    m_preview->setMinimumHeight(70);
    m_preview->setMaximumHeight(140);

    m_caveat = new QLabel(this);
    m_caveat->setObjectName(QStringLiteral("proofreadingCaveat"));
    m_caveat->setWordWrap(true);
    m_caveat->setStyleSheet(QStringLiteral("color: palette(placeholderText);"));

    m_cancelButton = new QPushButton(this);
    m_cancelButton->setObjectName(QStringLiteral("proofreadingCancel"));

    auto *footer = new QHBoxLayout;
    footer->addStretch();
    footer->addWidget(m_cancelButton);

    root->addWidget(m_title);
    root->addWidget(m_panel, 1);
    root->addWidget(m_correctedLabel);
    root->addWidget(m_preview);
    root->addWidget(m_caveat);
    root->addLayout(footer);

    // The panel's Replace selection is enabled by hasIssues() only, so the
    // guard is the contract of replaceRequested() for the caller.
    connect(m_panel, &ProofreadingPanel::replaceAllRequested, this, [this] {
        if (!m_panel->hasIssues())
            return;
        emit replaceRequested();
    });
    connect(m_panel, &ProofreadingPanel::copyCorrectedRequested, this,
            &ProofreadingPopupWindow::copyRequested);
    connect(m_cancelButton, &QPushButton::clicked, this, [this] { hide(); });

    retranslateUi();
}

void ProofreadingPopupWindow::showChecking(const QString &text, const QPoint &anchor)
{
    m_text = text;
    m_anchor = anchor;
    m_closedEmitted = false;
    m_preview->clear();
    m_panel->showChecking();
    updateGeometryAndPlacement();
    show();
    raise();
}

void ProofreadingPopupWindow::showResult(const ProofreadingResult &result)
{
    if (!isVisible() || result.error == TranslationError::Cancelled)
        return;

    m_panel->showResult(result);
    if (result.ok() && !result.issues.isEmpty())
        m_preview->setPlainText(correctedText());
    else
        m_preview->clear();
    updateGeometryAndPlacement();
}

QString ProofreadingPopupWindow::correctedText() const
{
    return ProofreadingText::corrected(m_text, m_panel->issues());
}

void ProofreadingPopupWindow::setPreferredWidth(int width)
{
    const int bounded = qBound(kMinWidth, width, kMaxWidth);
    if (m_width == bounded)
        return;
    m_width = bounded;
    setFixedWidth(m_width);
    if (isVisible())
        updateGeometryAndPlacement();
}

void ProofreadingPopupWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void ProofreadingPopupWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
}

void ProofreadingPopupWindow::closeEvent(QCloseEvent *event)
{
    emitClosedOnce();
    QWidget::closeEvent(event);
}

void ProofreadingPopupWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    emitClosedOnce();
}

void ProofreadingPopupWindow::retranslateUi()
{
    setWindowTitle(tr("Check the selection"));
    m_title->setText(tr("Check the selection"));
    m_correctedLabel->setText(tr("Corrected text:"));
    m_preview->setAccessibleName(tr("Corrected text:"));
    m_caveat->setText(tr("The selection is replaced only after you confirm. Undo depends on the "
                         "other application and may not work."));
    m_cancelButton->setText(tr("Cancel"));
}

void ProofreadingPopupWindow::updateGeometryAndPlacement()
{
    QScreen *screen = QGuiApplication::screenAt(m_anchor);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);

    layout()->activate();
    const int maxHeight = qMin(600, qMax(240, available.height() * 7 / 10));
    const int contentHeight = qMax(280, layout()->totalSizeHint().height());
    setFixedSize(m_width, qMin(maxHeight, contentHeight));
    layout()->activate();

    move(PopupPlacement::positionNear(m_anchor, size(), available, kAnchorOffset));
}

void ProofreadingPopupWindow::emitClosedOnce()
{
    if (m_closedEmitted)
        return;
    m_closedEmitted = true;
    emit closed();
}
