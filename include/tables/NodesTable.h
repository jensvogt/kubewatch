#pragma once

#include <tables/KubeTable.h>

class NodesTable : public KubeTable {
    Q_OBJECT

public:
    explicit NodesTable(QWidget *parent = nullptr);

    void Refresh() override;
    [[nodiscard]] QString ResourceName() const override { return "nodes"; }
};
