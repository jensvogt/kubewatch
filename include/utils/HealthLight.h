#pragma once

#include <QIcon>

// Traffic-light health indicator shared by the resource tables.
namespace HealthLight {

    // Ordered so the health column sorts from most-severe to least-severe.
    enum class Health { Error = 0, Warning = 1, Ok = 2 };

    QIcon Icon(Health health);

}// namespace HealthLight
