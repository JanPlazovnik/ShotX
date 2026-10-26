#include "settings.h"

#include <KGlobalAccel>
#include <KGlobalShortcutInfo>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QSettings config()
{
    return QSettings(QStringLiteral("shotx"), QStringLiteral("shotx"));
}

// The entry may come from ~/.config/autostart or from the system-wide /etc/xdg/autostart.
bool autostartEnabled()
{
    const QString path = QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("autostart/shotx.desktop"));
    if (path.isEmpty()) {
        return false;
    }
    QSettings entry(path, QSettings::IniFormat);
    return !entry.value(QStringLiteral("Desktop Entry/Hidden"), false).toBool();
}

// Always writes a user entry; Hidden=true overrides a system-wide one.
bool setAutostart(bool enabled)
{
    QFile source(QStandardPaths::locate(QStandardPaths::ApplicationsLocation, QStringLiteral("shotx.desktop")));
    if (!source.open(QIODevice::ReadOnly)) {
        return false;
    }
    QByteArray entry = source.readAll();
    if (!enabled) {
        entry += "Hidden=true\n";
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/autostart");
    QDir().mkpath(dir);
    QSaveFile target(dir + QStringLiteral("/shotx.desktop"));
    return target.open(QIODevice::WriteOnly) && target.write(entry) == entry.size() && target.commit();
}

}

Settings Settings::load()
{
    QSettings s = config();
    return {
        s.value(QStringLiteral("folder"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/Screenshots")).toString(),
        s.value(QStringLiteral("pattern"), QStringLiteral("'Screenshot_'yyyy-MM-dd_HH-mm-ss-zzz")).toString(),
        s.value(QStringLiteral("format"), QStringLiteral("png")).toString(),
        s.value(QStringLiteral("copy"), true).toBool(),
        s.value(QStringLiteral("save"), true).toBool(),
        s.value(QStringLiteral("notify"), true).toBool(),
    };
}

void Settings::store() const
{
    QSettings s = config();
    s.setValue(QStringLiteral("folder"), folder);
    s.setValue(QStringLiteral("pattern"), pattern);
    s.setValue(QStringLiteral("format"), format);
    s.setValue(QStringLiteral("copy"), copy);
    s.setValue(QStringLiteral("save"), save);
    s.setValue(QStringLiteral("notify"), notify);
}

QString Settings::filePath(const QDateTime &time) const
{
    return folder + QLatin1Char('/') + time.toString(pattern) + QLatin1Char('.') + format;
}

SettingsDialog::SettingsDialog(const QList<QAction *> &actions, QWidget *parent)
    : QDialog(parent)
    , m_actions(actions)
    , m_folder(new QLineEdit(this))
    , m_pattern(new QLineEdit(this))
    , m_example(new QLabel(this))
    , m_format(new QComboBox(this))
    , m_copy(new QCheckBox(QStringLiteral("Copy to clipboard"), this))
    , m_save(new QCheckBox(QStringLiteral("Save to folder"), this))
    , m_notify(new QCheckBox(QStringLiteral("Show a notification when saved"), this))
    , m_autostart(new QCheckBox(QStringLiteral("Start ShotX on login"), this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("ShotX Settings"));
    const Settings settings = Settings::load();

    auto *general = new QWidget(this);
    auto *generalLayout = new QVBoxLayout(general);

    auto *saving = new QGroupBox(QStringLiteral("Saving"), general);
    auto *savingLayout = new QFormLayout(saving);
    auto *folderRow = new QHBoxLayout();
    m_folder->setText(settings.folder);
    folderRow->addWidget(m_folder);
    auto *browse = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open-folder")), QStringLiteral("Browse…"), saving);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Screenshot folder"), m_folder->text());
        if (!dir.isEmpty()) {
            m_folder->setText(dir);
        }
    });
    folderRow->addWidget(browse);
    savingLayout->addRow(QStringLiteral("Folder:"), folderRow);

    m_pattern->setText(settings.pattern);
    m_pattern->setToolTip(QStringLiteral("Date/time format: yyyy MM dd HH mm ss zzz. Put literal text in single quotes."));
    savingLayout->addRow(QStringLiteral("File name:"), m_pattern);

    const QList<QByteArray> writable = QImageWriter::supportedImageFormats();
    for (const QString &format : {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("webp")}) {
        if (writable.contains(format.toLatin1())) {
            m_format->addItem(format.toUpper(), format);
        }
    }
    m_format->setCurrentIndex(std::max(0, m_format->findData(settings.format)));
    savingLayout->addRow(QStringLiteral("Format:"), m_format);

    m_example->setTextInteractionFlags(Qt::TextSelectableByMouse);
    savingLayout->addRow(QStringLiteral("Example:"), m_example);
    const auto updateExample = [this] {
        m_example->setText(QDateTime::currentDateTime().toString(m_pattern->text()) + QLatin1Char('.') + m_format->currentData().toString());
    };
    connect(m_pattern, &QLineEdit::textChanged, this, updateExample);
    connect(m_format, &QComboBox::currentIndexChanged, this, updateExample);
    updateExample();
    generalLayout->addWidget(saving);

    auto *after = new QGroupBox(QStringLiteral("After capture"), general);
    auto *afterLayout = new QVBoxLayout(after);
    m_copy->setChecked(settings.copy);
    m_save->setChecked(settings.save);
    m_notify->setChecked(settings.notify);
    afterLayout->addWidget(m_copy);
    afterLayout->addWidget(m_save);
    afterLayout->addWidget(m_notify);
    generalLayout->addWidget(after);

    auto *startup = new QGroupBox(QStringLiteral("Startup"), general);
    auto *startupLayout = new QVBoxLayout(startup);
    m_autostart->setChecked(autostartEnabled());
    startupLayout->addWidget(m_autostart);
    generalLayout->addWidget(startup);
    generalLayout->addStretch();

    auto *shortcuts = new QWidget(this);
    auto *shortcutsLayout = new QVBoxLayout(shortcuts);
    auto *grid = new QGridLayout();
    grid->addWidget(new QLabel(QStringLiteral("<b>Primary</b>"), shortcuts), 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("<b>Alternate</b>"), shortcuts), 0, 2);
    for (QAction *action : std::as_const(m_actions)) {
        const int row = grid->rowCount();
        grid->addWidget(new QLabel(action->text(), shortcuts), row, 0);
        const QList<QKeySequence> keys = KGlobalAccel::self()->shortcut(action);
        QList<QKeySequenceEdit *> edits;
        for (int i = 0; i < 2; ++i) {
            auto *edit = new QKeySequenceEdit(keys.value(i), shortcuts);
            edit->setMaximumSequenceLength(1);
            edit->setClearButtonEnabled(true);
            grid->addWidget(edit, row, i + 1);
            edits << edit;
        }
        m_keyEdits << edits;
    }
    shortcutsLayout->addLayout(grid);
    auto *defaults = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-reset")), QStringLiteral("Use default shortcuts"), shortcuts);
    connect(defaults, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < m_actions.size(); ++i) {
            const QList<QKeySequence> keys = KGlobalAccel::self()->defaultShortcut(m_actions.at(i));
            for (int j = 0; j < m_keyEdits.at(i).size(); ++j) {
                m_keyEdits.at(i).at(j)->setKeySequence(keys.value(j));
            }
        }
    });
    auto *defaultsRow = new QHBoxLayout();
    defaultsRow->addStretch();
    defaultsRow->addWidget(defaults);
    shortcutsLayout->addLayout(defaultsRow);
    auto *hint = new QLabel(QStringLiteral("If a shortcut is used by another app, ShotX asks before taking it over."), shortcuts);
    hint->setWordWrap(true);
    shortcutsLayout->addWidget(hint);
    shortcutsLayout->addStretch();

    auto *tabs = new QTabWidget(this);
    tabs->addTab(general, QStringLiteral("General"));
    tabs->addTab(shortcuts, QStringLiteral("Shortcuts"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
    resize(520, sizeHint().height());
}

void SettingsDialog::accept()
{
    Settings settings{
        m_folder->text().trimmed(),
        m_pattern->text(),
        m_format->currentData().toString(),
        m_copy->isChecked(),
        m_save->isChecked(),
        m_notify->isChecked(),
    };
    const QString name = QDateTime::currentDateTime().toString(settings.pattern);
    if (settings.folder.isEmpty() || name.isEmpty() || name.contains(QLatin1Char('/'))) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Choose a folder and a file name that does not contain '/'."));
        return;
    }
    if (!applyShortcuts()) {
        return;
    }
    settings.store();
    if (m_autostart->isChecked() != autostartEnabled() && !setAutostart(m_autostart->isChecked())) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Could not change the login autostart entry."));
    }
    QDialog::accept();
}

bool SettingsDialog::applyShortcuts()
{
    QList<QKeySequence> current;
    for (QAction *action : std::as_const(m_actions)) {
        current += KGlobalAccel::self()->shortcut(action);
    }

    QList<QList<QKeySequence>> wanted;
    QList<QKeySequence> seen;
    for (const QList<QKeySequenceEdit *> &edits : std::as_const(m_keyEdits)) {
        QList<QKeySequence> keys;
        for (QKeySequenceEdit *edit : edits) {
            const QKeySequence key = edit->keySequence();
            if (key.isEmpty() || keys.contains(key)) {
                continue;
            }
            if (seen.contains(key)) {
                QMessageBox::warning(this, windowTitle(),
                                     QStringLiteral("%1 is assigned to more than one action.").arg(key.toString(QKeySequence::NativeText)));
                return false;
            }
            seen << key;
            keys << key;
        }
        wanted << keys;
    }

    for (const QKeySequence &key : std::as_const(seen)) {
        if (current.contains(key)) {
            continue;
        }
        const QList<KGlobalShortcutInfo> owners = KGlobalAccel::globalShortcutsByKey(key);
        if (owners.isEmpty()) {
            continue;
        }
        if (!KGlobalAccel::promptStealShortcutSystemwide(this, owners, key)) {
            return false;
        }
        KGlobalAccel::stealShortcutSystemwide(key);
    }

    // Clear first so keys can move between ShotX's own actions.
    for (QAction *action : std::as_const(m_actions)) {
        KGlobalAccel::self()->setShortcut(action, {}, KGlobalAccel::NoAutoloading);
    }
    for (int i = 0; i < m_actions.size(); ++i) {
        KGlobalAccel::self()->setShortcut(m_actions.at(i), wanted.at(i), KGlobalAccel::NoAutoloading);
    }
    return true;
}
