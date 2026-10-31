#pragma once

#include <QMainWindow>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

// Main translator window: source text on the left, translation on the right.
// Stage 1 provides the layout only; translation is wired up in stage 2.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Shows the window, restores it from minimized state and brings it to front.
    void showAndActivate();

    // When true (default), closing the window hides it to the tray instead of
    // closing it. Turned off when there is no tray or the application quits.
    void setCloseHidesToTray(bool hide);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();

    QComboBox *m_sourceLang = nullptr;
    QToolButton *m_swapButton = nullptr;
    QComboBox *m_targetLang = nullptr;
    QComboBox *m_service = nullptr;
    QPlainTextEdit *m_sourceEdit = nullptr;
    QPlainTextEdit *m_resultEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_translateButton = nullptr;
    QPushButton *m_copyButton = nullptr;

    bool m_closeHidesToTray = true;
};
