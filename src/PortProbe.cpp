// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PortProbe.h"

#include <QTcpSocket>
#include <QTimer>

PortProbe::PortProbe(QObject *parent)
    : QObject(parent)
{
}

void PortProbe::setPorts(const QSet<int> &ports)
{
    for (auto it = m_http.begin(); it != m_http.end();) {
        it = ports.contains(it.key()) ? std::next(it) : m_http.erase(it);
    }
    for (int port : ports) {
        if (!m_http.contains(port) && !m_pending.contains(port)) {
            probe(port);
        }
    }
}

void PortProbe::probe(int port)
{
    m_pending.insert(port);
    auto *sock = new QTcpSocket(this);
    auto *timer = new QTimer(sock);
    timer->setSingleShot(true);
    auto done = [this, sock, port](bool http) {
        if (!m_pending.remove(port)) {
            return;
        }
        m_http.insert(port, http);
        sock->abort();
        sock->deleteLater();
        if (http) {
            Q_EMIT changed();
        }
    };
    connect(timer, &QTimer::timeout, this, [done] { done(false); });
    connect(sock, &QTcpSocket::errorOccurred, this, [done] { done(false); });
    connect(sock, &QTcpSocket::connected, this, [sock] { sock->write("HEAD / HTTP/1.0\r\nHost: localhost\r\n\r\n"); });
    connect(sock, &QTcpSocket::readyRead, this, [sock, done] {
        if (sock->bytesAvailable() >= 5) {
            done(sock->peek(5) == "HTTP/");
        }
    });
    timer->start(kTimeoutMs);
    sock->connectToHost(QStringLiteral("127.0.0.1"), static_cast<quint16>(port));
}
