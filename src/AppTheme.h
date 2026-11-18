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
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(QStringList checkoutColors READ checkoutColors NOTIFY changed)
    Q_PROPERTY(QVariantMap fonts READ fonts NOTIFY changed)

public:
    struct Info
    {
        QString id;
        QString name;
        bool builtin = false;
    };

    static QString builtinDefault() { return QStringLiteral("cyberdecker"); }
    static QString builtinLight() { return QStringLiteral("cyberdecker-light"); }
    // The config value that follows the desktop's light or dark mode.
    static QString system() { return QStringLiteral("system"); }
    // Built-in themes, then the valid files in userDir() (a user file with a
    // built-in's id replaces it). Invalid files are skipped with a log line.
    static QList<Info> available();
    // ~/.config/x3ro-deck/themes
    static QString userDir();

    explicit AppTheme(QObject *parent = nullptr);

    // Starts from the built-in default and overlays the theme `id`: a file in
    // userDir() or a built-in one. Keys the theme leaves out keep the
    // default's values. Returns false (changing nothing) when `id` is
    // unknown or unreadable, with the reason in `error`.
    bool load(const QString &id, QString *error = nullptr);
    // Shows `id`, or with system() `light` or `dark` by the desktop's color
    // scheme, following its changes. An unknown theme falls back to the default.
    void select(const QString &id, const QString &light = builtinLight(), const QString &dark = builtinDefault());
    bool followsSystem() const { return m_followSystem; }
    // The desktop switched between light and dark (also how tests simulate it).
    void setSystemDark(bool dark);
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

Q_SIGNALS:
    void changed();

private:
    void reset();
    static bool desktopIsDark();

    bool m_followSystem = false;
    bool m_systemDark = true;
    QString m_light;
    QString m_dark;
    QString m_name;
    QVariantMap m_colors;
    QStringList m_checkoutColors;
    QVariantMap m_fonts;
};
