#pragma once

#include <QByteArray>
#include <QObject>

// The byte stream behind a TerminalView: what the program prints comes in
// through received(), keystrokes go out through write(). PtyChannel runs a
// local program; other transports (the sessiond stream) implement the same.
class TerminalChannel : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    virtual void write(const QByteArray &bytes) = 0;
    virtual void resize(int rows, int cols) = 0;
    virtual bool isOpen() const = 0;

Q_SIGNALS:
    void received(const QByteArray &bytes);
    // The other end went away; nothing more arrives.
    void closed();
};
