#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

// The wire format between x3ro-deck-sessiond and its clients (DaemonBackend,
// x3ro-deck-attach). QtCore only, so the attach client stays small.
//
// Every message is a frame: a 4-byte big-endian length covering what follows,
// a 1-byte type, then the payload. Readers skip frame types they don't know.
//
//   Control  a JSON object. A request has "op" and "id"; its reply has
//            "re" (the request's id), "ok" and, on failure, "error". An event
//            has "event" and no "re". Unknown fields are ignored and an
//            unknown op replies ok:false, so new ops and fields need no new
//            version.
//   Output   u32 session id, then bytes the session wrote. Sent to clients
//            that attached to the session.
//   Input    u32 session id, then bytes to write to the session's terminal.
//
// Ops (version 1):
//   hello    {version}                              → {version, pid}
//   create   {name, cwd, env:{}, command?:[], cols?, rows?} → {session}
//            Runs `command`, or the user's login shell, in a new PTY.
//   list     {processes?}                           → {sessions:[{id, name, cwd, env, pid,
//                                                       cols, rows, foreground,
//                                                       foregroundGroup?}]}
//            foreground is what tmux shows as pane_current_command. With
//            processes:true, foregroundGroup names the whole foreground job,
//            leader first (see ProcessInfo::foregroundNames).
//   write    {name|session, data}                   data is a UTF-8 string
//   resize   {name|session, cols, rows}             at most 1000 each; the last one wins
//   rename   {name, to}
//   kill     {name|session}
//   attach   {name|session, cols?, rows?, replay?}  → {session, cols, rows}; Output
//            frames follow. The session takes the given size first; then,
//            unless replay is false, the first frame redraws its current
//            screen (see SessionScreen.h) on a terminal of that size.
//   screen   {name|session}                         → {cols, rows, cursor:{row, col},
//                                                       cursorVisible, altScreen, title,
//                                                       lines:[text]}
//   processes {name|session}                        → {foreground: pgid, processes:[{pid, ppid,
//                                                       pgid, name, args:[]}]}
//            The session's shell and its descendants, parents first.
//   detach   {session}
//   shutdown {}                                     ends every session, then the daemon
// Events:
//   exited   {session, name, status}                to clients attached to the session
namespace Sessiond
{

constexpr int kProtocolVersion = 1;
constexpr quint32 kMaxFrame = 16 * 1024 * 1024;

enum FrameType : quint8 {
    Control = 1,
    Output = 2,
    Input = 3,
};

struct Frame
{
    quint8 type = 0;
    QByteArray payload;
};

QByteArray encode(quint8 type, const QByteArray &payload);
QByteArray encodeControl(const QJsonObject &message);
QByteArray encodeData(quint8 type, quint32 session, const QByteArray &bytes);

// Moves the first complete frame out of `buffer`. Returns false while none is
// complete; sets `*bad` when the length is over kMaxFrame.
bool takeFrame(QByteArray &buffer, Frame &out, bool *bad = nullptr);
// Splits an Output or Input payload.
bool splitData(const QByteArray &payload, quint32 &session, QByteArray &bytes);
QJsonObject parseControl(const QByteArray &payload);

// See sessiondSocketPath() in x3ro-deck-socket.h.
QString socketPath();

} // namespace Sessiond
