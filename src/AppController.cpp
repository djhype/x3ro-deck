#include "AppController.h"

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
const QStringList kCheckoutRoles{QStringLiteral("id"),         QStringLiteral("name"),       QStringLiteral("offsetLabel"),
                                 QStringLiteral("ticket"),     QStringLiteral("branch"),     QStringLiteral("agentState"),
                                 QStringLiteral("agentLabel"), QStringLiteral("healthText"), QStringLiteral("healthState"),
                                 QStringLiteral("selected")};
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

QString canonical(const QString &path)
{
    const QString c = QFileInfo(path).canonicalFilePath();
    return c.isEmpty() ? path : c;
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

bool runs(const AgentProfile &p, const QString &paneCommand)
{
    return !p.process.isEmpty() && paneCommand.startsWith(p.process);
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

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_config(Config::load(&m_configError))
    , m_docker(new DockerClient(this))
    , m_bus(new AgentBus(this))
    , m_sessions(new TmuxManager(this))
    , m_git(new GitWatcher(this))
    , m_prs(new PrWatcher(this))
    , m_checkoutModel(new VariantListModel(kCheckoutRoles, this))
    , m_serviceModel(new VariantListModel(kServiceRoles, this))
    , m_attention(new VariantListModel(kAttentionRoles, this))
{
    // A session that survived a restart runs the profile last picked for it;
    // ensureAgentSession() switches a fresh one to the configured agent.
    QSettings settings;
    for (const CheckoutConfig &cc : std::as_const(m_config.checkouts)) {
        Checkout c;
        c.cfg = cc;
        c.session = sessionName(cc.name);
        c.project = cc.name;
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
    QString err;
    if (!m_bus->listen(&err)) {
        qWarning("x3ro-deck: cannot listen on %s: %s", qPrintable(AgentBus::socketPath()), qPrintable(err));
    }
    for (int i = 0; i < m_checkouts.size(); ++i) {
        m_prs->watch(m_checkouts[i].cfg.name, m_checkouts[i].cfg.path);
        m_git->watch(m_checkouts[i].cfg.name, m_checkouts[i].cfg.path);
    }
    m_docker->start();
    m_sessions->startPolling(3000);
    m_statsTimer.start();
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
        {QStringLiteral("path"), QString(c.cfg.path).replace(QDir::homePath(), QStringLiteral("~"))},
        {QStringLiteral("branch"), c.git.branch.isEmpty() ? QStringLiteral("(detached)") : c.git.branch},
        {QStringLiteral("ticket"), c.ticket},
        {QStringLiteral("title"), c.title},
        {QStringLiteral("commit"), c.git.commit},
        {QStringLiteral("project"), c.project},
        {QStringLiteral("session"), c.session},
        {QStringLiteral("attachHint"), m_sessions->attachHint(c.session)},
        {QStringLiteral("offset"), c.cfg.portOffset},
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
    };
}

QVariantList AppController::quickCommands() const
{
    QVariantList out;
    for (const QuickCommand &q : m_config.quickCommands) {
        out.append(
            QVariantMap{{QStringLiteral("label"), q.label}, {QStringLiteral("command"), q.command}, {QStringLiteral("group"), q.group}});
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

QVariantList AppController::links() const
{
    QVariantList out;
    for (const LinkConfig &l : m_config.links) {
        out.append(
            QVariantMap{{QStringLiteral("label"), l.label}, {QStringLiteral("port"), publicPort(m_current, l.service, l.containerPort)}});
    }
    return out;
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
    out.replace(QStringLiteral("{project}"), m_checkouts.at(index).project);
    static const QRegularExpression portRe(QStringLiteral(R"(\{port:([^:}]+):(\d+)\})"));
    QRegularExpressionMatch m;
    while ((m = portRe.match(out)).hasMatch()) {
        const int port = publicPort(index, m.captured(1), m.captured(2).toInt());
        out.replace(m.capturedStart(), m.capturedLength(), port ? QString::number(port) : m.captured(2));
    }
    return out;
}

int AppController::checkoutForPath(const QString &path) const
{
    int best = -1;
    qsizetype bestLen = -1;
    for (int i = 0; i < m_checkouts.size(); ++i) {
        const QString &root = m_checkouts.at(i).cfg.path;
        if ((path == root || path.startsWith(root + QLatin1Char('/'))) && root.size() > bestLen) {
            best = i;
            bestLen = root.size();
        }
    }
    return best;
}

// ───────────────────────── inputs ─────────────────────────

void AppController::onContainers(const QList<ContainerInfo> &containers)
{
    const QDateTime now = QDateTime::currentDateTime();
    for (Checkout &c : m_checkouts) {
        c.containers.clear();
    }
    QSet<QString> liveIssues;
    for (const ContainerInfo &ci : containers) {
        const QString dir = canonical(ci.workingDir);
        const int idx = checkoutForPath(dir);
        // Only compose projects rooted exactly at a checkout, not ones in subdirectories.
        if (idx < 0 || m_checkouts[idx].cfg.path != dir) {
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

void AppController::onPaneCommands(const QHash<QString, QString> &commands)
{
    for (int i = 0; i < m_checkouts.size(); ++i) {
        Checkout &c = m_checkouts[i];
        const QString cmd = commands.value(c.session);
        // Someone may start a different agent by hand in the session; follow it.
        if (!runs(profile(i), cmd)) {
            for (const AgentProfile &p : std::as_const(m_config.agents)) {
                if (runs(p, cmd)) {
                    useProfile(i, p.id);
                    break;
                }
            }
        }
        const bool running = runs(profile(i), cmd);
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
    const int idx = checkoutForPath(canonical(p.value(QStringLiteral("cwd")).toString()));
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
            {QStringLiteral("offsetLabel"), c.cfg.portOffset ? QStringLiteral("+%1").arg(c.cfg.portOffset) : QStringLiteral("base")},
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

void AppController::runQuick(int index)
{
    if (index < 0 || index >= m_config.quickCommands.size() || m_current >= m_checkouts.size()) {
        return;
    }
    const QuickCommand &q = m_config.quickCommands.at(index);
    Q_EMIT runInTerminal(m_current, q.label, expand(m_current, q.command));
}

void AppController::runCompose(const QString &which)
{
    if (m_current >= m_checkouts.size()) {
        return;
    }
    const QString cmd = which == QLatin1String("up") ? m_config.composeUp
        : which == QLatin1String("down")             ? m_config.composeDown
                                                     : m_config.composeRestart;
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
    return m_sessions->createSession(c.session, c.cfg.path,
                                     m_config.autostartAgents ? profile(index).launchLine(false, c.cfg.args) : QString());
}

bool AppController::reloadConfig()
{
    QString error;
    Config next = Config::load(&error);
    m_configError = error;
    bool sameCheckouts = next.checkouts.size() == m_checkouts.size();
    for (int i = 0; sameCheckouts && i < m_checkouts.size(); ++i) {
        sameCheckouts = next.checkouts.at(i).name == m_checkouts.at(i).cfg.name && next.checkouts.at(i).path == m_checkouts.at(i).cfg.path;
    }
    const Config prev = std::exchange(m_config, next);
    for (int i = 0; i < m_checkouts.size(); ++i) {
        Checkout &c = m_checkouts[i];
        const QString configuredBefore = prev.agent(c.cfg.agent).id;
        for (const CheckoutConfig &cc : std::as_const(m_config.checkouts)) {
            if (cc.name == c.cfg.name && cc.path == c.cfg.path) {
                c.cfg = cc;
            }
        }
        // A changed agent applies now unless one is running. Re-resolving the
        // current pick also covers a profile that was removed.
        const QString configured = m_config.agent(c.cfg.agent).id;
        useProfile(i, !c.agent.running && configured != configuredBefore ? configured : QString(c.agent.profile));
    }
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
        // No autostart here: the line typed below starts the agent.
        m_sessions->ensureSession(c.session, c.cfg.path, QString());
        m_sessions->submit(c.session, profile(m_current).launchLine(action == QLatin1String("continue"), c.cfg.args));
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

void AppController::refreshPr()
{
    if (m_current < m_checkouts.size()) {
        m_prs->refresh(m_checkouts[m_current].cfg.name);
    }
}
