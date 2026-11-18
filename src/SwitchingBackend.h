// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "SessionBackend.h"

// Two backends while the user switches between them: sessions still alive in
// the old (legacy) backend keep working there until they exit, and every new
// session starts in the new (primary) one. No session moves between them.
class SwitchingBackend : public SessionBackend
{
    Q_OBJECT
public:
    // Takes ownership of both.
    SwitchingBackend(SessionBackend *primary, SessionBackend *legacy, QObject *parent = nullptr);

    SessionBackend *primary() const { return m_primary; }
    SessionBackend *legacy() const { return m_legacy; }

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
    TerminalChannel *attachChannel(const QString &name, int rows, int cols, QObject *parent) const override;
    void sendKeys(const QString &name, const QStringList &keys, bool literal = false) override;
    void submit(const QString &name, const QString &text, int enterDelayMs = 0) override;
    void startPolling(int intervalMs) override;

Q_SIGNALS:
    // The legacy backend has no sessions left.
    void drained();

private:
    SessionBackend *owner(const QString &name) const;
    void merge();

    SessionBackend *m_primary;
    SessionBackend *m_legacy;
    QHash<QString, QStringList> m_primaryPanes;
    QHash<QString, QStringList> m_legacyPanes;
    bool m_drained = false;
    int m_emptyPolls = 0;
};
