// Notifier on Windows: toast notifications through C++/WinRT, with action
// buttons and body activation. Toasts need an AppUserModelID with a Start
// menu shortcut; the installer creates one, and a dev build makes its own.

#include "Notifier.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include <QStandardPaths>
#include <QUrlQuery>

#include <windows.h>

#include <propkey.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>

namespace
{
const wchar_t kAumid[] = L"life.x3ro.deck";

// %APPDATA%\Microsoft\Windows\Start Menu\Programs\x3ro deck.lnk carrying the
// AUMID, so Windows shows toasts for an unpackaged app.
bool ensureShortcut()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    const QString link = QDir(dir).filePath(QStringLiteral("x3ro deck.lnk"));
    if (QFile::exists(link)) {
        return true;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellLinkW *shell = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shell)))) {
        return false;
    }
    const std::wstring exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
    shell->SetPath(exe.c_str());
    bool ok = false;
    IPropertyStore *store = nullptr;
    if (SUCCEEDED(shell->QueryInterface(IID_PPV_ARGS(&store)))) {
        PROPVARIANT pv;
        if (SUCCEEDED(InitPropVariantFromString(kAumid, &pv))) {
            ok = SUCCEEDED(store->SetValue(PKEY_AppUserModel_ID, pv)) && SUCCEEDED(store->Commit());
            PropVariantClear(&pv);
        }
        store->Release();
    }
    IPersistFile *file = nullptr;
    if (ok && SUCCEEDED(shell->QueryInterface(IID_PPV_ARGS(&file)))) {
        QDir().mkpath(dir);
        ok = SUCCEEDED(file->Save(QDir::toNativeSeparators(link).toStdWString().c_str(), TRUE));
        file->Release();
    }
    shell->Release();
    return ok;
}

QString escapeXml(QString s)
{
    return s.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
        .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
        .replace(QLatin1Char('>'), QStringLiteral("&gt;"))
        .replace(QLatin1Char('"'), QStringLiteral("&quot;"));
}

QString arguments(const QString &tag, const QString &action)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("tag"), tag);
    q.addQueryItem(QStringLiteral("action"), action);
    return escapeXml(q.toString(QUrl::FullyEncoded));
}

class WinNotifier : public Notifier
{
public:
    explicit WinNotifier(QObject *parent)
        : Notifier(parent)
    {
        try {
            winrt::init_apartment(winrt::apartment_type::single_threaded);
        } catch (...) {
            // Qt already initialized COM on this thread; that is fine.
        }
        if (!ensureShortcut()) {
            qWarning("x3ro-deck: cannot create the Start menu shortcut toasts need; showing plain tray messages");
            return;
        }
        try {
            m_notifier = winrt::Windows::UI::Notifications::ToastNotificationManager::CreateToastNotifier(kAumid);
            m_usable = true;
        } catch (const winrt::hresult_error &e) {
            qWarning("x3ro-deck: toasts unavailable (%s); showing plain tray messages", qPrintable(QString::fromWCharArray(e.message().c_str())));
        }
    }

    bool isNative() const override { return m_usable; }

    void show(const Notification &n, const QString &tag) override
    {
        if (!m_usable) {
            return;
        }
        QString xml = QStringLiteral("<toast launch=\"%1\"%2><visual><binding template=\"ToastGeneric\"><text>%3</text><text>%4</text></binding></visual>")
                          .arg(arguments(tag, defaultAction()),
                               n.urgency == Urgency::Critical ? QStringLiteral(" scenario=\"reminder\"") : QString(), escapeXml(n.title),
                               escapeXml(n.body));
        if (!n.actions.isEmpty()) {
            xml += QStringLiteral("<actions>");
            for (const Action &a : n.actions) {
                xml += QStringLiteral("<action content=\"%1\" arguments=\"%2\" activationType=\"foreground\"/>")
                           .arg(escapeXml(a.label), arguments(tag, a.key));
            }
            xml += QStringLiteral("</actions>");
        }
        xml += QStringLiteral("</toast>");
        try {
            winrt::Windows::Data::Xml::Dom::XmlDocument doc;
            doc.LoadXml(winrt::hstring(xml.toStdWString()));
            winrt::Windows::UI::Notifications::ToastNotification toast(doc);
            toast.Tag(winrt::hstring(tag.left(64).toStdWString()));
            QPointer<WinNotifier> self(this);
            // Foreground activation reaches the running app through this event.
            toast.Activated([self](const auto &, const winrt::Windows::Foundation::IInspectable &args) {
                const auto activated = args.as<winrt::Windows::UI::Notifications::ToastActivatedEventArgs>();
                const QUrlQuery q(QString::fromWCharArray(activated.Arguments().c_str()));
                const QString tag = q.queryItemValue(QStringLiteral("tag"), QUrl::FullyDecoded);
                const QString action = q.queryItemValue(QStringLiteral("action"), QUrl::FullyDecoded);
                QMetaObject::invokeMethod(qApp, [self, tag, action] {
                    if (self) {
                        Q_EMIT self->actionInvoked(tag, action);
                    }
                }, Qt::QueuedConnection);
            });
            toast.Dismissed([self, tag](const auto &, const auto &) {
                QMetaObject::invokeMethod(qApp, [self, tag] {
                    if (self) {
                        Q_EMIT self->closed(tag);
                    }
                }, Qt::QueuedConnection);
            });
            m_notifier.Show(toast);
        } catch (const winrt::hresult_error &e) {
            qWarning("x3ro-deck: cannot show a toast: %s", qPrintable(QString::fromWCharArray(e.message().c_str())));
        }
    }

private:
    winrt::Windows::UI::Notifications::ToastNotifier m_notifier{nullptr};
    bool m_usable = false;
};
} // namespace

Notifier *Notifier::create(QObject *parent)
{
    return new WinNotifier(parent);
}
