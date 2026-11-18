#pragma once

#include <QList>
#include <QObject>
#include <QString>

// Desktop notifications with actions. One backend per OS; create() picks it.
// Callers tag each notification and get the tag back with every event.
class Notifier : public QObject
{
    Q_OBJECT
public:
    enum class Urgency {
        Low,
        Normal,
        Critical,
    };
    struct Action
    {
        QString key;
        QString label;
    };
    struct Notification
    {
        QString title;
        QString body; // plain text
        Urgency urgency = Urgency::Normal;
        QList<Action> actions; // buttons, in order
        QString defaultLabel = QStringLiteral("Open"); // the body click; empty for none
    };
    // The action key a click on the body reports.
    static QString defaultAction() { return QStringLiteral("default"); }

    // The platform's backend, or one that shows nothing.
    static Notifier *create(QObject *parent = nullptr);
    // false for the stand-in that shows nothing; the tray shows a plain message instead.
    virtual bool isNative() const { return true; }

    using QObject::QObject;
    virtual void show(const Notification &notification, const QString &tag) = 0;

Q_SIGNALS:
    void actionInvoked(const QString &tag, const QString &key);
    // Sent just before actionInvoked when the desktop hands over the right
    // to raise a window (xdg-activation on Wayland).
    void activationToken(const QString &tag, const QString &token);
    void closed(const QString &tag);
};
