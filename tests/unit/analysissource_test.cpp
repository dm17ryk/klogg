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

TEST_CASE( "source_rules_reject_and_filter: tools refuse the sources they cannot use",
           "[workbench][U15]" )
{
    const auto open = OpenSource::fromVariantList( {
        QVariantMap{ { "tabId", "f1" }, { "filePath", "/logs/a.log" }, { "sourceType", "file" } },
        QVariantMap{ { "tabId", "c1" },
                     { "filePath", "/captures/c1.log" },
                     { "sourceType", "com" },
                     { "focused", true } },
        QVariantMap{ { "tabId", "u1" }, { "filePath", "/remote/u.log" }, { "sourceType", "url" } },
        QVariantMap{
            { "tabId", "c2" }, { "filePath", "/captures/c2.log" }, { "sourceType", "com" } },
    } );
    REQUIRE( open.at( 1 ).sourceType == "com" );
    const auto parse = []( const char* text ) { return *AnalysisSource::parse( text ); };

    // Preview and plugin views: one source at a time.
    const auto one = SourceRules::oneSource();
    const auto all = resolveAnalysisSource( parse( "all" ), open, one );
    REQUIRE( all.reason == "invalid_source" );
    REQUIRE_FALSE( all.message.isEmpty() );
    REQUIRE( resolveAnalysisSource( parse( "tab:u1" ), open, one ).tabIds == QStringList{ "u1" } );
    REQUIRE( resolveAnalysisSource( parse( "file:/logs/a.log" ), open, one ).tabIds
             == QStringList{ "f1" } );

    // Script Runner: live COM tabs only; a file is never a live session.
    const auto com = SourceRules::liveComTabs();
    REQUIRE( resolveAnalysisSource( parse( "all" ), open, com ).tabIds
             == QStringList{ "c1", "c2" } );
    REQUIRE( resolveAnalysisSource( parse( "focused" ), open, com ).tabIds == QStringList{ "c1" } );
    REQUIRE( resolveAnalysisSource( parse( "tab:c2" ), open, com ).tabIds == QStringList{ "c2" } );
    REQUIRE( resolveAnalysisSource( parse( "tab:f1" ), open, com ).reason == "not_com_tab" );
    REQUIRE( resolveAnalysisSource( parse( "file:/captures/c1.log" ), open, com ).reason
             == "invalid_source" );
    const auto noCom = OpenSource::fromVariantList(
        { QVariantMap{ { "tabId", "f1" }, { "filePath", "/logs/a.log" }, { "focused", true } } } );
    REQUIRE( resolveAnalysisSource( parse( "all" ), noCom, com ).reason == "no_open_sources" );
    REQUIRE( resolveAnalysisSource( parse( "focused" ), noCom, com ).reason == "not_com_tab" );

    // The default rules are U15a's: everything is allowed.
    REQUIRE( resolveAnalysisSource( parse( "all" ), open ).tabIds.size() == 4 );

    // get_info windows become the same source list the pickers use.
    const QVariantList windows{
        QVariantMap{ { "windowIndex", 0 },
                     { "isActiveWindow", false },
                     { "currentTabId", "f1" },
                     { "tabs", QVariantList{ QVariantMap{ { "tabId", "f1" },
                                                          { "tabIndex", 0 },
                                                          { "filePath", "/logs/a.log" },
                                                          { "displayName", "a.log" },
                                                          { "sourceType", "file" } } } } },
        QVariantMap{ { "windowIndex", 1 },
                     { "isActiveWindow", true },
                     { "currentTabId", "c1" },
                     { "tabs", QVariantList{ QVariantMap{ { "tabId", "c1" },
                                                          { "tabIndex", 0 },
                                                          { "filePath", "/captures/c1.log" },
                                                          { "displayName", "COM5" },
                                                          { "sourceType", "com" } } } } },
    };
    const auto listed = OpenSource::fromVariantList( analysisSourcesFromWindowInfo( windows ) );
    REQUIRE( listed.size() == 2 );
    REQUIRE_FALSE( listed.at( 0 ).focused );
    REQUIRE( listed.at( 1 ).focused );
    REQUIRE( listed.at( 1 ).windowIndex == 1 );
    REQUIRE( listed.at( 1 ).sourceType == "com" );
    REQUIRE( listed.at( 1 ).label.contains( "COM5" ) );
    REQUIRE( listed.at( 1 ).label.contains( "2" ) ); // names the second window
}

TEST_CASE( "all_open_files_every_tab", "[workbench][U15]" )
{
    REQUIRE( resolveAnalysisSource( *AnalysisSource::parse( "all" ), {} ).reason
             == "no_open_sources" );
    REQUIRE( resolveAnalysisSource( *AnalysisSource::parse( "focused" ), {} ).reason
             == "no_focused_tab" );
    // No cap: the docks start the jobs as the analysis service admits them.
    QVariantList many;
    QStringList ids;
    for ( int index = 0; index < 40; ++index ) {
        ids << QString( "t%1" ).arg( index );
        many << QVariantMap{ { "tabId", ids.back() }, { "filePath", "/logs/" + ids.back() } };
    }
    const auto all
        = resolveAnalysisSource( *AnalysisSource::parse( "all" ), OpenSource::fromVariantList( many ) );
    REQUIRE( all.ok() );
    REQUIRE( all.tabIds == ids );
}
