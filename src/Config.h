#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

struct CheckoutConfig {
    QString name;
    QString path; // absolute, canonical
    int portOffset = 0;
    QString agent; // profile id; empty = Config::defaultAgent
};

struct QuickCommand {
    QString label;
    QString command; // may contain {port:<service>:<containerPort>} and {project}
    QString group;   // "check" | "env" | "data" — picks the accent colour
};

struct LinkConfig {
    QString label;
    QString service;
    int containerPort = 0;
};

// One way to run an agent. `kind` names the CLI it drives ("claude" or
// "codex"), which fixes the hook dialect, prompt keys and resume flags; a
// profile can point that CLI at another backend through `env`, which is how a
// local model (a model on a local server, say) runs under Claude Code or Codex.
struct AgentProfile {
    QString id;
    QString label;
    QString kind;
    QString command;      // typed into the checkout's tmux shell
    QString continueArgs; // appended to `command` to resume the latest conversation
    QString process;      // tmux's pane_current_command while the agent runs
    QList<QPair<QString, QString>> env;
    QString allowKey;
    QString alwaysKey;
    QString denyKey;
    QString prPrompt;    // sent when no PR exists yet; empty hides the button
    QStringList prompts; // one button each in the session panel
    int submitDelayMs = 0;

    // The shell line that starts (or, with `resume`, continues) the agent.
    // Env values are double-quoted, so `$VAR` expands from the shell.
    QString launchLine(bool resume) const;
};

struct Config {
    QList<CheckoutConfig> checkouts;
    QList<QuickCommand> quickCommands;
    QList<LinkConfig> links;
    QList<AgentProfile> agents; // never empty
    QString defaultAgent;
    QString konsoleProfile; // empty = Konsole's default profile
    QString composeUp = QStringLiteral("docker compose up -d");
    QString composeDown = QStringLiteral("docker compose down");
    QString composeRestart = QStringLiteral("docker compose restart");
    bool notifyOnDone = true;
    bool autostartAgents = true; // a newly created agent session runs its profile's command

    // The profile with this id, else the default one.
    const AgentProfile &agent(const QString &id) const;

    static QString configDir();
    static QString configPath();
    // Loads ~/.config/x3ro-deck/config.json, writing the defaults first if it is missing.
    static Config load(QString *error = nullptr);
};
