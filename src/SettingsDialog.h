#pragma once

#include "DeckDialog.h"

#include <QHash>
#include <QJsonObject>
#include <QKeySequence>
#include <QList>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QKeySequenceEdit;
class QTabWidget;
class QTableWidget;
class TerminalSettings;
struct Dependency;

// config.json as a form. It edits the JSON itself, so keys it has no field
// for survive a save.
class SettingsDialog : public DeckDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(const QJsonObject &json, QWidget *parent = nullptr);

    // The edited config, ready for Config::writeJson().
    QJsonObject config() const;
    // checkouts | agents | docker | general
    void showPage(const QString &page);
    // Fills General → Dependencies. `ready` is false while the login shell's PATH is still being read.
    void setDependencies(const QList<Dependency> &list, bool ready);

    void accept() override;

Q_SIGNALS:
    void aboutRequested();
    // The theme picker changed; the window shows it until Save or Cancel.
    void themePreview(const QString &id, const QString &light, const QString &dark);
    void editThemeRequested(const QString &id);

public:
    // Lists the themes again after the editor saved one, and picks `select`.
    void themesChanged(const QString &select);
    // Emits themePreview for the current picks (after the editor's own preview).
    void previewTheme();
    void reject() override;

private:
    struct HookRow
    {
        QLabel *status = nullptr;
        QPushButton *install = nullptr;
        QPushButton *uninstall = nullptr;
    };
    HookRow m_hooks[2];

    struct Agent
    {
        QString id; // ":new<n>" until config() names it after its label
        QJsonObject json; // the kind's defaults merged with the user's keys
    };
    struct ListButtons
    {
        QHBoxLayout *layout;
        QPushButton *add;
        QPushButton *remove;
        QPushButton *up;
        QPushButton *down;
    };

    QWidget *buildCheckoutsPage();
    QWidget *buildAgentsPage();
    QWidget *buildDockerPage();
    QWidget *buildGeneralPage();
    QWidget *buildShortcutsPage();
    QHash<QString, QKeySequence> shortcutBindings() const;
    void checkShortcuts();
    static ListButtons listButtons(const QString &addText);

    void loadCheckout(int row);
    void editCheckout(const QString &key, const QJsonValue &value);
    void addCheckout();
    void moveCheckout(int delta);
    void checkPath();
    void storeComposeFiles();
    void refreshComposePlaceholders();

    void loadAgent(int row);
    void editAgent(const QString &key, const QJsonValue &value);
    void setAgentKind(const QString &kind);
    int addAgent();
    void removeAgent();
    void moveAgent(int delta);
    void refreshAgentChoices();
    QString agentLabel(int row) const;
    QWidget *hookRow(int cli);
    void refreshHooks();
    void changeHook(int cli, bool install);

    bool validate();
    bool dirty() const { return config() != m_initial; }

    QJsonObject m_root;
    QJsonObject m_initial;
    QList<QJsonObject> m_checkouts;
    QList<Agent> m_agents;
    QString m_defaultAgent;
    int m_newAgents = 0;
    bool m_loading = false;

    QTabWidget *m_tabs;

    QListWidget *m_checkoutList;
    ListButtons m_checkoutButtons;
    QWidget *m_checkoutForm;
    QGridLayout *m_checkoutFields;
    QLineEdit *m_checkoutName;
    QLineEdit *m_checkoutPath;
    QLabel *m_checkoutPathWarning;
    QLineEdit *m_checkoutArgs;
    QComboBox *m_checkoutAgent;

    QComboBox *m_defaultAgentBox;
    QListWidget *m_agentList;
    ListButtons m_agentButtons;
    QWidget *m_agentForm;
    QGridLayout *m_agentFields;
    QLineEdit *m_agentLabel;
    QComboBox *m_agentKind;
    QLabel *m_agentKindHint;
    QLineEdit *m_agentCommand;
    QLineEdit *m_agentResume;
    QTableWidget *m_agentEnv;
    QPlainTextEdit *m_agentPrompts;
    QLineEdit *m_agentPrPrompt;
    QLineEdit *m_agentProcess;
    QWidget *m_agentKeys;
    QLabel *m_agentKeysHint;
    QLineEdit *m_keyAllow;
    QLineEdit *m_keyAlways;
    QLineEdit *m_keyDeny;
    QLineEdit *m_agentDelay;

    QLineEdit *m_dockerHost;
    QLabel *m_dockerEndpoint;
    QComboBox *m_composeBinary;
    QComboBox *m_theme;
    QHash<QString, QKeySequenceEdit *> m_shortcutEdits;
    QHash<QString, QLabel *> m_shortcutWarnings;
    QRadioButton *m_backendTmux;
    QRadioButton *m_backendDaemon;
    QComboBox *m_themeLight;
    QComboBox *m_themeDark;
    QList<QWidget *> m_themeSystemRows; // the light and dark rows, shown for System
    void fillThemes(const QString &theme, const QString &light, const QString &dark);
    QListWidget *m_composeFiles;
    QLabel *m_composeFilesEmpty;
    QLineEdit *m_composeProfiles;
    QLineEdit *m_composeEnvFile;
    QLineEdit *m_composeProject;
    QLineEdit *m_composeUp;
    QLineEdit *m_composeDown;
    QLineEdit *m_composeRestart;
    QTableWidget *m_quick;
    QTableWidget *m_links;

    QCheckBox *m_autostart;
    QCheckBox *m_notifyDone;
    TerminalSettings *m_terminals;
    QGridLayout *m_dependencies = nullptr;
    QComboBox *m_terminalSource;
    QComboBox *m_terminalProfile;
    void fillTerminalProfiles(const QString &selected);
};
