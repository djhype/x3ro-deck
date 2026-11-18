// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AppTheme.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStyleHints>

#include <algorithm>

namespace
{
QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString builtinPath(const QString &id)
{
    return QStringLiteral(":/data/themes/%1.json").arg(id);
}
} // namespace

QString AppTheme::userDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/x3ro-deck/themes");
}

AppTheme::AppTheme(QObject *parent)
    : QObject(parent)
{
    reset();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme scheme) { setSystemDark(scheme != Qt::ColorScheme::Light); });
#endif
    m_systemDark = desktopIsDark();
}

void AppTheme::reset()
{
    m_name.clear();
    m_colors.clear();
    m_checkoutColors.clear();
    m_fonts.clear();
    if (!apply(readFile(builtinPath(builtinDefault())))) {
        qWarning("x3ro-deck: the built-in theme is missing");
    }
}

bool AppTheme::desktopIsDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
    }
#endif
    return true; // Qt 6.4 cannot tell; Cyberdecker is dark
}

QList<AppTheme::Info> AppTheme::available()
{
    QList<Info> out;
    QDir builtin(QStringLiteral(":/data/themes"));
    for (const QString &file : builtin.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        AppTheme t;
        t.apply(readFile(builtin.filePath(file)));
        out.append({file.chopped(5), t.name(), true});
    }
    // The default first, its light companion next, the rest by name.
    std::stable_partition(out.begin(), out.end(), [](const Info &i) { return i.id == builtinLight(); });
    std::stable_partition(out.begin(), out.end(), [](const Info &i) { return i.id == builtinDefault(); });
    QDir user(userDir());
    for (const QString &file : user.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        AppTheme t;
        QString error;
        if (!t.apply(readFile(user.filePath(file)), &error)) {
            qWarning("x3ro-deck: skipping theme %s: %s", qPrintable(user.filePath(file)), qPrintable(error));
            continue;
        }
        const Info info{file.chopped(5), t.name().isEmpty() ? file.chopped(5) : t.name(), false};
        out.removeIf([&](const Info &i) { return i.id == info.id; });
        out.append(info);
    }
    return out;
}

bool AppTheme::load(const QString &id, QString *error)
{
    const QString wanted = id.isEmpty() ? builtinDefault() : id;
    QString path = userDir() + QLatin1Char('/') + wanted + QStringLiteral(".json");
    if (!QFile::exists(path)) {
        path = builtinPath(wanted);
    }
    if (!QFile::exists(path)) {
        if (error) {
            *error = QStringLiteral("no theme named %1 in %2").arg(wanted, userDir());
        }
        return false;
    }
    // Overlay a fresh default, so a broken file changes nothing.
    AppTheme next;
    QString why;
    if (!next.apply(readFile(path), &why)) {
        if (error) {
            *error = QStringLiteral("%1: %2").arg(path, why);
        }
        return false;
    }
    const bool same
        = m_name == next.m_name && m_colors == next.m_colors && m_checkoutColors == next.m_checkoutColors && m_fonts == next.m_fonts;
    m_name = next.m_name;
    m_colors = next.m_colors;
    m_checkoutColors = next.m_checkoutColors;
    m_fonts = next.m_fonts;
    if (!same) {
        Q_EMIT changed();
    }
    return true;
}

void AppTheme::select(const QString &id, const QString &light, const QString &dark)
{
    m_followSystem = id == system();
    m_light = light.isEmpty() ? builtinLight() : light;
    m_dark = dark.isEmpty() ? builtinDefault() : dark;
    const QString wanted = m_followSystem ? (m_systemDark ? m_dark : m_light) : id;
    if (QString error; !load(wanted, &error)) {
        qWarning("x3ro-deck: theme %s: %s; using the default", qPrintable(wanted), qPrintable(error));
        load(m_followSystem && !m_systemDark ? builtinLight() : builtinDefault());
    }
}

void AppTheme::setSystemDark(bool dark)
{
    if (dark == m_systemDark) {
        return;
    }
    m_systemDark = dark;
    if (m_followSystem) {
        select(system(), m_light, m_dark);
    }
}

bool AppTheme::apply(const QByteArray &json, QString *error)
{
    QJsonParseError parse{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parse);
    if (!doc.isObject()) {
        if (error) {
            *error = parse.error != QJsonParseError::NoError ? parse.errorString() : QStringLiteral("not a JSON object");
        }
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("name"))) {
        m_name = root.value(QStringLiteral("name")).toString();
    }
    const QJsonObject colors = root.value(QStringLiteral("colors")).toObject();
    for (auto it = colors.begin(); it != colors.end(); ++it) {
        const QColor c = QColor::fromString(it.value().toString());
        if (!c.isValid()) {
            if (error) {
                *error = QStringLiteral("colors.%1 is not a color").arg(it.key());
            }
            return false;
        }
        m_colors.insert(it.key(), c.name(c.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb));
    }
    if (root.contains(QStringLiteral("checkoutColors"))) {
        QStringList list;
        for (const QJsonValue &v : root.value(QStringLiteral("checkoutColors")).toArray()) {
            if (QColor::fromString(v.toString()).isValid()) {
                list.append(v.toString());
            }
        }
        if (!list.isEmpty()) {
            m_checkoutColors = list;
        }
    }
    const QJsonObject fonts = root.value(QStringLiteral("fonts")).toObject();
    for (auto it = fonts.begin(); it != fonts.end(); ++it) {
        if (!it.value().toString().isEmpty()) {
            m_fonts.insert(it.key(), it.value().toString());
        }
    }
    return true;
}

bool AppTheme::preview(const QJsonObject &theme, QString *error)
{
    AppTheme next;
    if (!next.apply(QJsonDocument(theme).toJson(), error)) {
        return false;
    }
    m_name = next.m_name;
    m_colors = next.m_colors;
    m_checkoutColors = next.m_checkoutColors;
    m_fonts = next.m_fonts;
    Q_EMIT changed();
    return true;
}

QJsonObject AppTheme::toJson() const
{
    return QJsonObject{{QStringLiteral("name"), m_name},
                       {QStringLiteral("colors"), QJsonObject::fromVariantMap(m_colors)},
                       {QStringLiteral("checkoutColors"), QJsonArray::fromStringList(m_checkoutColors)},
                       {QStringLiteral("fonts"), QJsonObject::fromVariantMap(m_fonts)}};
}

bool AppTheme::isBuiltin(const QString &id)
{
    return QFile::exists(builtinPath(id));
}

QString AppTheme::idFor(const QString &name, const QString &keep)
{
    QString base;
    for (const QChar c : name.toLower()) {
        if (c.isLetterOrNumber()) {
            base += c;
        } else if (!base.isEmpty() && !base.endsWith(QLatin1Char('-'))) {
            base += QLatin1Char('-');
        }
    }
    while (base.endsWith(QLatin1Char('-'))) {
        base.chop(1);
    }
    if (base.isEmpty()) {
        base = QStringLiteral("theme");
    }
    QString id = base;
    for (int n = 2; id != keep && (isBuiltin(id) || QFile::exists(userDir() + QLatin1Char('/') + id + QStringLiteral(".json"))); ++n) {
        id = QStringLiteral("%1-%2").arg(base).arg(n);
    }
    return id;
}

bool AppTheme::save(const QString &id, const QJsonObject &theme, QString *error)
{
    QDir().mkpath(userDir());
    QSaveFile f(userDir() + QLatin1Char('/') + id + QStringLiteral(".json"));
    if (f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(theme).toJson(QJsonDocument::Indented)) >= 0 && f.commit()) {
        return true;
    }
    if (error) {
        *error = f.errorString();
    }
    return false;
}

QColor AppTheme::color(const QString &token) const
{
    return QColor::fromString(m_colors.value(token).toString());
}

QString AppTheme::fill(const QString &tmpl, const QString &iconDir) const
{
    static const QRegularExpression re(QStringLiteral(R"(\$\{([^}]+)\})"));
    QString out;
    qsizetype last = 0;
    for (auto it = re.globalMatch(tmpl); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += QStringView(tmpl).mid(last, m.capturedStart() - last);
        last = m.capturedEnd();
        const QString key = m.captured(1);
        if (key.startsWith(QLatin1String("font."))) {
            out += m_fonts.value(key.mid(5)).toString();
        } else if (key.startsWith(QLatin1String("icon:"))) {
            const QString name = key.mid(5);
            const QString target = iconDir + QLatin1Char('/') + name + QStringLiteral(".svg");
            QSaveFile f(target);
            if (QDir().mkpath(iconDir) && f.open(QIODevice::WriteOnly)) {
                f.write(fill(QString::fromUtf8(readFile(QStringLiteral(":/data/ui/%1.svg").arg(name)))).toUtf8());
                f.commit();
            }
            out += target;
        } else {
            const qsizetype slash = key.indexOf(QLatin1Char('/'));
            QColor c = color(slash < 0 ? key : key.left(slash));
            if (!c.isValid()) {
                qWarning("x3ro-deck: the theme has no color %s", qPrintable(key));
                continue;
            }
            if (slash >= 0) {
                out += QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(key.mid(slash + 1));
            } else {
                out += c.name();
            }
        }
    }
    out += QStringView(tmpl).mid(last);
    return out;
}

QString AppTheme::styleSheet() const
{
    // Keyed by the colors, so two themes (or two instances) never share stale icons.
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QJsonDocument(QJsonObject::fromVariantMap(m_colors)).toJson(QJsonDocument::Compact));
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/x3ro-deck/theme/")
        + QString::fromLatin1(hash.result().toHex().left(12));
    return fill(QString::fromUtf8(readFile(QStringLiteral(":/data/style.qss"))), dir);
}
