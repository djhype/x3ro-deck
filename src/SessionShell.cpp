#include "SessionShell.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef Q_OS_UNIX
#include <pwd.h>
#include <unistd.h>
#endif

namespace
{
QString executable(const QString &program, const QString &path)
{
    if (program.isEmpty()) {
        return {};
    }
    if (program.contains(QLatin1Char('/'))) {
        const QFileInfo fi(QDir::cleanPath(program));
        return fi.isFile() && fi.isExecutable() ? fi.absoluteFilePath() : QString();
    }
    return QStandardPaths::findExecutable(program, path.split(QLatin1Char(':'), Qt::SkipEmptyParts));
}
} // namespace

SessionShell SessionShell::resolve(const QStringList &shell, const QString &path)
{
    SessionShell s;
    if (const QString program = executable(shell.value(0), path); !program.isEmpty()) {
        s.program = program;
        s.argv = shell;
        s.login = shell.size() == 1;
        return s;
    }
    QStringList fallbacks{qEnvironmentVariable("SHELL")};
#ifdef Q_OS_UNIX
    if (const passwd *pw = getpwuid(getuid()); pw && pw->pw_shell && *pw->pw_shell) {
        fallbacks.append(QString::fromLocal8Bit(pw->pw_shell));
    }
#endif
    fallbacks.append(QStringLiteral("/bin/sh"));
    for (const QString &candidate : std::as_const(fallbacks)) {
        if (const QString program = executable(candidate, path); !program.isEmpty()) {
            s.program = program;
            s.argv = {candidate};
            s.login = true;
            return s;
        }
    }
    s.program = QStringLiteral("/bin/sh");
    s.argv = {s.program};
    s.login = true;
    return s;
}
