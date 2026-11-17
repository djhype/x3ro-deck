#pragma once

#include "AgentBus.h"
#include "Config.h"
#include "DockerClient.h"
#include "GitWatcher.h"
#include "Models.h"
#include "PrWatcher.h"
#include "SessionBackend.h"

#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantMap>

struct AgentInfo
{
    QString profile; // AgentProfile::id
    // offline | ready | working | permission | question | done
    QString state = QStringLiteral("offline");
    bool running = false;
    QDateTime runningSince;
    bool hooksSeen = false; // a hook event arrived since the agent process started
    QDateTime lastHookAt;
    QString model;
    QString sessionId;
    QDateTime startedAt;
    QDateTime since; // when `state` was entered
    int tools = 0;
    QSet<QString> files;
    QString lastTool;
    QString lastMessage;
    QString questionTitle;
    QString permTool;
    QString permDetail;
    bool permCanAlways = false;
};

struct ServiceRow
{
    QString service;
    QStringList ids;
    QStringList names;
    QString health;
    QString ports;
    QString uptime;
    double cpu = -1;
    qint64 mem = -1;
};

struct Checkout
{
    CheckoutConfig cfg;
    QString session; // session backend name
    GitInfo git;
    QString ticket;
    QString title;
    QString project; // compose project, once docker reports one
    QList<ContainerInfo> containers;
    QList<ServiceRow> services;
    AgentInfo agent;
    PrInfo pr;
};

class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(QString tab READ tab NOTIFY tabChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    Q_PROPERTY(QVariantList quickCommands READ quickCommands NOTIFY configChanged)
    Q_PROPERTY(QVariantList agents READ agentProfiles NOTIFY configChanged)
    Q_PROPERTY(QVariantList links READ links NOTIFY currentChanged)
    Q_PROPERTY(int containersUp READ containersUp NOTIFY totalsChanged)
    Q_PROPERTY(int containersTotal READ containersTotal NOTIFY totalsChanged)
    Q_PROPERTY(int needsYou READ needsYou NOTIFY totalsChanged)
    Q_PROPERTY(bool dockerConnected READ dockerConnected NOTIFY totalsChanged)
    Q_PROPERTY(bool railVisible READ railVisible NOTIFY railVisibleChanged)
    Q_PROPERTY(QString configError READ configError NOTIFY configChanged)
    Q_PROPERTY(VariantListModel *checkouts READ checkoutModel CONSTANT)
    Q_PROPERTY(VariantListModel *services READ serviceModel CONSTANT)
    Q_PROPERTY(VariantListModel *attention READ attentionModel CONSTANT)

public:
    explicit AppController(QObject *parent = nullptr);

    void start();

    int currentIndex() const { return m_current; }
    QString tab() const { return m_tab; }
    QVariantMap current() const;
    QVariantList quickCommands() const;
    QVariantList agentProfiles() const;
    QVariantList links() const;
    int containersUp() const;
    int containersTotal() const;
    int needsYou() const { return static_cast<int>(m_attention->rowCount()); }
    bool dockerConnected() const { return m_docker->connected(); }
    bool railVisible() const { return m_railVisible; }
    QString configError() const { return m_configError; }

    VariantListModel *checkoutModel() const { return m_checkoutModel; }
    VariantListModel *serviceModel() const { return m_serviceModel; }
    VariantListModel *attentionModel() const { return m_attention; }

    const Config &config() const { return m_config; }
    int checkoutCount() const { return static_cast<int>(m_checkouts.size()); }
    const Checkout &checkout(int i) const { return m_checkouts.at(i); }
    const AgentProfile &profile(int i) const { return m_config.agent(m_checkouts.at(i).agent.profile); }
    SessionBackend *sessions() const { return m_sessions; }

    // The window reports whether container stats are on screen, so sampling stops when hidden.
    void setStatsVisible(bool visible);

    Q_INVOKABLE void select(int index);
    Q_INVOKABLE void setTab(const QString &tab);
    Q_INVOKABLE void toggleRail();
    Q_INVOKABLE void runQuick(int index);
    Q_INVOKABLE void runCompose(const QString &which);
    Q_INVOKABLE void openLink(int index);
    Q_INVOKABLE void serviceAction(int row, const QString &action);
    Q_INVOKABLE void attentionAction(const QString &id, const QString &action);
    Q_INVOKABLE void sendToAgent(const QString &text);
    Q_INVOKABLE void agentAction(const QString &action);
    // Picks the current checkout's agent profile; ignored while an agent runs there.
    Q_INVOKABLE void setAgentProfile(const QString &id);
    // Opens the settings dialog on `page` (checkouts, agents, docker or general).
    Q_INVOKABLE void openSettings(const QString &page = QString());
    // -1 means the current checkout.
    Q_INVOKABLE void openPr(int index = -1);
    Q_INVOKABLE void refreshPr();

    // Makes sure the checkout's agent session exists. Creating it switches to
    // the checkout's configured agent and, with autostartAgents, starts it.
    bool ensureAgentSession(int index);
    // Re-reads config.json and applies what can change at runtime. Returns
    // false when the checkout list changed, which takes a restart.
    bool reloadConfig();

Q_SIGNALS:
    void currentChanged();
    void tabChanged();
    void totalsChanged();
    void railVisibleChanged();
    void runInTerminal(int checkout, const QString &title, const QString &command);
    void focusAgentTerminal(int checkout);
    void attentionRaised(const QVariantMap &item);
    void agentStateChanged(int checkout);
    void configChanged();
    void settingsRequested(const QString &page);

private:
    void sampleStats();
    void onContainers(const QList<ContainerInfo> &containers);
    void onStats(const QHash<QString, ContainerStats> &stats);
    void onHookEvent(const QJsonObject &payload);
    void onPaneCommands(const QHash<QString, QString> &commands);
    void onGit(const QString &key, const GitInfo &info);
    void onPr(const QString &key, const PrInfo &info);

    int checkoutForPath(const QString &path) const;
    void setAgentState(int index, const QString &state);
    void useProfile(int index, const QString &id);
    void rebuildServices(int index);
    void publishCheckouts();
    void publishServices();
    void publishAttention();
    QString expand(int index, const QString &command) const;
    int publicPort(int index, const QString &service, int containerPort) const;

    // Before m_config, which Config::load() fills it while initializing.
    QString m_configError;
    Config m_config;
    QList<Checkout> m_checkouts;
    int m_current = 0;
    QString m_tab = QStringLiteral("docker");
    bool m_railVisible = true;

    DockerClient *m_docker;
    AgentBus *m_bus;
    SessionBackend *m_sessions;
    GitWatcher *m_git;
    PrWatcher *m_prs;
    VariantListModel *m_checkoutModel;
    VariantListModel *m_serviceModel;
    VariantListModel *m_attention;

    QHash<QString, ContainerStats> m_stats;
    QHash<QString, QDateTime> m_dockerIssueSince;
    QSet<QString> m_dismissed; // attention keys
    QSet<QString> m_raised; // attention keys already announced
    QTimer m_statsTimer;
    bool m_statsVisible = false;
    bool m_panesPolled = false;
};
