#pragma once

#include <QColor>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

// The UI's colors and fonts, from a JSON theme. QML reads them through the
// context property `appTheme` (qml/Theme.qml); the widget style sheet and its
// icons are templates filled from the same tokens.
class AppTheme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QVariantMap colors READ colors CONSTANT)
    Q_PROPERTY(QStringList checkoutColors READ checkoutColors CONSTANT)
    Q_PROPERTY(QVariantMap fonts READ fonts CONSTANT)

public:
    static QString builtinDefault() { return QStringLiteral("cyberdecker"); }
    // ~/.config/x3ro-deck/themes
    static QString userDir();

    explicit AppTheme(QObject *parent = nullptr);

    // Starts from the built-in default and overlays the theme `id`: a file in
    // userDir() or a built-in one. Keys the theme leaves out keep the
    // default's values. Returns false (keeping the default) when `id` is
    // unknown or unreadable, with the reason in `error`.
    bool load(const QString &id, QString *error = nullptr);
    // Overlays a theme document onto the current values.
    bool apply(const QByteArray &json, QString *error = nullptr);

    QString name() const { return m_name; }
    QVariantMap colors() const { return m_colors; }
    QStringList checkoutColors() const { return m_checkoutColors; }
    QVariantMap fonts() const { return m_fonts; }
    QColor color(const QString &token) const;

    // Fills ${token}, ${token/alpha}, ${font.name} and ${icon:name} in `tmpl`.
    // Icons come from :/data/ui/<name>.svg, recolored into `iconDir`.
    QString fill(const QString &tmpl, const QString &iconDir = QString()) const;
    // data/style.qss filled from the theme, its icons written under the cache directory.
    QString styleSheet() const;

private:
    QString m_name;
    QVariantMap m_colors;
    QStringList m_checkoutColors;
    QVariantMap m_fonts;
};
