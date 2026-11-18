// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Paths.h"

#include <QDir>
#include <QFileInfo>

namespace Paths
{

QString expandTilde(const QString &path)
{
    if (path == QLatin1String("~") || path.startsWith(QLatin1String("~/"))) {
        return QDir::homePath() + path.mid(1);
    }
    return path;
}

QString tilde(const QString &path)
{
    const QString home = clean(QDir::homePath());
    if (isWithin(path, home)) {
        return QLatin1Char('~') + clean(path).mid(home.size());
    }
    return path;
}

QString clean(const QString &path, Style style)
{
    QString p = path;
    if (style == Style::Windows) {
        p.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (p.size() >= 2 && p.at(1) == QLatin1Char(':') && p.at(0).isLetter()) {
            p[0] = p.at(0).toUpper();
        }
    }
    p = QDir::cleanPath(p);
    // cleanPath keeps "C:/" and "/" but strips other trailing separators.
    if (style == Style::Windows && p.size() == 2 && p.at(1) == QLatin1Char(':')) {
        p += QLatin1Char('/');
    }
    return p;
}

QString normalize(const QString &path)
{
    if (path.isEmpty()) {
        return path;
    }
    const QFileInfo info(expandTilde(path));
    const QString canonical = info.canonicalFilePath();
    return clean(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool same(const QString &a, const QString &b, Qt::CaseSensitivity cs, Style style)
{
    return clean(a, style).compare(clean(b, style), cs) == 0;
}

bool isWithin(const QString &path, const QString &root, Qt::CaseSensitivity cs, Style style)
{
    if (path.isEmpty() || root.isEmpty()) {
        return false;
    }
    const QString p = clean(path, style);
    QString r = clean(root, style);
    if (p.compare(r, cs) == 0) {
        return true;
    }
    if (!r.endsWith(QLatin1Char('/'))) {
        r += QLatin1Char('/');
    }
    return p.startsWith(r, cs);
}

} // namespace Paths
