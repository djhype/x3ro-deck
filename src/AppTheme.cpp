#include "AppTheme.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

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
    if (!apply(readFile(builtinPath(builtinDefault())))) {
        qWarning("x3ro-deck: the built-in theme is missing");
    }
}

bool AppTheme::load(const QString &id, QString *error)
{
    if (id.isEmpty() || id == builtinDefault()) {
        return true;
    }
    QString path = userDir() + QLatin1Char('/') + id + QStringLiteral(".json");
    if (!QFile::exists(path)) {
        path = builtinPath(id);
    }
    if (!QFile::exists(path)) {
        if (error) {
            *error = QStringLiteral("no theme named %1 in %2").arg(id, userDir());
        }
        return false;
    }
    // Overlay a copy, so a broken file leaves the default untouched.
    AppTheme next;
    next.m_name = m_name;
    next.m_colors = m_colors;
    next.m_checkoutColors = m_checkoutColors;
    next.m_fonts = m_fonts;
    QString why;
    if (!next.apply(readFile(path), &why)) {
        if (error) {
            *error = QStringLiteral("%1: %2").arg(path, why);
        }
        return false;
    }
    m_name = next.m_name;
    m_colors = next.m_colors;
    m_checkoutColors = next.m_checkoutColors;
    m_fonts = next.m_fonts;
    return true;
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
