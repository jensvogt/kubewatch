#pragma once

#include <tables/KubeTable.h>

class EventsTable : public KubeTable {
    Q_OBJECT

public:
    explicit EventsTable(QWidget *parent = nullptr);

    void Refresh() override;
    [[nodiscard]] QString ResourceName() const override { return "events"; }
    [[nodiscard]] int NamespaceColumn() const override { return kNamespaceColumn; }

private:
    static constexpr int kHealthColumn = 6;
    static constexpr int kNamespaceColumn = 7;
};
