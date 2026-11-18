// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DaemonBackend.h"

#include "Paths.h"
#include "SessionShell.h"
#include "SessiondChannel.h"
#include "SessiondProtocol.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>

namespace
{
constexpr int kReplyTimeoutMs = 3000;

QString sibling(const QString &program)
{
#if defined(Q_OS_WIN)
    const QString local = QCoreApplication::applicationDirPath() + QLatin1Char('/') + program + QStringLiteral(".exe");
#else
    const QString local = QCoreApplication::applicationDirPath() + QLatin1Char('/') + program;
#endif
    if (QFileInfo(local).isExecutable()) {
        return local;
    }
    const QString found = QStandardPaths::findExecutable(program);
    return found.isEmpty() ? program : found;
}

QJsonObject failed(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}
} // namespace

DaemonBackend::DaemonBackend(QObject *parent)
    : SessionBackend(parent)
    , m_sock(new QLocalSocket(this))
{
    connect(&m_poll, &QTimer::timeout, this, &DaemonBackend::poll);
}

DaemonBackend::~DaemonBackend() = default;

QString DaemonBackend::daemonProgram()
{
    const QString over = qEnvironmentVariable("X3RO_DECK_SESSIOND_BIN");
    return over.isEmpty() ? sibling(QStringLiteral("x3ro-deck-sessiond")) : over;
}

QString DaemonBackend::attachProgram()
{
    return sibling(QStringLiteral("x3ro-deck-attach"));
}

QByteArray DaemonBackend::keyBytes(const QString &key)
{
    static const QHash<QString, QByteArray> named{
        {QStringLiteral("Enter"), "\r"},      {QStringLiteral("KPEnter"), "\r"},       {QStringLiteral("Escape"), "\x1b"},
        {QStringLiteral("Tab"), "\t"},        {QStringLiteral("BTab"), "\x1b[Z"},      {QStringLiteral("BSpace"), "\x7f"},
        {QStringLiteral("Space"), " "},       {QStringLiteral("Up"), "\x1b[A"},        {QStringLiteral("Down"), "\x1b[B"},
        {QStringLiteral("Right"), "\x1b[C"},  {QStringLiteral("Left"), "\x1b[D"},      {QStringLiteral("Home"), "\x1b[H"},
        {QStringLiteral("End"), "\x1b[F"},    {QStringLiteral("DC"), "\x1b[3~"},       {QStringLiteral("Delete"), "\x1b[3~"},
        {QStringLiteral("IC"), "\x1b[2~"},    {QStringLiteral("PPage"), "\x1b[5~"},    {QStringLiteral("PageUp"), "\x1b[5~"},
        {QStringLiteral("NPage"), "\x1b[6~"}, {QStringLiteral("PageDown"), "\x1b[6~"},
    };
    if (const auto it = named.constFind(key); it != named.cend()) {
        return *it;
    }
    // C-x: the control character; C-Space is NUL.
    if (key.size() == 3 && key.startsWith(QLatin1String("C-"))) {
        const char c = key.at(2).toUpper().toLatin1();
        if (c >= '@' && c <= '_') {
            return QByteArray(1, char(c - '@'));
        }
        if (c == '?') {
            return "\x7f";
        }
    }
    if (key == QLatin1String("C-Space")) {
        return QByteArray(1, '\0');
    }
    // M-x: Escape, then the key.
    if (key.size() > 2 && key.startsWith(QLatin1String("M-"))) {
        return '\x1b' + keyBytes(key.mid(2));
    }
    return key.toUtf8();
}

bool DaemonBackend::startDaemon() const
{
    QProcess start;
    start.start(daemonProgram(), {QStringLiteral("--detach")});
    if (!start.waitForFinished(5000) || start.exitStatus() != QProcess::NormalExit || start.exitCode() != 0) {
        qWarning("x3ro-deck: cannot start %s: %s%s", qPrintable(daemonProgram()), qPrintable(start.errorString()),
                 start.readAllStandardError().constData());
        return false;
    }
    return true;
}

bool DaemonBackend::connectDaemon(bool start) const
{
    if (m_sock->state() == QLocalSocket::ConnectedState) {
        return true;
    }
    m_sock->abort();
    m_buffer.clear();
    const QString path = Sessiond::socketPath();
    m_sock->connectToServer(path);
    bool connected = m_sock->waitForConnected(1000);
    if (!connected && start) {
        // A daemon that is shutting down still holds its socket briefly, so
        // keep trying for a moment.
        QDeadlineTimer deadline(5000);
        while (!connected && !deadline.hasExpired()) {
            startDaemon();
            QThread::msleep(100);
            m_sock->abort();
            m_sock->connectToServer(path);
            connected = m_sock->waitForConnected(1000);
        }
    }
    if (!connected) {
        m_sock->abort();
        return false;
    }
    const QJsonObject hello
        = request({{QStringLiteral("op"), QStringLiteral("hello")}, {QStringLiteral("version"), Sessiond::kProtocolVersion}});
    if (!hello.value(QStringLiteral("ok")).toBool()) {
        m_sock->abort();
        return false;
    }
    return true;
}

QJsonObject DaemonBackend::request(QJsonObject req, bool startDaemon) const
{
    const bool isHello = req.value(QStringLiteral("op")).toString() == QLatin1String("hello");
    if (!isHello && !connectDaemon(startDaemon)) {
        return failed(QStringLiteral("the session daemon is not running"));
    }
    const int id = m_nextId++;
    req.insert(QStringLiteral("id"), id);
    m_sock->write(Sessiond::encodeControl(req));
    QDeadlineTimer deadline(kReplyTimeoutMs);
    for (;;) {
        Sessiond::Frame f;
        bool bad = false;
        while (Sessiond::takeFrame(m_buffer, f, &bad)) {
            if (f.type != Sessiond::Control) {
                continue;
            }
            const QJsonObject reply = Sessiond::parseControl(f.payload);
            if (reply.value(QStringLiteral("re")).toInt() == id) {
                return reply;
            }
        }
        if (bad || m_sock->state() != QLocalSocket::ConnectedState || deadline.hasExpired()) {
            break;
        }
        // Flushes the request as well.
        if (m_sock->bytesToWrite() > 0) {
            m_sock->waitForBytesWritten(int(deadline.remainingTime()));
        }
        if (m_sock->bytesAvailable() == 0) {
            m_sock->waitForReadyRead(int(qMax<qint64>(1, deadline.remainingTime())));
        }
        m_buffer.append(m_sock->readAll());
    }
    m_sock->abort();
    m_buffer.clear();
    return failed(QStringLiteral("no reply from the session daemon"));
}

QJsonObject DaemonBackend::find(const QString &name) const
{
    const QJsonObject reply = request({{QStringLiteral("op"), QStringLiteral("list")}});
    for (const QJsonValue &v : reply.value(QStringLiteral("sessions")).toArray()) {
        if (v.toObject().value(QStringLiteral("name")).toString() == name) {
            return v.toObject();
        }
    }
    return {};
}

bool DaemonBackend::hasSession(const QString &name) const
{
    return !find(name).isEmpty();
}

bool DaemonBackend::createSession(const QString &name, const QString &dir, const QString &startCommand, const Environment &env,
                                  const QStringList &shell)
{
    // Resolved here, against the app's PATH and $SHELL: the daemon may have
    // been started long before.
    const SessionShell sh = SessionShell::resolve(shell);
    QStringList command = sh.argv;
    command[0] = sh.program;
    QJsonObject envObject;
    for (auto it = env.cbegin(); it != env.cend(); ++it) {
        envObject.insert(it.key(), it.value());
    }
    const QJsonObject reply = request({{QStringLiteral("op"), QStringLiteral("create")},
                                       {QStringLiteral("name"), name},
                                       {QStringLiteral("cwd"), dir},
                                       {QStringLiteral("env"), envObject},
                                       {QStringLiteral("command"), QJsonArray::fromStringList(command)},
                                       {QStringLiteral("login"), sh.login},
                                       {QStringLiteral("shell"), QJsonArray::fromStringList(shell)},
                                       {QStringLiteral("cols"), 200},
                                       {QStringLiteral("rows"), 50}},
                                      true);
    if (!reply.value(QStringLiteral("ok")).toBool()) {
        qWarning("x3ro-deck-sessiond create %s failed: %s", qPrintable(name), qPrintable(reply.value(QStringLiteral("error")).toString()));
        return false;
    }
    if (!startCommand.isEmpty()) {
        // Typed into the shell, as with tmux, so the shell survives the agent.
        submit(name, startCommand);
    }
    return true;
}

SessionBackend::Environment DaemonBackend::sessionEnvironment(const QString &name) const
{
    Environment env;
    const QJsonObject tag = find(name).value(QStringLiteral("env")).toObject();
    for (auto it = tag.begin(); it != tag.end(); ++it) {
        env.insert(it.key(), it.value().toString());
    }
    return env;
}

QStringList DaemonBackend::sessionShell(const QString &name) const
{
    QStringList shell;
    for (const QJsonValue &v : find(name).value(QStringLiteral("shell")).toArray()) {
        shell.append(v.toString());
    }
    return shell;
}

QStringList DaemonBackend::sessionsIn(const QString &dir) const
{
    QStringList out;
    const QJsonObject reply = request({{QStringLiteral("op"), QStringLiteral("list")}});
    for (const QJsonValue &v : reply.value(QStringLiteral("sessions")).toArray()) {
        const QJsonObject s = v.toObject();
        if (Paths::same(s.value(QStringLiteral("cwd")).toString(), dir)) {
            out.append(s.value(QStringLiteral("name")).toString());
        }
    }
    return out;
}

bool DaemonBackend::renameSession(const QString &from, const QString &to)
{
    return request({{QStringLiteral("op"), QStringLiteral("rename")}, {QStringLiteral("name"), from}, {QStringLiteral("to"), to}})
        .value(QStringLiteral("ok"))
        .toBool();
}

void DaemonBackend::killSession(const QString &name)
{
    request({{QStringLiteral("op"), QStringLiteral("kill")}, {QStringLiteral("name"), name}});
}

void DaemonBackend::killAll()
{
    m_poll.stop();
    request({{QStringLiteral("op"), QStringLiteral("shutdown")}});
    m_sock->abort();
    m_buffer.clear();
}

QStringList DaemonBackend::attachCommand(const QString &name) const
{
    return {attachProgram(), name};
}

QString DaemonBackend::attachHint(const QString &name) const
{
    return QStringLiteral("x3ro-deck attach %1").arg(name);
}

TerminalChannel *DaemonBackend::attachChannel(const QString &name, int rows, int cols, QObject *parent) const
{
    auto *channel = new SessiondChannel(parent);
    channel->open(name, rows, cols);
    return channel;
}

QJsonObject DaemonBackend::write(const QString &name, const QByteArray &bytes) const
{
    return request({{QStringLiteral("op"), QStringLiteral("write")},
                    {QStringLiteral("name"), name},
                    {QStringLiteral("data"), QString::fromUtf8(bytes)}});
}

void DaemonBackend::sendKeys(const QString &name, const QStringList &keys, bool literal)
{
    QByteArray bytes;
    for (const QString &key : keys) {
        bytes += literal ? key.toUtf8() : keyBytes(key);
    }
    write(name, bytes);
}

void DaemonBackend::submit(const QString &name, const QString &text, int enterDelayMs)
{
    write(name, text.toUtf8());
    if (enterDelayMs > 0) {
        QTimer::singleShot(enterDelayMs, this, [this, name] { write(name, "\r"); });
    } else {
        // A separate write, as tmux's separate send-keys.
        write(name, "\r");
    }
}

void DaemonBackend::startPolling(int intervalMs)
{
    m_poll.start(intervalMs);
    poll();
}

void DaemonBackend::poll()
{
    QHash<QString, QStringList> out;
    const QJsonObject reply = request({{QStringLiteral("op"), QStringLiteral("list")}, {QStringLiteral("processes"), true}});
    for (const QJsonValue &v : reply.value(QStringLiteral("sessions")).toArray()) {
        const QJsonObject s = v.toObject();
        QStringList names{s.value(QStringLiteral("foreground")).toString()};
        for (const QJsonValue &n : s.value(QStringLiteral("foregroundGroup")).toArray()) {
            if (!names.contains(n.toString())) {
                names.append(n.toString());
            }
        }
        names.removeAll(QString());
        out.insert(s.value(QStringLiteral("name")).toString(), names);
    }
    Q_EMIT foregroundProcesses(out);
}
