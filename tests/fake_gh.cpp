// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

// A stand-in for gh in tst_prwatcher, compiled so that it runs on every OS
// (Windows runs no #! scripts). It logs each call's arguments to
// $FAKE_GH_LOG; `auth status` exits with $FAKE_GH_AUTH (default 0),
// `repo view` prints "trunk" and `pr view` finds no pull request.

#include <QFile>
#include <QStringList>

#include <cstdio>

int main(int argc, char *argv[])
{
    QStringList args;
    for (int i = 1; i < argc; ++i) {
        args.append(QString::fromLocal8Bit(argv[i]));
    }
    QFile log(qEnvironmentVariable("FAKE_GH_LOG"));
    if (log.open(QIODevice::Append)) {
        log.write(args.join(QLatin1Char(' ')).toUtf8() + '\n');
        log.close();
    }
    const QString verb = args.mid(0, 2).join(QLatin1Char(' '));
    if (verb == QLatin1String("auth status")) {
        return qEnvironmentVariableIntValue("FAKE_GH_AUTH");
    }
    if (verb == QLatin1String("repo view")) {
        std::fputs("trunk\n", stdout);
        return 0;
    }
    if (verb == QLatin1String("pr view")) {
        std::fputs("no pull requests found for branch\n", stderr);
        return 1;
    }
    return 0;
}
