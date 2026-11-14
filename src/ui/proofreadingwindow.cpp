#include "ui/proofreadingwindow.h"

#include "core/proofreadingtext.h"
#include "ui/proofreadingpanel.h"

#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>

ProofreadingWindow::ProofreadingWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    resize(720, 380);
}

void ProofreadingWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    // Source text on the left, the issue list on the right.
    auto *splitter = new QSplitter(Qt::Horizontal, central);
    m_sourceEdit = new QPlainTextEdit(splitter);
    m_sourceEdit->setObjectName(QStringLiteral("sourceText"));
    m_panel = new ProofreadingPanel(splitter);
    m_panel->setObjectName(QStringLiteral("proofreadingPanel"));
    // A fixed-ish pane next to the editor, as before; hidden until the
    // checking mode is on.
    m_panel->setMinimumWidth(240);
    m_panel->setMaximumWidth(320);
    m_panel->hide();
    splitter->addWidget(m_sourceEdit);
    splitter->addWidget(m_panel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setChildrenCollapsible(false);
    layout->addWidget(splitter, 1);

    auto *bottomBar = new QHBoxLayout;
    m_checkButton = new QPushButton(central);
    m_checkButton->setObjectName(QStringLiteral("check"));
    m_checkButton->hide();
    bottomBar->addStretch();
    bottomBar->addWidget(m_checkButton);
    layout->addLayout(bottomBar);

    setCentralWidget(central);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("status"));
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(m_statusLabel, 1);

    connect(m_sourceEdit, &QPlainTextEdit::textChanged, this, [this] {
        // Our own edits (Replace, Replace all) are handled explicitly by their
        // caller. A user edit invalidates the stored offsets: drop the list and
        // the highlights and cancel the open request instead of shifting them.
        if (m_applyingEdit)
            return;
        m_panel->clear();
        updateHighlights();
        emit checkCancelled();
    });

    connect(m_checkButton, &QPushButton::clicked, this, [this] { startCheck(); });
    connect(m_panel, &ProofreadingPanel::issueSelected, this,
            &ProofreadingWindow::selectIssue);
    connect(m_panel, &ProofreadingPanel::replacementChosen, this,
            &ProofreadingWindow::applyReplacement);
    connect(m_panel, &ProofreadingPanel::ignored, this,
            &ProofreadingWindow::ignoreIssue);
    connect(m_panel, &ProofreadingPanel::replaceAllRequested, this,
            [this] { replaceAll(true); });
    connect(m_panel, &ProofreadingPanel::copyCorrectedRequested, this,
            &ProofreadingWindow::copyCorrected);
    for (const QKeySequence &keys : {QKeySequence(Qt::CTRL | Qt::Key_Return),
                                     QKeySequence(Qt::CTRL | Qt::Key_Enter)}) {
        auto *shortcut = new QShortcut(keys, this);
        connect(shortcut, &QShortcut::activated, this, [this] { startCheck(); });
    }

    retranslateUi();
}

void ProofreadingWindow::showAndActivate()
{
    show();
    raise();
    activateWindow();
    m_sourceEdit->setFocus();
}

void ProofreadingWindow::setProofreadingAvailable(bool available)
{
    m_proofreadingAvailable = available;
    m_panel->setVisible(available);
    m_checkButton->setVisible(available);
    if (!available) {
        m_panel->clear();
        updateHighlights();
        emit checkCancelled();
    }
}

void ProofreadingWindow::showProofreadingResult(const ProofreadingResult &result)
{
    onProofreadingResult(result);
}

void ProofreadingWindow::retranslateUi()
{
    setWindowTitle(tr("Text checking"));
    m_sourceEdit->setAccessibleName(tr("Text to check"));
    m_sourceEdit->setPlaceholderText(tr("Enter text to check"));
    m_checkButton->setText(tr("Check"));
    m_checkButton->setToolTip(tr("Check the source text"));
}

void ProofreadingWindow::closeEvent(QCloseEvent *event)
{
    // The window stays alive for the next open and the text works as a
    // scratchpad; only the stale result and the running check are dropped.
    m_panel->clear();
    updateHighlights();
    emit checkCancelled();
    QMainWindow::closeEvent(event);
}

void ProofreadingWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
}

void ProofreadingWindow::startCheck()
{
    if (!m_proofreadingAvailable)
        return;

    const QString text = m_sourceEdit->toPlainText();
    if (text.trimmed().isEmpty()) {
        // A replacement can leave an empty field; the old offsets are stale.
        m_panel->clear();
        updateHighlights();
        setStatus(tr("Nothing to check"));
        return;
    }
    setStatus(QString());
    m_panel->show();
    m_panel->showChecking();
    updateHighlights();
    emit checkRequested(text);
}

void ProofreadingWindow::onProofreadingResult(const ProofreadingResult &result)
{
    if (result.error == TranslationError::Cancelled)
        return;

    m_panel->showResult(result);
    updateHighlights();
}

void ProofreadingWindow::updateHighlights()
{
    QList<QTextEdit::ExtraSelection> selections;
    const QString text = m_sourceEdit->toPlainText();
    for (const ProofreadingIssue &issue : m_panel->issues()) {
        if (issue.offset < 0 || issue.length < 0 || issue.offset > text.size()
            || issue.length > text.size() - issue.offset) {
            continue;
        }
        QTextEdit::ExtraSelection selection;
        QTextCharFormat format;
        format.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
        format.setUnderlineColor(QColor(0xd3, 0x2f, 0x2f));
        selection.format = format;
        QTextCursor cursor(m_sourceEdit->document());
        cursor.setPosition(issue.offset);
        cursor.setPosition(issue.offset + issue.length, QTextCursor::KeepAnchor);
        selection.cursor = cursor;
        selections.append(selection);
    }
    m_sourceEdit->setExtraSelections(selections);
}

void ProofreadingWindow::selectIssue(int row)
{
    const ProofreadingIssue *issue = m_panel->issueAt(row);
    if (!issue)
        return;
    const int size = m_sourceEdit->toPlainText().size();
    if (issue->offset < 0 || issue->offset > size)
        return;
    const int end = qMin(size, issue->offset + qMax(0, issue->length));
    QTextCursor cursor(m_sourceEdit->document());
    cursor.setPosition(issue->offset);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    m_sourceEdit->setTextCursor(cursor);
}

void ProofreadingWindow::applyReplacement(int row, int replacementIndex)
{
    const ProofreadingIssue *issue = m_panel->issueAt(row);
    if (!issue || replacementIndex < 0 || replacementIndex >= issue->replacements.size())
        return;
    if (!ProofreadingText::isApplicable(m_sourceEdit->toPlainText(), *issue))
        return;

    m_applyingEdit = true;
    QTextCursor cursor(m_sourceEdit->document());
    cursor.beginEditBlock();
    cursor.setPosition(issue->offset);
    cursor.setPosition(issue->offset + issue->length, QTextCursor::KeepAnchor);
    cursor.insertText(issue->replacements.at(replacementIndex));
    cursor.endEditBlock();
    m_applyingEdit = false;

    // The replacement moved the remaining offsets: ask for a fresh check
    // instead of shifting the list.
    startCheck();
}

void ProofreadingWindow::ignoreIssue(int row)
{
    Q_UNUSED(row);
    // The panel has already dropped the row and its issue.
    updateHighlights();
}

void ProofreadingWindow::replaceAll(bool preview)
{
    const QString current = m_sourceEdit->toPlainText();
    const QString corrected = m_panel->correctedText(current);
    if (preview && corrected != current && !previewCorrections(corrected))
        return;

    applyIssueEdits(m_panel->issues());
    m_panel->clear();
    updateHighlights();
    if (!preview)
        setStatus(tr("Corrections applied"));
}

bool ProofreadingWindow::previewCorrections(const QString &corrected)
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("proofreadingPreviewDialog"));
    dialog.setWindowTitle(tr("Preview of corrections"));

    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(tr("Corrected text:"), &dialog);
    auto *preview = new QPlainTextEdit(&dialog);
    preview->setObjectName(QStringLiteral("proofreadingPreview"));
    preview->setReadOnly(true);
    preview->setPlainText(corrected);
    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok))
        ok->setObjectName(QStringLiteral("proofreadingPreviewOk"));
    if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel))
        cancel->setObjectName(QStringLiteral("proofreadingPreviewCancel"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    layout->addWidget(label);
    layout->addWidget(preview, 1);
    layout->addWidget(buttons);
    dialog.resize(520, 360);
    return dialog.exec() == QDialog::Accepted;
}

void ProofreadingWindow::applyIssueEdits(const QVector<ProofreadingIssue> &issues)
{
    const QString text = m_sourceEdit->toPlainText();
    QVector<ProofreadingIssue> applicable;
    applicable.reserve(issues.size());
    for (const ProofreadingIssue &issue : issues) {
        if (ProofreadingText::isApplicable(text, issue))
            applicable.append(issue);
    }
    if (applicable.isEmpty())
        return;

    // The same order as ProofreadingText::corrected(): from the end of the
    // text to the beginning, the longer range first at the same offset and
    // the rule id for ties.
    std::sort(applicable.begin(), applicable.end(),
              [](const ProofreadingIssue &left, const ProofreadingIssue &right) {
                  if (left.offset != right.offset)
                      return left.offset > right.offset;
                  if (left.length != right.length)
                      return left.length > right.length;
                  return left.ruleId < right.ruleId;
              });

    // One edit block for all issues: a single Ctrl+Z restores the previous
    // text. setPlainText() would clear the undo stack instead.
    m_applyingEdit = true;
    QTextCursor cursor(m_sourceEdit->document());
    cursor.beginEditBlock();
    int appliedFrom = text.size();
    for (const ProofreadingIssue &issue : applicable) {
        if (issue.offset + issue.length > appliedFrom)
            continue;
        cursor.setPosition(issue.offset);
        cursor.setPosition(issue.offset + issue.length, QTextCursor::KeepAnchor);
        cursor.insertText(issue.replacements.first());
        appliedFrom = issue.offset;
    }
    cursor.endEditBlock();
    m_applyingEdit = false;
}

void ProofreadingWindow::copyCorrected()
{
    QGuiApplication::clipboard()->setText(m_panel->correctedText(m_sourceEdit->toPlainText()));
    statusBar()->showMessage(tr("Corrected text copied to the clipboard"), 2000);
}

void ProofreadingWindow::setStatus(const QString &text)
{
    statusBar()->clearMessage();
    m_statusLabel->setText(text);
}
