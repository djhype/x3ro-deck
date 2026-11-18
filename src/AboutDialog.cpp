#include "AboutDialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace
{
QLabel *label(const QString &text, const char *name = nullptr)
{
    auto *l = new QLabel(text);
    if (name) {
        l->setObjectName(QLatin1String(name));
    }
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextBrowserInteraction);
    l->setOpenExternalLinks(true);
    return l;
}

// Style sheets cannot color links, and a styled widget ignores its palette's.
QColor g_link;

QString link(const QString &url)
{
    const QString style = g_link.isValid() ? QStringLiteral(" style=\"color: %1\"").arg(g_link.name()) : QString();
    return QStringLiteral("<a href=\"%1\"%2>%1</a>").arg(url.toHtmlEscaped(), style);
}

QPlainTextEdit *licenseView(const QString &text)
{
    auto *view = new QPlainTextEdit(text);
    view->setReadOnly(true);
    view->setProperty("mono", true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    return view;
}

// One third-party component: name, license and where its source is, then the license text.
QWidget *notice(const QString &title, const QString &details, const QString &resource)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(label(title, "section"));
    layout->addWidget(label(details, "hint"));
    layout->addWidget(licenseView(AboutDialog::text(resource)), 1);
    return page;
}
} // namespace

QString AboutDialog::version()
{
    return QStringLiteral(X3RO_DECK_VERSION);
}

QString AboutDialog::sourceUrl()
{
    return QStringLiteral(X3RO_DECK_SOURCE_URL);
}

QString AboutDialog::text(const QString &resource)
{
    QFile f(resource);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

AboutDialog::AboutDialog(const QColor &linkColor, QWidget *parent)
    : DeckDialog(parent)
{
    setWindowTitle(QStringLiteral("About x3ro deck"));
    g_link = linkColor;
    resize(760, 620);

    auto *about = new QWidget;
    auto *aboutLayout = new QVBoxLayout(about);
    aboutLayout->setContentsMargins(20, 20, 20, 20);
    aboutLayout->setSpacing(10);
    auto *icon = new QLabel;
    icon->setPixmap(QIcon(QStringLiteral(":/data/icons/x3ro-deck.svg")).pixmap(72, 72));
    aboutLayout->addWidget(icon);
    auto *name = label(QStringLiteral("x3ro deck"), "title");
    aboutLayout->addWidget(name);
    aboutLayout->addWidget(label(credit()));
    aboutLayout->addWidget(
        label(QStringLiteral("Version %1 · built with Qt %2, running on Qt %3").arg(version(), QStringLiteral(QT_VERSION_STR), qVersion()),
              "hint"));
    aboutLayout->addSpacing(6);
    aboutLayout->addWidget(label(QStringLiteral("Source code: ") + link(sourceUrl())));
    aboutLayout->addWidget(
        label(QStringLiteral("This program is free software: you can redistribute it and/or modify it under the terms of the "
                             "GNU General Public License, version 3 or (at your option) any later version, with the "
                             "additional terms in its section 7 (see License). It comes with ABSOLUTELY NO WARRANTY."),
              "hint"));
    aboutLayout->addStretch();

    auto *thirdParty = new QTabWidget;
    thirdParty->setObjectName(QStringLiteral("settingsTabs"));
    thirdParty->addTab(notice(QStringLiteral("Qt %1").arg(qVersion()),
                              QStringLiteral("The Qt Company Ltd. and contributors. Used under the GNU Lesser General Public License v3 "
                                             "and linked dynamically, so you can replace it. Source: ")
                                  + link(QStringLiteral("https://download.qt.io/official_releases/qt/")),
                              QStringLiteral(":/data/licenses/LGPL-3.0.txt")),
                       QStringLiteral("Qt (LGPL-3.0)"));
    thirdParty->addTab(notice(QStringLiteral("libvterm"),
                              QStringLiteral("Paul Evans. The terminal emulation of the built-in terminal and x3ro-deck-sessiond. "
                                             "MIT License. Source: ")
                                  + link(QStringLiteral("https://www.leonerd.org.uk/code/libvterm/")),
                              QStringLiteral(":/data/licenses/libvterm-MIT.txt")),
                       QStringLiteral("libvterm (MIT)"));
    auto *thirdPartyPage = new QWidget;
    auto *thirdPartyLayout = new QVBoxLayout(thirdPartyPage);
    thirdPartyLayout->setContentsMargins(20, 12, 20, 20);
    thirdPartyLayout->addWidget(thirdParty);

    auto *licensePage = new QWidget;
    auto *licenseLayout = new QVBoxLayout(licensePage);
    licenseLayout->setContentsMargins(20, 20, 20, 20);
    licenseLayout->addWidget(licenseView(text(QStringLiteral(":/LICENSE"))));

    auto *tabs = new QTabWidget;
    tabs->setObjectName(QStringLiteral("settingsTabs"));
    tabs->addTab(about, QStringLiteral("About"));
    tabs->addTab(licensePage, QStringLiteral("License"));
    tabs->addTab(thirdPartyPage, QStringLiteral("Third-party notices"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tabs, 1);
    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(16, 0, 16, 16);
    buttonRow->addWidget(buttons);
    layout->addLayout(buttonRow);
}
