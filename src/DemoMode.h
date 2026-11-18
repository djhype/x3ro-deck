#pragma once

#include "Config.h"
#include "SessionBackend.h"
#include "TerminalChannel.h"

// `x3ro-deck --demo`: invented checkouts, agents, containers and pull
// requests for screenshots. Nothing touches tmux, Docker, gh, git or the
// user's config; AppController seeds its state from here instead.
namespace DemoMode
{
Config config();
// What the agent terminals and shell tabs show.
QByteArray agentTranscript(int checkout);
QByteArray shellTranscript();
} // namespace DemoMode

// A terminal that prints a canned transcript and then just sits there.
class DemoChannel : public TerminalChannel
{
    Q_OBJECT
public:
    DemoChannel(QByteArray transcript, QObject *parent);
    void write(const QByteArray &) override { }
    void resize(int, int) override { }
    bool isOpen() const override { return true; }
};

// Sessions that exist for every checkout and run nothing.
class DemoBackend : public SessionBackend
{
    Q_OBJECT
public:
    using SessionBackend::SessionBackend;
    bool hasSession(const QString &) const override { return true; }
    bool createSession(const QString &, const QString &, const QString &, const Environment & = {}, const QStringList & = {}) override
    {
        return true;
    }
    Environment sessionEnvironment(const QString &) const override { return {}; }
    QStringList sessionShell(const QString &) const override { return {}; }
    QStringList sessionsIn(const QString &) const override { return {}; }
    bool renameSession(const QString &, const QString &) override { return true; }
    void killSession(const QString &) override { }
    void killAll() override { }
    // Never run: attachChannel gives every terminal its transcript.
    QStringList attachCommand(const QString &) const override { return {QStringLiteral("true")}; }
    QString attachHint(const QString &) const override { return QStringLiteral("x3ro-deck attach <checkout>"); }
    TerminalChannel *attachChannel(const QString &name, int rows, int cols, QObject *parent) const override;
    void sendKeys(const QString &, const QStringList &, bool = false) override { }
    void submit(const QString &, const QString &, int = 0) override { }
    void startPolling(int) override { }
};
