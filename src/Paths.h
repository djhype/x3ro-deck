#pragma once

#include <QString>

// Path handling shared by checkout paths, hook cwds and session matching, so
// the same folder compares equal however it was spelled.
namespace Paths
{

enum class Style {
    Posix, // '/' only; drive letters mean nothing
    Windows, // '\' and '/' separate; "c:" is a drive
#if defined(Q_OS_WIN)
    Native = Windows,
#else
    Native = Posix,
#endif
};

// macOS and Windows volumes are case-insensitive by default.
constexpr Qt::CaseSensitivity nativeCase()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

// "~" and "~/x" become the home directory.
QString expandTilde(const QString &path);
// The home directory becomes "~", for display and config files.
QString tilde(const QString &path);

// Lexical cleanup only: '/' separators, no "." or ".." segments, no trailing
// separator, an upper-case drive letter on Windows.
QString clean(const QString &path, Style style = Style::Native);

// expandTilde, then the canonical path when it exists (symlinks resolved),
// else the absolute path, then clean().
QString normalize(const QString &path);

// Compare clean()ed paths.
bool same(const QString &a, const QString &b, Qt::CaseSensitivity cs = nativeCase(), Style style = Style::Native);
// Whether `path` is `root` or lies inside it. "/a/bc" is not inside "/a/b".
bool isWithin(const QString &path, const QString &root, Qt::CaseSensitivity cs = nativeCase(), Style style = Style::Native);

} // namespace Paths
