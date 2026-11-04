#include "ui/mainwindow.h"

#include "core/appinfo.h"
#include "core/languages.h"
#include "ui/languagecombobox.h"
#include "ui/translationerrortext.h"

#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

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
    topBar->addWidget(new QLabel(tr("Service:"), central));
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
    splitter->addWidget(m_sourceEdit);
    splitter->addWidget(m_resultEdit);
    splitter->setChildrenCollapsible(false);
    layout->addWidget(splitter, 1);

    // Bottom bar: actions.
    auto *bottomBar = new QHBoxLayout;
    m_translateButton = new QPushButton(tr("Translate"), central);
    m_translateButton->setObjectName(QStringLiteral("translate"));
    m_translateButton->setToolTip(tr("Ctrl+Enter"));
    m_copyButton = new QPushButton(tr("Copy"), central);
    m_copyButton->setObjectName(QStringLiteral("copy"));
    m_copyButton->setToolTip(tr("Copy the translation to the clipboard"));
    m_copyButton->setEnabled(false);
    bottomBar->addStretch();
    bottomBar->addWidget(m_translateButton);
    bottomBar->addWidget(m_copyButton);
    layout->addLayout(bottomBar);

    setCentralWidget(central);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("status"));
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(m_statusLabel, 1);

    connect(m_translateButton, &QPushButton::clicked, this, &MainWindow::requestTranslation);
    for (const QKeySequence &keys : {QKeySequence(Qt::CTRL | Qt::Key_Return),
                                     QKeySequence(Qt::CTRL | Qt::Key_Enter)}) {
        auto *shortcut = new QShortcut(keys, this);
        connect(shortcut, &QShortcut::activated, this, &MainWindow::requestTranslation);
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

void MainWindow::showTranslationStarted()
{
    m_resultEdit->clear();
    m_copyButton->setEnabled(false);
    m_detectedLang.clear();
    updateSwapButton();
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
    m_sourceEdit->setPlainText(text);
    m_sourceEdit->moveCursor(QTextCursor::Start);
    requestTranslation();
}

void MainWindow::showTranslation(const TranslationRequest &request,
                                 const TranslationResult &result)
{
    if (m_sourceLang->findData(request.sourceLang) >= 0)
        m_sourceLang->setCurrentCode(request.sourceLang);
    if (m_targetLang->findData(request.targetLang) >= 0)
        m_targetLang->setCurrentCode(request.targetLang);

    m_sourceEdit->setPlainText(request.text);
    m_sourceEdit->moveCursor(QTextCursor::Start);
    showResult(result);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_closeHidesToTray) {
        hide();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::requestTranslation()
{
    emit translateRequested(TranslationRequest{m_sourceEdit->toPlainText(),
                                               m_sourceLang->currentCode(),
                                               m_targetLang->currentCode()});
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

    // The translation becomes the new source text and is translated back.
    const QString translation = m_resultEdit->toPlainText();
    if (!translation.isEmpty()) {
        m_sourceEdit->setPlainText(translation);
        requestTranslation();
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
