#pragma once

#include "core/proofreadingtypes.h"

#include <QMainWindow>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class ProofreadingPanel;

// The checking mode as its own non-modal top-level window: a source editor, the
// reused issue panel and the Check/Replace/Ignore/Copy actions (see
// doc/proofreading.md). It knows nothing about a manager, a service, the
// network or the settings: it emits checkRequested() and checkCancelled() and
// AppController feeds the result back through showProofreadingResult(). The
// translation window is a different window and never contains this surface.
class ProofreadingWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ProofreadingWindow(QWidget *parent = nullptr);

    // The checking mode is on and a provider is registered: shows or hides the
    // panel and the Check button. Turning it off also clears the panel and the
    // highlights and cancels the running check.
    void setProofreadingAvailable(bool available);

    // A check requested through checkRequested() has finished (or failed).
    void showProofreadingResult(const ProofreadingResult &result);

    // Shows the window, brings it to front and focuses the editor.
    void showAndActivate();

signals:
    void checkRequested(const QString &text);
    void checkCancelled();

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void retranslateUi();
    void startCheck();
    void onProofreadingResult(const ProofreadingResult &result);
    void updateHighlights();
    void selectIssue(int row);
    void applyReplacement(int row, int replacementIndex);
    void ignoreIssue(int row);
    void replaceAll(bool preview);
    bool previewCorrections(const QString &corrected);
    void applyIssueEdits(const QVector<ProofreadingIssue> &issues);
    void copyCorrected();
    void setStatus(const QString &text);

    QPlainTextEdit *m_sourceEdit = nullptr;
    ProofreadingPanel *m_panel = nullptr;
    QPushButton *m_checkButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    bool m_proofreadingAvailable = false;
    bool m_applyingEdit = false;
};
