#include "ui/settingsdialog.h"

#include "core/hotkeysequence.h"
#include "core/languages.h"
#include "ui/languagecombobox.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <iterator>

SettingsDialog::SettingsDialog(const AppSettings &settings,
                               const QStringList &sourceLanguages,
                               const QStringList &targetLanguages, bool autostartApproved,
                               QWidget *parent)
    : QDialog(parent)
    , m_initial(settings)
    , m_targetLanguages(targetLanguages)
{
    setWindowTitle(tr("Settings"));
    setModal(true);
    resize(560, 430);

    buildUi(sourceLanguages);
    loadValues(settings, autostartApproved);

    connect(qApp, &QApplication::focusChanged, this,
            [this](QWidget *, QWidget *) { updateHotkeyEditing(); });
    connect(this, &QDialog::finished, this, [this] { setHotkeyEditing(false); });
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::buildUi(const QStringList &sourceLanguages)
{
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("settingsTabs"));

    // General.
    auto *generalPage = new QWidget(m_tabs);
    auto *generalLayout = new QVBoxLayout(generalPage);
    m_autostart = new QCheckBox(tr("Start qtranslate when Windows starts"), generalPage);
    m_autostart->setObjectName(QStringLiteral("autostart"));
    m_autostartHint = new QLabel(tr("Autostart is disabled in Windows Task Manager."),
                                 generalPage);
    m_autostartHint->setObjectName(QStringLiteral("autostartHint"));
    m_autostartHint->setWordWrap(true);
    m_autostartHint->setStyleSheet(QStringLiteral("color: #d32f2f;"));
    m_startMinimized = new QCheckBox(tr("Start minimized in the system tray"), generalPage);
    m_startMinimized->setObjectName(QStringLiteral("startMinimized"));
    m_restoreClipboard =
        new QCheckBox(tr("Restore clipboard after copying the selection"), generalPage);
    m_restoreClipboard->setObjectName(QStringLiteral("restoreClipboard"));
    m_autoClosePopup =
        new QCheckBox(tr("Close the popup when it loses focus"), generalPage);
    m_autoClosePopup->setObjectName(QStringLiteral("autoClosePopup"));

    generalLayout->addWidget(m_autostart);
    generalLayout->addWidget(m_autostartHint);
    generalLayout->addWidget(m_startMinimized);
    generalLayout->addWidget(m_restoreClipboard);
    generalLayout->addWidget(m_autoClosePopup);
    generalLayout->addStretch();

    // Hotkeys.
    auto *hotkeysPage = new QWidget(m_tabs);
    auto *hotkeysLayout = new QVBoxLayout(hotkeysPage);
    auto *hotkeysForm = new QFormLayout;
    m_popupHotkey = new QKeySequenceEdit(hotkeysPage);
    m_popupHotkey->setObjectName(QStringLiteral("popupHotkey"));
    m_mainWindowHotkey = new QKeySequenceEdit(hotkeysPage);
    m_mainWindowHotkey->setObjectName(QStringLiteral("mainWindowHotkey"));
    m_showMainWindowHotkey = new QKeySequenceEdit(hotkeysPage);
    m_showMainWindowHotkey->setObjectName(QStringLiteral("showMainWindowHotkey"));
    hotkeysForm->addRow(tr("Translate in a popup:"), m_popupHotkey);
    hotkeysForm->addRow(tr("Translate in the main window:"), m_mainWindowHotkey);
    hotkeysForm->addRow(tr("Show the main window:"), m_showMainWindowHotkey);
    auto *hotkeysHint = new QLabel(
        tr("Global hotkeys are disabled while a key combination field is focused. "
           "Leave a field empty to disable that action."),
        hotkeysPage);
    hotkeysHint->setWordWrap(true);
    hotkeysLayout->addLayout(hotkeysForm);
    hotkeysLayout->addWidget(hotkeysHint);
    hotkeysLayout->addStretch();

    // Translation.
    auto *translationPage = new QWidget(m_tabs);
    auto *translationLayout = new QVBoxLayout(translationPage);
    auto *translationForm = new QFormLayout;
    m_sourceLanguage = new LanguageComboBox(translationPage);
    m_sourceLanguage->setObjectName(QStringLiteral("sourceLanguage"));
    m_sourceLanguage->setLanguages(sourceLanguages, true);
    m_primaryLanguage = new LanguageComboBox(translationPage);
    m_primaryLanguage->setObjectName(QStringLiteral("primaryLanguage"));
    m_primaryLanguage->setLanguages(m_targetLanguages, false);
    m_secondaryLanguage = new LanguageComboBox(translationPage);
    m_secondaryLanguage->setObjectName(QStringLiteral("secondaryLanguage"));
    m_secondaryLanguage->setLanguages(m_targetLanguages, false);
    m_maxLength = new QSpinBox(translationPage);
    m_maxLength->setObjectName(QStringLiteral("maxLength"));
    m_maxLength->setRange(100, 100000);
    m_maxLength->setSingleStep(1000);
    m_maxLength->setSuffix(tr(" characters"));
    m_autoTranslateDelay = new QSpinBox(translationPage);
    m_autoTranslateDelay->setObjectName(QStringLiteral("autoTranslateDelay"));
    m_autoTranslateDelay->setRange(0, 60000);
    m_autoTranslateDelay->setSingleStep(100);
    m_autoTranslateDelay->setSuffix(tr(" ms"));
    m_autoTranslateDelay->setSpecialValueText(tr("Off"));

    translationForm->addRow(tr("Source language:"), m_sourceLanguage);
    translationForm->addRow(tr("Primary language:"), m_primaryLanguage);
    translationForm->addRow(tr("Additional language:"), m_secondaryLanguage);
    translationForm->addRow(tr("Maximum text length:"), m_maxLength);
    translationForm->addRow(tr("Auto-translate delay:"), m_autoTranslateDelay);
    auto *translationHint = new QLabel(
        tr("The additional language is reserved for automatic language switching "
           "and is not used yet."),
        translationPage);
    translationHint->setWordWrap(true);
    translationLayout->addLayout(translationForm);
    translationLayout->addWidget(translationHint);
    translationLayout->addStretch();

    m_tabs->addTab(generalPage, tr("General"));
    m_tabs->addTab(hotkeysPage, tr("Hotkeys"));
    m_tabs->addTab(translationPage, tr("Translation"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("settingsButtons"));
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs, 1);
    layout->addWidget(buttons);
}

void SettingsDialog::loadValues(const AppSettings &settings, bool autostartApproved)
{
    m_autostart->setChecked(settings.general.autostart);
    m_startMinimized->setChecked(settings.general.startMinimized);
    m_restoreClipboard->setChecked(settings.general.restoreClipboard);
    m_autoClosePopup->setChecked(settings.popup.autoCloseOnFocusLoss);

    auto updateAutostartHint = [this, autostartApproved] {
        m_autostartHint->setVisible(!autostartApproved && m_autostart->isChecked());
    };
    connect(m_autostart, &QCheckBox::toggled, this, updateAutostartHint);
    updateAutostartHint();

    m_popupHotkey->setKeySequence(settings.hotkeys.popup);
    m_mainWindowHotkey->setKeySequence(settings.hotkeys.mainWindow);
    m_showMainWindowHotkey->setKeySequence(settings.hotkeys.showMainWindow);

    if (!m_sourceLanguage->setCurrentCode(settings.translation.sourceLang))
        m_sourceLanguage->setCurrentCode(Languages::autoCode());
    if (!m_primaryLanguage->setCurrentCode(settings.translation.primaryLang)
        && m_primaryLanguage->count() > 0) {
        m_primaryLanguage->setCurrentIndex(0);
    }
    if (!m_secondaryLanguage->setCurrentCode(settings.translation.secondaryLang)
        && m_secondaryLanguage->count() > 0) {
        m_secondaryLanguage->setCurrentIndex(0);
    }

    m_maxLength->setValue(settings.translation.maxLength);
    m_autoTranslateDelay->setValue(settings.translation.autoTranslateDelayMs);
}

AppSettings SettingsDialog::settings() const
{
    AppSettings result = m_initial;
    result.general.autostart = m_autostart->isChecked();
    result.general.startMinimized = m_startMinimized->isChecked();
    result.general.restoreClipboard = m_restoreClipboard->isChecked();
    result.hotkeys.popup = m_popupHotkey->keySequence();
    result.hotkeys.mainWindow = m_mainWindowHotkey->keySequence();
    result.hotkeys.showMainWindow = m_showMainWindowHotkey->keySequence();
    result.translation.sourceLang = m_sourceLanguage->currentCode();
    result.translation.primaryLang = m_primaryLanguage->currentCode();
    result.translation.secondaryLang = m_secondaryLanguage->currentCode();
    result.translation.maxLength = m_maxLength->value();
    result.translation.autoTranslateDelayMs = m_autoTranslateDelay->value();
    result.popup.autoCloseOnFocusLoss = m_autoClosePopup->isChecked();
    return result;
}

bool SettingsDialog::validate(QString *error) const
{
    const struct
    {
        QKeySequence sequence;
        QString name;
    } hotkeys[] = {
        {m_popupHotkey->keySequence(), tr("Translate in a popup")},
        {m_mainWindowHotkey->keySequence(), tr("Translate in the main window")},
        {m_showMainWindowHotkey->keySequence(), tr("Show the main window")},
    };

    for (const auto &hotkey : hotkeys) {
        if (hotkey.sequence.isEmpty())
            continue;
        if (hotkey.sequence.count() != 1) {
            if (error) {
                *error = tr("\"%1\" uses more than one key combination. "
                            "Global hotkeys support a single combination.")
                             .arg(hotkey.sequence.toString(QKeySequence::NativeText));
            }
            return false;
        }
        if (!HotkeySequence::fromKeySequence(hotkey.sequence).isValid()) {
            if (error) {
                *error = tr("\"%1\" cannot be used as a global hotkey.")
                             .arg(hotkey.sequence.toString(QKeySequence::NativeText));
            }
            return false;
        }
    }

    for (int i = 0; i < int(std::size(hotkeys)); ++i) {
        if (hotkeys[i].sequence.isEmpty())
            continue;
        for (int j = i + 1; j < int(std::size(hotkeys)); ++j) {
            if (hotkeys[j].sequence == hotkeys[i].sequence) {
                if (error) {
                    *error = tr("The same key combination is assigned more than once: %1.")
                                 .arg(hotkeys[i].sequence.toString(QKeySequence::NativeText));
                }
                return false;
            }
        }
    }

    if (!m_primaryLanguage->currentCode().isEmpty()
        && m_primaryLanguage->currentCode() == m_secondaryLanguage->currentCode()) {
        if (error)
            *error = tr("The primary and additional languages must differ.");
        return false;
    }
    return true;
}

void SettingsDialog::accept()
{
    QString error;
    if (!validate(&error)) {
        QMessageBox::warning(this, tr("Settings"), error);
        return;
    }
    QDialog::accept();
}

void SettingsDialog::closeEvent(QCloseEvent *event)
{
    setHotkeyEditing(false);
    QDialog::closeEvent(event);
}

bool SettingsDialog::anyHotkeyEditFocused() const
{
    const QWidget *focus = QApplication::focusWidget();
    if (!focus)
        return false;
    for (const QKeySequenceEdit *edit :
         {m_popupHotkey, m_mainWindowHotkey, m_showMainWindowHotkey}) {
        if (edit == focus || edit->isAncestorOf(focus))
            return true;
    }
    return false;
}

void SettingsDialog::updateHotkeyEditing()
{
    setHotkeyEditing(anyHotkeyEditFocused());
}

void SettingsDialog::setHotkeyEditing(bool editing)
{
    if (m_hotkeyEditing == editing)
        return;
    m_hotkeyEditing = editing;
    emit hotkeyEditingChanged(editing);
}
