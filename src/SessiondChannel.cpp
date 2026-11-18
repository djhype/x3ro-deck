// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SessiondChannel.h"

#include "SessiondProtocol.h"

#include <QLocalSocket>
#include <QTimer>

namespace
{
// Output fed to the view per event-loop pass, so it paints between large bursts.
constexpr qsizetype kSliceBytes = 256 * 1024;
// How long the view's size has to hold before the daemon hears it. A view is
// created before its layout runs, so its first sizes are placeholders (one row,
// say), and the session's screen would lose its contents to them. The attach
// client gets the same coalescing from SIGWINCH.
constexpr int kSettleMs = 50;
} // namespace

SessiondChannel::SessiondChannel(QObject *parent)
    : TerminalChannel(parent)
    , m_sock(new QLocalSocket(this))
    , m_settle(new QTimer(this))
{
    m_settle->setSingleShot(true);
    m_settle->setInterval(kSettleMs);
    connect(m_settle, &QTimer::timeout, this, [this] {
        if (m_state == State::Attaching) {
            sendAttach();
        } else if (m_state == State::Attached) {
            sendResize();
        }
    });
}

SessiondChannel::~SessiondChannel()
{
    // Detaches: the daemon forgets a client whose connection closed.
    disconnect(m_sock, nullptr, this, nullptr);
    m_sock->abort();
}

void SessiondChannel::open(const QString &name, int rows, int cols, const QString &socketPath)
{
    if (m_state != State::Idle) {
        return;
    }
    m_name = name;
    m_rows = rows;
    m_cols = cols;
    m_state = State::Attaching;
    connect(m_sock, &QLocalSocket::connected, this, [this] {
        m_connected = true;
        m_sock->write(Sessiond::encodeControl({{QStringLiteral("op"), QStringLiteral("hello")},
                                               {QStringLiteral("id"), m_nextId++},
                                               {QStringLiteral("version"), Sessiond::kProtocolVersion}}));
        sendAttach();
    });
    connect(m_sock, &QLocalSocket::readyRead, this, &SessiondChannel::onReadyRead);
    connect(m_sock, &QLocalSocket::disconnected, this, [this] {
        // Whatever arrived before the daemon went away is still shown.
        m_buffer.append(m_sock->readAll());
        while (m_state != State::Closed && !m_buffer.isEmpty()) {
            const qsizetype before = m_buffer.size();
            process();
            if (m_buffer.size() == before) {
                break; // an incomplete frame
            }
        }
        finish(QByteArrayLiteral("[lost the session daemon]"));
    });
    connect(m_sock, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError error) {
        if (error == QLocalSocket::PeerClosedError) {
            return; // disconnected() follows
        }
        if (m_sock->state() == QLocalSocket::ConnectedState) {
            return;
        }
        if (!m_connected) {
            finish(QByteArrayLiteral("[no session daemon is running]"));
        } else {
            finish(QByteArrayLiteral("[lost the session daemon]"));
        }
    });
    m_settle->start();
    m_sock->connectToServer(socketPath.isEmpty() ? Sessiond::socketPath() : socketPath);
}

void SessiondChannel::sendAttach()
{
    // Once connected and once the size settled, whichever comes last.
    if (!m_connected || m_settle->isActive() || m_attachId != 0) {
        return;
    }
    m_attachId = m_nextId++;
    m_sock->write(Sessiond::encodeControl({{QStringLiteral("op"), QStringLiteral("attach")},
                                           {QStringLiteral("id"), m_attachId},
                                           {QStringLiteral("name"), m_name},
                                           {QStringLiteral("cols"), qMax(1, m_cols)},
                                           {QStringLiteral("rows"), qMax(1, m_rows)}}));
    m_sentRows = m_rows;
    m_sentCols = m_cols;
}

void SessiondChannel::onReadyRead()
{
    m_buffer.append(m_sock->readAll());
    process();
}

void SessiondChannel::process()
{
    qsizetype fed = 0;
    Sessiond::Frame f;
    bool bad = false;
    while (m_state != State::Closed && fed < kSliceBytes && Sessiond::takeFrame(m_buffer, f, &bad)) {
        if (f.type == Sessiond::Output) {
            quint32 id = 0;
            QByteArray bytes;
            if (m_session != 0 && Sessiond::splitData(f.payload, id, bytes) && id == m_session && !bytes.isEmpty()) {
                fed += bytes.size();
                Q_EMIT received(bytes);
            }
        } else if (f.type == Sessiond::Control) {
            const QJsonObject message = Sessiond::parseControl(f.payload);
            if (message.contains(QStringLiteral("re"))) {
                if (message.value(QStringLiteral("re")).toInt() == m_attachId && m_state == State::Attaching) {
                    onAttached(message);
                }
            } else if (message.value(QStringLiteral("event")).toString() == QLatin1String("exited") && m_session != 0
                       && quint32(message.value(QStringLiteral("session")).toInteger()) == m_session) {
                finish(QStringLiteral("[session %1 ended]").arg(message.value(QStringLiteral("name")).toString(m_name)).toUtf8());
            }
        }
    }
    if (bad) {
        finish(QByteArrayLiteral("[the session daemon sent a bad frame]"));
        return;
    }
    // More complete frames are waiting: continue after the view had a chance to paint.
    if (m_state != State::Closed && fed >= kSliceBytes && !m_processQueued) {
        m_processQueued = true;
        QTimer::singleShot(0, this, [this] {
            m_processQueued = false;
            process();
        });
    }
}

void SessiondChannel::onAttached(const QJsonObject &reply)
{
    if (!reply.value(QStringLiteral("ok")).toBool()) {
        finish(QStringLiteral("[%1: %2]").arg(m_name, reply.value(QStringLiteral("error")).toString()).toUtf8());
        return;
    }
    m_session = quint32(reply.value(QStringLiteral("session")).toInteger());
    // The daemon clamps the size; what it took is what it last heard.
    m_sentCols = reply.value(QStringLiteral("cols")).toInt(m_sentCols);
    m_sentRows = reply.value(QStringLiteral("rows")).toInt(m_sentRows);
    m_state = State::Attached;
    if (!m_pendingInput.isEmpty()) {
        m_sock->write(Sessiond::encodeData(Sessiond::Input, m_session, m_pendingInput));
        m_pendingInput.clear();
    }
    // A size that changed while the attach was on its way.
    if (!m_settle->isActive()) {
        m_settle->start();
    }
}

void SessiondChannel::write(const QByteArray &bytes)
{
    if (bytes.isEmpty()) {
        return;
    }
    if (m_state == State::Attached) {
        m_sock->write(Sessiond::encodeData(Sessiond::Input, m_session, bytes));
    } else if (m_state == State::Attaching) {
        m_pendingInput.append(bytes);
    }
}

void SessiondChannel::resize(int rows, int cols)
{
    m_rows = rows;
    m_cols = cols;
    // Before the attach request this only changes the size it asks for; after
    // it, the last size within kSettleMs wins.
    if (m_state == State::Attaching && m_attachId == 0) {
        if (m_settle->isActive()) {
            m_settle->start();
        }
    } else if (m_state == State::Attached) {
        m_settle->start();
    }
}

void SessiondChannel::sendResize()
{
    if (m_rows < 1 || m_cols < 1 || (m_rows == m_sentRows && m_cols == m_sentCols)) {
        return;
    }
    m_sentRows = m_rows;
    m_sentCols = m_cols;
    m_sock->write(Sessiond::encodeControl({{QStringLiteral("op"), QStringLiteral("resize")},
                                           {QStringLiteral("id"), m_nextId++},
                                           {QStringLiteral("session"), qint64(m_session)},
                                           {QStringLiteral("cols"), m_cols},
                                           {QStringLiteral("rows"), m_rows}}));
}

void SessiondChannel::finish(const QByteArray &note)
{
    if (m_state == State::Closed) {
        return;
    }
    m_state = State::Closed;
    m_settle->stop();
    m_pendingInput.clear();
    m_buffer.clear();
    disconnect(m_sock, nullptr, this, nullptr);
    m_sock->abort();
    // Queued: the owner may delete the channel on closed(), and finish() can
    // run inside open() or inside a signal from the socket.
    QMetaObject::invokeMethod(
        this,
        [this, note] {
            Q_EMIT received(QByteArrayLiteral("\r\n") + note + QByteArrayLiteral("\r\n"));
            Q_EMIT closed();
        },
        Qt::QueuedConnection);
}
