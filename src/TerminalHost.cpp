#include "TerminalHost.h"

#include "PtyChannel.h"
#include "TerminalProfile.h"
#include "TerminalScreen.h"
#include "TerminalView.h"

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

int &scrollbackDefault()
{
    static int lines = 10000;
    return lines;
}

TerminalProfile &currentProfile()
{
    static TerminalProfile profile = TerminalProfile::fallback();
    return profile;
}

// Every live host, so a new profile reaches the open terminals.
QList<TerminalHost *> &hosts()
{
    static QList<TerminalHost *> list;
    return list;
}

// What "unlimited" history means here: each line holds its cells in memory.
constexpr int kUnlimitedScrollback = 100000;
} // namespace

TerminalHost::TerminalHost(QWidget *parent)
    : QFrame(parent)
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
    hosts().append(this);
}

TerminalHost::~TerminalHost()
{
    hosts().removeOne(this);
    delete m_channel; // hangs up the program or detaches; agent sessions survive
    m_channel = nullptr;
}

TerminalHost *TerminalHost::shell(const QString &dir, QWidget *parent)
{
    auto *t = new TerminalHost(parent);
    t->m_dir = dir;
    t->spawn();
    return t;
}

TerminalHost *TerminalHost::program(const QString &program, const QStringList &args, QWidget *parent, ChannelFactory direct)
{
    auto *t = new TerminalHost(parent);
    t->m_program = program;
    t->m_args = args;
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
    if (scrollbackDefault() == lines) {
        return;
    }
    scrollbackDefault() = lines;
    for (TerminalHost *h : std::as_const(hosts())) {
        h->applyProfile();
    }
}

void TerminalHost::setProfile(const TerminalProfile &profile)
{
    currentProfile() = profile;
    for (TerminalHost *h : std::as_const(hosts())) {
        h->applyProfile();
    }
}

const TerminalProfile &TerminalHost::profile()
{
    return currentProfile();
}

void TerminalHost::applyProfile()
{
    if (!m_view) {
        return;
    }
    const TerminalProfile &p = currentProfile();
    m_view->setAppearance(p.appearance);
    const int lines = p.scrollback ? (*p.scrollback < 0 ? kUnlimitedScrollback : *p.scrollback) : scrollbackDefault();
    m_view->screen()->setScrollbackLimit(lines);
}

bool TerminalHost::isAlive() const
{
    return m_channel != nullptr;
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
    if (!m_view) {
        m_view = new TerminalView(this);
        m_view->setReservedShortcuts(appShortcuts());
        m_layout->addWidget(m_view);
        setFocusProxy(m_view);
        applyProfile();
    }
    // A reattach redraws everything; start from a clean screen and modes.
    m_view->reset();
    m_channel = m_direct ? m_direct(m_view->rows(), m_view->cols(), this) : nullptr;
    if (!m_channel) {
        QStringList argv = m_args;
        if (m_program.isEmpty()) {
            argv = currentProfile().shell;
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

void TerminalHost::respawn()
{
    if (isAlive()) {
        return;
    }
    spawn();
}

void TerminalHost::sendInput(const QString &text)
{
    if (m_channel) {
        m_channel->write(text.toUtf8());
    }
}

QString TerminalHost::foregroundProcessName() const
{
    if (auto *pty = qobject_cast<PtyChannel *>(m_channel)) {
        return pty->foregroundProcessName();
    }
    return QString();
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
    }
}
