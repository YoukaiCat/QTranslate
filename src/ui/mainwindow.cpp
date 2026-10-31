#include "ui/mainwindow.h"

#include "core/appinfo.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
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
    m_sourceLang = new QComboBox(central);
    m_sourceLang->setMinimumContentsLength(14);
    m_swapButton = new QToolButton(central);
    m_swapButton->setText(QStringLiteral("⇄"));
    m_swapButton->setToolTip(tr("Swap languages"));
    m_targetLang = new QComboBox(central);
    m_targetLang->setMinimumContentsLength(14);
    m_service = new QComboBox(central);
    m_service->setMinimumContentsLength(14);

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
    m_sourceEdit->setPlaceholderText(tr("Enter text to translate"));
    m_resultEdit = new QPlainTextEdit(splitter);
    m_resultEdit->setReadOnly(true);
    m_resultEdit->setPlaceholderText(tr("Translation"));
    splitter->addWidget(m_sourceEdit);
    splitter->addWidget(m_resultEdit);
    splitter->setChildrenCollapsible(false);
    layout->addWidget(splitter, 1);

    // Bottom bar: actions.
    auto *bottomBar = new QHBoxLayout;
    m_translateButton = new QPushButton(tr("Translate"), central);
    m_translateButton->setToolTip(tr("Ctrl+Enter"));
    m_copyButton = new QPushButton(tr("Copy"), central);
    bottomBar->addStretch();
    bottomBar->addWidget(m_translateButton);
    bottomBar->addWidget(m_copyButton);
    layout->addLayout(bottomBar);

    setCentralWidget(central);

    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel, 1);

    // Translation is not available yet (stage 2).
    const std::initializer_list<QWidget *> pending{m_sourceLang, m_swapButton, m_targetLang,
                                                   m_service, m_translateButton, m_copyButton};
    for (QWidget *w : pending)
        w->setEnabled(false);
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

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_closeHidesToTray) {
        hide();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}
