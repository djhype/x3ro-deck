#pragma once

#include <QList>
#include <QString>

struct CheckoutConfig {
    QString name;
    QString path; // absolute, canonical
    int portOffset = 0;
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

struct Config {
    QList<CheckoutConfig> checkouts;
    QList<QuickCommand> quickCommands;
    QList<LinkConfig> links;
    QString agentCommand = QStringLiteral("claude");
    QString konsoleProfile; // empty = Konsole's default profile
    QString composeUp = QStringLiteral("make services-up");
    QString composeDown = QStringLiteral("make services-down");
    QString composeRestart = QStringLiteral("make services-down && make services-up");
    QString allowKey = QStringLiteral("1");
    QString alwaysKey = QStringLiteral("2");
    QString denyKey = QStringLiteral("Escape");
    bool notifyOnDone = true;
    bool autostartAgents = true; // a newly created agent session runs agentCommand

    static QString configDir();
    static QString configPath();
    // Loads ~/.config/x3ro-deck/config.json, writing the defaults first if it is missing.
    static Config load(QString *error = nullptr);
};
