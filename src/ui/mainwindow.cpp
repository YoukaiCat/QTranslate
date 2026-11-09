#include "ui/mainwindow.h"

#include "core/appinfo.h"
#include "core/languages.h"
#include "core/proofreadingtext.h"
#include "ui/languagecombobox.h"
#include "ui/proofreadingpanel.h"
#include "ui/translationerrortext.h"

#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
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
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(AppInfo::name());
    buildUi();
    resize(820, 440);
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    // Top bar: languages and service.
    auto *topBar = new QHBoxLayout;
    m_sourceLang = new LanguageComboBox(central);
    m_sourceLang->setObjectName(QStringLiteral("sourceLanguage"));
    m_sourceLang->setToolTip(tr("Source language"));
    m_sourceLang->setAccessibleName(m_sourceLang->toolTip());
    m_swapButton = new QToolButton(central);
    m_swapButton->setObjectName(QStringLiteral("swapLanguages"));
    m_swapButton->setText(QStringLiteral("⇄"));
    m_swapButton->setToolTip(tr("Swap languages"));
    m_swapButton->setAccessibleName(m_swapButton->toolTip());
    m_targetLang = new LanguageComboBox(central);
    m_targetLang->setObjectName(QStringLiteral("targetLanguage"));
    m_targetLang->setToolTip(tr("Target language"));
    m_targetLang->setAccessibleName(m_targetLang->toolTip());
    m_service = new QComboBox(central);
    m_service->setObjectName(QStringLiteral("service"));
    m_service->setAccessibleName(tr("Translation service"));
    m_service->setMinimumContentsLength(14);
    m_service->setEnabled(false); // until there is more than one service (stage 6)

    topBar->addWidget(m_sourceLang);
    topBar->addWidget(m_swapButton);
    topBar->addWidget(m_targetLang);
    topBar->addStretch();
    m_serviceLabel = new QLabel(tr("Service:"), central);
    m_serviceLabel->setObjectName(QStringLiteral("serviceLabel"));
    topBar->addWidget(m_serviceLabel);
    topBar->addWidget(m_service);
    layout->addLayout(topBar);

    // Text panes.
    auto *splitter = new QSplitter(Qt::Horizontal, central);
    m_sourceEdit = new QPlainTextEdit(splitter);
    m_sourceEdit->setObjectName(QStringLiteral("sourceText"));
    m_sourceEdit->setAccessibleName(tr("Source text"));
    m_sourceEdit->setPlaceholderText(tr("Enter text to translate"));
    m_resultEdit = new QPlainTextEdit(splitter);
    m_resultEdit->setObjectName(QStringLiteral("translation"));
    m_resultEdit->setAccessibleName(tr("Translation"));
    m_resultEdit->setReadOnly(true);
    m_resultEdit->setPlaceholderText(tr("Translation"));
    m_proofreadingPanel = new ProofreadingPanel(splitter);
    // A fixed-ish pane next to the two text fields; hidden until the checking
    // mode is on.
    m_proofreadingPanel->setMinimumWidth(240);
    m_proofreadingPanel->setMaximumWidth(320);
    m_proofreadingPanel->hide();
    splitter->addWidget(m_sourceEdit);
    splitter->addWidget(m_resultEdit);
    splitter->addWidget(m_proofreadingPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setChildrenCollapsible(false);
    layout->addWidget(splitter, 1);

    // Bottom bar: actions.
    auto *bottomBar = new QHBoxLayout;
    m_checkButton = new QPushButton(tr("Check"), central);
    m_checkButton->setObjectName(QStringLiteral("check"));
    m_checkButton->setToolTip(tr("Check the source text"));
    m_checkButton->hide();
    m_translateButton = new QPushButton(tr("Translate"), central);
    m_translateButton->setObjectName(QStringLiteral("translate"));
    m_translateButton->setToolTip(tr("Ctrl+Enter"));
    m_copyButton = new QPushButton(tr("Copy"), central);
    m_copyButton->setObjectName(QStringLiteral("copy"));
    m_copyButton->setToolTip(tr("Copy the translation to the clipboard"));
    m_copyButton->setEnabled(false);
    bottomBar->addStretch();
    bottomBar->addWidget(m_checkButton);
    bottomBar->addWidget(m_translateButton);
    bottomBar->addWidget(m_copyButton);
    layout->addLayout(bottomBar);

    setCentralWidget(central);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("status"));
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(m_statusLabel, 1);

    m_autoTranslateTimer = new QTimer(this);
    m_autoTranslateTimer->setSingleShot(true);
    connect(m_autoTranslateTimer, &QTimer::timeout, this, [this] { requestTranslation(false); });
    connect(m_sourceEdit, &QPlainTextEdit::textChanged, this, [this] {
        // Our own edits (Replace, Replace all, autoReplace, applying the
        // corrections before a translation) must neither start the timer nor
        // invalidate the list: they are handled explicitly by their caller.
        if (m_applyingEdit)
            return;
        // The stored offsets belong to the old text: drop the list and the
        // highlights and cancel the open request instead of shifting them.
        m_proofreadingPanel->clear();
        updateHighlights();
        m_pendingTranslationCheck = false;
        emit checkCancelled();
        if (m_autoTranslateDelayMs <= 0)
            return;
        if (m_sourceEdit->toPlainText().trimmed().isEmpty())
            m_autoTranslateTimer->stop();
        else
            m_autoTranslateTimer->start(m_autoTranslateDelayMs);
    });

    connect(m_translateButton, &QPushButton::clicked, this, [this] { requestTranslation(true); });
    connect(m_checkButton, &QPushButton::clicked, this, [this] {
        m_pendingTranslationCheck = false;
        startCheck();
    });
    connect(m_proofreadingPanel, &ProofreadingPanel::issueSelected, this,
            &MainWindow::selectIssue);
    connect(m_proofreadingPanel, &ProofreadingPanel::replacementChosen, this,
            &MainWindow::applyReplacement);
    connect(m_proofreadingPanel, &ProofreadingPanel::ignored, this, &MainWindow::ignoreIssue);
    connect(m_proofreadingPanel, &ProofreadingPanel::replaceAllRequested, this,
            [this] { replaceAll(true); });
    connect(m_proofreadingPanel, &ProofreadingPanel::copyCorrectedRequested, this,
            &MainWindow::copyCorrected);
    for (const QKeySequence &keys : {QKeySequence(Qt::CTRL | Qt::Key_Return),
                                     QKeySequence(Qt::CTRL | Qt::Key_Enter)}) {
        auto *shortcut = new QShortcut(keys, this);
        connect(shortcut, &QShortcut::activated, this, [this] { requestTranslation(true); });
    }
    connect(m_swapButton, &QToolButton::clicked, this, &MainWindow::swapLanguages);
    connect(m_copyButton, &QPushButton::clicked, this, &MainWindow::copyResult);
    connect(m_service, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                if (!m_service->isEnabled())
                    return;
                const QString id = m_service->currentData().toString();
                if (!id.isEmpty())
                    emit serviceChanged(id);
            });
    connect(m_sourceLang, &LanguageComboBox::currentCodeChanged, this,
            &MainWindow::updateSwapButton);
    connect(m_targetLang, &LanguageComboBox::currentCodeChanged, this,
            &MainWindow::updateSwapButton);
    updateSwapButton();
}

void MainWindow::showAndActivate()
{
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    show();
    raise();
    activateWindow();
    m_sourceEdit->setFocus();
}

void MainWindow::setCloseHidesToTray(bool hide)
{
    m_closeHidesToTray = hide;
}

void MainWindow::setLanguages(const QStringList &sourceCodes, const QStringList &targetCodes)
{
    m_sourceLang->setLanguages(sourceCodes, true);
    m_targetLang->setLanguages(targetCodes, false);
}

void MainWindow::setSourceLanguage(const QString &code)
{
    m_sourceLang->setCurrentCode(code);
}

void MainWindow::setTargetLanguage(const QString &code)
{
    m_targetLang->setCurrentCode(code);
}

void MainWindow::setServices(const QList<ServiceEntry> &services, const QString &activeId)
{
    const QSignalBlocker blocker(m_service);
    m_service->clear();
    for (const ServiceEntry &service : services)
        m_service->addItem(service.name, service.id);
    m_service->setCurrentIndex(m_service->findData(activeId));
    m_service->setEnabled(services.size() > 1);
}

void MainWindow::setProofreadingAvailable(bool available)
{
    m_proofreadingAvailable = available;
    m_proofreadingPanel->setVisible(available);
    m_checkButton->setVisible(available);
    if (!available) {
        m_proofreadingPanel->clear();
        updateHighlights();
        m_pendingTranslationCheck = false;
        emit checkCancelled();
    }
}

void MainWindow::setProofreadingOptions(bool checkBeforeTranslation, bool autoReplace)
{
    m_checkBeforeTranslation = checkBeforeTranslation;
    m_autoReplace = autoReplace;
}

void MainWindow::showTranslationStarted()
{
    m_resultEdit->clear();
    m_copyButton->setEnabled(false);
    m_detectedLang.clear();
    updateSwapButton();
    m_proofreadingPanel->clear();
    updateHighlights();
    m_pendingTranslationCheck = false;
    emit checkCancelled();
    setStatus(tr("Translating…"));
}

void MainWindow::showResult(const TranslationResult &result)
{
    if (result.error == TranslationError::Cancelled)
        return;

    if (!result.ok()) {
        m_resultEdit->clear();
        m_copyButton->setEnabled(false);
        setStatus(TranslationErrorText::forResult(result), true);
        return;
    }

    m_resultEdit->setPlainText(result.translatedText);
    m_copyButton->setEnabled(!result.translatedText.isEmpty());
    m_detectedLang = result.detectedSourceLang;
    if (m_sourceLang->currentCode() == Languages::autoCode() && !m_detectedLang.isEmpty())
        setStatus(tr("Detected language: %1").arg(Languages::displayName(m_detectedLang)));
    else
        setStatus(QString());
    updateSwapButton();
}

void MainWindow::translateText(const QString &text)
{
    if (text.isEmpty())
        return;
    const QSignalBlocker blocker(m_sourceEdit);
    m_sourceEdit->setPlainText(text);
    m_sourceEdit->moveCursor(QTextCursor::Start);
    // setPlainText() is blocked above, so the stale-result handler does not run.
    m_proofreadingPanel->clear();
    updateHighlights();
    m_pendingTranslationCheck = false;
    emit checkCancelled();
    requestTranslation(true);
}

void MainWindow::setAutoTranslateDelay(int ms)
{
    m_autoTranslateDelayMs = qMax(0, ms);
    if (m_autoTranslateDelayMs == 0)
        m_autoTranslateTimer->stop();
}

void MainWindow::retranslateUi()
{
    m_sourceLang->setToolTip(tr("Source language"));
    m_sourceLang->setAccessibleName(m_sourceLang->toolTip());
    m_swapButton->setToolTip(tr("Swap languages"));
    m_swapButton->setAccessibleName(m_swapButton->toolTip());
    m_targetLang->setToolTip(tr("Target language"));
    m_targetLang->setAccessibleName(m_targetLang->toolTip());
    m_serviceLabel->setText(tr("Service:"));
    m_service->setAccessibleName(tr("Translation service"));
    m_sourceEdit->setAccessibleName(tr("Source text"));
    m_sourceEdit->setPlaceholderText(tr("Enter text to translate"));
    m_resultEdit->setAccessibleName(tr("Translation"));
    m_resultEdit->setPlaceholderText(tr("Translation"));
    m_translateButton->setText(tr("Translate"));
    m_translateButton->setToolTip(tr("Ctrl+Enter"));
    m_checkButton->setText(tr("Check"));
    m_checkButton->setToolTip(tr("Check the source text"));
    m_copyButton->setText(tr("Copy"));
    m_copyButton->setToolTip(tr("Copy the translation to the clipboard"));
    updateSwapButton();
}

void MainWindow::showTranslation(const TranslationRequest &request,
                                 const TranslationResult &result)
{
    if (m_sourceLang->findData(request.sourceLang) >= 0)
        m_sourceLang->setCurrentCode(request.sourceLang);
    if (m_targetLang->findData(request.targetLang) >= 0)
        m_targetLang->setCurrentCode(request.targetLang);

    const QSignalBlocker blocker(m_sourceEdit);
    m_sourceEdit->setPlainText(request.text);
    m_sourceEdit->moveCursor(QTextCursor::Start);
    // setPlainText() is blocked above, so the stale-result handler does not run.
    m_proofreadingPanel->clear();
    updateHighlights();
    m_pendingTranslationCheck = false;
    emit checkCancelled();
    showResult(result);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_proofreadingPanel->clear();
    updateHighlights();
    m_pendingTranslationCheck = false;
    emit checkCancelled();
    if (m_closeHidesToTray) {
        hide();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
}

void MainWindow::requestTranslation(bool userInitiated)
{
    if (m_autoTranslateTimer)
        m_autoTranslateTimer->stop();
    const QString text = m_sourceEdit->toPlainText();
    if (userInitiated && m_checkBeforeTranslation && m_proofreadingAvailable
        && !text.trimmed().isEmpty()) {
        // The flag must survive startCheck(): showProofreadingResult() reads it
        // to continue the translation after the check.
        m_pendingTranslationCheck = true;
        startCheck();
        return;
    }
    emit translateRequested(TranslationRequest{text,
                                               m_sourceLang->currentCode(),
                                               m_targetLang->currentCode()});
}

void MainWindow::startCheck()
{
    m_autoTranslateTimer->stop();
    const QString text = m_sourceEdit->toPlainText();
    if (text.trimmed().isEmpty()) {
        // A replacement can leave an empty field; the old offsets are stale.
        m_proofreadingPanel->clear();
        updateHighlights();
        m_pendingTranslationCheck = false;
        setStatus(tr("Nothing to check"));
        return;
    }
    setStatus(QString());
    m_proofreadingPanel->show();
    m_proofreadingPanel->showChecking();
    updateHighlights();
    emit checkRequested(text);
}

void MainWindow::showProofreadingResult(const ProofreadingResult &result)
{
    onProofreadingResult(result);
}

void MainWindow::onProofreadingResult(const ProofreadingResult &result)
{
    if (result.error == TranslationError::Cancelled)
        return;

    m_proofreadingPanel->showResult(result);
    updateHighlights();

    // Built from the current field/languages and emitted directly: going back
    // through requestTranslation() would restart the check-before-translation
    // branch and loop.
    const auto translate = [this] {
        // The corrections may have restarted the timer; only one request may
        // leave this flow.
        m_autoTranslateTimer->stop();
        emit translateRequested(TranslationRequest{m_sourceEdit->toPlainText(),
                                                   m_sourceLang->currentCode(),
                                                   m_targetLang->currentCode()});
    };

    if (m_pendingTranslationCheck) {
        m_pendingTranslationCheck = false;
        if (!result.ok() || result.issues.isEmpty()) {
            translate();
            return;
        }
        switch (askBeforeTranslation(result.issues.size())) {
        case BeforeTranslationChoice::ApplyCorrections:
            applyIssueEdits(result.issues);
            m_proofreadingPanel->clear();
            updateHighlights();
            translate();
            break;
        case BeforeTranslationChoice::TranslateAnyway:
            translate();
            break;
        case BeforeTranslationChoice::Dismissed:
            break;
        }
        return;
    }

    if (m_autoReplace && result.ok() && !result.issues.isEmpty())
        replaceAll(false);
}

void MainWindow::updateHighlights()
{
    QList<QTextEdit::ExtraSelection> selections;
    const QString text = m_sourceEdit->toPlainText();
    for (const ProofreadingIssue &issue : m_proofreadingPanel->issues()) {
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

void MainWindow::selectIssue(int row)
{
    const ProofreadingIssue *issue = m_proofreadingPanel->issueAt(row);
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

void MainWindow::applyReplacement(int row, int replacementIndex)
{
    const ProofreadingIssue *issue = m_proofreadingPanel->issueAt(row);
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
    m_pendingTranslationCheck = false;
    startCheck();
}

void MainWindow::ignoreIssue(int row)
{
    Q_UNUSED(row);
    // The panel has already dropped the row and its issue.
    updateHighlights();
}

void MainWindow::replaceAll(bool preview)
{
    const QString current = m_sourceEdit->toPlainText();
    const QString corrected = m_proofreadingPanel->correctedText(current);
    if (preview && corrected != current && !previewCorrections(corrected))
        return;

    applyIssueEdits(m_proofreadingPanel->issues());
    m_proofreadingPanel->clear();
    updateHighlights();
    if (!preview)
        setStatus(tr("Corrections applied"));
}

bool MainWindow::previewCorrections(const QString &corrected)
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

MainWindow::BeforeTranslationChoice MainWindow::askBeforeTranslation(int issueCount)
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("checkBeforeTranslationDialog"));
    dialog.setWindowTitle(tr("Check before translation"));

    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(tr("%1 issues found in the source text.").arg(issueCount), &dialog);
    label->setWordWrap(true);
    auto *buttons = new QHBoxLayout;
    auto *applyButton = new QPushButton(tr("Apply corrections and translate"), &dialog);
    applyButton->setObjectName(QStringLiteral("applyCorrectionsAndTranslate"));
    applyButton->setDefault(true);
    auto *anywayButton = new QPushButton(tr("Translate anyway"), &dialog);
    anywayButton->setObjectName(QStringLiteral("translateAnyway"));
    buttons->addStretch();
    buttons->addWidget(applyButton);
    buttons->addWidget(anywayButton);
    layout->addWidget(label);
    layout->addLayout(buttons);

    connect(applyButton, &QPushButton::clicked, &dialog, [&dialog] {
        dialog.done(int(BeforeTranslationChoice::ApplyCorrections));
    });
    connect(anywayButton, &QPushButton::clicked, &dialog, [&dialog] {
        dialog.done(int(BeforeTranslationChoice::TranslateAnyway));
    });

    switch (dialog.exec()) {
    case int(BeforeTranslationChoice::ApplyCorrections):
        return BeforeTranslationChoice::ApplyCorrections;
    case int(BeforeTranslationChoice::TranslateAnyway):
        return BeforeTranslationChoice::TranslateAnyway;
    default:
        return BeforeTranslationChoice::Dismissed;
    }
}

void MainWindow::applyIssueEdits(const QVector<ProofreadingIssue> &issues)
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
    // text to the beginning, the longer range first at the same offset, and
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

void MainWindow::copyCorrected()
{
    QGuiApplication::clipboard()->setText(
        m_proofreadingPanel->correctedText(m_sourceEdit->toPlainText()));
    statusBar()->showMessage(tr("Corrected text copied to the clipboard"), 2000);
}

void MainWindow::swapLanguages()
{
    const QString oldSource = m_sourceLang->currentCode();
    const QString newSource = m_targetLang->currentCode();
    const QString newTarget = oldSource == Languages::autoCode() ? m_detectedLang : oldSource;
    if (m_sourceLang->findData(newSource) < 0 || m_targetLang->findData(newTarget) < 0)
        return;

    m_sourceLang->setCurrentCode(newSource);
    m_targetLang->setCurrentCode(newTarget);

    // The translation becomes the new source text and is translated back. A
    // swap is not an explicit translate request, so it bypasses the check.
    const QString translation = m_resultEdit->toPlainText();
    if (!translation.isEmpty()) {
        const QSignalBlocker blocker(m_sourceEdit);
        m_sourceEdit->setPlainText(translation);
        requestTranslation(false);
    }
}

void MainWindow::copyResult()
{
    const QString text = m_resultEdit->toPlainText();
    if (text.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(text);
    statusBar()->showMessage(tr("Translation copied to the clipboard"), 2000);
}

void MainWindow::setStatus(const QString &text, bool isError)
{
    statusBar()->clearMessage();
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(isError ? QStringLiteral("color: #d32f2f;") : QString());
}

void MainWindow::updateSwapButton()
{
    const QString source = m_sourceLang->currentCode();
    const QString newTarget = source == Languages::autoCode() ? m_detectedLang : source;
    m_swapButton->setEnabled(m_sourceLang->findData(m_targetLang->currentCode()) >= 0
                             && m_targetLang->findData(newTarget) >= 0);
}
