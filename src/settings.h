#pragma once

#include <QDialog>

class QAction;
class QCheckBox;
class QComboBox;
class QDateTime;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;

struct Settings {
    QString folder;
    // QDateTime format string; literal text must be quoted, e.g. 'Screenshot_'yyyy-MM-dd.
    QString pattern;
    QString format;
    bool copy;
    bool save;
    bool notify;

    static Settings load();
    void store() const;
    QString filePath(const QDateTime &time) const;
};

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(const QList<QAction *> &actions, QWidget *parent = nullptr);

    void accept() override;

private:
    bool applyShortcuts();

    QList<QAction *> m_actions;
    QList<QList<QKeySequenceEdit *>> m_keyEdits;
    QLineEdit *m_folder;
    QLineEdit *m_pattern;
    QLabel *m_example;
    QComboBox *m_format;
    QCheckBox *m_copy;
    QCheckBox *m_save;
    QCheckBox *m_notify;
    QCheckBox *m_autostart;
};
