#include "DockerClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QUrl>

#include <memory>

namespace
{
qint64 parseDuration(const QString &text)
{
    // "27 minutes", "About an hour", "Less than a second", "3 days"
    static const QRegularExpression re(QStringLiteral(R"((\d+)\s+(second|minute|hour|day|week|month|year))"));
    const auto m = re.match(text);
    if (m.hasMatch()) {
        static const QHash<QString, qint64> unit{{QStringLiteral("second"), 1},     {QStringLiteral("minute"), 60},
                                                 {QStringLiteral("hour"), 3600},    {QStringLiteral("day"), 86400},
                                                 {QStringLiteral("week"), 604800},  {QStringLiteral("month"), 2592000},
                                                 {QStringLiteral("year"), 31536000}};
        return m.captured(1).toLongLong() * unit.value(m.captured(2));
    }
    if (text.contains(QLatin1String("About a minute"))) {
        return 60;
    }
    if (text.contains(QLatin1String("About an hour"))) {
        return 3600;
    }
    return 0;
}

ContainerInfo parseContainer(const QJsonObject &o)
{
    ContainerInfo c;
    c.id = o.value(QStringLiteral("Id")).toString();
    const QJsonArray names = o.value(QStringLiteral("Names")).toArray();
    c.name = names.isEmpty() ? c.id.left(12) : names.first().toString().mid(1);
    const QJsonObject labels = o.value(QStringLiteral("Labels")).toObject();
    c.project = labels.value(QStringLiteral("com.docker.compose.project")).toString();
    c.service = labels.value(QStringLiteral("com.docker.compose.service")).toString(c.name);
    c.workingDir = labels.value(QStringLiteral("com.docker.compose.project.working_dir")).toString();
    c.state = o.value(QStringLiteral("State")).toString();
    c.status = o.value(QStringLiteral("Status")).toString();

    if (c.state == QLatin1String("running")) {
        if (c.status.contains(QLatin1String("(unhealthy)"))) {
            c.health = QStringLiteral("unhealthy");
        } else if (c.status.contains(QLatin1String("health: starting"))) {
            c.health = QStringLiteral("starting");
        } else if (c.status.contains(QLatin1String("(healthy)"))) {
            c.health = QStringLiteral("healthy");
        } else {
            c.health = QStringLiteral("running");
        }
        c.upSeconds = parseDuration(c.status);
    } else if (c.state == QLatin1String("exited") || c.state == QLatin1String("dead")) {
        c.health = QStringLiteral("exited");
        static const QRegularExpression exitRe(QStringLiteral(R"(Exited \((-?\d+)\))"));
        const auto m = exitRe.match(c.status);
        c.exitCode = m.hasMatch() ? m.captured(1).toInt() : 0;
        // One-shot init containers (migrations, seeders) exit 0 by design.
        if (c.exitCode == 0) {
            c.health = QStringLiteral("completed");
        }
    } else if (c.state == QLatin1String("restarting")) {
        c.health = QStringLiteral("restarting");
    } else {
        c.health = c.state;
    }

    QList<int> seen;
    for (const QJsonValue &pv : o.value(QStringLiteral("Ports")).toArray()) {
        const QJsonObject p = pv.toObject();
        const int pub = p.value(QStringLiteral("PublicPort")).toInt();
        const int priv = p.value(QStringLiteral("PrivatePort")).toInt();
        if (pub > 0 && !seen.contains(pub)) {
            seen.append(pub);
            c.ports.append({priv, pub});
        }
    }
    return c;
}
} // namespace

DockerClient::DockerClient(QObject *parent)
    : QObject(parent)
    , m_socketPath(QStringLiteral("/var/run/docker.sock"))
{
    m_refreshDebounce.setSingleShot(true);
    m_refreshDebounce.setInterval(250);
    connect(&m_refreshDebounce, &QTimer::timeout, this, &DockerClient::refresh);

    m_reconnect.setSingleShot(true);
    m_reconnect.setInterval(3000);
    connect(&m_reconnect, &QTimer::timeout, this, [this] {
        connectEvents();
        refresh();
    });
}

DockerClient::~DockerClient()
{
    // QObject deletes child sockets only after the timers are gone, and a
    // socket closing then runs its callback into a half-destroyed client.
    const QList<QLocalSocket *> sockets = findChildren<QLocalSocket *>(Qt::FindDirectChildrenOnly);
    for (QLocalSocket *sock : sockets) {
        sock->disconnect();
        delete sock;
    }
}

void DockerClient::start()
{
    connectEvents();
    refresh();
}

void DockerClient::setConnected(bool c)
{
    if (m_connected != c) {
        m_connected = c;
        Q_EMIT connectedChanged(c);
    }
}

void DockerClient::request(const QByteArray &method, const QString &path, Callback cb)
{
    auto *sock = new QLocalSocket(this);
    auto buffer = std::make_shared<QByteArray>();
    auto done = std::make_shared<bool>(false);

    auto finish = [sock, buffer, done, cb](bool ok) {
        if (*done) {
            return;
        }
        *done = true;
        int status = 0;
        QByteArray body;
        if (ok) {
            const qsizetype split = buffer->indexOf("\r\n\r\n");
            if (split >= 0) {
                const QByteArray statusLine = buffer->left(buffer->indexOf("\r\n"));
                const QList<QByteArray> parts = statusLine.split(' ');
                status = parts.size() > 1 ? parts.at(1).toInt() : 0;
                body = buffer->mid(split + 4);
            }
        }
        cb(status, body);
        sock->deleteLater();
    };

    connect(sock, &QLocalSocket::connected, sock,
            [sock, method, path] { sock->write(method + ' ' + path.toUtf8() + " HTTP/1.0\r\nHost: docker\r\nContent-Length: 0\r\n\r\n"); });
    connect(sock, &QLocalSocket::readyRead, sock, [sock, buffer] { buffer->append(sock->readAll()); });
    connect(sock, &QLocalSocket::disconnected, sock, [sock, buffer, finish] {
        buffer->append(sock->readAll());
        finish(true);
    });
    connect(sock, &QLocalSocket::errorOccurred, sock, [finish](QLocalSocket::LocalSocketError err) {
        if (err != QLocalSocket::PeerClosedError) {
            finish(false);
        }
    });
    sock->connectToServer(m_socketPath);
}

void DockerClient::refresh()
{
    const QString filters = QString::fromUtf8(QUrl::toPercentEncoding(QStringLiteral(R"({"label":["com.docker.compose.project"]})")));
    request("GET", QStringLiteral("/containers/json?all=1&filters=") + filters, [this](int status, const QByteArray &body) {
        if (status != 200) {
            setConnected(false);
            if (!m_reconnect.isActive()) {
                m_reconnect.start();
            }
            return;
        }
        setConnected(true);
        QList<ContainerInfo> out;
        for (const QJsonValue &v : QJsonDocument::fromJson(body).array()) {
            out.append(parseContainer(v.toObject()));
        }
        Q_EMIT containersUpdated(out);
    });
}

void DockerClient::connectEvents()
{
    if (m_events) {
        m_events->disconnect(this);
        m_events->abort();
        m_events->deleteLater();
    }
    m_events = new QLocalSocket(this);
    m_eventBuffer.clear();
    m_eventHeaderDone = false;

    connect(m_events, &QLocalSocket::connected, this, [this] {
        const QString filters = QString::fromUtf8(QUrl::toPercentEncoding(QStringLiteral(R"({"type":["container"]})")));
        m_events->write("GET /events?filters=" + filters.toUtf8() + " HTTP/1.0\r\nHost: docker\r\n\r\n");
    });
    connect(m_events, &QLocalSocket::readyRead, this, [this] {
        m_eventBuffer.append(m_events->readAll());
        if (!m_eventHeaderDone) {
            const qsizetype split = m_eventBuffer.indexOf("\r\n\r\n");
            if (split < 0) {
                return;
            }
            m_eventBuffer.remove(0, split + 4);
            m_eventHeaderDone = true;
        }
        bool any = false;
        qsizetype nl;
        while ((nl = m_eventBuffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_eventBuffer.left(nl).trimmed();
            m_eventBuffer.remove(0, nl + 1);
            if (line.isEmpty()) {
                continue;
            }
            const QJsonObject ev = QJsonDocument::fromJson(line).object();
            const QJsonObject attrs = ev.value(QStringLiteral("Actor")).toObject().value(QStringLiteral("Attributes")).toObject();
            if (attrs.contains(QStringLiteral("com.docker.compose.project"))) {
                any = true;
            }
        }
        if (any) {
            m_refreshDebounce.start();
        }
    });
    auto lost = [this] {
        setConnected(false);
        if (!m_reconnect.isActive()) {
            m_reconnect.start();
        }
    };
    connect(m_events, &QLocalSocket::disconnected, this, lost);
    connect(m_events, &QLocalSocket::errorOccurred, this, lost);
    m_events->connectToServer(m_socketPath);
}

void DockerClient::restartContainer(const QString &id)
{
    request("POST", QStringLiteral("/containers/%1/restart?t=5").arg(id), [](int, const QByteArray &) { });
}

void DockerClient::stopContainer(const QString &id)
{
    request("POST", QStringLiteral("/containers/%1/stop?t=5").arg(id), [](int, const QByteArray &) { });
}

void DockerClient::startContainer(const QString &id)
{
    request("POST", QStringLiteral("/containers/%1/start").arg(id), [](int, const QByteArray &) { });
}

void DockerClient::sampleStats(const QStringList &ids)
{
    if (m_statsInFlight || ids.isEmpty()) {
        return;
    }
    m_statsInFlight = true;
    auto results = std::make_shared<QHash<QString, ContainerStats>>();
    auto pending = std::make_shared<int>(ids.size());

    for (const QString &id : ids) {
        // stream=false waits ~1s so precpu_stats is populated and CPU% is meaningful.
        request("GET", QStringLiteral("/containers/%1/stats?stream=false").arg(id),
                [this, id, results, pending](int status, const QByteArray &body) {
                    if (status == 200) {
                        const QJsonObject o = QJsonDocument::fromJson(body).object();
                        const QJsonObject cpu = o.value(QStringLiteral("cpu_stats")).toObject();
                        const QJsonObject pre = o.value(QStringLiteral("precpu_stats")).toObject();
                        const double cpuDelta
                            = cpu.value(QStringLiteral("cpu_usage")).toObject().value(QStringLiteral("total_usage")).toDouble()
                            - pre.value(QStringLiteral("cpu_usage")).toObject().value(QStringLiteral("total_usage")).toDouble();
                        const double sysDelta = cpu.value(QStringLiteral("system_cpu_usage")).toDouble()
                            - pre.value(QStringLiteral("system_cpu_usage")).toDouble();
                        const double online = cpu.value(QStringLiteral("online_cpus")).toDouble(1);
                        const QJsonObject mem = o.value(QStringLiteral("memory_stats")).toObject();
                        const double usage = mem.value(QStringLiteral("usage")).toDouble();
                        const double inactive
                            = mem.value(QStringLiteral("stats")).toObject().value(QStringLiteral("inactive_file")).toDouble();
                        ContainerStats s;
                        s.cpuPercent = sysDelta > 0 ? cpuDelta / sysDelta * online * 100.0 : 0.0;
                        s.memBytes = static_cast<qint64>(usage > inactive ? usage - inactive : usage);
                        results->insert(id, s);
                    }
                    if (--*pending == 0) {
                        m_statsInFlight = false;
                        Q_EMIT statsUpdated(*results);
                    }
                });
    }
}
