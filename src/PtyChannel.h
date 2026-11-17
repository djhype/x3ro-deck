#pragma once

#include "TerminalChannel.h"

#include <QMap>
#include <QStringList>

class QSocketNotifier;

// Runs a program in a local pseudo-terminal (forkpty), for the built-in
// terminal. The program sees TERM=xterm-256color and gets SIGWINCH on resize.
class PtyChannel : public TerminalChannel
{
    Q_OBJECT
public:
    explicit PtyChannel(QObject *parent = nullptr);
    ~PtyChannel() override;

    // `argv` includes the program; a bare name is looked up on PATH. `env` is
    // added to the app's environment. Returns false and sets `error` when the
    // program cannot start.
    bool start(const QStringList &argv, const QString &dir, int rows, int cols, const QMap<QString, QString> &env = {},
               QString *error = nullptr);

    void write(const QByteArray &bytes) override;
    void resize(int rows, int cols) override;
    bool isOpen() const override { return m_fd >= 0; }

    qint64 pid() const { return m_pid; }
    // The process group in the terminal's foreground, by its command name.
    QString foregroundProcessName() const;
    // Valid after closed(): the exit status, or -1 when killed by a signal.
    int exitCode() const { return m_exitCode; }

private:
    void onReadable();
    void onWritable();
    void hangUp();
    void reap();

    int m_fd = -1;
    qint64 m_pid = -1;
    int m_exitCode = -1;
    QSocketNotifier *m_readNotifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
    QByteArray m_pending;
};
