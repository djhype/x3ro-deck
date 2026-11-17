#include "SessiondProtocol.h"

#include <QJsonDocument>
#include <QtEndian>

#include <x3ro-deck-socket.h>

namespace Sessiond
{

QByteArray encode(quint8 type, const QByteArray &payload)
{
    QByteArray out(5, Qt::Uninitialized);
    qToBigEndian<quint32>(quint32(payload.size() + 1), out.data());
    out[4] = char(type);
    out.append(payload);
    return out;
}

QByteArray encodeControl(const QJsonObject &message)
{
    return encode(Control, QJsonDocument(message).toJson(QJsonDocument::Compact));
}

QByteArray encodeData(quint8 type, quint32 session, const QByteArray &bytes)
{
    QByteArray payload(4, Qt::Uninitialized);
    qToBigEndian<quint32>(session, payload.data());
    payload.append(bytes);
    return encode(type, payload);
}

bool takeFrame(QByteArray &buffer, Frame &out, bool *bad)
{
    if (buffer.size() < 4) {
        return false;
    }
    const quint32 length = qFromBigEndian<quint32>(buffer.constData());
    if (length == 0 || length > kMaxFrame) {
        if (bad) {
            *bad = true;
        }
        return false;
    }
    if (quint64(buffer.size()) < 4 + quint64(length)) {
        return false;
    }
    out.type = quint8(buffer.at(4));
    out.payload = buffer.mid(5, qsizetype(length) - 1);
    buffer.remove(0, qsizetype(length) + 4);
    return true;
}

bool splitData(const QByteArray &payload, quint32 &session, QByteArray &bytes)
{
    if (payload.size() < 4) {
        return false;
    }
    session = qFromBigEndian<quint32>(payload.constData());
    bytes = payload.mid(4);
    return true;
}

QJsonObject parseControl(const QByteArray &payload)
{
    return QJsonDocument::fromJson(payload).object();
}

QString socketPath()
{
    return QString::fromStdString(x3ro_deck::sessiondSocketPath().path);
}

} // namespace Sessiond
