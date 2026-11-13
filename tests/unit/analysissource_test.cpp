// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch.hpp>

#include <QVariantMap>

#include "analysissource.h"

namespace {
QList<OpenSource> openTabs()
{
    return OpenSource::fromVariantList( {
        QVariantMap{ { "tabId", "t1" }, { "filePath", "/logs/a.log" }, { "windowIndex", 0 },
                     { "tabIndex", 0 } },
        QVariantMap{ { "tabId", "t2" }, { "filePath", "/logs/b.log" }, { "windowIndex", 0 },
                     { "tabIndex", 1 }, { "focused", true } },
        QVariantMap{ { "tabId", "t3" }, { "filePath", "COM5" }, { "windowIndex", 1 },
                     { "tabIndex", 0 } },
        QVariantMap{ { "filePath", "/no/tab/id.log" } },
    } );
}
} // namespace

TEST_CASE( "selection_parse_and_resolve", "[workbench][U15]" )
{
    REQUIRE( AnalysisSource::parse( "" )->kind == AnalysisSource::Kind::Focused );
    REQUIRE( AnalysisSource::parse( "focused" )->kind == AnalysisSource::Kind::Focused );
    REQUIRE( AnalysisSource::parse( "all" )->kind == AnalysisSource::Kind::AllOpen );
    const auto tab = AnalysisSource::parse( "tab:t3" );
    REQUIRE( tab->kind == AnalysisSource::Kind::Tab );
    REQUIRE( tab->tabId == "t3" );
    const auto file = AnalysisSource::parse( "file:C:/logs/a b.log" );
    REQUIRE( file->kind == AnalysisSource::Kind::File );
    REQUIRE( file->filePath == "C:/logs/a b.log" );
    REQUIRE( file->toString() == "file:C:/logs/a b.log" );
    QString error;
    REQUIRE_FALSE( AnalysisSource::parse( "tab:", &error ) );
    REQUIRE_FALSE( AnalysisSource::parse( "window", &error ) );
    REQUIRE( error.contains( "focused, all" ) );

    const auto open = openTabs();
    REQUIRE( open.size() == 3 );
    REQUIRE( resolveAnalysisSource( *AnalysisSource::parse( "focused" ), open ).tabIds
             == QStringList{ "t2" } );
    REQUIRE( resolveAnalysisSource( *tab, open ).tabIds == QStringList{ "t3" } );
    REQUIRE( resolveAnalysisSource( *AnalysisSource::parse( "tab:gone" ), open ).reason
             == "source_not_found" );
    REQUIRE( resolveAnalysisSource( *AnalysisSource::parse( "all" ), open ).tabIds
             == QStringList{ "t1", "t2", "t3" } );
}

TEST_CASE( "file_source_without_tab", "[workbench][U15]" )
{
    const auto open = openTabs();
    // An open file resolves to its tab; a file that is not open is returned to be opened.
    REQUIRE( resolveAnalysisSource( *AnalysisSource::parse( "file:/logs/./b.log" ), open ).tabIds
             == QStringList{ "t2" } );
    const auto closed = resolveAnalysisSource( *AnalysisSource::parse( "file:/logs/c.log" ), open );
    REQUIRE( closed.ok() );
    REQUIRE( closed.tabIds.isEmpty() );
    REQUIRE( closed.fileToOpen == "/logs/c.log" );
}

TEST_CASE( "all_open_files_limits", "[workbench][U15]" )
{
    REQUIRE( resolveAnalysisSource( *AnalysisSource::parse( "all" ), {} ).reason
             == "no_open_sources" );
    REQUIRE( resolveAnalysisSource( *AnalysisSource::parse( "focused" ), {} ).reason
             == "no_focused_tab" );
    const auto limited = resolveAnalysisSource( *AnalysisSource::parse( "all" ), openTabs(), 2 );
    REQUIRE( limited.reason == "too_many_sources" );
    REQUIRE( limited.message.contains( "3" ) );
}
