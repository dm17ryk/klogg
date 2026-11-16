/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef KLOGG_LOGGER_H
#define KLOGG_LOGGER_H

#include <cstdint>

#include <QStringList>

#include "log.h"

namespace logging {

enum class LogLevel { None, Fatal, Error, Warning, Info, Debug };

void enableLogging( bool enableLogging = true, LogLevel logLevel = LogLevel::Info);
void enableFileLogging( bool enableLogging = true, LogLevel logLevel = LogLevel::Info);

// The most recent formatted log lines, oldest first. Warnings and errors are always kept, even
// with console and file logging off; lower levels only while console or file logging records
// them. Diagnostic reports include these lines.
QStringList recentLogLines();
void setRecentLogCapacity( int lines );
void clearRecentLogLines();
} // namespace logging

#endif