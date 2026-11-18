// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Small file and format helpers shared by the quick-command detectors.

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <algorithm>

namespace DetectorUtil
{
inline QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

inline bool exists(const QString &dir, const QString &name)
{
    return QFileInfo::exists(QDir(dir).filePath(name));
}

// The first of `names` that exists in `dir`, or empty.
inline QString firstExisting(const QString &dir, const QStringList &names)
{
    for (const QString &n : names) {
        if (exists(dir, n)) {
            return n;
        }
    }
    return {};
}

// A YAML or TOML scalar without its quotes or a trailing comment.
inline QString unquote(QString v)
{
    v = v.trimmed();
    if (!v.isEmpty() && (v.front() == QLatin1Char('"') || v.front() == QLatin1Char('\''))) {
        const qsizetype end = v.indexOf(v.front(), 1);
        return end < 0 ? v.mid(1) : v.mid(1, end - 1);
    }
    if (const qsizetype hash = v.indexOf(QStringLiteral(" #")); hash >= 0) {
        v.truncate(hash);
    }
    return v.trimmed();
}

// `object`'s keys in the order they appear in `json` after `after` (the
// enclosing key), since QJsonObject sorts them.
inline QStringList keysInFileOrder(const QByteArray &json, const QJsonObject &object, const QByteArray &after)
{
    QStringList names = object.keys();
    const qsizetype start = std::max<qsizetype>(0, json.indexOf(after));
    auto position = [&](const QString &name) {
        const QByteArray quoted = QJsonDocument(QJsonObject{{name, 0}}).toJson(QJsonDocument::Compact).mid(1).chopped(3);
        const qsizetype at = json.indexOf(quoted, start);
        return at < 0 ? json.size() : at;
    };
    std::stable_sort(names.begin(), names.end(), [&](const QString &a, const QString &b) { return position(a) < position(b); });
    return names;
}

// JSON with comments and trailing commas (deno.jsonc, tsconfig-style) as JSON.
inline QByteArray stripJsonc(const QByteArray &in)
{
    QByteArray out;
    out.reserve(in.size());
    bool str = false;
    for (qsizetype i = 0; i < in.size(); ++i) {
        const char c = in.at(i);
        if (str) {
            out += c;
            if (c == '\\' && i + 1 < in.size()) {
                out += in.at(++i);
            } else if (c == '"') {
                str = false;
            }
        } else if (c == '"') {
            str = true;
            out += c;
        } else if (c == '/' && i + 1 < in.size() && in.at(i + 1) == '/') {
            while (i < in.size() && in.at(i) != '\n') {
                ++i;
            }
            out += '\n';
        } else if (c == '/' && i + 1 < in.size() && in.at(i + 1) == '*') {
            const qsizetype end = in.indexOf("*/", i + 2);
            i = end < 0 ? in.size() : end + 1;
        } else {
            out += c;
        }
    }
    static const QRegularExpression trailing(QStringLiteral(R"re(,(\s*[}\]]))re"));
    return QString::fromUtf8(out).replace(trailing, QStringLiteral("\\1")).toUtf8();
}

// One `key = value` line of a TOML file and the table it sits in.
struct TomlEntry
{
    QString table; // "tool.poe.tasks"; quoted parts keep their dots: tasks."db:migrate" → tasks.db:migrate
    QStringList path; // the table's parts
    QString key;
    QString value; // raw, as written (the first line of a multi-line value)
};

// Table headers and key = value lines, in file order. Array-of-tables
// headers ([[x]]) count as tables named x.
inline QList<TomlEntry> tomlEntries(const QByteArray &toml, QStringList *tables = nullptr)
{
    static const QRegularExpression header(QStringLiteral(R"re(^\[\[?\s*(.+?)\s*\]\]?\s*(#.*)?$)re"));
    static const QRegularExpression entry(QStringLiteral(R"re(^("[^"]+"|'[^']+'|[A-Za-z0-9_.:-]+)\s*=\s*(.*)$)re"));
    static const QRegularExpression part(QStringLiteral(R"re("([^"]*)"|'([^']*)'|([^.\s]+))re"));
    QList<TomlEntry> out;
    QStringList path;
    for (const QString &raw : QString::fromUtf8(toml).split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (const QRegularExpressionMatch h = header.match(line); h.hasMatch()) {
            path.clear();
            for (auto it = part.globalMatch(h.captured(1)); it.hasNext();) {
                const QRegularExpressionMatch p = it.next();
                path.append(p.captured(1) + p.captured(2) + p.captured(3));
            }
            if (tables) {
                tables->append(path.join(QLatin1Char('.')));
            }
            continue;
        }
        if (const QRegularExpressionMatch e = entry.match(line); e.hasMatch()) {
            out.append({path.join(QLatin1Char('.')), path, unquote(e.captured(1)), e.captured(2).trimmed()});
        }
    }
    return out;
}
} // namespace DetectorUtil
