#pragma once

#include <tables/KubeTable.h>

class ReplicaSetsTable : public KubeTable {
    Q_OBJECT

public:
    explicit ReplicaSetsTable(QWidget *parent = nullptr);

    void Refresh() override;
    [[nodiscard]] QString ResourceName() const override { return "replicasets"; }
    [[nodiscard]] int NamespaceColumn() const override { return kNamespaceColumn; }

private:
    static constexpr int kHealthColumn = 3;
    static constexpr int kNamespaceColumn = 4;
};
