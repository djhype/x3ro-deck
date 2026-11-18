// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "TerminalChannel.h"

#include <QJsonObject>
#include <QString>

class QLocalSocket;
class QTimer;

// A built-in terminal attached straight to an x3ro-deck-sessiond session,
// without an x3ro-deck-attach process in a local PTY: Output frames come in
// through received(), keystrokes go out as Input frames, and resize() is the
// protocol's resize op. Each channel has its own connection to the daemon.
//
// Sizes reach the daemon once they held for a moment, so a view's
// placeholder sizes before its first layout never resize the session.
//
// It closes when the session exits, the daemon goes away or the attach fails,
// and then prints the attach client's one-line note into the stream first.
// closed() is always delivered from the event loop, never from open().
class SessiondChannel : public TerminalChannel
{
    Q_OBJECT
public:
    explicit SessiondChannel(QObject *parent = nullptr);
    ~SessiondChannel() override;

    // Attaches to the session called `name` at this size; the daemon resizes
    // the session first and then redraws its screen.
    void open(const QString &name, int rows, int cols, const QString &socketPath = {});

    void write(const QByteArray &bytes) override;
    void resize(int rows, int cols) override;
    bool isOpen() const override { return m_state != State::Closed; }
    // The daemon's id for the session, once attached; 0 before.
    quint32 session() const { return m_session; }

private:
    enum class State { Idle, Attaching, Attached, Closed };

    void onReadyRead();
    void process();
    void onAttached(const QJsonObject &reply);
    void sendAttach();
    void sendResize();
    void finish(const QByteArray &note);

    QLocalSocket *m_sock;
    QTimer *m_settle;
    bool m_connected = false;
    QString m_name;
    State m_state = State::Idle;
    quint32 m_session = 0;
    int m_rows = 0;
    int m_cols = 0;
    int m_sentRows = 0;
    int m_sentCols = 0;
    int m_nextId = 1;
    int m_attachId = 0;
    QByteArray m_buffer;
    QByteArray m_pendingInput; // typed before the attach reply
    bool m_processQueued = false;
};
