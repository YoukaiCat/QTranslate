#include "ui/proofreadingpanel.h"

#include "core/proofreadingtext.h"
#include "ui/proofreadingerrortext.h"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

ProofreadingPanel::ProofreadingPanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void ProofreadingPanel::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("proofreadingTitle"));
    m_stateLabel = new QLabel(this);
    m_stateLabel->setObjectName(QStringLiteral("proofreadingState"));
    m_stateLabel->setWordWrap(true);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("proofreadingIssues"));
    m_list->setWordWrap(true);

    m_replacements = new QComboBox(this);
    m_replacements->setObjectName(QStringLiteral("proofreadingReplacement"));

    m_replaceButton = new QPushButton(this);
    m_replaceButton->setObjectName(QStringLiteral("proofreadingReplace"));
    m_ignoreButton = new QPushButton(this);
    m_ignoreButton->setObjectName(QStringLiteral("proofreadingIgnore"));
    m_replaceAllButton = new QPushButton(this);
    m_replaceAllButton->setObjectName(QStringLiteral("proofreadingReplaceAll"));
    m_copyButton = new QPushButton(this);
    m_copyButton->setObjectName(QStringLiteral("proofreadingCopy"));

    auto *firstRow = new QHBoxLayout;
    firstRow->addWidget(m_replaceButton);
    firstRow->addWidget(m_ignoreButton);
    auto *secondRow = new QHBoxLayout;
    secondRow->addWidget(m_replaceAllButton);
    secondRow->addWidget(m_copyButton);

    layout->addWidget(m_title);
    layout->addWidget(m_stateLabel);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_replacements);
    layout->addLayout(firstRow);
    layout->addLayout(secondRow);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        refillReplacements();
        if (row >= 0)
            emit issueSelected(row);
    });
    connect(m_replacements, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { updateActions(); });
    connect(m_replaceButton, &QPushButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        const int index = m_replacements->currentIndex();
        const ProofreadingIssue *issue = issueAt(row);
        if (!issue || index < 0 || index >= issue->replacements.size())
            return;
        emit replacementChosen(row, index);
    });
    connect(m_ignoreButton, &QPushButton::clicked, this, &ProofreadingPanel::ignoreCurrentIssue);
    connect(m_replaceAllButton, &QPushButton::clicked, this,
            &ProofreadingPanel::replaceAllRequested);
    connect(m_copyButton, &QPushButton::clicked, this,
            &ProofreadingPanel::copyCorrectedRequested);

    retranslateUi();
    updateActions();
}

void ProofreadingPanel::showChecking()
{
    {
        const QSignalBlocker blocker(m_list);
        m_list->clear();
    }
    m_issues.clear();
    m_errorResult = {};
    setState(State::Checking);
    refillReplacements();
}

void ProofreadingPanel::showResult(const ProofreadingResult &result)
{
    {
        const QSignalBlocker blocker(m_list);
        m_list->clear();
    }
    m_issues.clear();
    m_errorResult = {};

    if (!result.ok()) {
        m_errorResult = result;
        setState(State::Error);
    } else if (result.issues.isEmpty()) {
        setState(State::NoIssues);
    } else {
        m_issues = result.issues;
        setState(State::Empty);
        for (const ProofreadingIssue &issue : m_issues) {
            m_list->addItem(issue.message + QLatin1Char('\n')
                            + tr("Rule: %1").arg(issue.ruleId));
        }
    }
    refillReplacements();
}

void ProofreadingPanel::clear()
{
    {
        const QSignalBlocker blocker(m_list);
        m_list->clear();
    }
    m_issues.clear();
    m_errorResult = {};
    setState(State::Empty);
    refillReplacements();
}

bool ProofreadingPanel::hasIssues() const
{
    return !m_issues.isEmpty();
}

const ProofreadingIssue *ProofreadingPanel::issueAt(int row) const
{
    if (row < 0 || row >= m_issues.size())
        return nullptr;
    return &m_issues.at(row);
}

QString ProofreadingPanel::correctedText(const QString &text) const
{
    return ProofreadingText::corrected(text, m_issues);
}

void ProofreadingPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
}

void ProofreadingPanel::retranslateUi()
{
    m_title->setText(tr("Issues"));
    m_replaceButton->setText(tr("Replace"));
    m_ignoreButton->setText(tr("Ignore"));
    m_replaceAllButton->setText(tr("Replace all"));
    m_copyButton->setText(tr("Copy corrected text"));
    setState(m_state);

    // The list rows carry the translated "Rule:" prefix; rebuild their text
    // without disturbing the selection.
    {
        const QSignalBlocker blocker(m_list);
        for (int row = 0; row < m_list->count() && row < m_issues.size(); ++row) {
            const ProofreadingIssue &issue = m_issues.at(row);
            m_list->item(row)->setText(issue.message + QLatin1Char('\n')
                                       + tr("Rule: %1").arg(issue.ruleId));
        }
    }
    refillReplacements(true);
}

void ProofreadingPanel::setState(State state)
{
    m_state = state;
    switch (state) {
    case State::Empty:
        m_stateLabel->clear();
        m_stateLabel->setStyleSheet(QString());
        break;
    case State::Checking:
        m_stateLabel->setText(tr("Checking…"));
        m_stateLabel->setStyleSheet(QString());
        break;
    case State::NoIssues:
        m_stateLabel->setText(tr("No issues found."));
        m_stateLabel->setStyleSheet(QString());
        break;
    case State::Error:
        m_stateLabel->setText(ProofreadingErrorText::forResult(m_errorResult));
        m_stateLabel->setStyleSheet(QStringLiteral("color: #d32f2f;"));
        break;
    }
}

void ProofreadingPanel::refillReplacements(bool keepIndex)
{
    const QSignalBlocker blocker(m_replacements);
    const int previousIndex = m_replacements->currentIndex();
    m_replacements->clear();

    const ProofreadingIssue *issue = issueAt(m_list->currentRow());
    if (!issue) {
        m_replacements->setEnabled(false);
    } else if (issue->replacements.isEmpty()) {
        m_replacements->addItem(tr("No suggestions"));
        m_replacements->setEnabled(false);
    } else {
        m_replacements->addItems(issue->replacements);
        m_replacements->setEnabled(true);
        // A language change must not change which suggestion is selected; a
        // normal row change starts again at the first one.
        if (keepIndex && previousIndex >= 0 && previousIndex < issue->replacements.size())
            m_replacements->setCurrentIndex(previousIndex);
    }
    updateActions();
}

void ProofreadingPanel::updateActions()
{
    const ProofreadingIssue *issue = issueAt(m_list->currentRow());
    const int index = m_replacements->currentIndex();
    const bool canReplace =
        issue && index >= 0 && index < issue->replacements.size();
    m_replaceButton->setEnabled(canReplace);
    m_ignoreButton->setEnabled(issue != nullptr);
    m_replaceAllButton->setEnabled(hasIssues());
    m_copyButton->setEnabled(hasIssues());
}

void ProofreadingPanel::ignoreCurrentIssue()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_issues.size())
        return;

    {
        const QSignalBlocker blocker(m_list);
        delete m_list->takeItem(row);
        if (m_list->count() > 0)
            m_list->setCurrentRow(qMin(row, m_list->count() - 1));
        else
            m_list->setCurrentRow(-1);
    }
    m_issues.removeAt(row);
    refillReplacements();

    // Emitted after the row and its issue are gone, so the owner repaints the
    // highlights from the remaining rows only.
    emit ignored(row);
}