#pragma once

#include <QFrame>
#include <QKeySequence>
#include <QPointer>
#include <QStringList>

class QKeyEvent;
class QLabel;
class QVBoxLayout;
class PtyChannel;
class TerminalInterface;
class TerminalView;
namespace KParts
{
class ReadOnlyPart;
}

// Hosts one terminal: an embedded Konsole (konsolepart), which uses your
// Konsole profile, or the built-in TerminalView running the program in a
// local PTY (config "terminal": "builtin").
class TerminalHost : public QFrame
{
    Q_OBJECT
public:
    // A login shell in `dir`.
    static TerminalHost *shell(const QString &dir, const QString &profile, QWidget *parent = nullptr);
    // `args` includes argv[0], as TerminalInterface::startProgram expects.
    static TerminalHost *program(const QString &program, const QStringList &args, const QString &profile, QWidget *parent = nullptr);

    // Keys the window handles itself; the terminal lets them through.
    static void setAppShortcuts(const QList<QKeySequence> &shortcuts);
    // Whether terminals created from now on are built-in instead of konsolepart.
    static void setBuiltin(bool builtin);
    // History lines a built-in terminal created from now on keeps.
    static void setScrollbackLines(int lines);
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

private Q_SLOTS:
    void onOverrideShortcut(QKeyEvent *event, bool &override);

private:
    TerminalHost(QWidget *parent);
    void spawn();
    void spawnKonsole();
    void spawnBuiltin();
    void showError(const QString &text);

    QString m_dir;
    QString m_program;
    QStringList m_args;
    QString m_profile;
    QPointer<KParts::ReadOnlyPart> m_part;
    TerminalInterface *m_term = nullptr;
    bool m_builtin = false;
    TerminalView *m_view = nullptr;
    PtyChannel *m_pty = nullptr;
    QVBoxLayout *m_layout;
    QLabel *m_placeholder;
};
