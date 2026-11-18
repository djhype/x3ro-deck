// Notifier on macOS: UNUserNotificationCenter, with a category per set of
// action buttons. Notification actions need a signed app bundle; where the
// center refuses (an unsigned or unbundled dev build), isNative() turns false
// and MainWindow falls back to plain tray messages.

#include "Notifier.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QSet>

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

class MacNotifier;

@interface DeckNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@property(nonatomic, assign) QPointer<MacNotifier> *owner;
@end

class MacNotifier : public Notifier
{
public:
    explicit MacNotifier(QObject *parent);
    ~MacNotifier() override;
    void show(const Notification &notification, const QString &tag) override;
    bool isNative() const override { return m_usable; }

    void responded(const QString &tag, const QString &key)
    {
        if (key == defaultAction()) {
            Q_EMIT actionInvoked(tag, key);
        } else if (key == QLatin1String("dismiss")) {
            Q_EMIT closed(tag);
        } else {
            Q_EMIT actionInvoked(tag, key);
        }
    }

private:
    QString categoryFor(const Notification &n);

    bool m_usable = false;
    QSet<QString> m_categories;
    DeckNotificationDelegate *m_delegate = nil;
    QPointer<MacNotifier> m_self;
};

@implementation DeckNotificationDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler
{
    Q_UNUSED(center)
    Q_UNUSED(notification)
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionSound);
}

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
    didReceiveNotificationResponse:(UNNotificationResponse *)response
             withCompletionHandler:(void (^)(void))completionHandler
{
    Q_UNUSED(center)
    NSString *tag = response.notification.request.content.userInfo[@"tag"];
    NSString *action = response.actionIdentifier;
    QString key = QString::fromNSString(action);
    if ([action isEqualToString:UNNotificationDefaultActionIdentifier]) {
        key = Notifier::defaultAction();
    } else if ([action isEqualToString:UNNotificationDismissActionIdentifier]) {
        key = QStringLiteral("dismiss");
    }
    const QString t = QString::fromNSString(tag ?: @"");
    QPointer<MacNotifier> owner = self.owner ? *self.owner : QPointer<MacNotifier>();
    if (owner) {
        QMetaObject::invokeMethod(owner, [owner, t, key] {
            if (owner) {
                owner->responded(t, key);
            }
        }, Qt::QueuedConnection);
    }
    completionHandler();
}
@end

MacNotifier::MacNotifier(QObject *parent)
    : Notifier(parent)
    , m_self(this)
{
    // Without a bundle identifier the center throws; plain messages then.
    if (NSBundle.mainBundle.bundleIdentifier == nil) {
        qWarning("x3ro-deck: notifications need the app bundle; showing plain tray messages");
        return;
    }
    m_usable = true;
    m_delegate = [[DeckNotificationDelegate alloc] init];
    m_delegate.owner = &m_self;
    UNUserNotificationCenter *center = UNUserNotificationCenter.currentNotificationCenter;
    center.delegate = m_delegate;
    QPointer<MacNotifier> self = this;
    [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                          completionHandler:^(BOOL granted, NSError *error) {
                              if (granted) {
                                  return;
                              }
                              const QString why = error ? QString::fromNSString(error.localizedDescription)
                                                        : QStringLiteral("not allowed in System Settings → Notifications");
                              QMetaObject::invokeMethod(qApp, [self, why] {
                                  qWarning("x3ro-deck: notifications unavailable (%s); an unsigned build cannot use "
                                           "UNUserNotificationCenter. Showing plain tray messages.",
                                           qPrintable(why));
                                  if (self) {
                                      self->m_usable = false;
                                  }
                              }, Qt::QueuedConnection);
                          }];
}

MacNotifier::~MacNotifier()
{
    if (m_delegate) {
        UNUserNotificationCenter.currentNotificationCenter.delegate = nil;
        m_delegate.owner = nullptr;
        [m_delegate release];
    }
}

QString MacNotifier::categoryFor(const Notification &n)
{
    if (n.actions.isEmpty()) {
        return {};
    }
    QStringList keys;
    for (const Action &a : n.actions) {
        keys.append(a.key);
    }
    const QString id = QStringLiteral("x3ro-deck.") + keys.join(QLatin1Char('.'));
    if (!m_categories.contains(id)) {
        m_categories.insert(id);
        UNUserNotificationCenter *center = UNUserNotificationCenter.currentNotificationCenter;
        NSMutableArray<UNNotificationAction *> *actions = [NSMutableArray array];
        for (const Action &a : n.actions) {
            const UNNotificationActionOptions options =
                a.key == QLatin1String("deny") ? UNNotificationActionOptionDestructive : UNNotificationActionOptionNone;
            [actions addObject:[UNNotificationAction actionWithIdentifier:a.key.toNSString() title:a.label.toNSString() options:options]];
        }
        UNNotificationCategory *category = [UNNotificationCategory categoryWithIdentifier:id.toNSString()
                                                                                  actions:actions
                                                                        intentIdentifiers:@[]
                                                                                  options:UNNotificationCategoryOptionCustomDismissAction];
        // setNotificationCategories replaces the set, so send all of them.
        [center getNotificationCategoriesWithCompletionHandler:^(NSSet<UNNotificationCategory *> *existing) {
            NSMutableSet *all = [NSMutableSet setWithSet:existing];
            [all addObject:category];
            [center setNotificationCategories:all];
        }];
    }
    return id;
}

void MacNotifier::show(const Notification &n, const QString &tag)
{
    if (!m_usable) {
        return;
    }
    UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
    content.title = n.title.toNSString();
    content.body = n.body.toNSString();
    content.userInfo = @{@"tag" : tag.toNSString()};
    if (n.urgency == Urgency::Critical) {
        content.sound = UNNotificationSound.defaultSound;
    }
    if (const QString category = categoryFor(n); !category.isEmpty()) {
        content.categoryIdentifier = category.toNSString();
    }
    UNNotificationRequest *request = [UNNotificationRequest requestWithIdentifier:tag.toNSString() content:content trigger:nil];
    [UNUserNotificationCenter.currentNotificationCenter addNotificationRequest:request withCompletionHandler:nil];
    [content release];
}

Notifier *Notifier::create(QObject *parent)
{
    return new MacNotifier(parent);
}
