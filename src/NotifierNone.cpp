#include "Notifier.h"

namespace
{
// Until an OS has its own backend: MainWindow shows a plain tray message
// instead (no action buttons), and NEEDS YOU and the tray badge still show
// the items.
class NoNotifier : public Notifier
{
public:
    using Notifier::Notifier;
    void show(const Notification &, const QString &) override { }
    bool isNative() const override { return false; }
};
} // namespace

Notifier *Notifier::create(QObject *parent)
{
    return new NoNotifier(parent);
}
