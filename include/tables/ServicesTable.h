#pragma once

#include <tables/KubeTable.h>

class ServicesTable : public KubeTable {
    Q_OBJECT

public:
    explicit ServicesTable(QWidget *parent = nullptr);

    void Refresh() override;
    [[nodiscard]] QString ResourceName() const override { return "services"; }
    [[nodiscard]] int NamespaceColumn() const override { return kNamespaceColumn; }

private:
    static constexpr int kNamespaceColumn = 5;
};
