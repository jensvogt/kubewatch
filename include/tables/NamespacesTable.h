#pragma once

#include <tables/KubeTable.h>

class NamespacesTable : public KubeTable {
    Q_OBJECT

public:
    explicit NamespacesTable(QWidget *parent = nullptr);

    void Refresh() override;
    [[nodiscard]] QString ResourceName() const override { return "namespaces"; }
};
