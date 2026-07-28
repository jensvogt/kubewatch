#pragma once

// Qt includes
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

// Awsmock includes
#include <components/PageableTable.h>

#include <algorithm>
#include <functional>

// Base class for every Kubernetes resource table shown in the main window's
// stacked pages. A concrete subclass configures its own headers/resize modes
// in its constructor and implements Refresh() to fetch and populate itself.
//
// MainWindow supplies the two argument providers once, at construction time,
// binding them to its own context/namespace combo boxes -- this is the only
// piece of MainWindow state a table needs.
class KubeTable : public PageableTable {
    Q_OBJECT

public:
    using ResourceArgsProvider = std::function<QStringList(const QString &)>;
    using BaseArgsProvider = std::function<QStringList()>;

    explicit KubeTable(QWidget *parent = nullptr) : PageableTable(parent) {}

    void SetArgsProviders(ResourceArgsProvider resourceArgsFn, BaseArgsProvider baseArgsFn) {
        resourceArgsFn_ = std::move(resourceArgsFn);
        baseArgsFn_ = std::move(baseArgsFn);
    }

    // Fetches the resource(s) via kubectl and repopulates the current page.
    virtual void Refresh() = 0;

    // The kubectl resource name this table lists (used for the row context
    // menu's Edit/Delete/Logs actions).
    [[nodiscard]] virtual QString ResourceName() const = 0;

    // Column holding the row's namespace (hidden), or -1 for cluster-scoped resources.
    [[nodiscard]] virtual int NamespaceColumn() const { return -1; }

    [[nodiscard]] virtual bool SupportsLogs() const { return false; }

protected:
    [[nodiscard]] QStringList ResourceArgs(const QString &resource) const { return resourceArgsFn_(resource); }
    [[nodiscard]] QStringList BaseArgs() const { return baseArgsFn_(); }

    // Sets header names and the default resize modes (first column stretches, the
    // rest size to their contents) -- the layout every kube resource table uses.
    void ConfigureHeaders(const QStringList &headers) {
        SetHeaderNames(headers);
        QList<QHeaderView::ResizeMode> resizeModes;
        resizeModes << QHeaderView::Stretch;
        for (int col = 1; col < headers.size(); ++col) {
            resizeModes << QHeaderView::ResizeToContents;
        }
        SetResizeModes(resizeModes);
    }

    // Populates the table with the slice of items belonging to the table's current page.
    // Applies the table's own name-prefix filter to the full item set first, so the
    // pagination totals/labels reflect the filtered count rather than the unfiltered one.
    template<class RowFn>
    void PopulatePage(const QJsonArray &items, RowFn rowFn) {
        PopulatePage(items, -1, [](const QJsonObject &) { return 0L; }, std::move(rowFn));
    }

    // Same as above, but if `sortColumn` is the table's currently active sort column
    // (i.e. the user clicked that column's header), the full filtered item set is
    // sorted by `sortKeyFn` -- respecting the current sort direction -- before it's
    // sliced into a page.
    //
    // This matters because the table's proxy model only ever holds the current page's
    // rows: sorting there can reorder rows within a page, but can never bring a row
    // from another page to the top. Columns whose sort key can be computed directly
    // from the raw item (e.g. the health traffic light) sort the whole set here instead,
    // so e.g. all Error-health rows surface to page 1 regardless of which page they'd
    // otherwise have landed on.
    template<class RowFn, class SortKeyFn>
    void PopulatePage(const QJsonArray &items, int sortColumn, SortKeyFn sortKeyFn, RowFn rowFn) {
        const QString prefix = GetPrefix();
        QList<QJsonObject> filtered;
        for (const auto &item: items) {
            if (QJsonObject obj = item.toObject(); prefix.isEmpty() || obj["metadata"].toObject()["name"].toString().startsWith(prefix)) {
                filtered.append(obj);
            }
        }

        if (sortColumn >= 0 && GetSortColumn() == sortColumn) {
            const bool ascending = GetSortDirection() == 1;
            std::stable_sort(filtered.begin(), filtered.end(), [&](const QJsonObject &a, const QJsonObject &b) {
                const long keyA = sortKeyFn(a);
                const long keyB = sortKeyFn(b);
                return ascending ? keyA < keyB : keyA > keyB;
            });
        }

        Clear();
        SetTotalSize(filtered.size());

        const long start = GetPageIndex() * GetPageSize();
        long end = start + GetPageSize();
        if (end > filtered.size()) end = filtered.size();

        for (long i = start; i < end; ++i) {
            rowFn(static_cast<int>(i - start), filtered[static_cast<int>(i)]);
        }
    }

private:
    ResourceArgsProvider resourceArgsFn_;
    BaseArgsProvider baseArgsFn_;
};
