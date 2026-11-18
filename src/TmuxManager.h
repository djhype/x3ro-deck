// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "SessionBackend.h"

#include <QTimer>

// Owns the tmux server that keeps agent sessions alive across x3ro-deck
// restarts. It runs on its own socket (-L x3ro-deck) with a minimal config:
// x3ro-deck draws the chrome, so tmux's status bar is off.
//
// Reach a session from plain Konsole with: tmux -L x3ro-deck attach -t <checkout>
class TmuxManager : public SessionBackend
{
    Q_OBJECT
public:
    explicit TmuxManager(QObject *parent = nullptr);

    static QString socketName() { return QStringLiteral("x3ro-deck"); }
    QString configPath() const { return m_configPath; }

    bool hasSession(const QString &name) const override;
    bool createSession(const QString &name, const QString &dir, const QString &startCommand, const Environment &env = {},
                       const QStringList &shell = {}) override;
    Environment sessionEnvironment(const QString &name) const override;
    QStringList sessionShell(const QString &name) const override;
    QStringList sessionsIn(const QString &dir) const override;
    bool renameSession(const QString &from, const QString &to) override;
    void killSession(const QString &name) override;
    void killAll() override;
    QStringList attachCommand(const QString &name) const override;
    QString attachHint(const QString &name) const override;
    void sendKeys(const QString &name, const QStringList &keys, bool literal = false) override;
    void submit(const QString &name, const QString &text, int enterDelayMs = 0) override;
    void startPolling(int intervalMs) override;

private:
    QStringList baseArgs() const;
    void poll();

    QString m_configPath;
    QTimer m_poll;
    bool m_pollInFlight = false;
};
