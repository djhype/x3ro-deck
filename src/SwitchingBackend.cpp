#include "SwitchingBackend.h"

SwitchingBackend::SwitchingBackend(SessionBackend *primary, SessionBackend *legacy, QObject *parent)
    : SessionBackend(parent)
    , m_primary(primary)
    , m_legacy(legacy)
{
    m_primary->setParent(this);
    m_legacy->setParent(this);
    connect(m_primary, &SessionBackend::foregroundProcesses, this, [this](const QHash<QString, QStringList> &panes) {
        m_primaryPanes = panes;
        merge();
    });
    connect(m_legacy, &SessionBackend::foregroundProcesses, this, [this](const QHash<QString, QStringList> &panes) {
        m_legacyPanes = panes;
        merge();
        // Two empty polls in a row, so one failed poll does not end the switch.
        m_emptyPolls = panes.isEmpty() ? m_emptyPolls + 1 : 0;
        if (m_emptyPolls >= 2 && !m_drained) {
            m_drained = true;
            Q_EMIT drained();
        }
    });
}

SessionBackend *SwitchingBackend::owner(const QString &name) const
{
    return !m_drained && m_legacy->hasSession(name) ? m_legacy : m_primary;
}

void SwitchingBackend::merge()
{
    QHash<QString, QStringList> all = m_primaryPanes;
    for (auto it = m_legacyPanes.cbegin(); it != m_legacyPanes.cend(); ++it) {
        all.insert(it.key(), it.value());
    }
    Q_EMIT foregroundProcesses(all);
}

bool SwitchingBackend::hasSession(const QString &name) const
{
    return owner(name)->hasSession(name);
}

bool SwitchingBackend::createSession(const QString &name, const QString &dir, const QString &startCommand, const Environment &env,
                                     const QStringList &shell)
{
    return m_primary->createSession(name, dir, startCommand, env, shell);
}

SessionBackend::Environment SwitchingBackend::sessionEnvironment(const QString &name) const
{
    return owner(name)->sessionEnvironment(name);
}

QStringList SwitchingBackend::sessionShell(const QString &name) const
{
    return owner(name)->sessionShell(name);
}

QStringList SwitchingBackend::sessionsIn(const QString &dir) const
{
    QStringList out = m_primary->sessionsIn(dir);
    if (!m_drained) {
        for (const QString &s : m_legacy->sessionsIn(dir)) {
            if (!out.contains(s)) {
                out.append(s);
            }
        }
    }
    return out;
}

bool SwitchingBackend::renameSession(const QString &from, const QString &to)
{
    return owner(from)->renameSession(from, to);
}

void SwitchingBackend::killSession(const QString &name)
{
    owner(name)->killSession(name);
}

void SwitchingBackend::killAll()
{
    m_legacy->killAll();
    m_primary->killAll();
}

QStringList SwitchingBackend::attachCommand(const QString &name) const
{
    return owner(name)->attachCommand(name);
}

QString SwitchingBackend::attachHint(const QString &name) const
{
    return owner(name)->attachHint(name);
}

TerminalChannel *SwitchingBackend::attachChannel(const QString &name, int rows, int cols, QObject *parent) const
{
    return owner(name)->attachChannel(name, rows, cols, parent);
}

void SwitchingBackend::sendKeys(const QString &name, const QStringList &keys, bool literal)
{
    owner(name)->sendKeys(name, keys, literal);
}

void SwitchingBackend::submit(const QString &name, const QString &text, int enterDelayMs)
{
    owner(name)->submit(name, text, enterDelayMs);
}

void SwitchingBackend::startPolling(int intervalMs)
{
    m_primary->startPolling(intervalMs);
    m_legacy->startPolling(intervalMs);
}
