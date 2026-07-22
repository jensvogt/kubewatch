//
// Created by vogje01 on 2/15/26.
//

#pragma once

// Qt includes
#include <QHeaderView>
#include <QStandardItemModel>
#include <QTimer>

// Awsmock includes
#include <utils/IconUtils.h>
#include <utils/DateTimeUtils.h>
#include <utils/Configuration.h>
#include <utils/PrefixFilterModel.h>
#include <utils/EventBus.h>

QT_BEGIN_NAMESPACE

namespace Ui {
    class PageableTable;
}

QT_END_NAMESPACE

class PageableTable : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructor
     *
     * @param parent parent widget
     */
    explicit PageableTable(QWidget *parent = nullptr);

    /**
     * @brief Destructor
     */
    ~PageableTable() override;

    /**
     * @brief Return the page index
     *
     * @return current page index
     */
    [[nodiscard]] long GetPageIndex() const {
        return _pageIndex;
    }

    /**
     * @brief Return the page index
     *
     * @return current page size
     */
    [[nodiscard]] long GetPageSize() const {
        return _pageSize;
    }

    /**
     * @brief Set the header names
     *
     * @param headerNames list of header names
     */
    void SetHeaderNames(const QStringList &headerNames);

    /**
     * @brief Sets the column resize mode
     *
     * @param resizeModes column resize mode list
     */
    void SetResizeModes(const QList<QHeaderView::ResizeMode> &resizeModes) const;

    /**
     * @brief Sets the hidden columns
     *
     * @param hiddenColumns list of column indexes
     */
    void SetHiddenColumns(const QList<int> &hiddenColumns) const;

    /**
     * @brief Sets the total size
     *
     * @param totalSize total item count
     */
    void SetTotalSize(const long totalSize) {
        _totalSize = totalSize;
        _maxPage = (_totalSize + _pageSize - 1) / _pageSize;
        CalculatePageStatus();
    }

    /**
     * @brief Save the current selection
     */
    void SaveSelection();

    /**
     * @brief Restore the saved selection
     */
    void RestoreSelection();

    /**
     * @brief Sho details of the selected row
     */
    void ShowDetails();

    /**
     * @brief Sets the total size
     *
     * @return total item count
     */
    [[nodiscard]]
    long GetTotalSize() const {
        return _totalSize;
    }

    /**
     * @brief Sets the size
     *
     * @return item count
     */
    [[nodiscard]]
    int GetSize() const {
        return _dataModel->rowCount();
    }

    /**
     * @brief Returns the sort column
     *
     * @return sort column name
     */
    [[nodiscard]]
    int GetSortColumn() const {
        return _sortColumn;
    }

    /**
     * @brief Returns the sort column
     *
     * @return sort column name
     */
    [[nodiscard]]
    QString GetSortAttribute() const {
        return _sortAttribute;
    }

    /**
     * @brief Returns the sort column
     *
     * @param sortColumn column name
     * @param sortAttribute database attribute
     */
    void SetSortColumn(const int &sortColumn, const QString &sortAttribute) {
        _sortColumn = sortColumn;
        _sortAttribute = sortAttribute;
        UpdateSorting();
    }

    /**
     * @brief Returns the sort column
     *
     * @param sortColumn column name
     * @param sortAttribute database attribute
     * @param direction sort direction 1=ascending, -1=descending
     */
    void SetSorting(const int &sortColumn, const QString &sortAttribute, const int direction) {
        _sortColumn = sortColumn;
        _sortAttribute = sortAttribute;
        _sortDirection = direction;
        UpdateSorting();
    }

    /**
     * @brief Returns the sort direction as integer
     *
     * @return sort direction, 1 = ascending, -1 = descending
     */
    [[nodiscard]]
    int GetSortDirection() const {
        return _sortDirection;
    }

    /**
     * @brief Returns the sort direction
     *
     * @param sortDirection sort direction
     */
    void SetSortDirection(const int sortDirection) {
        _sortDirection = sortDirection;
    }

    /**
     * @brief Sets a string column
     *
     * @param row table row
     * @param column table column
     * @param value column value
     * @param alignment column aligment
     */
    void SetColumn(int row, int column, const QString &value, const Qt::Alignment &alignment = Qt::AlignLeft | Qt::AlignVCenter) const;

    /**
     * @brief Sets a datetime column
     *
     * @param row table row
     * @param column table column
     * @param value column value
     */
    void SetColumn(int row, int column, const QDateTime &value) const;

    /**
     * @brief Sets a long integer column
     *
     * @param row table row
     * @param column table column
     * @param value column value
     */
    void SetColumn(int row, int column, const long &value) const;

    /**
     * @brief Sets a boolean column with icons
     *
     * @param row table row
     * @param column table column
     * @param value column value
     * @param enabledIcon icon to how when value = true
     * @param disabledIcon icon to how when value = false
     */
    void SetColumn(int row, int column, bool value, const QIcon &enabledIcon, const QIcon &disabledIcon) const;

    /**
     * @brief Sets a placeholder text for the search field
     *
     * @param placeholder search field placeholder text
     */
    void SetSearchFieldPlaceholder(const QString &placeholder);

    /**
     * @brief Sets a hidden string column
     *
     * @param row table row
     * @param column table column
     * @param value column value
     */
    void SetHiddenColumn(int row, int column, const QString &value) const;

    /**
     * @brief Sets a hidden boolean column
     *
     * @param row table row
     * @param column table column
     * @param value column value
     */
    void SetHiddenColumn(int row, int column, bool value) const;

    /**
     * @brief Set message label
     *
     * @param message message to display
     */
    void SetMessageLabel(const QString &message) const;

    /**
     * @brief Return the value of a column, depending on the data type
     *
     * @tparam T data type
     * @param index row index
     * @param column column index
     * @return value
     */
    template<class T>
    T GetValue(const QModelIndex &index, const int column) {
        const auto *item = _dataModel->item(index.row(), column);
        if (!item) return {};

        QString sValue = item->text();

        if constexpr (std::is_same_v<T, int>) {
            return sValue.toInt();
        } else if constexpr (std::is_same_v<T, long>) {
            return sValue.toLong();
        } else if constexpr (std::is_same_v<T, double>) {
            return sValue.toDouble();
        } else if constexpr (std::is_same_v<T, QString>) {
            return sValue;
        } else if constexpr (std::is_same_v<T, bool>) {
            return item->data(Qt::UserRole).toBool();
        } else {
            return {};
        }
    }

    /**
     * @brief Returns the table index
     *
     * @param pos mouse position
     * @return table row/column index
     */
    [[nodiscard]]
    QModelIndex GetIndexFromPosition(const QPoint &pos) const;

    /**
     * @brief Returns the global position
     *
     * @param tablePosition table position
     * @return global position
     */
    [[nodiscard]]
    QPoint GetGlobalPosition(const QPoint &tablePosition) const;

    /**
     * @brief Remove a row from the table
     *
     * @param index row index
     */
    void RemoveRow(const QModelIndex &index) const;

    /**
     * @brief Returns a list of selected row indexes.
     *
     * @return list of selected rows
     */
    [[nodiscard]]
    QModelIndexList GetSelectedRows() const;

    /**
     * @brief Converts a proxy index to a source index
     *
     * @param index proxy index
     * @return source index
     */
    [[nodiscard]]
    QModelIndex GetSourceIndex(const QModelIndex &index) const;

    /**
     * @brief Returns the prefix value
     *
     * @return prefix value
     */
    [[nodiscard]] QString GetPrefix() const {
        return _proxyModel->getPrefix();
    }

    /**
     * @brief Set multi-row selection
     */
    void SetMultiRowSelection(bool enabled) const;

    void ClearContent() {
        Clear();
    }

    /**
     * @brief Clears the whole table
     */
    void Clear() {
        _dataModel->removeRows(0, _dataModel->rowCount());
        CalculatePageStatus();
    }

    /**
     * @brief Refresh the sort order
     */
    void UpdateSorting() const;

    /**
     * @brief Table data changed
     *
     * @param startIndex start index
     * @param endIndex end index
     */
    void DataChanged(const QModelIndex &startIndex, const QModelIndex &endIndex) const {
        _dataModel->dataChanged(startIndex, endIndex);
    }

    /**
     * @brief Convert the IDs to indexes
     *
     * @param ids table row IDs
     * @return map of row indexes
     */
    QHash<QString, int> GetRowIndexesFromIds(const QVector<QString> &ids) {
        QHash<QString, int> result;
        for (const auto &id: ids) {
            for (int j = 0; j < _dataModel->rowCount(); j++) {
                if (QModelIndex index = _dataModel->index(j, 0); GetValue<QString>(index, 0) == id) {
                    result.insert(id, j);
                }
            }
        }
        return result;
    }

    /**
     * @brief Sets the list of service APIs
     *
     * @param serviceApis list of service APIs
     */
    void setServiceApis(const QStringList &serviceApis) {
        _serviceApis = serviceApis;
    }

signals:
    /**
     * @brief Sent when the context menu is selected
     *
     * @param pos page index
     */
    void ContextMenuRequested(const QPoint &pos);

    /**
     * @brief Double-click proxy signal
     *
     * @param index normalized table index
     */
    void DoubleClicked(const QModelIndex &index);

    /**
     * @brief Single click proxy signal
     *
     * @param index normalized table index
     */
    void SingleClick(const QModelIndex &index);

    /**
     * @brief Signals the parent to reload the table
     */
    void ReloadTable();

    /**
     * @brief Signals the parent to reload the table
     *
     * @param index selected row index
     */
    void ShowDetailsSignal(const QModelIndex &index);

private:
    /**
     * @brief Sets a status message
     *
     * @param message status message
     */
    void SetStatus(const QString &message) const;

    /**
     * @brief Sets the last update time
     */
    void SetLastUpdate() const;

    /**
     * @brief Calculate the pageing status
     */
    void CalculatePageStatus();

    /**
     * @brief Page index
     */
    Ui::PageableTable *_ui;

    /**
     * @brief Page index
     */
    long _pageIndex{};

    /**
     * @brief Page index
     */
    long _pageSize{};

    /**
     * @brief Page index
     */
    long _maxPage{};

    /**
     * @brief Total size
     */
    long _totalSize{};

    /**
     * @brief key column
     */
    int _keyColumn{};

    /**
     * @brief Column header names
     */
    QStringList _headerNames;

    /**
     * @brief Data model
     */
    QStandardItemModel *_dataModel;

    /**
     * @brief Proxy table model for prefixes
     */
    PrefixFilterProxyModel *_proxyModel;

    /**
     * @brief Sort column index
     */
    int _sortColumn{};

    /**
     * @brief Database sort attribute
     */
    QString _sortAttribute;

    /**
     * @brief Sort column
     */
    int _sortDirection = -1;

    /**
     * @brief Selected row
     */
    QSet<QString> _savedIds;

    /**
     * @brief Search field prefix
     */
    QString _searchFieldPlaceholder = "Prefix";

    /**
     * @brief List of service APIs
     */
    QStringList _serviceApis;
};

