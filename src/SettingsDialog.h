#pragma once

#include <QDialog>
#include <QJsonObject>
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
class QTabWidget;
class QTableWidget;
class TerminalSettings;

// config.json as a form. It edits the JSON itself, so keys it has no field
// for survive a save.
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(const QJsonObject &json, QWidget *parent = nullptr);

    // The edited config, ready for Config::writeJson().
    QJsonObject config() const;
    // checkouts | agents | docker | general
    void showPage(const QString &page);

    void accept() override;
    void reject() override;

private:
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
    static ListButtons listButtons(const QString &addText);

    void loadCheckout(int row);
    void editCheckout(const QString &key, const QJsonValue &value);
    void addCheckout();
    void moveCheckout(int delta);
    void checkPath();

    void loadAgent(int row);
    void editAgent(const QString &key, const QJsonValue &value);
    void setAgentKind(const QString &kind);
    int addAgent();
    void removeAgent();
    void moveAgent(int delta);
    void refreshAgentChoices();
    QString agentLabel(int row) const;

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
    QLineEdit *m_checkoutOffset;
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
    QLineEdit *m_composeUp;
    QLineEdit *m_composeDown;
    QLineEdit *m_composeRestart;
    QTableWidget *m_quick;
    QTableWidget *m_links;

    QCheckBox *m_autostart;
    QCheckBox *m_notifyDone;
    QComboBox *m_konsoleProfile;
    TerminalSettings *m_terminals;
    QComboBox *m_terminalSource;
    QComboBox *m_terminalProfile;
    void fillTerminalProfiles(const QString &selected);
};
