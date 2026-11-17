#pragma once

#include "TerminalSettingsSource.h"

// Konsole: the default profile named in konsolerc, konsole/*.profile with
// their Parent= chain, and konsole/*.colorscheme. Konsole compiles its own
// color schemes into the binary, so Breeze, its default, is built in here.
class KonsoleSettings : public TerminalSettingsSource
{
public:
    struct Locations
    {
        QString configDir; // holds konsolerc
        QStringList dataDirs; // each may hold konsole/; earlier ones win
        QString executable; // the konsole binary, empty when not installed
        static Locations system();
    };

    explicit KonsoleSettings(Locations locations = Locations::system());

    QString id() const override { return QStringLiteral("konsole"); }
    QString displayName() const override { return QStringLiteral("Konsole"); }
    QStringList desktopEntries() const override;
    QStringList nativeDesktops() const override;
    bool detect() const override;
    QList<ProfileRef> profiles() const override;
    QString defaultProfile() const override;
    std::optional<TerminalProfile> load(const QString &profile) const override;
    QStringList watchedPaths(const QString &profile) const override;

    // Konsole's built-in profile, which no file describes.
    static QString builtinProfile() { return QStringLiteral("FALLBACK/"); }

private:
    QStringList konsoleDirs() const;
    QString profilePath(const QString &ref) const;
    QStringList profileChain(const QString &profile) const; // leaf first
    QString colorSchemePath(const QString &name) const;

    Locations m_loc;
};
