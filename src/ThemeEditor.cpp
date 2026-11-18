// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ThemeEditor.h"

#include "AppTheme.h"
#include "DetectorUtil.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QVBoxLayout>

namespace
{
QLabel *sectionLabel(const QString &text)
{
    auto *label = new QLabel(text.toUpper());
    label->setObjectName(QStringLiteral("section"));
    return label;
}

QString colorText(const QColor &c)
{
    return c.name(c.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb);
}

// Token order as Cyberdecker's file lists them (backgrounds, lines, text,
// accents, cards), rather than QJsonObject's alphabetical one.
QStringList tokenOrder(const QJsonObject &colors)
{
    QFile f(QStringLiteral(":/data/themes/cyberdecker.json"));
    const QByteArray json = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    return DetectorUtil::keysInFileOrder(json, colors, "\"colors\"");
}
} // namespace

ThemeEditor::ThemeEditor(AppTheme *live, const QString &id, QWidget *parent)
    : DeckDialog(parent)
    , m_live(live)
{
    setWindowTitle(QStringLiteral("Edit theme"));
    resize(640, 760);

    AppTheme base;
    base.load(id);
    QJsonObject doc = base.toJson();
    if (AppTheme::isBuiltin(id)) {
        doc.insert(QStringLiteral("name"), QStringLiteral("%1 (copy)").arg(base.name()));
    } else {
        m_id = id;
    }

    m_name = new QLineEdit;
    auto *top = new QGridLayout;
    top->setHorizontalSpacing(14);
    top->addWidget(new QLabel(QStringLiteral("Name")), 0, 0, Qt::AlignRight | Qt::AlignVCenter);
    top->addWidget(m_name, 0, 1);
    auto *note = new QLabel(m_id.isEmpty() ? QStringLiteral("Built-in themes stay as they are; this edits a copy.")
                                           : QStringLiteral("Saves to %1/%2.json").arg(AppTheme::userDir(), m_id));
    note->setObjectName(QStringLiteral("hint"));
    note->setWordWrap(true);
    top->addWidget(note, 1, 1);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(2, 1);
    int row = 0;
    grid->addWidget(sectionLabel(QStringLiteral("Colors")), row++, 0, 1, 3);
    const QJsonObject colors = doc.value(QStringLiteral("colors")).toObject();
    for (const QString &token : tokenOrder(colors)) {
        auto *swatch = new QPushButton;
        swatch->setFixedSize(44, 24);
        swatch->setAutoDefault(false);
        auto *hex = new QLineEdit;
        hex->setProperty("mono", true);
        hex->setFixedWidth(120);
        m_swatches.insert(token, swatch);
        m_hex.insert(token, hex);
        grid->addWidget(new QLabel(token), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(swatch, row, 1);
        grid->addWidget(hex, row, 2, Qt::AlignLeft);
        ++row;
        connect(swatch, &QPushButton::clicked, this, [this, token] {
            pickColor(token, m_doc.value(QStringLiteral("colors")).toObject().value(token).toString(),
                      [this, token](const QColor &c) { setColor(token, c); });
        });
        connect(hex, &QLineEdit::editingFinished, this, [this, token, hex] {
            const QColor c = QColor::fromString(hex->text().trimmed());
            if (c.isValid()) {
                setColor(token, c);
            } else {
                hex->setText(m_doc.value(QStringLiteral("colors")).toObject().value(token).toString());
            }
        });
    }
    grid->addWidget(sectionLabel(QStringLiteral("Checkout colors")), row++, 0, 1, 3);
    auto *checkoutRow = new QHBoxLayout;
    checkoutRow->setSpacing(6);
    const QJsonArray checkoutColors = doc.value(QStringLiteral("checkoutColors")).toArray();
    for (int i = 0; i < checkoutColors.size(); ++i) {
        auto *swatch = new QPushButton;
        swatch->setFixedSize(44, 24);
        swatch->setAutoDefault(false);
        m_checkoutSwatches.append(swatch);
        checkoutRow->addWidget(swatch);
        connect(swatch, &QPushButton::clicked, this, [this, i] {
            pickColor(QStringLiteral("Checkout color %1").arg(i + 1),
                      m_doc.value(QStringLiteral("checkoutColors")).toArray().at(i).toString(),
                      [this, i](const QColor &c) { setCheckoutColor(i, c); });
        });
    }
    checkoutRow->addStretch();
    grid->addLayout(checkoutRow, row++, 1, 1, 2);
    grid->addWidget(sectionLabel(QStringLiteral("Fonts")), row++, 0, 1, 3);
    for (const QString &key : {QStringLiteral("sans"), QStringLiteral("mono"), QStringLiteral("display")}) {
        auto *box = new QFontComboBox;
        if (key == QLatin1String("mono")) {
            box->setFontFilters(QFontComboBox::MonospacedFonts);
        }
        m_fonts.insert(key, box);
        grid->addWidget(new QLabel(key), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(box, row++, 1, 1, 2);
        connect(box, &QFontComboBox::currentFontChanged, this, [this, key](const QFont &f) {
            if (!m_loading) {
                setFont(key, f.family());
            }
        });
    }

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("scrollContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 12, 0);
    contentLayout->addLayout(grid);
    contentLayout->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *importButton = new QPushButton(QStringLiteral("Import…"));
    auto *exportButton = new QPushButton(QStringLiteral("Export…"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(importButton);
    bottom->addWidget(exportButton);
    bottom->addStretch();
    bottom->addWidget(buttons);
    for (QPushButton *b : {importButton, exportButton}) {
        b->setAutoDefault(false);
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);
    layout->addLayout(top);
    layout->addWidget(scroll, 1);
    layout->addLayout(bottom);

    connect(m_name, &QLineEdit::textEdited, this, &ThemeEditor::setName);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (QString error; !save(&error)) {
            QMessageBox::warning(this, QStringLiteral("Save theme"), error);
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(importButton, &QPushButton::clicked, this, [this] {
        // Parented past this dialog, whose stylesheet would restyle the system file dialog.
        const QString path = QFileDialog::getOpenFileName(parentWidget(), QStringLiteral("Import theme"), QDir::homePath(),
                                                          QStringLiteral("Themes (*.json)"));
        if (QString error; !path.isEmpty() && !importFile(path, &error)) {
            QMessageBox::warning(this, QStringLiteral("Import theme"), error);
        }
    });
    connect(exportButton, &QPushButton::clicked, this, [this] {
        const QString name = (m_id.isEmpty() ? AppTheme::idFor(m_name->text()) : m_id) + QStringLiteral(".json");
        const QString path = QFileDialog::getSaveFileName(parentWidget(), QStringLiteral("Export theme"), QDir::home().filePath(name),
                                                          QStringLiteral("Themes (*.json)"));
        if (QString error; !path.isEmpty() && !exportFile(path, &error)) {
            QMessageBox::warning(this, QStringLiteral("Export theme"), error);
        }
    });

    load(doc);
    preview();
}

void ThemeEditor::load(const QJsonObject &doc)
{
    m_loading = true;
    m_doc = doc;
    m_name->setText(doc.value(QStringLiteral("name")).toString());
    const QJsonObject colors = doc.value(QStringLiteral("colors")).toObject();
    for (auto it = m_swatches.cbegin(); it != m_swatches.cend(); ++it) {
        refreshSwatch(it.value(), colors.value(it.key()).toString());
        m_hex.value(it.key())->setText(colors.value(it.key()).toString());
    }
    const QJsonArray checkout = doc.value(QStringLiteral("checkoutColors")).toArray();
    for (int i = 0; i < m_checkoutSwatches.size(); ++i) {
        refreshSwatch(m_checkoutSwatches.at(i), checkout.at(i).toString());
    }
    const QJsonObject fonts = doc.value(QStringLiteral("fonts")).toObject();
    for (auto it = m_fonts.cbegin(); it != m_fonts.cend(); ++it) {
        it.value()->setCurrentFont(QFont(fonts.value(it.key()).toString()));
    }
    m_loading = false;
}

void ThemeEditor::refreshSwatch(QPushButton *swatch, const QString &color)
{
    swatch->setStyleSheet(
        QStringLiteral("QPushButton { background: %1; border: 1px solid rgba(128, 128, 128, 0.6); border-radius: 6px; }").arg(color));
    swatch->setToolTip(color);
}

void ThemeEditor::pickColor(const QString &title, const QString &current, const std::function<void(const QColor &)> &apply)
{
    const QColor c = QColorDialog::getColor(QColor::fromString(current), parentWidget(), title, QColorDialog::ShowAlphaChannel);
    if (c.isValid()) {
        apply(c);
    }
}

void ThemeEditor::preview()
{
    if (m_live) {
        m_live->preview(m_doc);
    }
}

void ThemeEditor::setName(const QString &name)
{
    m_doc.insert(QStringLiteral("name"), name.trimmed());
    if (m_name->text() != name) {
        m_name->setText(name);
    }
}

void ThemeEditor::setColor(const QString &token, const QColor &color)
{
    QJsonObject colors = m_doc.value(QStringLiteral("colors")).toObject();
    colors.insert(token, colorText(color));
    m_doc.insert(QStringLiteral("colors"), colors);
    if (QPushButton *swatch = m_swatches.value(token)) {
        refreshSwatch(swatch, colorText(color));
        m_hex.value(token)->setText(colorText(color));
    }
    preview();
}

void ThemeEditor::setCheckoutColor(int index, const QColor &color)
{
    QJsonArray list = m_doc.value(QStringLiteral("checkoutColors")).toArray();
    if (index < 0 || index >= list.size()) {
        return;
    }
    list.replace(index, colorText(color));
    m_doc.insert(QStringLiteral("checkoutColors"), list);
    refreshSwatch(m_checkoutSwatches.value(index), colorText(color));
    preview();
}

void ThemeEditor::setFont(const QString &key, const QString &family)
{
    QJsonObject fonts = m_doc.value(QStringLiteral("fonts")).toObject();
    fonts.insert(key, family);
    m_doc.insert(QStringLiteral("fonts"), fonts);
    preview();
}

bool ThemeEditor::save(QString *error)
{
    QString name = m_doc.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        name = QStringLiteral("My theme");
        m_doc.insert(QStringLiteral("name"), name);
    }
    if (m_id.isEmpty()) {
        m_id = AppTheme::idFor(name);
    }
    if (!AppTheme::save(m_id, m_doc, error)) {
        return false;
    }
    Q_EMIT saved(m_id);
    return true;
}

bool ThemeEditor::importFile(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = f.errorString();
        }
        return false;
    }
    AppTheme t;
    QString why;
    if (!t.apply(f.readAll(), &why)) {
        if (error) {
            *error = QStringLiteral("%1 is not a theme: %2").arg(path, why);
        }
        return false;
    }
    QJsonObject doc = t.toJson();
    if (doc.value(QStringLiteral("name")).toString() == QLatin1String("Cyberdecker")) {
        doc.insert(QStringLiteral("name"), QFileInfo(path).completeBaseName());
    }
    m_id = AppTheme::idFor(doc.value(QStringLiteral("name")).toString());
    if (!AppTheme::save(m_id, doc, error)) {
        return false;
    }
    load(doc);
    preview();
    Q_EMIT saved(m_id);
    return true;
}

bool ThemeEditor::exportFile(const QString &path, QString *error) const
{
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(m_doc).toJson(QJsonDocument::Indented)) >= 0 && f.commit()) {
        return true;
    }
    if (error) {
        *error = f.errorString();
    }
    return false;
}
