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

#include "logpane.h"
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

namespace {
PaneLayoutNode paneNode( const QString& id, std::vector<int> files, int currentFile = -1 )
{
    PaneLayoutNode node;
    node.isPane = true;
    node.paneId = id;
    node.files = std::move( files );
    node.currentFile = currentFile;
    return node;
}

PaneLayoutNode splitNode( Qt::Orientation orientation, std::vector<PaneLayoutNode> children,
                          QList<int> sizes = {} )
{
    PaneLayoutNode node;
    node.isPane = false;
    node.orientation = orientation;
    node.children = std::move( children );
    node.sizes = std::move( sizes );
    return node;
}

// H[ a (files 0, 1; current 1), V[ b (file 2), c (empty) ] ], b active.
PaneLayout threePaneLayout()
{
    PaneLayout layout;
    layout.root = splitNode(
        Qt::Horizontal,
        { paneNode( "a", { 0, 1 }, 1 ),
          splitNode( Qt::Vertical, { paneNode( "b", { 2 }, 2 ), paneNode( "c", {} ) },
                     { 300, 100 } ) },
        { 600, 400 } );
    layout.activePaneId = "b";
    return layout;
}
} // namespace

TEST_CASE( "SessionInfo keeps a window's pane layout and old sessions load without one",
           "[session][storage][workbench][U16]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );
    const auto oldPath = tempDir.filePath( "session_old.ini" );
    {
        // A session written before split panes existed.
        QSettings settings( oldPath, QSettings::IniFormat );
        settings.clear();
        settings.beginGroup( "Window" );
        settings.setValue( "version", 2 );
        settings.beginWriteArray( "windows", 1 );
        settings.setArrayIndex( 0 );
        settings.setValue( "id", "Main" );
        settings.beginGroup( "OpenFiles" );
        settings.setValue( "version", 2 );
        settings.beginWriteArray( "openFiles", 1 );
        settings.setArrayIndex( 0 );
        settings.setValue( "fileName", "/tmp/old.log" );
        settings.endArray();
        settings.endGroup();
        settings.endArray();
        settings.endGroup();
        settings.sync();
    }

    SessionInfo old;
    {
        QSettings settings( oldPath, QSettings::IniFormat );
        old.retrieveFromStorage( settings );
    }
    REQUIRE( old.windows() == QStringList{ "Main" } );
    REQUIRE( old.openFiles( "Main" ).size() == 1 );
    REQUIRE( old.paneLayout( "Main" ).isEmpty() );

    const auto layout = PaneLayoutFormat::serialize( threePaneLayout() );
    old.setOpenFiles( "Main", { { "/tmp/a.log", 0, {} },
                                { "/tmp/b.log", 0, {} },
                                { "/tmp/c.log", 0, {} } } );
    old.setPaneLayout( "Main", layout );
    const auto newPath = tempDir.filePath( "session_new.ini" );
    {
        QSettings settings( newPath, QSettings::IniFormat );
        old.saveToStorage( settings );
        settings.sync();
        // The session format version is unchanged, so older CILogg versions still read the
        // session and open every file in one pane.
        REQUIRE( settings.value( "Window/version" ).toInt() == 2 );
        REQUIRE( settings.value( "Window/windows/1/OpenFiles/version" ).toInt() == 2 );
    }

    SessionInfo restored;
    {
        QSettings settings( newPath, QSettings::IniFormat );
        restored.retrieveFromStorage( settings );
    }
    REQUIRE( restored.paneLayout( "Main" ) == layout );
    REQUIRE( restored.openFiles( "Main" ).size() == 3 );
}

TEST_CASE( "Pane layout format round-trips layouts and rejects malformed ones",
           "[session][workbench][U16]" )
{
    const auto json = PaneLayoutFormat::serialize( threePaneLayout() );
    const auto parsed = PaneLayoutFormat::parse( json, 3 );
    INFO( json.toStdString() );
    REQUIRE( parsed.status == PaneLayoutFormat::Status::Valid );
    REQUIRE( parsed.layout.paneCount() == 3 );
    REQUIRE( parsed.layout.activePaneId == "b" );
    const auto& root = parsed.layout.root;
    REQUIRE_FALSE( root.isPane );
    REQUIRE( root.orientation == Qt::Horizontal );
    REQUIRE( root.sizes == QList<int>{ 600, 400 } );
    REQUIRE( root.children.at( 0 ).paneId == "a" );
    REQUIRE( root.children.at( 0 ).files == std::vector<int>{ 0, 1 } );
    REQUIRE( root.children.at( 0 ).currentFile == 1 );
    REQUIRE( root.children.at( 1 ).orientation == Qt::Vertical );
    REQUIRE( root.children.at( 1 ).children.at( 1 ).files.empty() );
    REQUIRE( PaneLayoutFormat::serialize( parsed.layout ) == json );

    REQUIRE( PaneLayoutFormat::parse( {}, 3 ).status == PaneLayoutFormat::Status::Absent );
    REQUIRE( PaneLayoutFormat::parse( "{broken", 3 ).status == PaneLayoutFormat::Status::Invalid );
    REQUIRE( PaneLayoutFormat::parse( R"({"version":2,"root":{}})", 3 ).status
             == PaneLayoutFormat::Status::Unsupported );
    // File 2 does not exist when the window saved only two files.
    REQUIRE( PaneLayoutFormat::parse( json, 2 ).status == PaneLayoutFormat::Status::Invalid );

    auto duplicate = threePaneLayout();
    duplicate.root.children.at( 1 ).children.at( 1 ).files = { 0 };
    REQUIRE( PaneLayoutFormat::parse( PaneLayoutFormat::serialize( duplicate ), 3 ).status
             == PaneLayoutFormat::Status::Invalid );

    auto foreignCurrent = threePaneLayout();
    foreignCurrent.root.children.at( 0 ).currentFile = 2;
    REQUIRE( PaneLayoutFormat::parse( PaneLayoutFormat::serialize( foreignCurrent ), 3 ).status
             == PaneLayoutFormat::Status::Invalid );

    auto unknownActive = threePaneLayout();
    unknownActive.activePaneId = "missing";
    REQUIRE( PaneLayoutFormat::parse( PaneLayoutFormat::serialize( unknownActive ), 3 ).status
             == PaneLayoutFormat::Status::Invalid );

    auto lonelySplit = threePaneLayout();
    lonelySplit.root.children.at( 1 ).children.pop_back();
    lonelySplit.root.children.at( 1 ).sizes = { 100 };
    REQUIRE( PaneLayoutFormat::parse( PaneLayoutFormat::serialize( lonelySplit ), 3 ).status
             == PaneLayoutFormat::Status::Invalid );

    std::vector<PaneLayoutNode> nine;
    for ( int pane = 0; pane < 9; ++pane ) {
        nine.push_back( paneNode( QString::number( pane ), {} ) );
    }
    PaneLayout tooMany;
    tooMany.root = splitNode( Qt::Horizontal, nine );
    tooMany.activePaneId = "0";
    REQUIRE( PaneLayoutFormat::parse( PaneLayoutFormat::serialize( tooMany ), 0 ).status
             == PaneLayoutFormat::Status::Invalid );
    nine.pop_back();
    PaneLayout eight;
    eight.root = splitNode( Qt::Horizontal, nine );
    eight.activePaneId = "0";
    REQUIRE( PaneLayoutFormat::parse( PaneLayoutFormat::serialize( eight ), 0 ).status
             == PaneLayoutFormat::Status::Valid );
}
