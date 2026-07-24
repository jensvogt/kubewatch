#pragma once

#include <tables/KubeTable.h>

class JobsTable : public KubeTable {
    Q_OBJECT

public:
    explicit JobsTable(QWidget *parent = nullptr);

    void Refresh() override;
    [[nodiscard]] QString ResourceName() const override { return "jobs"; }
    [[nodiscard]] int NamespaceColumn() const override { return kNamespaceColumn; }
    [[nodiscard]] bool SupportsLogs() const override { return true; }

private:
    static constexpr int kNamespaceColumn = 5;
};
