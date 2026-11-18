#include "ComposeFiles.h"

#include <QDir>
#include <QRegularExpression>

namespace ComposeFiles
{

QStringList detect(const QString &dir)
{
    static const QStringList base{QStringLiteral("compose.yaml"), QStringLiteral("compose.yml"), QStringLiteral("docker-compose.yaml"),
                                  QStringLiteral("docker-compose.yml")};
    static const QRegularExpression override(QStringLiteral(R"(^(docker-)?compose\.[^.]+(\.[^.]+)*\.ya?ml$)"));
    const QStringList names = QDir(dir).entryList(QDir::Files | QDir::Readable, QDir::Name);
    QStringList out;
    for (const QString &b : base) {
        if (names.contains(b)) {
            out.append(b);
        }
    }
    for (const QString &n : names) {
        if (!base.contains(n) && override.match(n).hasMatch()) {
            out.append(n);
        }
    }
    return out;
}

QString shellQuote(const QString &s)
{
    static const QRegularExpression safe(QStringLiteral(R"(^[A-Za-z0-9_./:=@%+,-]+$)"));
    if (safe.match(s).hasMatch()) {
        return s;
    }
    QString q = s;
    q.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

QString options(const QStringList &files, const QStringList &profiles, const QString &envFile, const QString &project)
{
    QString out;
    for (const QString &f : files) {
        out += QStringLiteral(" -f ") + shellQuote(f);
    }
    for (const QString &p : profiles) {
        out += QStringLiteral(" --profile ") + shellQuote(p);
    }
    if (!envFile.isEmpty()) {
        out += QStringLiteral(" --env-file ") + shellQuote(envFile);
    }
    if (!project.isEmpty()) {
        out += QStringLiteral(" -p ") + shellQuote(project);
    }
    return out;
}

} // namespace ComposeFiles
