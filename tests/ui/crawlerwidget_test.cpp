/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#include <QComboBox>
#include <QJsonDocument>
#include <QLineEdit>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <qglobal.h>
#include <qnamespace.h>
#include <qtestmouse.h>

#include "savedsearches.h"
#include "session.h"
#include "test_utils.h"

#include "infoline.h"
#include "logdata.h"
#include "logfiltereddata.h"

#include "crawlerwidget.h"

static const qint64 SL_NB_LINES = 100LL;

namespace {
bool generateDataFiles( QTemporaryFile& file )
{
    char newLine[ 90 ];

    if ( file.open() ) {
        for ( int i = 0; i < SL_NB_LINES; i++ ) {
            snprintf( newLine, 89,
                      "LOGDATA \t is a part of glogg, we are going to test it thoroughly, this is "
                      "line %06d",
                      i );
            file.write( newLine, static_cast<qint64>( qstrlen( newLine ) ) );
#ifdef Q_OS_WIN
            file.write( "\r\n", 2 );
#else
            file.write( "\n", 1 );
#endif
        }
        file.flush();
    }

    return true;
}

} // namespace

struct CrawlerWidgetPrivate {};

template <>
struct CrawlerWidget::access_by<CrawlerWidgetPrivate> {
    std::unique_ptr<CrawlerWidget> crawler;

    bool isLoadingFinished()
    {
        return !crawler->loadingInProgress_;
    }

    LinesCount getLogNbLines()
    {
        return crawler->logData_->getNbLine();
    }

    LinesCount getLogFilteredNbLines()
    {
        return crawler->logFilteredData_->getNbLine();
    }

    void selectAllInMainView()
    {
        crawler->logMainView_->selectAll();
    }

    void selectAllInFilteredView()
    {
        crawler->filteredView_->selectAll();
    }

    QString mainViewSelectedText()
    {
        return crawler->logMainView_->getSelectedText();
    }

    QString filteredViewSelectedText()
    {
        return crawler->filteredView_->getSelectedText();
    }

    void setSearchPattern( const QString& pattern )
    {
        clearSearchPattern();
        QTest::keyClicks( crawler->searchLineEdit_, pattern );
    }

    void clearSearchPattern()
    {
        crawler->searchLineEdit_->setFocus();
        QTest::keyClick( crawler->searchLineEdit_, Qt::Key_A, Qt::ControlModifier );
        QTest::keyClick( crawler->searchLineEdit_, Qt::Key_Delete );
        waitUiState( [ this ]() { return crawler->searchLineEdit_->currentText().isEmpty(); } );
    }

    void setAutoRefresh( bool enabled )
    {
        if ( crawler->searchRefreshButton_->isChecked() != enabled ) {
            QTest::mouseClick( crawler->searchRefreshButton_, Qt::LeftButton );
            waitUiState( [ this, enabled ]() {
                return crawler->searchRefreshButton_->isChecked() == enabled;
            } );
        }
    }

    void setViewContextLazy( const QString& context )
    {
        crawler->setViewContextLazy( context );
    }

    void enableCaseSensitiveSearch()
    {
        if ( !crawler->matchCaseButton_->isChecked() ) {
            QTest::mouseClick( crawler->matchCaseButton_, Qt::LeftButton );
            QTest::qWait( 100 );
        }
    }

    void enableRegexSearch()
    {
        if ( !crawler->useRegexpButton_->isChecked() ) {
            QTest::mouseClick( crawler->useRegexpButton_, Qt::LeftButton );
            QTest::qWait( 100 );
        }
    }

    void enableInverseMatch()
    {
        if ( !crawler->inverseButton_->isChecked() ) {
            QTest::mouseClick( crawler->inverseButton_, Qt::LeftButton );
            QTest::qWait( 100 );
        }
    }

    void enableBooleanCombinationMode()
    {
        if ( !crawler->booleanButton_->isChecked() ) {
            QTest::mouseClick( crawler->booleanButton_, Qt::LeftButton );
            QTest::qWait( 100 );
        }
    }

    void disableBooleanCombinationMode()
    {
        if ( crawler->booleanButton_->isChecked() ) {
            crawler->booleanButton_->setChecked( false );
            QTest::qWait( 50 );
        }
    }

    void runSearch()
    {
        QTest::mouseClick( crawler->searchButton_, Qt::LeftButton );

        QTest::qWait( 100 );

        waitUiState( [ & ]() { return crawler->stopButton_->isHidden(); } );
    }

    QString currentSearchText() const
    {
        return crawler->searchLineEdit_->currentText();
    }

    QString searchInfoText() const
    {
        return crawler->searchInfoLine_->text();
    }

    bool isSearchStopped() const
    {
        return crawler->stopButton_->isHidden();
    }

    bool isCaseSensitiveSearchEnabled() const
    {
        return crawler->matchCaseButton_->isChecked();
    }

    bool isRegexSearchEnabled() const
    {
        return crawler->useRegexpButton_->isChecked();
    }

    bool isInverseMatchEnabled() const
    {
        return crawler->inverseButton_->isChecked();
    }

    bool isBooleanCombinationModeEnabled() const
    {
        return crawler->booleanButton_->isChecked();
    }

    bool isAutoRefreshEnabled() const
    {
        return crawler->searchRefreshButton_->isChecked();
    }

    void setAdvancedSearchOptions( int before, int after, MatchMode mode,
                                   std::optional<int> maximum )
    {
        crawler->linkContextCheckBox_->setChecked( false );
        crawler->beforeContextSpinBox_->setValue( before );
        crawler->afterContextSpinBox_->setValue( after );
        crawler->matchModeComboBox_->setCurrentIndex(
            crawler->matchModeComboBox_->findData( static_cast<int>( mode ) ) );
        crawler->maxMatchesCheckBox_->setChecked( maximum.has_value() );
        if ( maximum ) {
            crawler->maxMatchesSpinBox_->setValue( *maximum );
        }
    }

    SearchOptions advancedSearchOptions() const
    {
        return crawler->currentSearchOptions();
    }

    MatchMode matchMode() const
    {
        return crawler->currentMatchMode();
    }

    void keepNextSearchResults()
    {
        crawler->keepSearchResultsButton_->setChecked( true );
    }

    void selectFilteredTab( int index )
    {
        crawler->tabbedFilteredView_->setCurrentIndex( index );
    }

    int filteredTabCount() const
    {
        return crawler->tabbedFilteredView_->count();
    }

    void enableTextWrap()
    {
        crawler->textWrapSet( true );
        waitUiState( [ this ]() { return crawler->isTextWrapEnabled(); } );
    }

    void disableTextWrap()
    {
        crawler->textWrapSet( false );
        waitUiState( [ this ]() { return !crawler->isTextWrapEnabled(); } );
    }

    void resizeAndShow( int width, int height )
    {
        crawler->resize( width, height );
        crawler->show();
        QTest::qWait( 100 );
    }

    void focusFilteredView()
    {
        crawler->filteredView_->setFocus();
        waitUiState( [ this ]() { return crawler->focusedViewObjectName() == "filteredView"; } );
    }

    QVariantMap filteredVisibleLineRange() const
    {
        return crawler->filteredVisibleLineRange();
    }

    QVariantMap mainVisibleLineRange() const
    {
        return crawler->mainVisibleLineRange();
    }

    QString focusedViewObjectName() const
    {
        return crawler->focusedViewObjectName();
    }

    void render()
    {
        crawler->grab();
    }
};

using CrawlerWidgetVisitor = CrawlerWidget::access_by<CrawlerWidgetPrivate>;

SCENARIO( "Crawler widget search", "[ui]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session;
    session.savedSearches().clear();

    REQUIRE( session.savedSearches().recentSearches().empty() );

    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

    waitUiState( [ & ]() { return crawlerVisitor.getLogNbLines().get() == SL_NB_LINES; } );
    waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } );

    crawlerVisitor.render();
    crawlerVisitor.disableBooleanCombinationMode();

    REQUIRE( crawlerVisitor.getLogNbLines().get() == SL_NB_LINES );

    GIVEN( "loaded log data" )
    {
        THEN( "Has no lines in log view" )
        {
            REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 0 );
        }

        WHEN( "search for lines" )
        {
            crawlerVisitor.setSearchPattern( "this is line" );
            crawlerVisitor.runSearch();

            REQUIRE( waitUiState( [ &crawlerVisitor ]() {
                return crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES;
            } ) );

            THEN( "all lines are matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES );
            }

            AND_WHEN( "copy all from main view" )
            {
                crawlerVisitor.selectAllInMainView();
                auto text = crawlerVisitor.mainViewSelectedText();
                THEN( "text has same number of lines" )
                {
                    REQUIRE( text.split( QChar::LineFeed ).size() == SL_NB_LINES );
                }
            }

            AND_WHEN( "copy all from filtered view" )
            {
                crawlerVisitor.selectAllInFilteredView();
                auto text = crawlerVisitor.filteredViewSelectedText();
                THEN( "text has same number of lines" )
                {
                    REQUIRE( text.split( QChar::LineFeed ).size() == SL_NB_LINES );
                }
            }
        }

        WHEN( "search for 10" )
        {
            crawlerVisitor.setSearchPattern( "10" );

            crawlerVisitor.runSearch();

            waitUiState( [ & ]() { return crawlerVisitor.getLogFilteredNbLines().get() == 1; } );

            THEN( "single line match" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 1 );
            }
        }

        WHEN( "case sensitive search" )
        {
            crawlerVisitor.setSearchPattern( "THIS" );
            crawlerVisitor.enableCaseSensitiveSearch();
            crawlerVisitor.runSearch();

            THEN( "no lines matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 0 );
            }
        }

        WHEN( "inverse match search" )
        {
            crawlerVisitor.setSearchPattern( "not match" );
            crawlerVisitor.enableInverseMatch();
            crawlerVisitor.runSearch();

            THEN( "all lines matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES );
            }
        }

        WHEN( "boolean search" )
        {
            crawlerVisitor.setSearchPattern( "\"glogg\" or \"klogg\"" );
            crawlerVisitor.enableBooleanCombinationMode();
            crawlerVisitor.runSearch();

            THEN( "has lines matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() >= 2 );
            }
        }
        WHEN( "auto-refresh is enabled with a pattern" )
        {
            crawlerVisitor.setAutoRefresh( false );
            crawlerVisitor.setSearchPattern( "this is line" );
            crawlerVisitor.setAutoRefresh( true );

            THEN( "search starts without pressing manual search" )
            {
                REQUIRE( waitUiState( [ &crawlerVisitor ]() {
                    return crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES;
                } ) );
            }

            AND_WHEN( "manual search is pressed after pattern change" )
            {
                crawlerVisitor.setAutoRefresh( false );
                crawlerVisitor.clearSearchPattern();
                crawlerVisitor.setSearchPattern( "line 000010" );
                crawlerVisitor.runSearch();

                THEN( "manual search recompiles and applies the new pattern" )
                {
                    REQUIRE( waitUiState( [ &crawlerVisitor ]() {
                        return crawlerVisitor.getLogFilteredNbLines().get() == 1;
                    } ) );
                }
            }

            AND_WHEN( "auto-refresh is toggled off then on after pattern change" )
            {
                crawlerVisitor.setAutoRefresh( false );
                crawlerVisitor.clearSearchPattern();
                crawlerVisitor.setSearchPattern( "definitely_not_present_pattern" );
                crawlerVisitor.setAutoRefresh( true );

                THEN( "re-enable auto-refresh recompiles and applies the new pattern" )
                {
                    REQUIRE( waitUiState( [ &crawlerVisitor ]() {
                        return crawlerVisitor.getLogFilteredNbLines().get() == 0;
                    } ) );
                }
            }
        }
    }
}

SCENARIO( "Crawler restore with invalid saved expressions", "[ui][startup]" )
{
    QTemporaryFile file{ "crawler_restore_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session;
    auto& savedSearches = session.savedSearches();

    struct SavedSearchesRestoreGuard {
        SavedSearches& searches;
        QStringList history;
        int historySize;

        ~SavedSearchesRestoreGuard()
        {
            searches.clear();
            searches.setHistorySize( historySize );
            for ( auto it = history.crbegin(); it != history.crend(); ++it ) {
                searches.addRecent( *it );
            }
        }
    };

    SavedSearchesRestoreGuard restoreGuard{ savedSearches, savedSearches.recentSearches(),
                                            savedSearches.historySize() };

    savedSearches.clear();
    savedSearches.addRecent( "((VOICE COMMAND)|(VOICE CMD)|(VPD Voice Commands)" );

    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

    REQUIRE( waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } ) );

    THEN( "search field is not auto-populated from history" )
    {
        REQUIRE( crawlerVisitor.currentSearchText().isEmpty() );
    }

    WHEN( "auto-refresh is restored with an invalid regex search pattern" )
    {
        crawlerVisitor.crawler->setViewContext(
            "{\"S\":[400,100],\"IC\":false,\"AR\":true,\"FF\":false,\"RE\":true,\"IR\":false,"
            "\"BC\":false,\"SP\":\"((VOICE COMMAND)|(VOICE CMD)|(VPD Voice Commands)\"}" );

        THEN( "startup remains alive and reports expression error" )
        {
            REQUIRE( waitUiState( [ &crawlerVisitor ]() {
                return crawlerVisitor.searchInfoText().contains( "Error in expression" );
            } ) );
            REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 0 );
        }
    }

    WHEN( "auto-refresh is restored with invalid boolean expression" )
    {
        crawlerVisitor.crawler->setViewContext(
            "{\"S\":[400,100],\"IC\":false,\"AR\":true,\"FF\":false,\"RE\":true,\"IR\":false,"
            "\"BC\":true,\"SP\":\"\\\"a\\\" and (\"}" );

        THEN( "startup remains alive and reports expression error" )
        {
            REQUIRE( waitUiState( [ &crawlerVisitor ]() {
                return crawlerVisitor.searchInfoText().contains( "Error in expression" );
            } ) );
            REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 0 );
        }
    }

    WHEN( "malformed view context is restored" )
    {
        crawlerVisitor.crawler->setViewContext( "not_a_json_context" );

        THEN( "restore does not crash and keeps search empty" )
        {
            REQUIRE( crawlerVisitor.currentSearchText().isEmpty() );
            REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 0 );
        }
    }
}

TEST_CASE( "Crawler widget full view context can be copied to another stream tab", "[ui][context]" )
{
    QTemporaryFile sourceFile{ "crawler_context_source_XXXXXX" };
    QTemporaryFile targetFile{ "crawler_context_target_XXXXXX" };
    REQUIRE( generateDataFiles( sourceFile ) );
    REQUIRE( generateDataFiles( targetFile ) );

    Session session;
    CrawlerWidgetVisitor sourceVisitor;
    sourceVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( sourceFile.fileName(), []() { return new CrawlerWidget(); } ) ) );
    REQUIRE( waitUiState( [ & ]() { return sourceVisitor.isLoadingFinished(); } ) );

    sourceVisitor.crawler->setSizes( { 321, 123 } );
    sourceVisitor.setSearchPattern( "\"LOGDATA\"" );
    sourceVisitor.enableCaseSensitiveSearch();
    sourceVisitor.enableRegexSearch();
    sourceVisitor.enableInverseMatch();
    sourceVisitor.enableBooleanCombinationMode();
    sourceVisitor.setAutoRefresh( true );
    sourceVisitor.disableBooleanCombinationMode();
    sourceVisitor.setAdvancedSearchOptions( 3, 5, MatchMode::WholeWord, 17 );

    const auto sourceContext = sourceVisitor.crawler->context();
    REQUIRE( sourceContext != nullptr );
    const auto serializedContext = sourceContext->toString();

    CrawlerWidgetVisitor targetVisitor;
    targetVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( targetFile.fileName(), []() { return new CrawlerWidget(); } ) ) );
    REQUIRE( waitUiState( [ & ]() { return targetVisitor.isLoadingFinished(); } ) );

    targetVisitor.crawler->setViewContext( serializedContext );
    REQUIRE( waitUiState( [ &targetVisitor ]() { return targetVisitor.isSearchStopped(); } ) );

    const auto copiedContext = targetVisitor.crawler->context();
    REQUIRE( copiedContext != nullptr );

    const auto sourceProperties
        = QJsonDocument::fromJson( serializedContext.toUtf8() ).toVariant().toMap();
    const auto copiedProperties
        = QJsonDocument::fromJson( copiedContext->toString().toUtf8() ).toVariant().toMap();

    REQUIRE( copiedProperties.value( "S" ).toList().size()
             == sourceProperties.value( "S" ).toList().size() );
    REQUIRE( copiedProperties.value( "S" ).toList().size() >= 2 );
    REQUIRE( copiedProperties.value( "SP" ).toString() == "\"LOGDATA\"" );
    REQUIRE( copiedProperties.value( "IC" ).toBool() == false );
    REQUIRE( copiedProperties.value( "AR" ).toBool() == true );
    REQUIRE( copiedProperties.value( "FF" ).toBool() == false );
    REQUIRE( copiedProperties.value( "RE" ).toBool() == true );
    REQUIRE( copiedProperties.value( "IR" ).toBool() == true );
    REQUIRE( copiedProperties.value( "BC" ).toBool() == false );
    REQUIRE( copiedProperties.value( "MM" ).toString() == "whole_word" );
    REQUIRE( copiedProperties.value( "CB" ).toULongLong() == 3 );
    REQUIRE( copiedProperties.value( "CA" ).toULongLong() == 5 );
    REQUIRE( copiedProperties.value( "ML" ).toULongLong() == 17 );
    REQUIRE( targetVisitor.currentSearchText() == "\"LOGDATA\"" );
    REQUIRE( targetVisitor.isCaseSensitiveSearchEnabled() );
    REQUIRE( targetVisitor.isRegexSearchEnabled() );
    REQUIRE( targetVisitor.isInverseMatchEnabled() );
    REQUIRE_FALSE( targetVisitor.isBooleanCombinationModeEnabled() );
    REQUIRE( targetVisitor.isAutoRefreshEnabled() );
    REQUIRE( targetVisitor.matchMode() == MatchMode::WholeWord );
    REQUIRE( targetVisitor.advancedSearchOptions().contextBefore == 3 );
    REQUIRE( targetVisitor.advancedSearchOptions().contextAfter == 5 );
    REQUIRE( targetVisitor.advancedSearchOptions().maxMatches == 17 );
}

TEST_CASE( "Crawler widget lazy view context restores controls before running filter",
           "[ui][context]" )
{
    QTemporaryFile targetFile{ "crawler_lazy_context_target_XXXXXX" };
    REQUIRE( generateDataFiles( targetFile ) );

    Session session;
    CrawlerWidgetVisitor targetVisitor;
    targetVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( targetFile.fileName(), []() { return new CrawlerWidget(); } ) ) );
    REQUIRE( waitUiState( [ & ]() { return targetVisitor.isLoadingFinished(); } ) );

    targetVisitor.setViewContextLazy(
        "{\"S\":[321,123],\"IC\":false,\"AR\":true,\"FF\":false,\"RE\":false,"
        "\"IR\":false,\"BC\":false,\"SP\":\"line 000010\"}" );

    REQUIRE( targetVisitor.currentSearchText() == "line 000010" );
    REQUIRE( targetVisitor.isAutoRefreshEnabled() );
    REQUIRE( targetVisitor.isSearchStopped() );
    REQUIRE( targetVisitor.getLogFilteredNbLines().get() == 0 );

    REQUIRE( waitUiState( [ &targetVisitor ]() {
        return targetVisitor.isSearchStopped() && targetVisitor.getLogFilteredNbLines().get() == 1;
    } ) );
}

TEST_CASE( "Retained result tabs restore their advanced filter snapshot", "[ui][filter-options]" )
{
    QTemporaryFile file{ "crawler_retained_filter_options_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session;
    CrawlerWidgetVisitor visitor;
    visitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );
    REQUIRE( waitUiState( [ & ] { return visitor.isLoadingFinished(); } ) );
    visitor.disableBooleanCombinationMode();

    visitor.setSearchPattern( "this is line" );
    visitor.setAdvancedSearchOptions( 1, 2, MatchMode::Contains, 2 );
    visitor.runSearch();

    visitor.keepNextSearchResults();
    visitor.setSearchPattern( "LOGDATA" );
    visitor.setAdvancedSearchOptions( 4, 5, MatchMode::WholeWord, std::nullopt );
    visitor.runSearch();
    REQUIRE( visitor.filteredTabCount() == 2 );

    visitor.selectFilteredTab( 0 );
    REQUIRE( waitUiState( [ & ] {
        return visitor.matchMode() == MatchMode::Contains
               && visitor.advancedSearchOptions().contextBefore == 1;
    } ) );
    REQUIRE( visitor.advancedSearchOptions().contextAfter == 2 );
    REQUIRE( visitor.advancedSearchOptions().maxMatches == 2 );

    visitor.selectFilteredTab( 1 );
    REQUIRE( waitUiState( [ & ] {
        return visitor.matchMode() == MatchMode::WholeWord
               && visitor.advancedSearchOptions().contextBefore == 4;
    } ) );
    REQUIRE( visitor.advancedSearchOptions().contextAfter == 5 );
    REQUIRE_FALSE( visitor.advancedSearchOptions().maxMatches.has_value() );
}

TEST_CASE( "Crawler widget lazy view context reports invalid filters asynchronously",
           "[ui][context]" )
{
    QTemporaryFile targetFile{ "crawler_lazy_invalid_context_XXXXXX" };
    REQUIRE( generateDataFiles( targetFile ) );

    Session session;
    CrawlerWidgetVisitor targetVisitor;
    targetVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( targetFile.fileName(), []() { return new CrawlerWidget(); } ) ) );
    REQUIRE( waitUiState( [ & ]() { return targetVisitor.isLoadingFinished(); } ) );

    targetVisitor.setViewContextLazy(
        "{\"S\":[321,123],\"IC\":false,\"AR\":true,\"FF\":false,\"RE\":true,"
        "\"IR\":false,\"BC\":false,\"SP\":\"((VOICE COMMAND)|(VOICE CMD)\"}" );

    REQUIRE( targetVisitor.currentSearchText() == "((VOICE COMMAND)|(VOICE CMD)" );
    REQUIRE( targetVisitor.isAutoRefreshEnabled() );
    REQUIRE( targetVisitor.isSearchStopped() );

    REQUIRE( waitUiState( [ &targetVisitor ]() {
        return targetVisitor.searchInfoText().contains( "Error in expression" );
    } ) );
    REQUIRE( targetVisitor.getLogFilteredNbLines().get() == 0 );
}

TEST_CASE( "Crawler widget exposes stable automation object names", "[ui][automation]" )
{
    QTemporaryFile file{ "crawler_automation_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session;
    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

    REQUIRE( waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } ) );
    crawlerVisitor.render();

    auto* crawler = crawlerVisitor.crawler.get();
    REQUIRE( crawler != nullptr );

    auto* visibilityCombo = crawler->findChild<QComboBox*>( "visibilityComboBox" );
    REQUIRE( visibilityCombo != nullptr );
    REQUIRE( visibilityCombo->accessibleName() == "Filtered view visibility" );

    auto* searchLineEdit = crawler->findChild<QComboBox*>( "searchLineEdit" );
    REQUIRE( searchLineEdit != nullptr );
    REQUIRE( searchLineEdit->accessibleName() == "Search pattern history" );

    auto* searchLineEditInner = crawler->findChild<QLineEdit*>( "searchLineEditInner" );
    REQUIRE( searchLineEditInner != nullptr );
    REQUIRE( searchLineEditInner->accessibleName() == "Search pattern" );

    REQUIRE( crawler->findChild<QToolButton*>( "matchCaseButton" ) != nullptr );
    REQUIRE( crawler->findChild<QToolButton*>( "useRegexpButton" ) != nullptr );
    REQUIRE( crawler->findChild<QToolButton*>( "inverseMatchButton" ) != nullptr );
    REQUIRE( crawler->findChild<QToolButton*>( "booleanSearchButton" ) != nullptr );
    REQUIRE( crawler->findChild<QToolButton*>( "searchRefreshButton" ) != nullptr );
    REQUIRE( crawler->findChild<QToolButton*>( "advancedFilterButton" ) != nullptr );
    REQUIRE( crawler->findChild<QSpinBox*>( "beforeContextSpinBox" ) != nullptr );
    REQUIRE( crawler->findChild<QSpinBox*>( "afterContextSpinBox" ) != nullptr );
    REQUIRE( crawler->findChild<QCheckBox*>( "linkContextCheckBox" ) != nullptr );
    auto* matchModeComboBox = crawler->findChild<QComboBox*>( "matchModeComboBox" );
    REQUIRE( matchModeComboBox != nullptr );
    REQUIRE( matchModeComboBox->accessibleName() == "Filter match mode" );
    REQUIRE( crawler->findChild<QCheckBox*>( "maxMatchesCheckBox" ) != nullptr );
    REQUIRE( crawler->findChild<QSpinBox*>( "maxMatchesSpinBox" ) != nullptr );
    REQUIRE( crawler->findChild<QToolButton*>( "clearSearchButton" ) != nullptr );
    REQUIRE( crawler->findChild<QToolButton*>( "searchButton" ) != nullptr );
    REQUIRE( crawler->findChild<QToolButton*>( "keepSearchResultsButton" ) != nullptr );
    REQUIRE( crawler->findChild<QToolButton*>( "stopSearchButton" ) != nullptr );
    REQUIRE( crawler->findChild<PredefinedFiltersComboBox*>( "predefinedFiltersComboBox" )
             != nullptr );
    REQUIRE( crawler->findChild<InfoLine*>( "searchInfoLine" ) != nullptr );
    REQUIRE( crawler->findChild<LogMainView*>( "logMainView" ) != nullptr );
    REQUIRE( crawler->findChild<FilteredView*>( "filteredView" ) != nullptr );
    REQUIRE( crawler->findChild<QTabWidget*>( "filteredViewsTabWidget" ) != nullptr );

    auto* booleanButton = crawler->findChild<QToolButton*>( "booleanSearchButton" );
    REQUIRE( booleanButton != nullptr );
    crawlerVisitor.disableBooleanCombinationMode();
    matchModeComboBox->setCurrentIndex(
        matchModeComboBox->findData( static_cast<int>( MatchMode::WholeLine ) ) );
    REQUIRE_FALSE( booleanButton->isEnabled() );
    matchModeComboBox->setCurrentIndex(
        matchModeComboBox->findData( static_cast<int>( MatchMode::Contains ) ) );
    REQUIRE( booleanButton->isEnabled() );
    booleanButton->setChecked( true );
    auto* matchModeModel = qobject_cast<QStandardItemModel*>( matchModeComboBox->model() );
    REQUIRE( matchModeModel != nullptr );
    REQUIRE_FALSE(
        matchModeModel
            ->item( matchModeComboBox->findData( static_cast<int>( MatchMode::WholeLine ) ) )
            ->isEnabled() );
}

TEST_CASE( "Crawler widget exposes semantic automation state", "[ui][automation]" )
{
    QTemporaryFile file{ "crawler_automation_state_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session;
    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

    REQUIRE( waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } ) );
    crawlerVisitor.disableBooleanCombinationMode();
    crawlerVisitor.disableTextWrap();
    crawlerVisitor.setSearchPattern( "this is line" );
    crawlerVisitor.runSearch();

    REQUIRE( waitUiState( [ & ]() {
        return crawlerVisitor.crawler->matchCount() == SL_NB_LINES
               && !crawlerVisitor.crawler->isSearchInProgress();
    } ) );

    auto* crawler = crawlerVisitor.crawler.get();
    REQUIRE( crawler != nullptr );

    const auto visibleRange = crawler->visibleLineRange();
    const auto mainVisibleRange = crawler->mainVisibleLineRange();
    const auto filteredVisibleRange = crawler->filteredVisibleLineRange();
    REQUIRE( crawler->searchText() == "this is line" );
    REQUIRE( crawler->matchCount() == SL_NB_LINES );
    REQUIRE_FALSE( crawler->isSearchInProgress() );
    REQUIRE_FALSE( crawler->isLoadingInProgress() );
    REQUIRE_FALSE( crawler->focusedViewObjectName().isEmpty() );
    REQUIRE( crawler->isTextWrapEnabled() == false );
    REQUIRE( visibleRange.value( "start" ).toULongLong() >= 1 );
    REQUIRE( visibleRange.value( "end" ).toULongLong()
             >= visibleRange.value( "start" ).toULongLong() );
    REQUIRE( mainVisibleRange.value( "start" ).toULongLong() >= 1 );
    REQUIRE( mainVisibleRange.value( "end" ).toULongLong()
             >= mainVisibleRange.value( "start" ).toULongLong() );
    REQUIRE( filteredVisibleRange.value( "start" ).toULongLong() >= 1 );
    REQUIRE( filteredVisibleRange.value( "end" ).toULongLong()
             >= filteredVisibleRange.value( "start" ).toULongLong() );
    REQUIRE( crawler->currentLineNumber().get() <= SL_NB_LINES );
    REQUIRE( crawler->lastErrorText().isEmpty() );
}

TEST_CASE( "Filtered visible range tracks wrapped rows", "[ui][wrap]" )
{
    QTemporaryFile file{ "crawler_wrap_range_test_XXXXXX.log" };
    REQUIRE( file.open() );

    const QString longPayload
        = "MATCH "
          "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA "
          "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB "
          "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC "
          "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD";
    const QString fileContents
        = "MATCH short one\n" + longPayload + "\n" + "MATCH short two\n" + "MATCH short three\n";

    REQUIRE( file.write( fileContents.toUtf8() ) == fileContents.toUtf8().size() );
    file.flush();

    Session session;
    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

    REQUIRE( waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } ) );
    crawlerVisitor.disableBooleanCombinationMode();
    crawlerVisitor.resizeAndShow( 520, 180 );
    crawlerVisitor.enableTextWrap();
    crawlerVisitor.setSearchPattern( "MATCH" );
    crawlerVisitor.runSearch();
    crawlerVisitor.focusFilteredView();
    crawlerVisitor.render();

    REQUIRE( waitUiState( [ & ]() { return crawlerVisitor.getLogFilteredNbLines().get() == 4; } ) );

    const auto filteredVisibleRange = crawlerVisitor.filteredVisibleLineRange();
    REQUIRE( filteredVisibleRange.value( "start" ).toULongLong() == 1 );
    REQUIRE( filteredVisibleRange.value( "end" ).toULongLong() >= 2 );
    REQUIRE( crawlerVisitor.focusedViewObjectName() == "filteredView" );
}
