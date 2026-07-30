#pragma once

#include <tables/KubeTable.h>

class DaemonSetsTable : public KubeTable {
    Q_OBJECT

public:
    explicit DaemonSetsTable(QWidget *parent = nullptr);

    void Refresh() override;
    [[nodiscard]] QString ResourceName() const override { return "daemonsets"; }
    [[nodiscard]] int NamespaceColumn() const override { return kNamespaceColumn; }

private:
    static constexpr int kNamespaceColumn = 2;
};
