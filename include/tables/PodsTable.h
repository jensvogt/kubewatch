#pragma once

#include <tables/KubeTable.h>

class PodsTable : public KubeTable {
    Q_OBJECT

public:
    explicit PodsTable(QWidget *parent = nullptr);

    void Refresh() override;
    [[nodiscard]] QString ResourceName() const override { return "pods"; }
    [[nodiscard]] int NamespaceColumn() const override { return kNamespaceColumn; }
    [[nodiscard]] bool SupportsLogs() const override { return true; }

private:
    static constexpr int kNamespaceColumn = 5;
};
