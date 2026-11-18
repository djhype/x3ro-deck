#pragma once

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPair>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QVariantMap>

struct CheckoutConfig
{
    QString name;
    QString path; // absolute, canonical
    QString agent; // profile id; empty = Config::defaultAgent
    QString args; // shell fragment merged into the agent's command line; wins on flag conflicts
    // Compose commands run in the checkout's folder; empty = defaultCompose().
    QString composeUp;
    QString composeDown;
    QString composeRestart;
    // Global compose options the default commands pass: -f per file (in
    // order; none = compose's own lookup), --profile, --env-file and -p.
    QStringList composeFiles;
    QStringList composeProfiles;
    QString composeEnvFile;
    QString composeProject; // empty = compose's default, the folder name

    // `which` is up, down or restart.
    QString composeCommand(const QString &which) const;
    // The command for `which` built from the options above.
    QString defaultCompose(const QString &which) const;
    // The options as command-line flags, with a leading space.
    QString composeOptions() const;
};

struct QuickCommand
{
    QString label;
    QString command; // may contain {port:<service>:<containerPort>} and {project}
    QString group; // free-form; picks the accent colour (check, env and data have their own)
};

struct LinkConfig
{
    QString label;
    QString service;
    int containerPort = 0;
};

// One way to run an agent. `kind` names the CLI it drives ("claude" or
// "codex"), which fixes the hook dialect, prompt keys and resume flags; a
// profile can point that CLI at another backend through `env`, which is how a
// local model behind an Anthropic- or OpenAI-compatible server runs under
// Claude Code or Codex. Kind
// "custom" runs any other command; it sends no hooks, so only the pane poll
// tracks it.
struct AgentProfile
{
    QString id;
    QString label;
    QString kind;
    QString command; // typed into the checkout's session shell
    QString continueArgs; // appended to `command` to resume the latest conversation
    QString process; // the session's foreground process while the agent runs; a custom command's first word by default
    QList<QPair<QString, QString>> env;
    QString allowKey;
    QString alwaysKey;
    QString denyKey;
    QString prPrompt; // sent when no PR exists yet; empty hides the button
    QStringList prompts; // one button each in the session panel
    int submitDelayMs = 0;

    // The shell line that starts (or, with `resume`, continues) the agent.
    // `extraArgs` (a checkout's custom args) replace same-named flags in `command`.
    QString launchLine(bool resume, const QString &extraArgs = QString()) const;
    // `env` with $VAR and ${VAR} expanded against `from`, for the session to
    // start with. The values never pass through the session shell.
    QMap<QString, QString> sessionEnv(const QProcessEnvironment &from = QProcessEnvironment::systemEnvironment()) const;
};

// Expands $VAR and ${VAR} in `value`; unset variables expand to nothing, and
// a `$` not followed by a name stays as is.
QString expandVariables(const QString &value, const QProcessEnvironment &env);

struct Config
{
    QList<CheckoutConfig> checkouts;
    QList<QuickCommand> quickCommands;
    QList<LinkConfig> links;
    QList<AgentProfile> agents; // never empty
    QString defaultAgent;
    QString dockerHost; // empty = resolve (see DockerEndpoint.h)
    QString composeBinary = QStringLiteral("docker compose"); // what {compose} in commands becomes
    bool notifyOnDone = true;
    bool autostartAgents = true; // a newly created agent session runs its profile's command
    QString sessionBackend = QStringLiteral("tmux"); // "tmux" or "daemon" (x3ro-deck-sessiond); takes a restart
    int terminalScrollback = 10000; // history lines per built-in terminal when the imported profile sets none
    QString terminalSettings; // terminal to import settings from: empty = the desktop's default, "none", or a source id
    QString terminalProfile; // that terminal's profile; empty = its default
    QString theme = QStringLiteral("cyberdecker"); // a file in ~/.config/x3ro-deck/themes, a built-in theme, or "system"
    QString themeLight = QStringLiteral("cyberdecker-light"); // what "system" shows in light mode
    QString themeDark = QStringLiteral("cyberdecker"); // and in dark mode
    QString windowsShell; // pwsh | powershell | cmd | bash; empty = PowerShell 7, else Windows PowerShell
    QVariantMap shortcuts; // id → key sequence (PortableText) for rebound shortcuts; see Shortcuts.h

    // The profile with this id, else the default one.
    const AgentProfile &agent(const QString &id) const;

    static QString configDir();
    static QString configPath();
    // Loads ~/.config/x3ro-deck/config.json, writing the defaults first if it is missing.
    static Config load(QString *error = nullptr);
    // The same file as JSON, with defaults for missing top-level keys. On a
    // parse error it returns the defaults and sets `error`.
    static QJsonObject readJson(QString *error = nullptr);
    static bool writeJson(const QJsonObject &root, QString *error = nullptr);
    // The keys a profile of this kind has unless it overrides them.
    static QJsonObject kindDefaults(const QString &kind);
};
