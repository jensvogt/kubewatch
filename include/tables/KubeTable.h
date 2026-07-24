#pragma once

// Qt includes
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

// Awsmock includes
#include <components/PageableTable.h>

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
        const QString prefix = GetPrefix();
        QJsonArray filtered;
        if (prefix.isEmpty()) {
            filtered = items;
        } else {
            for (const auto &item: items) {
                if (item.toObject()["metadata"].toObject()["name"].toString().startsWith(prefix)) {
                    filtered.append(item);
                }
            }
        }

        Clear();
        SetTotalSize(filtered.size());

        const long start = GetPageIndex() * GetPageSize();
        long end = start + GetPageSize();
        if (end > filtered.size()) end = filtered.size();

        for (long i = start; i < end; ++i) {
            rowFn(static_cast<int>(i - start), filtered[static_cast<int>(i)].toObject());
        }
    }

private:
    ResourceArgsProvider resourceArgsFn_;
    BaseArgsProvider baseArgsFn_;
};
