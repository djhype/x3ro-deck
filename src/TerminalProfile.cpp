#include "TerminalProfile.h"

#ifdef Q_OS_UNIX
#include <pwd.h>
#include <unistd.h>
#endif

QString loginShell()
{
    const QString env = qEnvironmentVariable("SHELL");
    if (!env.isEmpty()) {
        return env;
    }
#ifdef Q_OS_UNIX
    if (const passwd *pw = getpwuid(getuid()); pw && pw->pw_shell && *pw->pw_shell) {
        return QString::fromLocal8Bit(pw->pw_shell);
    }
#endif
    return QStringLiteral("/bin/sh");
}

TerminalProfile TerminalProfile::fallback()
{
    TerminalProfile p;
    p.appearance = TerminalAppearance::fallback();
    p.shell = {loginShell()};
    return p;
}
