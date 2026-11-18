// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QTimer>

#include <functional>

class QLocalSocket;

struct PortMapping
{
    int privatePort = 0;
    int publicPort = 0;
};

struct ContainerInfo
{
    QString id;
    QString name;
    QString project;
    QString service;
    QString workingDir;
    QStringList configFiles; // compose files the project was started with, absolute
    QString state; // docker State: running, exited, restarting, …
    QString status; // docker Status: "Up 27 minutes (healthy)"
    QString health; // derived: healthy | running | starting | unhealthy | exited | restarting | other
    int exitCode = 0;
    qint64 upSeconds = -1; // parsed from Status, -1 when not running
    QList<PortMapping> ports;
};

struct ContainerStats
{
    double cpuPercent = -1;
    qint64 memBytes = -1;
};

// Talks to the Docker Engine API over a Unix socket (see DockerEndpoint.h).
//
// Every request uses HTTP/1.0, so the daemon never chunk-encodes a response and
// closes the connection when it is done; /events then becomes a plain stream of
// newline-delimited JSON objects.
class DockerClient : public QObject
{
    Q_OBJECT
public:
    explicit DockerClient(QObject *parent = nullptr);
    ~DockerClient() override;

    void start();
    // Switches to another Engine API socket and reconnects if running.
    void setSocketPath(const QString &path);
    QString socketPath() const { return m_socketPath; }
    void restartContainer(const QString &id);
    void stopContainer(const QString &id);
    void startContainer(const QString &id);
    // Samples CPU/memory for these containers; results arrive via statsUpdated.
    void sampleStats(const QStringList &ids);

    bool connected() const { return m_connected; }

Q_SIGNALS:
    void containersUpdated(const QList<ContainerInfo> &containers);
    void statsUpdated(const QHash<QString, ContainerStats> &stats);
    void connectedChanged(bool connected);

private:
    using Callback = std::function<void(int status, const QByteArray &body)>;
    void request(const QByteArray &method, const QString &path, Callback cb);
    void refresh();
    void connectEvents();
    void setConnected(bool c);

    QString m_socketPath;
    QLocalSocket *m_events = nullptr;
    QByteArray m_eventBuffer;
    bool m_eventHeaderDone = false;
    QTimer m_refreshDebounce;
    QTimer m_reconnect;
    bool m_connected = false;
    bool m_statsInFlight = false;
    bool m_started = false;
};
