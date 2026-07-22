//
// Created by vogje01 on 12/10/25.
//

#ifndef AWSMOCK_QT_UI_PREFIX_FILTER_MODEL_H
#define AWSMOCK_QT_UI_PREFIX_FILTER_MODEL_H

#include <QSortFilterProxyModel>

class PrefixFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit PrefixFilterProxyModel(QObject *parent = nullptr);

    // Set the column index to apply the filter on
    void setFilterColumn(const int column) {
        beginFilterChange();
        m_filterColumn = column;
        endFilterChange();
    }

    // Set the character that rows must NOT start with
    void setFilterPrefix(const QString &prefix) {
        beginFilterChange();
        m_filterPrefix = prefix;
        endFilterChange();
    }

    void clearFilter() {
        if (!m_filterPrefix.isEmpty()) {
            // Only proceed if the filter is currently active
            beginFilterChange();
            m_filterPrefix = QString(); // Reset to an empty string
            endFilterChange();
        }
    }

    QString getPrefix() {
        return m_filterPrefix;
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    int m_filterColumn = 0;
    QString m_filterPrefix;
};

#endif //AWSMOCK_QT_UI_PREFIX_FILTER_MODEL_H
