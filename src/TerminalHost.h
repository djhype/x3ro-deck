#pragma once

#include <QFrame>
#include <QKeySequence>
#include <QPointer>
#include <QStringList>

class QKeyEvent;
class QLabel;
class QVBoxLayout;
class TerminalInterface;
namespace KParts
{
class ReadOnlyPart;
}

// Hosts one embedded Konsole (konsolepart), so every terminal uses your
// Konsole profile: zsh, powerlevel10k, fonts, colours and keybindings.
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
    ~TerminalHost() override;

    void sendInput(const QString &text);
    QString foregroundProcessName() const;
    bool isAlive() const { return !m_part.isNull(); }
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

    QString m_dir;
    QString m_program;
    QStringList m_args;
    QString m_profile;
    QPointer<KParts::ReadOnlyPart> m_part;
    TerminalInterface *m_term = nullptr;
    QVBoxLayout *m_layout;
    QLabel *m_placeholder;
};
