#pragma once

#include <QList>
#include <QString>

// TCP ports the user's own processes listen on, with each process's name and
// working directory. Linux reads /proc; other systems report nothing yet.
namespace ListeningPorts
{
struct Listener
{
    int pid = 0;
    QString name; // /proc/<pid>/comm
    QString cwd;
    int port = 0;
};

QList<Listener> scan();

// Ports in LISTEN state owned by `uid` → socket inode, from /proc/net/tcp(6) text.
QList<QPair<int, quint64>> parseNetTcp(const QByteArray &table, uint uid);
} // namespace ListeningPorts
