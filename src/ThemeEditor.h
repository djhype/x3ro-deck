#pragma once

#include "DeckDialog.h"

#include <QHash>
#include <QJsonObject>

#include <functional>

class AppTheme;
class QFontComboBox;
class QLineEdit;
class QPushButton;

// Edits a theme token by token with the running app as the preview. A
// built-in theme is never changed: editing one starts "<name> (copy)". Save
// writes ~/.config/x3ro-deck/themes/<id>.json; Cancel leaves the caller to
// put the saved theme back.
class ThemeEditor : public DeckDialog
{
    Q_OBJECT
public:
    ThemeEditor(AppTheme *live, const QString &id, QWidget *parent = nullptr);

    QJsonObject theme() const { return m_doc; }
    // The file it saves to; empty until a built-in's copy is saved.
    QString id() const { return m_id; }
    void setName(const QString &name);
    void setColor(const QString &token, const QColor &color);
    void setCheckoutColor(int index, const QColor &color);
    void setFont(const QString &key, const QString &family);
    bool save(QString *error = nullptr);
    // Copies a theme file into the themes folder and edits it.
    bool importFile(const QString &path, QString *error = nullptr);
    bool exportFile(const QString &path, QString *error = nullptr) const;

Q_SIGNALS:
    // A theme file was written (Save or Import); the picker should list it.
    void saved(const QString &id);

private:
    void load(const QJsonObject &doc);
    void preview();
    void refreshSwatch(QPushButton *swatch, const QString &color);
    void pickColor(const QString &title, const QString &current, const std::function<void(const QColor &)> &apply);

    AppTheme *m_live;
    QJsonObject m_doc;
    QString m_id;
    QLineEdit *m_name;
    QHash<QString, QPushButton *> m_swatches; // color token → swatch
    QHash<QString, QLineEdit *> m_hex;
    QList<QPushButton *> m_checkoutSwatches;
    QHash<QString, QFontComboBox *> m_fonts;
    bool m_loading = false;
};
