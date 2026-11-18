#pragma once

#include "DeckDialog.h"

#include <QColor>

// Credit, version, license and third-party notices. LICENSE's section 7(b)
// term requires modified versions to keep the credit shown here.
class AboutDialog : public DeckDialog
{
    Q_OBJECT
public:
    explicit AboutDialog(const QColor &linkColor = QColor(), QWidget *parent = nullptr);

    static QString credit() { return QStringLiteral("x3ro deck by x3ro, licensed GPL-3.0-or-later"); }
    static QString version();
    static QString sourceUrl();
    // A resource's text (":/LICENSE", ":/data/licenses/…").
    static QString text(const QString &resource);
};
