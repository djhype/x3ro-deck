#include "TerminalHost.h"

#include "PtyChannel.h"
#include "TerminalScreen.h"
#include "TerminalView.h"

#include <KParts/PartLoader>
#include <KParts/ReadOnlyPart>
#include <KPluginMetaData>
#include <kde_terminal_interface.h>

#include <QKeyEvent>
#include <QLabel>
#include <QStyle>
#include <QVBoxLayout>

namespace
{
QList<QKeySequence> &appShortcuts()
{
    static QList<QKeySequence> list;
    return list;
}

bool &builtinDefault()
{
    static bool builtin = false;
    return builtin;
}

int &scrollbackDefault()
{
    static int lines = 10000;
    return lines;
}
} // namespace

TerminalHost::TerminalHost(QWidget *parent)
    : QFrame(parent)
    , m_builtin(builtinDefault())
    , m_layout(new QVBoxLayout(this))
    , m_placeholder(new QLabel(this))
{
    setObjectName(QStringLiteral("terminalFrame"));
    setProperty("accent", QString());
    m_layout->setContentsMargins(1, 1, 1, 1);
    m_layout->setSpacing(0);
    m_placeholder->setObjectName(QStringLiteral("terminalPlaceholder"));
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setWordWrap(true);
    m_placeholder->hide();
    m_layout->addWidget(m_placeholder);
}

TerminalHost::~TerminalHost()
{
    delete m_channel; // hangs up the program or detaches; agent sessions survive
    m_channel = nullptr;
    // The part is our child; detach first so its destroyed() does not reach a
    // half-destroyed host. Deleting it hangs up the terminal (agent sessions survive).
    if (m_part) {
        disconnect(m_part, nullptr, this, nullptr);
        delete m_part.data();
    }
}

TerminalHost *TerminalHost::shell(const QString &dir, const QString &profile, QWidget *parent)
{
    auto *t = new TerminalHost(parent);
    t->m_dir = dir;
    t->m_profile = profile;
    t->spawn();
    return t;
}

TerminalHost *TerminalHost::program(const QString &program, const QStringList &args, const QString &profile, QWidget *parent,
                                    ChannelFactory direct)
{
    auto *t = new TerminalHost(parent);
    t->m_program = program;
    t->m_args = args;
    t->m_profile = profile;
    t->m_direct = std::move(direct);
    t->spawn();
    return t;
}

void TerminalHost::setAppShortcuts(const QList<QKeySequence> &shortcuts)
{
    appShortcuts() = shortcuts;
}

void TerminalHost::setScrollbackLines(int lines)
{
    scrollbackDefault() = lines;
}

void TerminalHost::setBuiltin(bool builtin)
{
    builtinDefault() = builtin;
}

bool TerminalHost::isAlive() const
{
    return m_builtin ? m_channel != nullptr : !m_part.isNull();
}

void TerminalHost::showError(const QString &text)
{
    m_placeholder->setText(text);
    m_placeholder->show();
    if (m_view) {
        m_view->hide();
    }
}

void TerminalHost::spawn()
{
    if (m_builtin) {
        spawnBuiltin();
    } else {
        spawnKonsole();
    }
}

void TerminalHost::spawnBuiltin()
{
    if (!m_view) {
        m_view = new TerminalView(this);
        m_view->setReservedShortcuts(appShortcuts());
        m_view->screen()->setScrollbackLimit(scrollbackDefault());
        m_layout->addWidget(m_view);
        setFocusProxy(m_view);
    }
    // A reattach redraws everything; start from a clean screen and modes.
    m_view->reset();
    m_channel = m_direct ? m_direct(m_view->rows(), m_view->cols(), this) : nullptr;
    if (!m_channel) {
        QStringList argv = m_args;
        if (m_program.isEmpty()) {
            const QString shell = qEnvironmentVariable("SHELL");
            argv = {shell.isEmpty() ? QStringLiteral("/bin/sh") : shell};
        }
        auto *pty = new PtyChannel(this);
        QString error;
        if (!pty->start(argv, m_dir, m_view->rows(), m_view->cols(), {}, &error)) {
            delete pty;
            showError(QStringLiteral("Could not start the terminal:\n%1").arg(error));
            return;
        }
        m_channel = pty;
    }
    m_placeholder->hide();
    m_view->show();
    m_view->setChannel(m_channel);
    connect(m_channel, &TerminalChannel::closed, this, [this] {
        m_view->setChannel(nullptr);
        m_channel->deleteLater();
        m_channel = nullptr;
        Q_EMIT exited();
    });
}

void TerminalHost::spawnKonsole()
{
    const KPluginMetaData md(QStringLiteral("kf6/parts/konsolepart"));
    auto result = KParts::PartLoader::instantiatePart<KParts::ReadOnlyPart>(md, this, this);
    if (!result) {
        m_placeholder->setText(QStringLiteral("Could not load Konsole's terminal part (konsolepart):\n%1").arg(result.errorString));
        m_placeholder->show();
        return;
    }
    m_part = result.plugin;
    m_term = qobject_cast<TerminalInterface *>(m_part);
    if (!m_term) {
        m_placeholder->setText(QStringLiteral("konsolepart does not implement TerminalInterface"));
        m_placeholder->show();
        delete m_part;
        return;
    }
    m_placeholder->hide();
    m_layout->addWidget(m_part->widget());
    setFocusProxy(m_part->widget());

    // konsolepart deletes itself when its process exits.
    connect(m_part, &QObject::destroyed, this, [this] {
        m_term = nullptr;
        Q_EMIT exited();
    });
    // Not part of TerminalInterface, so it has to be connected by signature.
    connect(m_part, SIGNAL(overrideShortcut(QKeyEvent *, bool &)), this, SLOT(onOverrideShortcut(QKeyEvent *, bool &)));

    if (!m_profile.isEmpty()) {
        m_term->setCurrentProfile(m_profile);
    }
    if (m_program.isEmpty()) {
        m_term->showShellInDir(m_dir);
    } else {
        m_term->startProgram(m_program, m_args);
    }
}

void TerminalHost::respawn()
{
    if (isAlive()) {
        return;
    }
    spawn();
}

void TerminalHost::onOverrideShortcut(QKeyEvent *event, bool &override)
{
    const QKeySequence seq(event->keyCombination());
    for (const QKeySequence &s : appShortcuts()) {
        if (s.matches(seq) == QKeySequence::ExactMatch) {
            override = false;
            return;
        }
    }
}

void TerminalHost::sendInput(const QString &text)
{
    if (m_channel) {
        m_channel->write(text.toUtf8());
    } else if (m_term) {
        m_term->sendInput(text);
    }
}

QString TerminalHost::foregroundProcessName() const
{
    if (auto *pty = qobject_cast<PtyChannel *>(m_channel)) {
        return pty->foregroundProcessName();
    }
    return m_term ? m_term->foregroundProcessName() : QString();
}

void TerminalHost::setAccent(const QString &state)
{
    if (property("accent").toString() == state) {
        return;
    }
    setProperty("accent", state);
    style()->unpolish(this);
    style()->polish(this);
}

void TerminalHost::focusTerminal()
{
    if (m_view) {
        m_view->setFocus(Qt::OtherFocusReason);
        return;
    }
    if (m_part && m_part->widget()) {
        m_part->widget()->setFocus(Qt::OtherFocusReason);
    }
}
