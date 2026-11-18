// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Models.h"

namespace
{
QVariant idOf(const QVariantMap &row)
{
    return row.value(QStringLiteral("id"));
}
} // namespace

VariantListModel::VariantListModel(const QStringList &roles, QObject *parent)
    : QAbstractListModel(parent)
    , m_roles(roles)
{
    if (!m_roles.contains(QStringLiteral("id"))) {
        m_roles.prepend(QStringLiteral("id"));
    }
}

int VariantListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant VariantListModel::data(const QModelIndex &index, int role) const
{
    const int r = role - Qt::UserRole;
    if (!index.isValid() || index.row() >= m_rows.size() || r < 0 || r >= m_roles.size()) {
        return {};
    }
    return m_rows.at(index.row()).value(m_roles.at(r));
}

QHash<int, QByteArray> VariantListModel::roleNames() const
{
    QHash<int, QByteArray> names;
    for (int i = 0; i < m_roles.size(); ++i) {
        names.insert(Qt::UserRole + i, m_roles.at(i).toUtf8());
    }
    return names;
}

void VariantListModel::setRows(const QList<QVariantMap> &rows)
{
    const int before = static_cast<int>(m_rows.size());

    // 1. Drop rows whose id is gone.
    QList<QVariant> newIds;
    for (const QVariantMap &r : rows) {
        newIds.append(idOf(r));
    }
    for (int i = static_cast<int>(m_rows.size()) - 1; i >= 0; --i) {
        if (!newIds.contains(idOf(m_rows.at(i)))) {
            beginRemoveRows({}, i, i);
            m_rows.removeAt(i);
            endRemoveRows();
        }
    }

    // 2. Walk the target order: update in place, move, or insert.
    for (int i = 0; i < rows.size(); ++i) {
        const QVariant id = newIds.at(i);
        if (i < m_rows.size() && idOf(m_rows.at(i)) == id) {
            if (m_rows.at(i) != rows.at(i)) {
                m_rows[i] = rows.at(i);
                Q_EMIT dataChanged(index(i), index(i));
            }
            continue;
        }
        int found = -1;
        for (int j = i + 1; j < m_rows.size(); ++j) {
            if (idOf(m_rows.at(j)) == id) {
                found = j;
                break;
            }
        }
        if (found >= 0) {
            beginMoveRows({}, found, found, {}, i);
            m_rows.move(found, i);
            endMoveRows();
            m_rows[i] = rows.at(i);
            Q_EMIT dataChanged(index(i), index(i));
        } else {
            beginInsertRows({}, i, i);
            m_rows.insert(i, rows.at(i));
            endInsertRows();
        }
    }

    if (before != m_rows.size()) {
        Q_EMIT countChanged();
    }
}
