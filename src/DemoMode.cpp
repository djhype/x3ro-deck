// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DemoMode.h"

#include <QJsonArray>
#include <QTimer>

namespace
{
struct Fake
{
    const char *name;
    const char *agent;
};
const Fake kCheckouts[] = {{"aurora-web", "claude"}, {"billing-api", "codex"}, {"infra-tools", "claude"}, {"mobile-app", "claude"}};
} // namespace

namespace DemoMode
{

Config config()
{
    Config c;
    for (const Fake &f : kCheckouts) {
        CheckoutConfig cc;
        cc.name = QLatin1String(f.name);
        cc.path = QStringLiteral("/home/demo/src/") + cc.name;
        cc.agent = QLatin1String(f.agent);
        c.checkouts.append(cc);
    }
    for (const char *kind : {"claude", "codex"}) {
        AgentProfile p;
        const QJsonObject d = Config::kindDefaults(QLatin1String(kind));
        p.id = QLatin1String(kind);
        p.label = p.id == QLatin1String("claude") ? QStringLiteral("Claude") : QStringLiteral("Codex");
        p.kind = p.id;
        p.command = d.value(QStringLiteral("command")).toString();
        p.continueArgs = d.value(QStringLiteral("continueArgs")).toString();
        p.process = d.value(QStringLiteral("process")).toString();
        p.prPrompt = d.value(QStringLiteral("prPrompt")).toString();
        for (const QJsonValue &v : d.value(QStringLiteral("prompts")).toArray()) {
            p.prompts.append(v.toString());
        }
        c.agents.append(p);
    }
    c.defaultAgent = QStringLiteral("claude");
    c.terminalSettings = QStringLiteral("none"); // the app's own terminal look, no import
    c.autostartAgents = false;
    c.links = {{QStringLiteral("web"), QStringLiteral("web"), 80}, {QStringLiteral("api"), QStringLiteral("api"), 3000}};
    c.quickCommands = {{QStringLiteral("Test"), QStringLiteral("pnpm run test"), QStringLiteral("check")},
                       {QStringLiteral("Seed DB"), QStringLiteral("make seed"), QStringLiteral("data")}};
    return c;
}

QByteArray agentTranscript(int checkout)
{
    static const char *const lines[] = {
        "\x1b[38;5;213m✻\x1b[0m Reading src/routes/checkout.ts\r\n"
        "\x1b[38;5;213m✻\x1b[0m Updating the price summary to show taxes per line item\r\n\r\n"
        "  \x1b[32m+\x1b[0m const taxes = items.map((i) => i.price * i.taxRate);\r\n"
        "  \x1b[32m+\x1b[0m return { subtotal, taxes, total: subtotal + sum(taxes) };\r\n\r\n"
        "\x1b[2mRunning the test suite…\x1b[0m\r\n",
        "\x1b[36m▌\x1b[0m codex: refactoring the invoice export\r\n\r\n"
        "  Proposed command: \x1b[1mpnpm prisma migrate dev --name invoice_totals\x1b[0m\r\n\r\n"
        "  Allow? \x1b[2m(y) yes  (a) always  (esc) no\x1b[0m\r\n",
        "\x1b[38;5;213m✻\x1b[0m Which Terraform workspace should the new bucket go to?\r\n\r\n"
        "  1. staging\r\n  2. production\r\n",
        "\x1b[38;5;213m✻\x1b[0m Done. The onboarding screens now respect the system font size.\r\n"
        "  \x1b[2m4 files changed, 61 insertions(+), 18 deletions(-)\x1b[0m\r\n",
    };
    return QByteArray(lines[qBound(0, checkout, 3)]);
}

QByteArray shellTranscript()
{
    return QByteArrayLiteral("\x1b[36mdemo@deck\x1b[0m:\x1b[34m~/src/aurora-web\x1b[0m$ pnpm run test\r\n"
                             "\r\n \x1b[32m✓\x1b[0m src/cart.test.ts (12)\r\n \x1b[32m✓\x1b[0m src/checkout.test.ts (8)\r\n\r\n"
                             " Test Files  \x1b[32m2 passed\x1b[0m (2)\r\n      Tests  \x1b[32m20 passed\x1b[0m (20)\r\n\r\n"
                             "\x1b[36mdemo@deck\x1b[0m:\x1b[34m~/src/aurora-web\x1b[0m$ ");
}

} // namespace DemoMode

DemoChannel::DemoChannel(QByteArray transcript, QObject *parent)
    : TerminalChannel(parent)
{
    // After the first layout, so the text lands in a terminal of its real size.
    QTimer::singleShot(500, this, [this, transcript] { Q_EMIT received(transcript); });
}

TerminalChannel *DemoBackend::attachChannel(const QString &name, int, int, QObject *parent) const
{
    int index = 0;
    for (int i = 0; i < 4; ++i) {
        if (name == QLatin1String(kCheckouts[i].name)) {
            index = i;
        }
    }
    return new DemoChannel(DemoMode::agentTranscript(index), parent);
}
