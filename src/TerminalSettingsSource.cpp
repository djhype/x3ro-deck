#include "TerminalSettingsSource.h"

#include <QFile>

namespace
{
QString unescape(const QString &value)
{
    if (!value.contains(QLatin1Char('\\'))) {
        return value;
    }
    QString out;
    out.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c != QLatin1Char('\\') || i + 1 == value.size()) {
            out += c;
            continue;
        }
        switch (value.at(++i).unicode()) {
        case 's':
            out += QLatin1Char(' ');
            break;
        case 't':
            out += QLatin1Char('\t');
            break;
        case 'n':
            out += QLatin1Char('\n');
            break;
        case 'r':
            out += QLatin1Char('\r');
            break;
        default:
            out += value.at(i);
        }
    }
    return out;
}
} // namespace

IniGroups parseIni(const QByteArray &text)
{
    IniGroups groups;
    QString group;
    for (const QByteArray &raw : text.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';'))) {
            continue;
        }
        if (line.startsWith(QLatin1Char('['))) {
            // KConfig nests groups as [a][b]; only the flat form matters here.
            const qsizetype end = line.indexOf(QLatin1Char(']'));
            group = line.mid(1, end < 0 ? -1 : end - 1);
            continue;
        }
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            continue;
        }
        QString key = line.left(eq).trimmed();
        if (key.endsWith(QLatin1Char(']'))) {
            const qsizetype open = key.indexOf(QLatin1Char('['));
            if (key.mid(open + 1, 1) != QLatin1String("$")) {
                continue; // a translation
            }
            key = key.left(open).trimmed();
        }
        groups[group].insert(key, unescape(line.mid(eq + 1).trimmed()));
    }
    return groups;
}

IniGroups readIni(const QString &path, bool *ok)
{
    QFile file(path);
    const bool opened = file.open(QIODevice::ReadOnly);
    if (ok) {
        *ok = opened;
    }
    return opened ? parseIni(file.readAll()) : IniGroups();
}
