#pragma once

#include <tables/KubeTable.h>

class StatefulSetsTable : public KubeTable {
    Q_OBJECT

public:
    explicit StatefulSetsTable(QWidget *parent = nullptr);

    void Refresh() override;
    [[nodiscard]] QString ResourceName() const override { return "statefulsets"; }
    [[nodiscard]] int NamespaceColumn() const override { return kNamespaceColumn; }

private:
    static constexpr int kNamespaceColumn = 2;
};
