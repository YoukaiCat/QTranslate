#pragma once

#include "core/proofreadingtypes.h"

#include <QVector>
#include <QWidget>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

// Issue list of the main-window checking mode (see doc/proofreading.md, §6).
// It never edits a document and holds no manager, service or settings: the
// owner applies the edits, feeds results back through showResult() and
// repaints the highlights from issues().
class ProofreadingPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ProofreadingPanel(QWidget *parent = nullptr);

    void showChecking();                               // clears the list, shows "Checking…"
    void showResult(const ProofreadingResult &result); // issues, "No issues found." or the error
    void clear();                                      // empty list, no state text

    bool hasIssues() const;
    QVector<ProofreadingIssue> issues() const { return m_issues; } // rows still shown, in order
    const ProofreadingIssue *issueAt(int row) const;
    QString correctedText(const QString &text) const; // ProofreadingText::corrected()

signals:
    void issueSelected(int row);
    void replacementChosen(int row, int replacementIndex);
    void ignored(int row);
    void replaceAllRequested();
    void copyCorrectedRequested();

protected:
    void changeEvent(QEvent *event) override;

private:
    enum class State { Empty, Checking, NoIssues, Error };

    void buildUi();
    void retranslateUi();
    void setState(State state);
    void refillReplacements(bool keepIndex = false);
    void updateActions();
    void ignoreCurrentIssue();

    QLabel *m_title = nullptr;
    QLabel *m_stateLabel = nullptr;
    QListWidget *m_list = nullptr;
    QComboBox *m_replacements = nullptr;
    QPushButton *m_replaceButton = nullptr;
    QPushButton *m_ignoreButton = nullptr;
    QPushButton *m_replaceAllButton = nullptr;
    QPushButton *m_copyButton = nullptr;
    QVector<ProofreadingIssue> m_issues;
    ProofreadingResult m_errorResult; // the last failed result, for retranslation
    State m_state = State::Empty;
};