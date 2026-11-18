#include "DockerEndpoint.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
// A host string as Docker spells it, from `source`.
DockerEndpoint fromHost(const QString &host, const QString &source)
{
    DockerEndpoint e;
    e.source = source;
    if (host.startsWith(QLatin1String("unix://"))) {
        e.socketPath = host.mid(7);
    } else if (host.startsWith(QLatin1Char('/'))) {
        e.socketPath = host;
    } else if (host.startsWith(QLatin1String("npipe://"))) {
        // npipe:////./pipe/docker_engine → \\.\pipe\docker_engine, which
        // QLocalSocket opens as a named pipe on Windows.
        e.socketPath = host.mid(8).replace(QLatin1Char('/'), QLatin1Char('\\'));
    } else {
        const QString scheme = host.section(QStringLiteral("://"), 0, 0);
        e.error = QStringLiteral("%1 endpoints are not supported (%2)").arg(scheme.isEmpty() ? host : scheme + QStringLiteral("://"), host);
    }
    return e;
}

QJsonObject readJsonFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

// The docker endpoint of context `name`, or an empty string.
QString contextHost(const QString &configDir, const QString &name)
{
    QDirIterator it(configDir + QStringLiteral("/contexts/meta"), {QStringLiteral("meta.json")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QJsonObject meta = readJsonFile(it.next());
        if (meta.value(QStringLiteral("Name")).toString() == name) {
            return meta.value(QStringLiteral("Endpoints"))
                .toObject()
                .value(QStringLiteral("docker"))
                .toObject()
                .value(QStringLiteral("Host"))
                .toString();
        }
    }
    return {};
}
} // namespace

QString DockerEndpoint::display() const
{
    if (!error.isEmpty()) {
        return error;
    }
    if (socketPath.startsWith(QLatin1Char('\\'))) {
        return QStringLiteral("npipe://%1 (%2)").arg(QString(socketPath).replace(QLatin1Char('\\'), QLatin1Char('/')), source);
    }
    return QStringLiteral("unix://%1 (%2)").arg(socketPath, source);
}

DockerEnvironment DockerEnvironment::system()
{
    return {QProcessEnvironment::systemEnvironment(), QDir::homePath(), [](const QString &p) { return QFileInfo::exists(p); }};
}

DockerEndpoint resolveDockerEndpoint(const QString &setting, const DockerEnvironment &where)
{
    if (!setting.trimmed().isEmpty()) {
        return fromHost(setting.trimmed(), QStringLiteral("setting"));
    }
    if (const QString host = where.env.value(QStringLiteral("DOCKER_HOST")); !host.isEmpty()) {
        return fromHost(host, QStringLiteral("DOCKER_HOST"));
    }

    const QString configDir = where.env.value(QStringLiteral("DOCKER_CONFIG"), where.home + QStringLiteral("/.docker"));
    QString context = where.env.value(QStringLiteral("DOCKER_CONTEXT"));
    if (context.isEmpty()) {
        context = readJsonFile(configDir + QStringLiteral("/config.json")).value(QStringLiteral("currentContext")).toString();
    }
    if (!context.isEmpty() && context != QLatin1String("default")) {
        if (const QString host = contextHost(configDir, context); !host.isEmpty()) {
            return fromHost(host, QStringLiteral("context ") + context);
        }
    }

    const QString runtime = where.env.value(QStringLiteral("XDG_RUNTIME_DIR"));
    QList<QPair<QString, QString>> candidates;
    if (!runtime.isEmpty()) {
        candidates.append({runtime + QStringLiteral("/docker.sock"), QStringLiteral("rootless Docker")});
        candidates.append({runtime + QStringLiteral("/podman/podman.sock"), QStringLiteral("Podman")});
    }
    candidates.append({where.home + QStringLiteral("/.colima/default/docker.sock"), QStringLiteral("Colima")});
    candidates.append({where.home + QStringLiteral("/.orbstack/run/docker.sock"), QStringLiteral("OrbStack")});
    for (const auto &[path, source] : std::as_const(candidates)) {
        if (where.exists(path)) {
            return {path, source, {}};
        }
    }
#if defined(Q_OS_WIN)
    return {QStringLiteral("\\\\.\\pipe\\docker_engine"), QStringLiteral("Docker Desktop"), {}};
#else
    return {QStringLiteral("/var/run/docker.sock"), QStringLiteral("default"), {}};
#endif
}
