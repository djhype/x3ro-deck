#pragma once

#include <QFrame>
#include <QKeySequence>
#include <QStringList>

#include <functional>

struct TerminalProfile;
class QLabel;
class QVBoxLayout;
class TerminalChannel;
class TerminalView;

// Hosts one TerminalView, running the program in a local PTY or reading a
// direct channel.
class TerminalHost : public QFrame
{
    Q_OBJECT
public:
    // A login shell in `dir`.
    static TerminalHost *shell(const QString &dir, QWidget *parent = nullptr);
    // Opens a direct channel at the given grid size, or returns null to run
    // the program instead.
    using ChannelFactory = std::function<TerminalChannel *(int rows, int cols, QObject *parent)>;

    // `args` includes argv[0]. The terminal reads `direct`'s channel instead
    // when it gives one.
    static TerminalHost *program(const QString &program, const QStringList &args, QWidget *parent = nullptr, ChannelFactory direct = {});

    // Keys the window handles itself; the terminal lets them through.
    static void setAppShortcuts(const QList<QKeySequence> &shortcuts);
    // History lines a terminal keeps when the profile sets none.
    static void setScrollbackLines(int lines);
    // Imported terminal settings: open terminals take the appearance and
    // scrollback at once, and new shells run the profile's shell.
    static void setProfile(const TerminalProfile &profile);
    static const TerminalProfile &profile();
    ~TerminalHost() override;

    void sendInput(const QString &text);
    QString foregroundProcessName() const;
    bool isAlive() const;
    // Restart the program or shell after it exited.
    void respawn();
    void setAccent(const QString &state); // "", "attention", "focus"
    void focusTerminal();

Q_SIGNALS:
    void exited();

private:
    TerminalHost(QWidget *parent);
    void spawn();
    void showError(const QString &text);
    void applyProfile();

    QString m_dir;
    QString m_program;
    QStringList m_args;
    TerminalView *m_view = nullptr;
    ChannelFactory m_direct;
    TerminalChannel *m_channel = nullptr;
    QVBoxLayout *m_layout;
    QLabel *m_placeholder;
};
