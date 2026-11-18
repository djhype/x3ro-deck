#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// Registers x3ro-deck-hook in Claude Code's ~/.claude/settings.json and
// Codex's ~/.codex/hooks.json. Both files belong to the user and may hold
// other hooks, so it edits only its own entries and keeps the key order and
// every other value as written.
class HookInstaller
{
public:
    enum class Cli { Claude, Codex };
    enum class State { NotInstalled, Installed, Outdated, Error };
    struct Status
    {
        State state = State::NotInstalled;
        QString command; // the installed command, when there is one
        QString error;
    };

    static QString settingsFile(Cli cli);
    // x3ro-deck-hook next to the app, else on PATH; empty when neither has it.
    static QString hookPath();
    // The command line for `path`, quoted when it needs to be.
    static QString command(const QString &hookPath);
    static QStringList events(Cli cli);
    // A hook command that runs x3ro-deck-hook, wherever it lives.
    static bool isOurs(const QString &command);

    static Status status(Cli cli, const QString &file, const QString &command);
    // Both copy the file to <file>.bak before writing.
    static bool install(Cli cli, const QString &file, const QString &command, QString *error);
    static bool uninstall(const QString &file, QString *error);

    // The same edits on file contents. A null result means `json` did not parse.
    static QByteArray installed(Cli cli, const QByteArray &json, const QString &command, QString *error = nullptr);
    static QByteArray removed(const QByteArray &json, QString *error = nullptr);
    static Status statusOf(Cli cli, const QByteArray &json, const QString &command);
    // Parses and writes back unchanged apart from whitespace, for tests.
    static QByteArray normalized(const QByteArray &json, QString *error = nullptr);
};
