// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AppController.h"

#include "ComposeFiles.h"
#include "DaemonBackend.h"
#include "DemoMode.h"
#include "DockerEndpoint.h"
#include "ListeningPorts.h"
#include "Paths.h"
#include "PortProbe.h"
#include "Shortcuts.h"
#include "SwitchingBackend.h"
#include "TmuxManager.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace
{
const QStringList kCheckoutRoles{QStringLiteral("id"),         QStringLiteral("name"),        QStringLiteral("ticket"),
                                 QStringLiteral("branch"),     QStringLiteral("agentState"),  QStringLiteral("agentLabel"),
                                 QStringLiteral("healthText"), QStringLiteral("healthState"), QStringLiteral("selected")};
const QStringList kServiceRoles{QStringLiteral("id"),     QStringLiteral("name"), QStringLiteral("health"), QStringLiteral("ports"),
                                QStringLiteral("uptime"), QStringLiteral("cpu"),  QStringLiteral("mem")};
const QStringList kAttentionRoles{QStringLiteral("id"),        QStringLiteral("itemId"),      QStringLiteral("key"),
                                  QStringLiteral("kind"),      QStringLiteral("checkout"),    QStringLiteral("checkoutName"),
                                  QStringLiteral("title"),     QStringLiteral("detail"),      QStringLiteral("since"),
                                  QStringLiteral("canAlways"), QStringLiteral("dismissable"), QStringLiteral("prUrl"),
                                  QStringLiteral("prPrompt"),  QStringLiteral("agentLabel")};

int healthRank(const QString &h)
{
    if (h == QLatin1String("unhealthy")) return 5;
    if (h == QLatin1String("restarting")) return 4;
    if (h == QLatin1String("exited")) return 3;
    if (h == QLatin1String("starting")) return 2;
    if (h == QLatin1String("healthy") || h == QLatin1String("running") || h == QLatin1String("completed")) return 0;
    return 1;
}

bool isUp(const ContainerInfo &c)
{
    return c.health == QLatin1String("healthy") || c.health == QLatin1String("running");
}

// Completed one-shot containers are neither up nor a problem, so totals skip them.
bool counts(const ContainerInfo &c)
{
    return c.health != QLatin1String("completed");
}

QString shortDuration(qint64 s)
{
    if (s < 0) return QStringLiteral("—");
    if (s < 60) return QStringLiteral("%1s").arg(s);
    if (s < 3600) return QStringLiteral("%1m").arg(s / 60);
    if (s < 86400) return QStringLiteral("%1h").arg(s / 3600);
    return QStringLiteral("%1d").arg(s / 86400);
}

QString formatMem(qint64 bytes)
{
    if (bytes < 0) return {};
    const double mb = bytes / (1024.0 * 1024.0);
    return mb >= 1024 ? QStringLiteral("%1G").arg(mb / 1024.0, 0, 'f', 1) : QStringLiteral("%1M").arg(qRound(mb));
}

QString sessionName(const QString &checkout)
{
    QString s = checkout;
    s.replace(QRegularExpression(QStringLiteral("[.:]")), QStringLiteral("_"));
    return s;
}

// Codex has sent shell commands both as a string and as an argv array.
QString commandText(const QJsonValue &command)
{
    if (!command.isArray()) {
        return command.toString();
    }
    QStringList argv;
    for (const QJsonValue &v : command.toArray()) {
        argv.append(v.toString());
    }
    return argv.join(QLatin1Char(' '));
}

// Files named in a Codex apply_patch body ("*** Update File: src/main.cpp").
QStringList patchFiles(const QJsonObject &input)
{
    static const QRegularExpression re(QStringLiteral(R"(^\*\*\* (?:Add|Update|Delete) File: (.+)$)"), QRegularExpression::MultilineOption);
    QStringList files;
    for (auto it = re.globalMatch(commandText(input.value(QStringLiteral("command")))); it.hasNext();) {
        files.append(it.next().captured(1).trimmed());
    }
    return files;
}

// Tools that stop for an answer: Claude Code's AskUserQuestion and Codex's request_user_input.
bool isQuestionTool(const QString &tool)
{
    return tool == QLatin1String("AskUserQuestion") || tool == QLatin1String("request_user_input");
}

QString questionText(const QJsonObject &input)
{
    QStringList questions;
    for (const QJsonValue &q : input.value(QStringLiteral("questions")).toArray()) {
        questions.append(q.toObject().value(QStringLiteral("question")).toString());
    }
    return questions.isEmpty() ? input.value(QStringLiteral("question")).toString() : questions.join(QLatin1Char('\n'));
}

QString describeInput(const QString &tool, const QJsonObject &input, const QString &root)
{
    auto rel = [&root](const QString &p) {
        return p.startsWith(root + QLatin1Char('/')) ? p.mid(root.size() + 1) : p;
    };
    if (tool == QLatin1String("Bash")) return commandText(input.value(QStringLiteral("command")));
    if (tool == QLatin1String("apply_patch")) {
        QStringList files = patchFiles(input);
        std::transform(files.begin(), files.end(), files.begin(), rel);
        return files.join(QStringLiteral(", "));
    }
    if (tool == QLatin1String("ExitPlanMode")) return input.value(QStringLiteral("plan")).toString();
    if (input.contains(QStringLiteral("file_path"))) return rel(input.value(QStringLiteral("file_path")).toString());
    if (input.contains(QStringLiteral("notebook_path"))) return rel(input.value(QStringLiteral("notebook_path")).toString());
    if (input.contains(QStringLiteral("url"))) return input.value(QStringLiteral("url")).toString();
    if (input.contains(QStringLiteral("pattern"))) return input.value(QStringLiteral("pattern")).toString();
    QString compact = QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact));
    return compact.size() > 240 ? compact.left(240) + QStringLiteral("…") : compact;
}

QString permissionTitle(const QString &tool)
{
    if (tool == QLatin1String("Bash")) return QStringLiteral("Wants to run a command");
    if (tool == QLatin1String("Edit") || tool == QLatin1String("MultiEdit") || tool == QLatin1String("Write")
        || tool == QLatin1String("NotebookEdit"))
        return QStringLiteral("Wants to edit a file");
    if (tool == QLatin1String("apply_patch")) return QStringLiteral("Wants to edit files");
    if (tool == QLatin1String("WebFetch")) return QStringLiteral("Wants to fetch a URL");
    if (tool.isEmpty()) return QStringLiteral("Needs your permission");
    return QStringLiteral("Wants to use %1").arg(tool);
}

// Shown while an agent runs but none of its hook events have arrived.
QString hookHint(const QString &kind)
{
    if (kind == QLatin1String("custom")) {
        return {};
    }
    if (kind == QLatin1String("codex")) {
        return QStringLiteral("No hook events from Codex yet. Codex runs a new hook only after you trust it: type /hooks in "
                              "Codex and trust x3ro-deck-hook.");
    }
    return QStringLiteral("No hook events from Claude Code yet. Check that x3ro-deck-hook is registered in ~/.claude/settings.json.");
}

bool runs(const AgentProfile &p, const QStringList &paneCommands)
{
    return !p.process.isEmpty()
        && std::any_of(paneCommands.begin(), paneCommands.end(), [&p](const QString &c) { return c.startsWith(p.process); });
}

// The profile whose agent the foreground job runs. The job's leader decides
// before the processes it started.
QString profileRunning(const QList<AgentProfile> &profiles, const QStringList &paneCommands)
{
    for (const QString &cmd : paneCommands) {
        for (const AgentProfile &p : profiles) {
            if (runs(p, {cmd})) {
                return p.id;
            }
        }
    }
    return {};
}

QVariantMap prMap(const PrInfo &pr)
{
    if (pr.number == 0) {
        return {};
    }
    return {{QStringLiteral("number"), pr.number},
            {QStringLiteral("url"), pr.url},
            {QStringLiteral("title"), pr.title},
            {QStringLiteral("state"), pr.draft && pr.state == QLatin1String("OPEN") ? QStringLiteral("DRAFT") : pr.state},
            {QStringLiteral("base"), pr.base},
            {QStringLiteral("review"), pr.reviewDecision},
            {QStringLiteral("mergeable"), pr.mergeable},
            {QStringLiteral("mergeState"), pr.mergeState},
            {QStringLiteral("additions"), pr.additions},
            {QStringLiteral("deletions"), pr.deletions},
            {QStringLiteral("checksPassed"), pr.checksPassed},
            {QStringLiteral("checksFailed"), pr.checksFailed},
            {QStringLiteral("checksPending"), pr.checksPending},
            {QStringLiteral("failing"), pr.failing},
            {QStringLiteral("approvers"), pr.approvers},
            {QStringLiteral("changesRequestedBy"), pr.changesRequestedBy}};
}

// A closed, unmerged PR is history; the branch is ready for a new one.
QString viewableUrl(const PrInfo &pr)
{
    return pr.number && pr.state != QLatin1String("CLOSED") ? pr.url : QString();
}

// Commands after which the branch's PR may have appeared or changed.
bool touchesPr(const QString &command)
{
    static const QRegularExpression re(QStringLiteral(R"(\bgh\s+(pr|api)\b|githubWrite|\bgit\s+push\b)"));
    return re.match(command).hasMatch();
}
} // namespace

SessionBackend *AppController::makeBackend(const QString &kind, QObject *parent)
{
    if (kind == QLatin1String("daemon")) {
        return new DaemonBackend(parent);
    }
    return new TmuxManager(parent);
}

// The configured backend, or while a switch lets the old backend's sessions
// finish, both (see SwitchingBackend).
SessionBackend *AppController::createBackend(const QString &kind, QObject *parent)
{
    QSettings settings;
    const QString legacy = settings.value(kLegacyBackendKey).toString();
    SessionBackend *primary = makeBackend(kind, parent);
    if (legacy.isEmpty() || legacy == kind) {
        settings.remove(kLegacyBackendKey);
        return primary;
    }
    auto *both = new SwitchingBackend(primary, makeBackend(legacy, parent), parent);
    connect(both, &SwitchingBackend::drained, both, [] { QSettings().remove(kLegacyBackendKey); });
    return both;
}

void AppController::switchBackend(const QString &to, BackendSwitch how)
{
    QSettings settings;
    if (how == BackendSwitch::Restart) {
        QStringList resume;
        for (const Checkout &c : std::as_const(m_checkouts)) {
            if (c.agent.running) {
                resume.append(c.cfg.name);
            }
        }
        settings.setValue(kResumeKey, resume);
        settings.remove(kLegacyBackendKey);
        m_sessions->killAll();
    } else if (how == BackendSwitch::Finish) {
        // A switch during a switch keeps the oldest sessions' backend only
        // when it is not the one switched to.
        auto *both = qobject_cast<SwitchingBackend *>(m_sessions);
        const QString old = both && settings.value(kLegacyBackendKey).toString() != to ? settings.value(kLegacyBackendKey).toString()
                                                                                       : m_config.sessionBackend;
        if (old != to) {
            settings.setValue(kLegacyBackendKey, old);
        }
    }
}

bool AppController::hasAnySession() const
{
    return std::any_of(m_checkouts.cbegin(), m_checkouts.cend(), [this](const Checkout &c) { return m_sessions->hasSession(c.session); });
}

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_config(s_demo ? DemoMode::config() : Config::load(&m_configError))
    , m_docker(new DockerClient(this))
    , m_bus(new AgentBus(this))
    , m_sessions(s_demo ? static_cast<SessionBackend *>(new DemoBackend(this)) : createBackend(m_config.sessionBackend, this))
    , m_terminalSettings(new TerminalSettings(this))
    , m_git(new GitWatcher(this))
    , m_prs(new PrWatcher(this))
    , m_deps(new Dependencies(this))
    , m_quick(new QuickCommands(this))
    , m_probe(new PortProbe(this))
    , m_checkoutModel(new VariantListModel(kCheckoutRoles, this))
    , m_serviceModel(new VariantListModel(kServiceRoles, this))
    , m_attention(new VariantListModel(kAttentionRoles, this))
{
    setWindowsShell(m_config.windowsShell);
    m_terminalSettings->select(m_config.terminalSettings, m_config.terminalProfile);
    m_deps->setConfig(m_config);
    m_firstRun = m_config.checkouts.isEmpty();

    // A session that survived a restart runs the profile last picked for it;
    // ensureAgentSession() switches a fresh one to the configured agent.
    QSettings settings;
    for (const CheckoutConfig &cc : std::as_const(m_config.checkouts)) {
        Checkout c;
        c.cfg = cc;
        c.session = sessionName(cc.name);
        c.project = cc.composeProject.isEmpty() ? cc.name : cc.composeProject;
        c.agent.profile = m_config.agent(settings.value(QStringLiteral("agents/") + cc.name, cc.agent).toString()).id;
        m_checkouts.append(c);
    }

    connect(m_docker, &DockerClient::containersUpdated, this, &AppController::onContainers);
    connect(m_docker, &DockerClient::statsUpdated, this, &AppController::onStats);
    connect(m_docker, &DockerClient::connectedChanged, this, &AppController::totalsChanged);
    connect(m_bus, &AgentBus::hookEvent, this, &AppController::onHookEvent);
    connect(m_sessions, &SessionBackend::foregroundProcesses, this, &AppController::onPaneCommands);
    connect(m_git, &GitWatcher::updated, this, &AppController::onGit);
    connect(m_prs, &PrWatcher::updated, this, &AppController::onPr);
    connect(m_attention, &VariantListModel::countChanged, this, &AppController::totalsChanged);
    connect(m_probe, &PortProbe::changed, this, &AppController::currentChanged);
    connect(this, &AppController::configChanged, this, &AppController::quickCommandsChanged);
    connect(this, &AppController::currentChanged, this, &AppController::quickCommandsChanged);
    connect(m_quick, &QuickCommands::changed, this, [this](int checkout) {
        if (checkout == m_current) {
            Q_EMIT quickCommandsChanged();
        }
    });
    connect(m_deps, &Dependencies::changed, this, [this] {
        Q_EMIT dependenciesChanged();
        Q_EMIT currentChanged();
    });

    // Dev servers started in a checkout (an agent's, a quick command's) get
    // browser links like Docker's published ports.
    m_portScan.setInterval(3000);
    connect(&m_portScan, &QTimer::timeout, this, &AppController::scanListeningPorts);
    m_statsTimer.setInterval(5000);
    connect(&m_statsTimer, &QTimer::timeout, this, &AppController::sampleStats);
}

void AppController::sampleStats()
{
    if (!m_statsVisible || m_current >= m_checkouts.size()) {
        return;
    }
    QStringList ids;
    for (const ContainerInfo &c : std::as_const(m_checkouts[m_current].containers)) {
        if (c.state == QLatin1String("running")) {
            ids.append(c.id);
        }
    }
    m_docker->sampleStats(ids);
}

void AppController::start()
{
    if (s_demo) {
        seedDemo();
        return;
    }
    QString err;
    if (!m_bus->listen(&err)) {
        qWarning("x3ro-deck: cannot listen on %s: %s", qPrintable(AgentBus::socketPath()), qPrintable(err));
    }
    QStringList names;
    QStringList dirs;
    for (int i = 0; i < m_checkouts.size(); ++i) {
        m_prs->watch(m_checkouts[i].cfg.name, m_checkouts[i].cfg.path);
        m_git->watch(m_checkouts[i].cfg.name, m_checkouts[i].cfg.path);
        names.append(m_checkouts[i].cfg.name);
        dirs.append(m_checkouts[i].cfg.path);
    }
    m_quick->setCheckouts(names, dirs);
    m_docker->setSocketPath(resolveDockerEndpoint(m_config.dockerHost).socketPath);
    m_docker->start();
    m_sessions->startPolling(3000);
    m_statsTimer.start();
    m_portScan.start();
    scanListeningPorts();
    m_deps->probeLoginPath(m_terminalSettings->current().shell);
    publishCheckouts();
    publishServices();
    publishAttention();
}

void AppController::setStatsVisible(bool visible)
{
    const bool became = visible && !m_statsVisible;
    m_statsVisible = visible;
    if (became) {
        sampleStats();
    }
}

// ───────────────────────── read-side properties ─────────────────────────

QVariantMap AppController::current() const
{
    if (m_current >= m_checkouts.size()) {
        return {};
    }
    const Checkout &c = m_checkouts.at(m_current);
    const AgentInfo &a = c.agent;
    const AgentProfile &p = profile(m_current);
    int up = 0;
    int total = 0;
    for (const ContainerInfo &ci : c.containers) {
        up += isUp(ci) ? 1 : 0;
        total += counts(ci) ? 1 : 0;
    }
    QString model = a.model;
    for (const auto &[name, value] : p.env) {
        if (model.isEmpty() && name == QLatin1String("ANTHROPIC_MODEL")) {
            model = value;
        }
    }
    return {
        {QStringLiteral("name"), c.cfg.name},
        {QStringLiteral("path"), Paths::tilde(c.cfg.path)},
        {QStringLiteral("branch"), c.git.branch.isEmpty() ? QStringLiteral("(detached)") : c.git.branch},
        {QStringLiteral("ticket"), c.ticket},
        {QStringLiteral("title"), c.title},
        {QStringLiteral("commit"), c.git.commit},
        {QStringLiteral("project"), c.project},
        // Whether the Up button has anything to start.
        {QStringLiteral("composeFound"),
         !c.cfg.composeFiles.isEmpty() || !c.cfg.composeUp.isEmpty() || !ComposeFiles::detect(c.cfg.path).isEmpty()},
        {QStringLiteral("session"), c.session},
        {QStringLiteral("attachHint"), m_sessions->attachHint(c.session)},
        {QStringLiteral("containersUp"), up},
        {QStringLiteral("containersTotal"), total},
        {QStringLiteral("agentState"), a.state},
        {QStringLiteral("agentRunning"), a.running},
        {QStringLiteral("agentId"), p.id},
        {QStringLiteral("agentLabel"), p.label},
        {QStringLiteral("agentKind"), p.kind},
        {QStringLiteral("model"), model},
        {QStringLiteral("runningSince"), a.runningSince.isValid() ? a.runningSince.toMSecsSinceEpoch() : 0},
        {QStringLiteral("hookHint"), a.running && !a.hooksSeen ? hookHint(p.kind) : QString()},
        {QStringLiteral("continueCommand"), p.continueArgs.isEmpty() ? QString() : p.command + QLatin1Char(' ') + p.continueArgs},
        {QStringLiteral("prPrompt"), p.prPrompt},
        {QStringLiteral("prompts"), p.prompts},
        {QStringLiteral("startedAt"), a.startedAt.isValid() ? a.startedAt.toMSecsSinceEpoch() : 0},
        {QStringLiteral("since"), a.since.isValid() ? a.since.toMSecsSinceEpoch() : 0},
        {QStringLiteral("tools"), a.tools},
        {QStringLiteral("files"), static_cast<int>(a.files.size())},
        {QStringLiteral("lastTool"), a.lastTool},
        {QStringLiteral("lastMessage"), a.lastMessage},
        {QStringLiteral("pr"), prMap(c.pr)},
        {QStringLiteral("prUrl"), viewableUrl(c.pr)},
        {QStringLiteral("prNotice"), prNotice(c.pr.problem)},
        {QStringLiteral("agentMissing"),
         [&] {
             const Dependency *d = agentMissing(m_current);
             if (d && d->id == QLatin1String("sessiond")) {
                 return QVariantMap{{QStringLiteral("title"), QStringLiteral("The session daemon is required")},
                                    {QStringLiteral("hint"), d->hint}};
             }
             return d ? QVariantMap{{QStringLiteral("title"), QStringLiteral("%1 not found").arg(d->name)},
                                    {QStringLiteral("hint"),
                                     QStringLiteral("%1 is not on PATH or the login shell's PATH. %2").arg(d->binary, d->hint)}}
                      : QVariantMap{};
         }()},
    };
}

QVariantMap AppController::prNotice(const QString &problem)
{
    auto notice = [](const QString &title, const QString &hint, bool info = false) {
        return QVariantMap{{QStringLiteral("title"), title},
                           {QStringLiteral("hint"), hint},
                           {QStringLiteral("tone"), info ? QStringLiteral("info") : QStringLiteral("warning")},
                           {QStringLiteral("action"), info ? QString() : QStringLiteral("Check again")}};
    };
    if (problem == QLatin1String("gh-missing")) {
        return notice(QStringLiteral("GitHub CLI (gh) not found"),
                      QStringLiteral("Install the GitHub CLI (https://cli.github.com), then run gh auth login. "
                                     "Pull request status is off until then."));
    }
    if (problem == QLatin1String("gh-logged-out")) {
        return notice(QStringLiteral("GitHub CLI is not logged in"),
                      QStringLiteral("Run gh auth login in a terminal. X3RO Deck checks again every few minutes."));
    }
    if (problem == QLatin1String("no-github-remote")) {
        return notice(QStringLiteral("No GitHub remote"),
                      QStringLiteral("The origin remote is missing or not on GitHub, so there is no pull request status."), true);
    }
    return {};
}

QVariantMap AppController::missingNotice(const QString &dependency) const
{
    const Dependency *d = m_deps->missing(dependency);
    if (!d) {
        return {};
    }
    return {{QStringLiteral("title"), QStringLiteral("%1 not found").arg(d->name)}, {QStringLiteral("hint"), d->hint}};
}

const Dependency *AppController::agentMissing(int index) const
{
    if (index < 0 || index >= m_checkouts.size()) {
        return nullptr;
    }
    // Without the session daemon nothing can run, whatever the agent.
    if (const Dependency *d = m_deps->missing(QStringLiteral("sessiond"))) {
        return d;
    }
    return m_deps->missing(Dependencies::commandBinary(profile(index).command));
}

QVariantList AppController::dependencies() const
{
    QVariantList out;
    for (const Dependency &d : m_deps->list()) {
        out.append(QVariantMap{{QStringLiteral("name"), d.name},
                               {QStringLiteral("purpose"), d.purpose},
                               {QStringLiteral("path"), d.path},
                               {QStringLiteral("hint"), d.hint},
                               {QStringLiteral("checked"), m_deps->ready()}});
    }
    return out;
}

QVariantMap AppController::shortcutHints() const
{
    return Shortcuts::hints(m_config.shortcuts);
}

QList<QuickCommand> AppController::manualQuickCommands() const
{
    if (m_current >= m_checkouts.size()) {
        return m_config.quickCommands;
    }
    return mergeQuickCommands(m_config.quickCommands, m_checkouts.at(m_current).cfg.quickCommands);
}

QVariantList AppController::quickCommands() const
{
    QVariantList out;
    for (const QuickEntry &e : m_quick->entries(m_current, manualQuickCommands())) {
        out.append(QVariantMap{{QStringLiteral("key"), e.key},
                               {QStringLiteral("label"), e.label},
                               {QStringLiteral("command"), e.command},
                               {QStringLiteral("group"), e.group},
                               {QStringLiteral("source"), e.source},
                               {QStringLiteral("description"), e.description},
                               {QStringLiteral("pinned"), e.pinned},
                               {QStringLiteral("hidden"), e.hidden}});
    }
    return out;
}

QVariantList AppController::agentProfiles() const
{
    QVariantList out;
    for (const AgentProfile &p : m_config.agents) {
        out.append(QVariantMap{{QStringLiteral("id"), p.id}, {QStringLiteral("label"), p.label}, {QStringLiteral("kind"), p.kind}});
    }
    return out;
}

// The configured links, then every published port of the checkout's
// running containers that answers HTTP, named after its service.
QVariantList AppController::links() const
{
    QVariantList out;
    for (const LinkConfig &l : m_config.links) {
        out.append(
            QVariantMap{{QStringLiteral("label"), l.label}, {QStringLiteral("port"), publicPort(m_current, l.service, l.containerPort)}});
    }
    if (m_current >= m_checkouts.size()) {
        return out;
    }
    struct Found
    {
        QString service;
        int privatePort;
        int publicPort;
    };
    QList<Found> found;
    QSet<int> seen;
    for (const ContainerInfo &c : m_checkouts.at(m_current).containers) {
        if (c.state != QLatin1String("running")) {
            continue;
        }
        for (const PortMapping &p : c.ports) {
            const bool configured = std::any_of(m_config.links.cbegin(), m_config.links.cend(), [&](const LinkConfig &l) {
                return l.service == c.service && l.containerPort == p.privatePort;
            });
            if (p.publicPort > 0 && !configured && m_probe->isHttp(p.publicPort) && !seen.contains(p.publicPort)) {
                seen.insert(p.publicPort);
                found.append({c.service, p.privatePort, p.publicPort});
            }
        }
    }
    std::sort(found.begin(), found.end(),
              [](const Found &a, const Found &b) { return std::tie(a.service, a.publicPort) < std::tie(b.service, b.publicPort); });
    // Then the checkout's own processes, named after the program.
    QList<QPair<QString, int>> processes;
    for (const auto &[name, port] : m_checkouts.at(m_current).listening) {
        if (m_probe->isHttp(port) && !seen.contains(port)) {
            seen.insert(port);
            processes.append({name, port});
        }
    }
    for (const Found &f : std::as_const(found)) {
        // A service with several web ports names each by its container port.
        const bool several = std::count_if(found.cbegin(), found.cend(), [&](const Found &o) { return o.service == f.service; }) > 1;
        out.append(QVariantMap{{QStringLiteral("label"), several ? QStringLiteral("%1:%2").arg(f.service).arg(f.privatePort) : f.service},
                               {QStringLiteral("port"), f.publicPort}});
    }
    for (const auto &[name, port] : std::as_const(processes)) {
        const bool several = std::count_if(processes.cbegin(), processes.cend(), [&](const auto &o) { return o.first == name; }) > 1;
        out.append(QVariantMap{{QStringLiteral("label"), several ? QStringLiteral("%1:%2").arg(name).arg(port) : name},
                               {QStringLiteral("port"), port}});
    }
    return out;
}

void AppController::scanListeningPorts()
{
    QList<QList<QPair<QString, int>>> next(m_checkouts.size());
    for (const ListeningPorts::Listener &l : ListeningPorts::scan()) {
        const int idx = l.cwd.isEmpty() ? -1 : checkoutForPath(Paths::normalize(l.cwd));
        if (idx >= 0 && !next[idx].contains(qMakePair(l.name, l.port))) {
            next[idx].append({l.name, l.port});
        }
    }
    bool changed = false;
    for (int i = 0; i < m_checkouts.size(); ++i) {
        std::sort(next[i].begin(), next[i].end(), [](const auto &a, const auto &b) { return a.second < b.second; });
        if (next[i] != m_checkouts[i].listening) {
            m_checkouts[i].listening = next[i];
            changed = true;
        }
    }
    if (changed) {
        probePorts();
        Q_EMIT currentChanged();
    }
}

void AppController::seedDemo()
{
    static const char *const branches[][2] = {{"feat/line-item-taxes", "4f2a91c Show taxes per line item"},
                                              {"refactor/invoice-export", "b81d3e0 Split the invoice exporter"},
                                              {"chore/state-bucket", "0c7e5aa Add the state bucket module"},
                                              {"fix/dynamic-type", "e93b210 Respect the system font size"}};
    for (int i = 0; i < m_checkouts.size() && i < 4; ++i) {
        onGit(m_checkouts[i].cfg.name, GitInfo{QLatin1String(branches[i][0]), QLatin1String(branches[i][1])});
    }
    PrInfo pr;
    pr.number = 482;
    pr.url = QStringLiteral("https://example.com/pull/482");
    pr.title = QStringLiteral("Show taxes per line item");
    pr.state = QStringLiteral("OPEN");
    pr.base = QStringLiteral("main");
    pr.reviewDecision = QStringLiteral("REVIEW_REQUIRED");
    pr.mergeable = QStringLiteral("MERGEABLE");
    pr.mergeState = QStringLiteral("BLOCKED");
    pr.additions = 61;
    pr.deletions = 18;
    pr.checksPassed = 7;
    pr.checksPending = 1;
    onPr(m_checkouts.value(0).cfg.name, pr);

    auto container = [](const QString &dir, const char *service, const char *health, int privatePort, int publicPort) {
        ContainerInfo c;
        c.id = QLatin1String(service) + dir.section(QLatin1Char('/'), -1);
        c.name = dir.section(QLatin1Char('/'), -1) + QLatin1Char('-') + QLatin1String(service) + QStringLiteral("-1");
        c.project = dir.section(QLatin1Char('/'), -1);
        c.service = QLatin1String(service);
        c.workingDir = dir;
        c.state = QStringLiteral("running");
        c.health = QLatin1String(health);
        c.status = QStringLiteral("Up 2 hours");
        c.upSeconds = 7200;
        if (privatePort) {
            c.ports = {PortMapping{privatePort, publicPort}};
        }
        return c;
    };
    if (m_checkouts.size() >= 2) {
        const QString a = m_checkouts[0].cfg.path;
        const QString b = m_checkouts[1].cfg.path;
        onContainers({container(a, "web", "healthy", 80, 8080), container(a, "api", "running", 3000, 3000),
                      container(a, "db", "healthy", 5432, 5432), container(b, "api", "unhealthy", 8000, 8001),
                      container(b, "worker", "running", 0, 0)});
    }

    // Agents: running in every session, then one hook story each.
    QHash<QString, QStringList> panes;
    for (int i = 0; i < m_checkouts.size(); ++i) {
        panes.insert(m_checkouts[i].session, {profile(i).process});
    }
    onPaneCommands(panes);
    auto hook = [this](int i, QJsonObject p) {
        if (i < m_checkouts.size()) {
            p.insert(QStringLiteral("cwd"), m_checkouts[i].cfg.path);
            p.insert(QStringLiteral("session_id"), QStringLiteral("demo-%1").arg(i));
            onHookEvent(p);
        }
    };
    hook(0,
         {{QStringLiteral("hook_event_name"), QStringLiteral("UserPromptSubmit")},
          {QStringLiteral("prompt"), QStringLiteral("Show taxes per line item")}});
    hook(1,
         {{QStringLiteral("hook_event_name"), QStringLiteral("UserPromptSubmit")},
          {QStringLiteral("prompt"), QStringLiteral("Split the invoice exporter")}});
    hook(1,
         {{QStringLiteral("hook_event_name"), QStringLiteral("PermissionRequest")},
          {QStringLiteral("tool_name"), QStringLiteral("Bash")},
          {QStringLiteral("tool_input"),
           QJsonObject{{QStringLiteral("command"), QStringLiteral("pnpm prisma migrate dev --name invoice_totals")}}}});
    hook(2,
         {{QStringLiteral("hook_event_name"), QStringLiteral("UserPromptSubmit")},
          {QStringLiteral("prompt"), QStringLiteral("Add a state bucket")}});
    hook(2,
         {{QStringLiteral("hook_event_name"), QStringLiteral("PreToolUse")},
          {QStringLiteral("tool_name"), QStringLiteral("AskUserQuestion")},
          {QStringLiteral("tool_input"),
           QJsonObject{{QStringLiteral("questions"),
                        QJsonArray{QJsonObject{
                            {QStringLiteral("question"), QStringLiteral("Which Terraform workspace should the new bucket go to?")}}}}}}});
    hook(3,
         {{QStringLiteral("hook_event_name"), QStringLiteral("UserPromptSubmit")},
          {QStringLiteral("prompt"), QStringLiteral("Respect the system font size")}});
    hook(3, {{QStringLiteral("hook_event_name"), QStringLiteral("Stop")}});
    publishCheckouts();
    publishServices();
    publishAttention();
}

void AppController::probePorts()
{
    if (s_demo) {
        return; // no network in the demo
    }
    QSet<int> ports = m_dockerPorts;
    for (const Checkout &c : std::as_const(m_checkouts)) {
        for (const auto &l : c.listening) {
            ports.insert(l.second);
        }
    }
    m_probe->setPorts(ports);
}

int AppController::containersUp() const
{
    int n = 0;
    for (const Checkout &c : m_checkouts) {
        for (const ContainerInfo &ci : c.containers) {
            n += isUp(ci) ? 1 : 0;
        }
    }
    return n;
}

int AppController::containersTotal() const
{
    int n = 0;
    for (const Checkout &c : m_checkouts) {
        n += static_cast<int>(std::count_if(c.containers.cbegin(), c.containers.cend(), counts));
    }
    return n;
}

int AppController::publicPort(int index, const QString &service, int containerPort) const
{
    if (index < 0 || index >= m_checkouts.size()) {
        return 0;
    }
    for (const ContainerInfo &c : m_checkouts.at(index).containers) {
        if (c.service == service) {
            for (const PortMapping &p : c.ports) {
                if (p.privatePort == containerPort) {
                    return p.publicPort;
                }
            }
        }
    }
    return 0;
}

QString AppController::expand(int index, const QString &command) const
{
    QString out = command;
    out.replace(QStringLiteral("{compose}"), m_config.composeBinary);
    out.replace(QStringLiteral("{project}"), m_checkouts.at(index).project);
    static const QRegularExpression portRe(QStringLiteral(R"(\{port:([^:}]+):(\d+)\})"));
    QRegularExpressionMatch m;
    while ((m = portRe.match(out)).hasMatch()) {
        const int port = publicPort(index, m.captured(1), m.captured(2).toInt());
        out.replace(m.capturedStart(), m.capturedLength(), port ? QString::number(port) : m.captured(2));
    }
    return out;
}

QString AppController::defaultProjectName(const QString &dir)
{
    QString out;
    for (const QChar ch : QFileInfo(dir).fileName().toLower()) {
        if ((ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) || ch.isDigit() || ch == QLatin1Char('_') || ch == QLatin1Char('-')) {
            out += ch;
        }
    }
    return out;
}

// In order: a checkout whose settings name the container's project; the
// checkout rooted exactly at the project's working_dir; the one holding its
// first compose file (for --project-directory elsewhere); and, only for containers without those labels (older
// compose), the one checkout whose folder gives the same default project
// name. Projects rooted in a checkout's subdirectory belong to none, and a
// checkout that names its project takes no others.
int AppController::checkoutForContainer(const ContainerInfo &ci) const
{
    for (int i = 0; i < m_checkouts.size(); ++i) {
        if (!m_checkouts[i].cfg.composeProject.isEmpty() && ci.project == m_checkouts[i].cfg.composeProject) {
            return i;
        }
    }
    auto rootedAt = [this](const QString &dir) {
        const QString path = Paths::normalize(dir);
        for (int i = 0; i < m_checkouts.size(); ++i) {
            if (m_checkouts[i].cfg.composeProject.isEmpty() && Paths::same(m_checkouts[i].cfg.path, path)) {
                return i;
            }
        }
        return -1;
    };
    if (!ci.workingDir.isEmpty()) {
        if (const int i = rootedAt(ci.workingDir); i >= 0) {
            return i;
        }
    }
    if (!ci.configFiles.isEmpty()) {
        return rootedAt(QFileInfo(ci.configFiles.first()).absolutePath());
    }
    if (!ci.workingDir.isEmpty()) {
        return -1;
    }
    int found = -1;
    for (int i = 0; i < m_checkouts.size(); ++i) {
        if (m_checkouts[i].cfg.composeProject.isEmpty() && defaultProjectName(m_checkouts[i].cfg.path) == ci.project) {
            if (found >= 0) {
                return -1; // two checkouts share the name; neither is sure
            }
            found = i;
        }
    }
    return found;
}

int AppController::checkoutForPath(const QString &path) const
{
    int best = -1;
    qsizetype bestLen = -1;
    for (int i = 0; i < m_checkouts.size(); ++i) {
        const QString &root = m_checkouts.at(i).cfg.path;
        if (root.size() > bestLen && Paths::isWithin(path, root)) {
            best = i;
            bestLen = root.size();
        }
    }
    return best;
}

// ───────────────────────── inputs ─────────────────────────

void AppController::onContainers(const QList<ContainerInfo> &containers)
{
    m_containers = containers;
    const QDateTime now = QDateTime::currentDateTime();
    for (Checkout &c : m_checkouts) {
        c.containers.clear();
    }
    QSet<QString> liveIssues;
    for (const ContainerInfo &ci : containers) {
        const int idx = checkoutForContainer(ci);
        if (idx < 0) {
            continue;
        }
        m_checkouts[idx].containers.append(ci);
        m_checkouts[idx].project = ci.project;
        if (!isUp(ci) && counts(ci)) {
            liveIssues.insert(ci.id);
            if (!m_dockerIssueSince.contains(ci.id)) {
                m_dockerIssueSince.insert(ci.id, now);
            }
        }
    }
    for (auto it = m_dockerIssueSince.begin(); it != m_dockerIssueSince.end();) {
        it = liveIssues.contains(it.key()) ? std::next(it) : m_dockerIssueSince.erase(it);
    }
    for (int i = 0; i < m_checkouts.size(); ++i) {
        rebuildServices(i);
    }
    QSet<int> published;
    for (const Checkout &c : std::as_const(m_checkouts)) {
        for (const ContainerInfo &ci : c.containers) {
            for (const PortMapping &p : ci.ports) {
                if (ci.state == QLatin1String("running") && p.publicPort > 0) {
                    published.insert(p.publicPort);
                }
            }
        }
    }
    m_dockerPorts = published;
    probePorts();
    publishCheckouts();
    publishServices();
    publishAttention();
    Q_EMIT currentChanged();
    Q_EMIT totalsChanged();
}

void AppController::onStats(const QHash<QString, ContainerStats> &stats)
{
    for (auto it = stats.cbegin(); it != stats.cend(); ++it) {
        m_stats.insert(it.key(), it.value());
    }
    rebuildServices(m_current);
    publishServices();
}

void AppController::rebuildServices(int index)
{
    Checkout &c = m_checkouts[index];
    QMap<QString, ServiceRow> rows;
    for (const ContainerInfo &ci : std::as_const(c.containers)) {
        ServiceRow &r = rows[ci.service];
        r.service = ci.service;
        r.ids.append(ci.id);
        r.names.append(ci.name);
        if (r.health.isEmpty() || healthRank(ci.health) > healthRank(r.health)) {
            r.health = ci.health;
        }
        if (r.uptime.isEmpty() || ci.upSeconds >= 0) {
            r.uptime = shortDuration(ci.upSeconds);
        }
        QStringList ports = r.ports.isEmpty() ? QStringList{} : r.ports.split(QStringLiteral(", "));
        for (const PortMapping &p : ci.ports) {
            if (!ports.contains(QString::number(p.publicPort))) {
                ports.append(QString::number(p.publicPort));
            }
        }
        std::sort(ports.begin(), ports.end(), [](const QString &a, const QString &b) { return a.toInt() < b.toInt(); });
        r.ports = ports.join(QStringLiteral(", "));
        if (m_stats.contains(ci.id) && ci.state == QLatin1String("running")) {
            const ContainerStats s = m_stats.value(ci.id);
            r.cpu = (r.cpu < 0 ? 0 : r.cpu) + s.cpuPercent;
            r.mem = (r.mem < 0 ? 0 : r.mem) + s.memBytes;
        }
    }
    c.services = rows.values();
    // Services with published ports first; workers and internal helpers after.
    std::stable_sort(c.services.begin(), c.services.end(), [](const ServiceRow &a, const ServiceRow &b) {
        if (a.ports.isEmpty() != b.ports.isEmpty()) {
            return !a.ports.isEmpty();
        }
        return a.service < b.service;
    });
}

void AppController::onGit(const QString &key, const GitInfo &info)
{
    for (int i = 0; i < m_checkouts.size(); ++i) {
        Checkout &c = m_checkouts[i];
        if (c.cfg.name != key) {
            continue;
        }
        c.git = info;
        // "sam/abc-123-new-portal-config-route" → ABC-123, "New portal config route"
        static const QRegularExpression re(QStringLiteral(R"((?:^|/)([A-Za-z]{2,10}-\d+)(?:[-_](.*))?$)"));
        const auto m = re.match(info.branch);
        if (m.hasMatch()) {
            c.ticket = m.captured(1).toUpper();
            QString t = m.captured(2);
            t.replace(QLatin1Char('-'), QLatin1Char(' ')).replace(QLatin1Char('_'), QLatin1Char(' '));
            if (!t.isEmpty()) {
                t[0] = t[0].toUpper();
            }
            c.title = t;
        } else {
            c.ticket.clear();
            c.title = info.branch;
        }
        m_prs->setBranch(key, info.branch);
    }
    publishCheckouts();
    Q_EMIT currentChanged();
}

void AppController::onPaneCommands(const QHash<QString, QStringList> &commands)
{
    for (int i = 0; i < m_checkouts.size(); ++i) {
        Checkout &c = m_checkouts[i];
        const QStringList cmds = commands.value(c.session);
        // Someone may start a different agent by hand in the session; follow it.
        if (!runs(profile(i), cmds)) {
            if (const QString id = profileRunning(m_config.agents, cmds); !id.isEmpty()) {
                useProfile(i, id);
            }
        }
        const bool running = runs(profile(i), cmds);
        const bool wasRunning = c.agent.running;
        c.agent.running = running;
        if (running && !wasRunning) {
            const QDateTime now = QDateTime::currentDateTime();
            c.agent.runningSince = now;
            // An agent found running at app start has had working hooks all
            // along. A fresh one usually sends SessionStart before this poll sees it.
            c.agent.hooksSeen = !m_panesPolled || (c.agent.lastHookAt.isValid() && c.agent.lastHookAt.secsTo(now) < 10);
        }
        // Only transitions in our own session change state: hooks also report
        // agents running elsewhere (another terminal), which must stay visible.
        if (wasRunning && !running) {
            setAgentState(i, QStringLiteral("offline"));
        } else if (running && c.agent.state == QLatin1String("offline")) {
            setAgentState(i, QStringLiteral("ready"));
        }
        if (wasRunning != running) {
            publishCheckouts();
            if (i == m_current) {
                Q_EMIT currentChanged();
            }
        }
    }
    m_panesPolled = true;
}

// Claude Code and Codex send the same hook format. Codex adds an Interrupt
// event, names its file edits apply_patch and has no Notification event.
void AppController::onHookEvent(const QJsonObject &p)
{
    const int idx = checkoutForPath(Paths::normalize(p.value(QStringLiteral("cwd")).toString()));
    if (idx < 0) {
        return;
    }
    Checkout &c = m_checkouts[idx];
    AgentInfo &a = c.agent;
    const QString ev = p.value(QStringLiteral("hook_event_name")).toString();
    const QString sid = p.value(QStringLiteral("session_id")).toString();
    const QDateTime now = QDateTime::currentDateTime();

    if (!sid.isEmpty() && sid != a.sessionId && ev != QLatin1String("SessionEnd")) {
        a.sessionId = sid;
        a.startedAt = now;
        a.tools = 0;
        a.files.clear();
        a.lastTool.clear();
    }
    a.hooksSeen = true;
    a.lastHookAt = now;
    if (const QString model = p.value(QStringLiteral("model")).toString(); !model.isEmpty()) {
        a.model = model;
    }
    const QString tool = p.value(QStringLiteral("tool_name")).toString();
    const QJsonObject input = p.value(QStringLiteral("tool_input")).toObject();
    const bool asks = ev == QLatin1String("PreToolUse") || ev == QLatin1String("PermissionRequest");

    if (ev == QLatin1String("SessionStart")) {
        setAgentState(idx, QStringLiteral("ready"));
    } else if (ev == QLatin1String("UserPromptSubmit")) {
        a.lastMessage.clear();
        setAgentState(idx, QStringLiteral("working"));
    } else if (asks && isQuestionTool(tool)) {
        a.questionTitle = QStringLiteral("Has a question for you");
        a.lastMessage = questionText(input);
        setAgentState(idx, QStringLiteral("question"));
    } else if (asks && tool == QLatin1String("ExitPlanMode")) {
        // The plan prompt's options are not allow/always/deny; read it in the terminal.
        a.questionTitle = QStringLiteral("Has a plan for you to review");
        a.lastMessage = describeInput(tool, input, c.cfg.path);
        setAgentState(idx, QStringLiteral("question"));
    } else if (ev == QLatin1String("PreToolUse")) {
        // A later PreToolUse means any earlier permission prompt was answered.
        a.lastTool = tool + QStringLiteral("(") + describeInput(tool, input, c.cfg.path) + QStringLiteral(")");
        setAgentState(idx, QStringLiteral("working"));
    } else if (ev == QLatin1String("PermissionRequest")) {
        a.permTool = tool;
        a.permDetail = describeInput(tool, input, c.cfg.path);
        // Codex offers "approve for this session" on every prompt; Claude Code
        // offers "always" only when it has a rule to suggest.
        a.permCanAlways
            = profile(idx).kind == QLatin1String("codex") || !p.value(QStringLiteral("permission_suggestions")).toArray().isEmpty();
        setAgentState(idx, QStringLiteral("permission"));
    } else if (ev == QLatin1String("PostToolUse") || ev == QLatin1String("PostToolUseFailure")) {
        ++a.tools;
        if (input.contains(QStringLiteral("file_path"))
            && (tool == QLatin1String("Edit") || tool == QLatin1String("Write") || tool == QLatin1String("MultiEdit")
                || tool == QLatin1String("NotebookEdit"))) {
            a.files.insert(input.value(QStringLiteral("file_path")).toString());
        }
        if (tool == QLatin1String("apply_patch")) {
            for (const QString &f : patchFiles(input)) {
                a.files.insert(f);
            }
        }
        if (tool == QLatin1String("Bash") && touchesPr(commandText(input.value(QStringLiteral("command"))))) {
            m_prs->refresh(c.cfg.name, 2000);
        }
        setAgentState(idx, QStringLiteral("working"));
    } else if (ev == QLatin1String("Notification")) {
        const QString type = p.value(QStringLiteral("notification_type")).toString();
        const QString message = p.value(QStringLiteral("message")).toString();
        const bool waiting = a.state == QLatin1String("permission") || a.state == QLatin1String("question");
        if (type == QLatin1String("permission_prompt")) {
            if (!waiting) {
                a.permTool.clear();
                a.permDetail = message;
                a.permCanAlways = false;
                setAgentState(idx, QStringLiteral("permission"));
            }
        } else if (type == QLatin1String("elicitation_dialog")) {
            a.questionTitle = QStringLiteral("Has a question for you");
            a.lastMessage = message;
            setAgentState(idx, QStringLiteral("question"));
        } else if (type == QLatin1String("idle_prompt")) {
            if (!waiting) {
                setAgentState(idx, QStringLiteral("done"));
            }
        }
    } else if (ev == QLatin1String("Stop")) {
        a.lastMessage = p.value(QStringLiteral("last_assistant_message")).toString();
        setAgentState(idx, QStringLiteral("done"));
        m_prs->refresh(c.cfg.name, 1000);
    } else if (ev == QLatin1String("Interrupt") || ev == QLatin1String("SessionEnd")) {
        setAgentState(idx, QStringLiteral("ready"));
    }

    publishCheckouts();
    if (idx == m_current) {
        Q_EMIT currentChanged();
    }
}

void AppController::setAgentState(int index, const QString &state)
{
    AgentInfo &a = m_checkouts[index].agent;
    if (a.state == state) {
        return;
    }
    a.state = state;
    a.since = QDateTime::currentDateTime();
    if (state != QLatin1String("permission")) {
        a.permTool.clear();
        a.permDetail.clear();
    }
    if (state == QLatin1String("offline")) {
        a.model.clear();
    }
    publishCheckouts();
    publishAttention();
    if (index == m_current) {
        Q_EMIT currentChanged();
    }
    Q_EMIT agentStateChanged(index);
}

void AppController::useProfile(int index, const QString &id)
{
    AgentInfo &a = m_checkouts[index].agent;
    const QString resolved = m_config.agent(id).id;
    if (a.profile == resolved) {
        return;
    }
    a.profile = resolved;
    QSettings().setValue(QStringLiteral("agents/") + m_checkouts.at(index).cfg.name, resolved);
    publishCheckouts();
    publishAttention();
    if (index == m_current) {
        Q_EMIT currentChanged();
    }
}

void AppController::onPr(const QString &key, const PrInfo &info)
{
    for (int i = 0; i < m_checkouts.size(); ++i) {
        if (m_checkouts[i].cfg.name != key) {
            continue;
        }
        m_checkouts[i].pr = info;
        publishAttention();
        if (i == m_current) {
            Q_EMIT currentChanged();
        }
    }
}

// ───────────────────────── outputs ─────────────────────────

void AppController::publishCheckouts()
{
    QList<QVariantMap> rows;
    for (int i = 0; i < m_checkouts.size(); ++i) {
        const Checkout &c = m_checkouts.at(i);
        int up = 0;
        int worst = 0;
        int total = 0;
        for (const ContainerInfo &ci : c.containers) {
            up += isUp(ci) ? 1 : 0;
            total += counts(ci) ? 1 : 0;
            worst = std::max(worst, healthRank(ci.health));
        }
        rows.append({
            {QStringLiteral("id"), c.cfg.name},
            {QStringLiteral("name"), c.cfg.name},
            {QStringLiteral("ticket"), c.ticket.isEmpty() ? c.git.branch : c.ticket},
            {QStringLiteral("branch"), c.git.branch},
            {QStringLiteral("agentState"), c.agent.state},
            {QStringLiteral("agentLabel"), profile(i).label},
            {QStringLiteral("healthText"), total ? QStringLiteral("%1/%2").arg(up).arg(total) : QStringLiteral("down")},
            {QStringLiteral("healthState"),
             total == 0 ? QStringLiteral("down")
                        : (worst >= 3 ? QStringLiteral("bad") : (worst > 0 ? QStringLiteral("warn") : QStringLiteral("ok")))},
            {QStringLiteral("selected"), i == m_current},
        });
    }
    m_checkoutModel->setRows(rows);
}

void AppController::publishServices()
{
    QList<QVariantMap> rows;
    if (m_current < m_checkouts.size()) {
        for (const ServiceRow &r : m_checkouts.at(m_current).services) {
            rows.append({
                {QStringLiteral("id"), r.service},
                {QStringLiteral("name"), r.ids.size() > 1 ? QStringLiteral("%1 ×%2").arg(r.service).arg(r.ids.size()) : r.service},
                {QStringLiteral("health"), r.health},
                {QStringLiteral("ports"), r.ports.isEmpty() ? QStringLiteral("—") : r.ports},
                {QStringLiteral("uptime"), r.uptime},
                {QStringLiteral("cpu"), r.cpu < 0 ? QString() : QStringLiteral("%1%").arg(qRound(r.cpu))},
                {QStringLiteral("mem"), formatMem(r.mem)},
            });
        }
    }
    m_serviceModel->setRows(rows);
}

void AppController::publishAttention()
{
    struct Item
    {
        int priority;
        QVariantMap row;
        QDateTime since;
    };
    QList<Item> items;
    const QDateTime now = QDateTime::currentDateTime();

    for (int i = 0; i < m_checkouts.size(); ++i) {
        const Checkout &c = m_checkouts.at(i);
        const AgentInfo &a = c.agent;
        const AgentProfile &agent = profile(i);
        auto make = [&](const QString &id, const QString &kind, const QString &title, const QString &detail, const QDateTime &since,
                        bool dismissable) {
            const QDateTime s = since.isValid() ? since : now;
            const QString key = id + QLatin1Char('|') + QString::number(s.toMSecsSinceEpoch());
            return QVariantMap{{QStringLiteral("id"), id},
                               {QStringLiteral("itemId"), id},
                               {QStringLiteral("key"), key},
                               {QStringLiteral("kind"), kind},
                               {QStringLiteral("checkout"), i},
                               {QStringLiteral("checkoutName"), c.cfg.name},
                               {QStringLiteral("title"), title},
                               {QStringLiteral("detail"), detail},
                               {QStringLiteral("since"), s.toMSecsSinceEpoch()},
                               {QStringLiteral("canAlways"), a.permCanAlways},
                               {QStringLiteral("dismissable"), dismissable},
                               {QStringLiteral("prUrl"), viewableUrl(c.pr)},
                               {QStringLiteral("prPrompt"), agent.prPrompt},
                               {QStringLiteral("agentLabel"), agent.label}};
        };
        const QString agentId = QStringLiteral("agent:") + c.cfg.name;
        if (a.state == QLatin1String("permission")) {
            items.append({0,
                          make(agentId, QStringLiteral("permission"), permissionTitle(a.permTool),
                               a.permTool.isEmpty() ? a.permDetail : a.permTool + QStringLiteral("(") + a.permDetail + QStringLiteral(")"),
                               a.since, false),
                          a.since});
        } else if (a.state == QLatin1String("question")) {
            items.append({1, make(agentId, QStringLiteral("question"), a.questionTitle, a.lastMessage, a.since, false), a.since});
        } else if (a.state == QLatin1String("done")) {
            QString msg = a.lastMessage.simplified();
            if (msg.size() > 280) {
                msg = msg.left(280) + QStringLiteral("…");
            }
            items.append(
                {3, make(agentId, QStringLiteral("done"), QStringLiteral("Finished — waiting for you"), msg, a.since, true), a.since});
        }

        for (const ContainerInfo &ci : c.containers) {
            QString title;
            if (ci.health == QLatin1String("unhealthy")) {
                title = QStringLiteral("%1 is unhealthy").arg(ci.service);
            } else if (ci.health == QLatin1String("restarting")) {
                title = QStringLiteral("%1 keeps restarting").arg(ci.service);
            } else if (ci.health == QLatin1String("exited") && ci.exitCode != 0) {
                title = QStringLiteral("%1 exited with code %2").arg(ci.service).arg(ci.exitCode);
            } else if (ci.health == QLatin1String("starting") && ci.upSeconds > 90) {
                title = QStringLiteral("%1 stuck in health: starting").arg(ci.service);
            }
            if (!title.isEmpty()) {
                const QDateTime since = m_dockerIssueSince.value(ci.id, now);
                items.append({2, make(QStringLiteral("docker:") + ci.id, QStringLiteral("docker"), title, ci.name, since, true), since});
            }
        }
    }

    items.erase(std::remove_if(items.begin(), items.end(),
                               [this](const Item &it) { return m_dismissed.contains(it.row.value(QStringLiteral("key")).toString()); }),
                items.end());
    std::stable_sort(items.begin(), items.end(),
                     [](const Item &x, const Item &y) { return x.priority != y.priority ? x.priority < y.priority : x.since < y.since; });

    QList<QVariantMap> rows;
    for (const Item &it : std::as_const(items)) {
        rows.append(it.row);
        const QString key = it.row.value(QStringLiteral("key")).toString();
        if (!m_raised.contains(key)) {
            m_raised.insert(key);
            Q_EMIT attentionRaised(it.row);
        }
    }
    m_attention->setRows(rows);
}

// ───────────────────────── actions ─────────────────────────

void AppController::select(int index)
{
    if (index < 0 || index >= m_checkouts.size() || index == m_current) {
        return;
    }
    m_current = index;
    publishCheckouts();
    publishServices();
    Q_EMIT currentChanged();
    sampleStats();
}

void AppController::setTab(const QString &tab)
{
    if (tab != m_tab) {
        m_tab = tab;
        Q_EMIT tabChanged();
    }
}

void AppController::toggleRail()
{
    m_railVisible = !m_railVisible;
    Q_EMIT railVisibleChanged();
}

void AppController::runQuick(const QString &key)
{
    if (m_current >= m_checkouts.size()) {
        return;
    }
    for (const QuickEntry &e : m_quick->entries(m_current, manualQuickCommands())) {
        if (e.key == key) {
            Q_EMIT runInTerminal(m_current, e.label, expand(m_current, e.command));
            return;
        }
    }
}

void AppController::pinQuick(const QString &key, bool on)
{
    m_quick->setPinned(m_current, key, on);
}

void AppController::hideQuick(const QString &key, bool on)
{
    m_quick->setHidden(m_current, key, on);
}

void AppController::runCompose(const QString &which)
{
    if (m_current >= m_checkouts.size()) {
        return;
    }
    const QString cmd = m_checkouts.at(m_current).cfg.composeCommand(which);
    Q_EMIT runInTerminal(m_current, QStringLiteral("compose %1").arg(which), expand(m_current, cmd));
}

void AppController::openLink(int index)
{
    const QVariantMap l = links().value(index).toMap();
    const int port = l.value(QStringLiteral("port")).toInt();
    if (port > 0) {
        QDesktopServices::openUrl(QUrl(QStringLiteral("http://localhost:%1").arg(port)));
    }
}

void AppController::serviceAction(int row, const QString &action)
{
    if (m_current >= m_checkouts.size()) {
        return;
    }
    const Checkout &c = m_checkouts.at(m_current);
    if (row < 0 || row >= c.services.size()) {
        return;
    }
    const ServiceRow &r = c.services.at(row);
    if (action == QLatin1String("logs")) {
        Q_EMIT runInTerminal(m_current, QStringLiteral("logs · %1").arg(r.service),
                             QStringLiteral("docker logs -f --tail 300 %1").arg(r.names.first()));
        return;
    }
    for (const QString &id : r.ids) {
        if (action == QLatin1String("restart")) {
            m_docker->restartContainer(id);
        } else if (action == QLatin1String("stop")) {
            m_docker->stopContainer(id);
        } else if (action == QLatin1String("start")) {
            m_docker->startContainer(id);
        }
    }
}

void AppController::attentionAction(const QString &id, const QString &action)
{
    QVariantMap row;
    for (int i = 0; i < m_attention->rowCount(); ++i) {
        if (m_attention->row(i).value(QStringLiteral("id")) == id) {
            row = m_attention->row(i);
        }
    }
    if (row.isEmpty()) {
        return;
    }
    const int idx = row.value(QStringLiteral("checkout")).toInt();
    const QString kind = row.value(QStringLiteral("kind")).toString();

    if (action == QLatin1String("dismiss")) {
        m_dismissed.insert(row.value(QStringLiteral("key")).toString());
        publishAttention();
        return;
    }
    if (action == QLatin1String("open")) {
        select(idx);
        if (kind == QLatin1String("docker")) {
            setTab(QStringLiteral("docker"));
        } else {
            setTab(QStringLiteral("agents"));
            Q_EMIT focusAgentTerminal(idx);
        }
        return;
    }

    if (kind == QLatin1String("permission")) {
        const QString &session = m_checkouts.at(idx).session;
        const AgentProfile &p = profile(idx);
        const QString key = action == QLatin1String("allow") ? p.allowKey : action == QLatin1String("always") ? p.alwaysKey : p.denyKey;
        m_sessions->sendKeys(session, {key});
        // Clear the card now; hook events confirm the new state moments later.
        setAgentState(idx, action == QLatin1String("deny") ? QStringLiteral("ready") : QStringLiteral("working"));
    } else if (kind == QLatin1String("docker")) {
        const QString containerId = id.mid(7);
        if (action == QLatin1String("restart")) {
            m_docker->restartContainer(containerId);
        } else if (action == QLatin1String("logs")) {
            for (const ContainerInfo &ci : m_checkouts.at(idx).containers) {
                if (ci.id == containerId) {
                    select(idx);
                    setTab(QStringLiteral("docker"));
                    Q_EMIT runInTerminal(idx, QStringLiteral("logs · %1").arg(ci.service),
                                         QStringLiteral("docker logs -f --tail 300 %1").arg(ci.name));
                }
            }
        }
    }
}

bool AppController::ensureAgentSession(int index)
{
    const Checkout &c = m_checkouts.at(index);
    if (m_sessions->hasSession(c.session)) {
        return true;
    }
    // A renamed checkout takes over the session it had under its old name.
    for (const QString &old : m_sessions->sessionsIn(c.cfg.path)) {
        const bool ours = std::any_of(m_checkouts.cbegin(), m_checkouts.cend(), [&old](const Checkout &o) { return o.session == old; });
        if (!ours && m_sessions->renameSession(old, c.session)) {
            return true;
        }
    }
    useProfile(index, c.cfg.agent);
    const AgentProfile &p = profile(index);
    // An agent stopped by a backend switch ("Restart agents now") continues
    // its conversation in the new backend.
    QSettings settings;
    QStringList resume = settings.value(kResumeKey).toStringList();
    const bool resuming = resume.removeAll(c.cfg.name) > 0;
    if (resuming) {
        if (resume.isEmpty()) {
            settings.remove(kResumeKey);
        } else {
            settings.setValue(kResumeKey, resume);
        }
    }
    QString start;
    if (resuming) {
        start = p.launchLine(!p.continueArgs.isEmpty(), c.cfg.args);
    } else if (m_config.autostartAgents) {
        start = p.launchLine(false, c.cfg.args);
    }
    return m_sessions->createSession(c.session, c.cfg.path, start, p.sessionEnv(), m_terminalSettings->current().shell);
}

bool AppController::reloadConfig()
{
    QString error;
    Config next = Config::load(&error);
    m_configError = error;
    // Agents run in the backend picked at startup, and their terminals live as
    // long as the app, so switching either takes a restart.
    bool sameCheckouts = next.checkouts.size() == m_checkouts.size() && next.sessionBackend == m_config.sessionBackend;
    for (int i = 0; sameCheckouts && i < m_checkouts.size(); ++i) {
        sameCheckouts = next.checkouts.at(i).name == m_checkouts.at(i).cfg.name && next.checkouts.at(i).path == m_checkouts.at(i).cfg.path;
    }
    const Config prev = std::exchange(m_config, next);
    setWindowsShell(m_config.windowsShell);
    m_terminalSettings->select(m_config.terminalSettings, m_config.terminalProfile);
    m_deps->setConfig(m_config);
    m_docker->setSocketPath(resolveDockerEndpoint(m_config.dockerHost).socketPath);
    for (int i = 0; i < m_checkouts.size(); ++i) {
        Checkout &c = m_checkouts[i];
        const QString configuredBefore = prev.agent(c.cfg.agent).id;
        for (const CheckoutConfig &cc : std::as_const(m_config.checkouts)) {
            if (cc.name == c.cfg.name && cc.path == c.cfg.path) {
                c.cfg = cc;
                if (!cc.composeProject.isEmpty()) {
                    c.project = cc.composeProject;
                }
            }
        }
        // A changed agent applies now unless one is running. Re-resolving the
        // current pick also covers a profile that was removed.
        const QString configured = m_config.agent(c.cfg.agent).id;
        useProfile(i, !c.agent.running && configured != configuredBefore ? configured : QString(c.agent.profile));
    }
    // A changed compose project name changes which containers are whose.
    onContainers(QList<ContainerInfo>(m_containers));
    publishCheckouts();
    publishAttention();
    Q_EMIT configChanged();
    Q_EMIT currentChanged();
    return sameCheckouts;
}

void AppController::sendToAgent(const QString &text)
{
    if (m_current >= m_checkouts.size()) {
        return;
    }
    m_sessions->submit(m_checkouts.at(m_current).session, text, profile(m_current).submitDelayMs);
    Q_EMIT focusAgentTerminal(m_current);
}

void AppController::agentAction(const QString &action)
{
    if (m_current >= m_checkouts.size()) {
        return;
    }
    const Checkout &c = m_checkouts.at(m_current);
    if (action == QLatin1String("interrupt")) {
        m_sessions->sendKeys(c.session, {QStringLiteral("Escape")});
        // Claude Code sends no hook event for an interrupt.
        if (c.agent.state == QLatin1String("working")) {
            setAgentState(m_current, QStringLiteral("ready"));
        }
    } else if (action == QLatin1String("start") || action == QLatin1String("continue")) {
        if (agentMissing(m_current)) {
            return; // the session panel says what's missing
        }
        const AgentProfile &p = profile(m_current);
        const SessionBackend::Environment env = p.sessionEnv();
        const QStringList shell = m_terminalSettings->current().shell;
        // A session's environment and shell are fixed when it starts, so
        // another profile's env or a newly imported shell takes a fresh session.
        // The attached terminal reattaches by itself. A session that recorded
        // no shell predates the import and is kept.
        if (!c.agent.running && m_sessions->hasSession(c.session)) {
            const QStringList had = m_sessions->sessionShell(c.session);
            if (m_sessions->sessionEnvironment(c.session) != env || (!had.isEmpty() && had != shell)) {
                m_sessions->killSession(c.session);
            }
        }
        // No autostart here: the line typed below starts the agent.
        m_sessions->ensureSession(c.session, c.cfg.path, QString(), env, shell);
        m_sessions->submit(c.session, p.launchLine(action == QLatin1String("continue"), c.cfg.args));
    }
    Q_EMIT focusAgentTerminal(m_current);
}

void AppController::setAgentProfile(const QString &id)
{
    if (m_current < m_checkouts.size() && !m_checkouts.at(m_current).agent.running) {
        useProfile(m_current, id);
    }
}

void AppController::openSettings(const QString &page)
{
    Q_EMIT settingsRequested(page);
}

void AppController::openPr(int index)
{
    if (index < 0) {
        index = m_current;
    }
    if (index < m_checkouts.size() && !m_checkouts[index].pr.url.isEmpty()) {
        QDesktopServices::openUrl(QUrl(m_checkouts[index].pr.url));
    }
}

void AppController::chooseWelcomeFolder()
{
    Q_EMIT welcomeFolderRequested();
}

void AppController::setWelcomeFolder(const QString &dir)
{
    if (dir.isEmpty()) {
        return;
    }
    m_welcome = Worktrees::candidates(Worktrees::discover(dir));
    Q_EMIT welcomeChanged();
}

void AppController::toggleWelcomeCandidate(int index)
{
    if (index >= 0 && index < m_welcome.size()) {
        m_welcome[index].checked = !m_welcome[index].checked;
        Q_EMIT welcomeChanged();
    }
}

void AppController::cancelWelcome()
{
    m_welcome.clear();
    Q_EMIT welcomeChanged();
}

void AppController::finishWelcome()
{
    if (std::none_of(m_welcome.cbegin(), m_welcome.cend(), [](const Worktrees::Candidate &c) { return c.checked; })) {
        return;
    }
    QString error;
    const QJsonObject root = Config::readJson(&error);
    if (error.isEmpty() && Config::writeJson(Worktrees::withCheckouts(root, m_welcome), &error)) {
        Q_EMIT welcomeFinished();
    } else {
        Q_EMIT welcomeFailed(error);
    }
}

QVariantList AppController::welcomeCandidates() const
{
    QVariantList out;
    for (const Worktrees::Candidate &c : m_welcome) {
        out.append(QVariantMap{{QStringLiteral("name"), c.name},
                               {QStringLiteral("path"), Paths::tilde(c.path)},
                               {QStringLiteral("branch"), c.branch},
                               {QStringLiteral("checked"), c.checked}});
    }
    return out;
}

void AppController::refreshPr()
{
    m_prs->recheck();
    if (m_current < m_checkouts.size()) {
        m_prs->refresh(m_checkouts[m_current].cfg.name);
    }
}
