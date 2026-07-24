#pragma once

#include <tables/KubeTable.h>

// Lists a namespaced resource that only needs a Name/Age display (Deployments,
// StatefulSets, DaemonSets, ConfigMaps, Secrets, PersistentVolumeClaims,
// Ingresses -- kubectl returns the same shape for all of these).
class GenericTable : public KubeTable {
    Q_OBJECT

public:
    explicit GenericTable(QString resource, QWidget *parent = nullptr);

    void Refresh() override;
    [[nodiscard]] QString ResourceName() const override { return resource_; }
    [[nodiscard]] int NamespaceColumn() const override { return kNamespaceColumn; }

private:
    static constexpr int kNamespaceColumn = 2;
    QString resource_;
};
