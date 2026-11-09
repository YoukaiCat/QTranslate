#pragma once

#include "core/proofreadingtypes.h"

#include <QPoint>
#include <QString>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class ProofreadingPanel;

// Non-activating confirmation window of the external-selection scenario (see
// doc/proofreading.md, §2 scenario 3, and doc/architecture.md, §4.13). It shows
// the issues of the captured text, the corrected whole selection and the undo
// caveat, and asks for an explicit confirmation before anything is replaced in
// another application. It never grabs, pastes or copies: AppController owns the
// grab, the manager and the clipboard, feeds the result through showResult()
// and reacts to the signals.
//
// The window must not take the foreground: the source application has to stay
// active until the paste, otherwise Platform::isForegroundWindow() cannot pass.
// Qt::WindowDoesNotAcceptFocus and Qt::WA_ShowWithoutActivating are therefore
// part of the contract. There is no activateWindow(), no setFocus() and no Esc
// shortcut; the explicit Cancel button is the close path.
class ProofreadingPopupWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ProofreadingPopupWindow(QWidget *parent = nullptr);

    // Shows the captured `text` in the checking state near `anchor` (global
    // cursor position) and remembers it for the correction.
    void showChecking(const QString &text, const QPoint &anchor);

    // Switches to the issues, no-issues or error state of the panel and fills
    // the preview. Ignored while hidden and for a cancelled result.
    void showResult(const ProofreadingResult &result);

    // ProofreadingText::corrected() of the captured text and the shown issues;
    // the same text as the preview.
    QString correctedText() const;

    void setPreferredWidth(int width); // test seam

signals:
    void replaceRequested(); // only while the confirmation is enabled
    void copyRequested();
    void closed(); // hidden or closed; once per showChecking()

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void buildUi();
    void retranslateUi();
    void updateGeometryAndPlacement();
    void emitClosedOnce();

    QLabel *m_title = nullptr;
    ProofreadingPanel *m_panel = nullptr;
    QLabel *m_correctedLabel = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QLabel *m_caveat = nullptr;
    QPushButton *m_cancelButton = nullptr;

    QString m_text;
    QPoint m_anchor;
    int m_width = 460;
    bool m_closedEmitted = false;
};
