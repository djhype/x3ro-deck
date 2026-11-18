#include "Notifier.h"

namespace
{
// Until an OS has its own backend: notifications are dropped, and NEEDS YOU
// in the window and the tray badge still show the items.
class NoNotifier : public Notifier
{
public:
    using Notifier::Notifier;
    void show(const Notification &, const QString &) override { }
};
} // namespace

Notifier *Notifier::create(QObject *parent)
{
    return new NoNotifier(parent);
}
