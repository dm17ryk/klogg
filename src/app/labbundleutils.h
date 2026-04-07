#pragma once

#include <optional>

#include "labrequest.h"
#include "labtypes.h"

std::optional<LabJobBundle> loadLabJobBundle( const LabCliRequest& request, QString* errorMessage );
std::optional<LabAgentConfig> loadLabAgentConfig( const QString& path, QString* errorMessage );
