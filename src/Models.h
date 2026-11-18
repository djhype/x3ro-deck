// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QVariantMap>

// A list model over QVariantMap rows with a fixed set of roles. setRows()
// diffs by the "id" role, so QML views keep their delegates, scroll position
// and add/remove transitions instead of rebuilding on every update.
class VariantListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    VariantListModel(const QStringList &roles, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(const QList<QVariantMap> &rows);
    QVariantMap row(int i) const { return m_rows.value(i); }

Q_SIGNALS:
    void countChanged();

private:
    QStringList m_roles;
    QList<QVariantMap> m_rows;
};
