/*
 * Copyright (C) 2011, 2014 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "sessioninfo.h"

#include <algorithm>

#include <QSettings>

#include "log.h"

constexpr int OPENFILES_VERSION = 1;
constexpr int SESSION_VERSION = 1;
constexpr int MAX_WINDOWS_IN_SESSION = 64;
constexpr int MAX_FILES_PER_WINDOW = 4096;

void SessionInfo::retrieveFromStorage( QSettings& settings )
{
    LOG_DEBUG << "SessionInfo::retrieveFromStorage";

    settings.beginGroup( "Window" );

    if ( settings.value( "version", 0 ).toInt() == SESSION_VERSION ) {
        windows_.clear();
        const auto windowsCount = settings.beginReadArray( "windows" );
        if ( windowsCount > MAX_WINDOWS_IN_SESSION ) {
            LOG_WARNING << "Session contains too many windows (" << windowsCount
                        << "), truncating to " << MAX_WINDOWS_IN_SESSION;
        }

        const auto safeWindowsCount = std::min( windowsCount, MAX_WINDOWS_IN_SESSION );
        for ( auto windowIndex = 0; windowIndex < safeWindowsCount; ++windowIndex ) {
            settings.setArrayIndex( static_cast<int>( windowIndex ) );
            QString windowId = settings.value( "id" ).toString();
            if ( windowId.trimmed().isEmpty() ) {
                LOG_WARNING << "Skipping window session with empty id at index " << windowIndex;
                continue;
            }

            auto window = Window{ windowId };
            window.geometry = settings.value( "geometry" ).toByteArray();

            if ( settings.contains( "OpenFiles/version" ) ) {
                settings.beginGroup( "OpenFiles" );
                if ( settings.value( "version" ).toInt() == OPENFILES_VERSION ) {
                    int size = settings.beginReadArray( "openFiles" );
                    if ( size > MAX_FILES_PER_WINDOW ) {
                        LOG_WARNING << "Window session " << windowId << " has too many files ("
                                    << size << "), truncating to " << MAX_FILES_PER_WINDOW;
                    }
                    LOG_DEBUG << "SessionInfo: " << size << " files.";
                    const auto safeSize = std::min( size, MAX_FILES_PER_WINDOW );
                    for ( int i = 0; i < safeSize; ++i ) {
                        settings.setArrayIndex( i );
                        QString file_name = settings.value( "fileName" ).toString();
                        if ( file_name.trimmed().isEmpty() ) {
                            LOG_WARNING << "Skipping empty file entry in window " << windowId;
                            continue;
                        }
                        uint64_t top_line = settings.value( "topLine" ).toULongLong();
                        QString view_context = settings.value( "viewContext" ).toString();
                        window.openFiles.emplace_back( file_name, top_line, view_context );
                    }
                    settings.endArray();
                }
                else {
                    LOG_ERROR << "Unknown version of OpenFiles, ignoring it...";
                }
                settings.endGroup();
            }

            LOG_INFO << "Loaded settings for window session " << windowId;
            windows_.emplace_back( window );
        }
        settings.endArray();
    }
    else {
        LOG_ERROR << "Unknown version of session, ignoring it...";
    }

    settings.endGroup();
}

void SessionInfo::saveToStorage( QSettings& settings ) const
{
    LOG_DEBUG << "SessionInfo::saveToStorage";

    settings.beginGroup( "Window" );
    settings.setValue( "version", SESSION_VERSION );

    settings.remove( "windows" );
    settings.beginWriteArray( "windows" );
    for ( auto windowIndex = 0u; windowIndex < windows_.size(); ++windowIndex ) {
        const auto& window = windows_.at( windowIndex );
        settings.setArrayIndex( static_cast<int>( windowIndex ) );

        settings.setValue( "id", window.id );
        settings.setValue( "geometry", window.geometry );

        settings.beginGroup( "OpenFiles" );
        settings.setValue( "version", OPENFILES_VERSION );
        settings.remove( "openFiles" );
        settings.beginWriteArray( "openFiles" );
        for ( unsigned i = 0; i < window.openFiles.size(); ++i ) {
            settings.setArrayIndex( static_cast<int>( i ) );
            const OpenFile* open_file = &( window.openFiles.at( i ) );
            settings.setValue( "fileName", open_file->fileName );
            settings.setValue( "topLine", qint64( open_file->topLine ) );
            settings.setValue( "viewContext", open_file->viewContext );
        }
        settings.endArray();
        settings.endGroup(); // OpenFiles
    }
    settings.endArray();
    settings.endGroup(); // Win
}
