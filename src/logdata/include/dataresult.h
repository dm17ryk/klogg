// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sourcelocation.h"
#include <QString>
#include <optional>

struct DataError {
    QString code;
    QString message;
    QString fieldId;
    std::optional<SourceLocation> location;
};

template <typename T>
struct DataResult {
    std::optional<T> value;
    DataError error;
};

namespace cilogg {
struct DataStatus {
    bool ok = false;
    DataError error;
};
} // namespace cilogg
