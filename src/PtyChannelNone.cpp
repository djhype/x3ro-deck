#include "PtyChannel.h"

// Platforms without forkpty (Windows): local terminals arrive with ConPTY
// support. Until then a terminal says so instead of running anything.

PtyChannel::PtyChannel(QObject *parent)
    : TerminalChannel(parent)
{
}

PtyChannel::~PtyChannel() = default;

bool PtyChannel::start(const QStringList &argv, const QString &dir, int rows, int cols, const QMap<QString, QString> &env, QString *error)
{
    Q_UNUSED(argv)
    Q_UNUSED(dir)
    Q_UNUSED(rows)
    Q_UNUSED(cols)
    Q_UNUSED(env)
    if (error) {
        *error = QStringLiteral("Local terminals need ConPTY support, which this build of X3RO Deck does not have yet.");
    }
    return false;
}

void PtyChannel::write(const QByteArray &bytes)
{
    Q_UNUSED(bytes)
}

void PtyChannel::resize(int rows, int cols)
{
    Q_UNUSED(rows)
    Q_UNUSED(cols)
}

QString PtyChannel::foregroundProcessName() const
{
    return {};
}
