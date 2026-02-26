/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
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

#include <catch2/catch.hpp>

#include <QSettings>
#include <QTemporaryDir>

#include "sessioninfo.h"

namespace {
constexpr int SessionVersion = 1;
constexpr int OpenFilesVersion = 1;
constexpr int ExpectedMaxWindowsInSession = 64;
constexpr int ExpectedMaxFilesPerWindow = 4096;

void writeBaseSession( QSettings& settings )
{
    settings.clear();
    settings.beginGroup( "Window" );
    settings.setValue( "version", SessionVersion );
}
} // namespace

TEST_CASE( "SessionInfo::retrieveFromStorage caps number of windows per session",
           "[session][storage][limits]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );
    const auto settingsPath = tempDir.filePath( "session_windows.ini" );

    constexpr int overLimitWindows = ExpectedMaxWindowsInSession + 5;
    {
        QSettings settings( settingsPath, QSettings::IniFormat );
        writeBaseSession( settings );

        settings.beginWriteArray( "windows", overLimitWindows );
        for ( int windowIndex = 0; windowIndex < overLimitWindows; ++windowIndex ) {
            settings.setArrayIndex( windowIndex );
            settings.setValue( "id", QString( "window_%1" ).arg( windowIndex ) );
            settings.setValue( "geometry", QByteArray{ "dummy-geometry" } );
            settings.beginGroup( "OpenFiles" );
            settings.setValue( "version", OpenFilesVersion );
            settings.beginWriteArray( "openFiles", 0 );
            settings.endArray();
            settings.endGroup();
        }
        settings.endArray();
        settings.endGroup();
        settings.sync();
    }

    SessionInfo sessionInfo;
    {
        QSettings settings( settingsPath, QSettings::IniFormat );
        sessionInfo.retrieveFromStorage( settings );
    }

    const auto windows = sessionInfo.windows();
    REQUIRE( windows.size() == ExpectedMaxWindowsInSession );
    REQUIRE( windows.front() == "window_0" );
    REQUIRE( windows.back()
             == QString( "window_%1" ).arg( ExpectedMaxWindowsInSession - 1 ) );
}

TEST_CASE( "SessionInfo::retrieveFromStorage caps number of files per window",
           "[session][storage][limits]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );
    const auto settingsPath = tempDir.filePath( "session_files.ini" );

    constexpr int overLimitFiles = ExpectedMaxFilesPerWindow + 5;
    {
        QSettings settings( settingsPath, QSettings::IniFormat );
        writeBaseSession( settings );

        settings.beginWriteArray( "windows", 1 );
        settings.setArrayIndex( 0 );
        settings.setValue( "id", "Main" );
        settings.setValue( "geometry", QByteArray{ "dummy-geometry" } );

        settings.beginGroup( "OpenFiles" );
        settings.setValue( "version", OpenFilesVersion );
        settings.beginWriteArray( "openFiles", overLimitFiles );
        for ( int fileIndex = 0; fileIndex < overLimitFiles; ++fileIndex ) {
            settings.setArrayIndex( fileIndex );
            settings.setValue( "fileName", QString( "/tmp/file_%1.log" ).arg( fileIndex ) );
            settings.setValue( "topLine", fileIndex );
            settings.setValue( "viewContext", QString( "ctx_%1" ).arg( fileIndex ) );
        }
        settings.endArray();
        settings.endGroup();

        settings.endArray();
        settings.endGroup();
        settings.sync();
    }

    SessionInfo sessionInfo;
    {
        QSettings settings( settingsPath, QSettings::IniFormat );
        sessionInfo.retrieveFromStorage( settings );
    }

    const auto openFiles = sessionInfo.openFiles( "Main" );
    REQUIRE( openFiles.size() == ExpectedMaxFilesPerWindow );
    REQUIRE( openFiles.front().fileName == "/tmp/file_0.log" );
    REQUIRE( openFiles.back().fileName
             == QString( "/tmp/file_%1.log" ).arg( ExpectedMaxFilesPerWindow - 1 ) );
}

TEST_CASE(
    "SessionInfo::retrieveFromStorage skips windows with empty id and files with empty fileName",
    "[session][storage][validation]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );
    const auto settingsPath = tempDir.filePath( "session_validation.ini" );

    {
        QSettings settings( settingsPath, QSettings::IniFormat );
        writeBaseSession( settings );

        settings.beginWriteArray( "windows", 3 );

        settings.setArrayIndex( 0 );
        settings.setValue( "id", "" );
        settings.setValue( "geometry", QByteArray{ "dummy-geometry" } );

        settings.setArrayIndex( 1 );
        settings.setValue( "id", "   " );
        settings.setValue( "geometry", QByteArray{ "dummy-geometry" } );

        settings.setArrayIndex( 2 );
        settings.setValue( "id", "Main" );
        settings.setValue( "geometry", QByteArray{ "dummy-geometry" } );
        settings.beginGroup( "OpenFiles" );
        settings.setValue( "version", OpenFilesVersion );
        settings.beginWriteArray( "openFiles", 4 );

        settings.setArrayIndex( 0 );
        settings.setValue( "fileName", "/tmp/valid_0.log" );
        settings.setValue( "topLine", 0 );
        settings.setValue( "viewContext", "ctx_0" );

        settings.setArrayIndex( 1 );
        settings.setValue( "fileName", "" );
        settings.setValue( "topLine", 1 );
        settings.setValue( "viewContext", "ctx_1" );

        settings.setArrayIndex( 2 );
        settings.setValue( "fileName", "   " );
        settings.setValue( "topLine", 2 );
        settings.setValue( "viewContext", "ctx_2" );

        settings.setArrayIndex( 3 );
        settings.setValue( "fileName", "/tmp/valid_1.log" );
        settings.setValue( "topLine", 3 );
        settings.setValue( "viewContext", "ctx_3" );

        settings.endArray();
        settings.endGroup();

        settings.endArray();
        settings.endGroup();
        settings.sync();
    }

    SessionInfo sessionInfo;
    {
        QSettings settings( settingsPath, QSettings::IniFormat );
        sessionInfo.retrieveFromStorage( settings );
    }

    const auto windows = sessionInfo.windows();
    REQUIRE( windows.size() == 1 );
    REQUIRE( windows.front() == "Main" );

    const auto openFiles = sessionInfo.openFiles( "Main" );
    REQUIRE( openFiles.size() == 2 );
    REQUIRE( openFiles.at( 0 ).fileName == "/tmp/valid_0.log" );
    REQUIRE( openFiles.at( 1 ).fileName == "/tmp/valid_1.log" );
}
