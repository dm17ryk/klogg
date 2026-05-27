/*
 * Copyright (C) 2009, 2010 Nicolas Bonnefon and other contributors
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

#include <chrono>
#include <cmath>
#include <exception>
#include <memory>
#include <utility>

#include <QPointer>

#include <robin_hood.h>
#include <tbb/flow_graph.h>
#include <vector>

#include "configuration.h"
#include "dispatch_to.h"
#include "issuereporter.h"
#include "linetypes.h"
#include "log.h"
#include "progress.h"
#include "runnable_lambda.h"

#include "logdata.h"
#include "regularexpression.h"

#include "logfiltereddataworker.h"
#include "synchronization.h"

namespace {
struct PartialSearchResults {
    PartialSearchResults() = default;

    PartialSearchResults( const PartialSearchResults& ) = delete;
    PartialSearchResults( PartialSearchResults&& ) = default;
    PartialSearchResults& operator=( const PartialSearchResults& ) = delete;
    PartialSearchResults& operator=( PartialSearchResults&& ) = default;

    SearchResultArray matchingLines;
    LineLength maxLength;

    LineNumber chunkStart;
    LinesCount processedLines;
};

struct SearchBlockData {
    SearchBlockData() = default;
    SearchBlockData( LineNumber start, LogData::RawLines blockLines )
        : chunkStart( start )
        , lines( std::move( blockLines ) )
    {
    }

    SearchBlockData( const SearchBlockData& ) = delete;
    SearchBlockData( SearchBlockData&& ) = default;

    SearchBlockData& operator=( const SearchBlockData& ) = delete;
    SearchBlockData& operator=( SearchBlockData&& ) = default;

    LineNumber chunkStart;
    LogData::RawLines lines;
};

PartialSearchResults filterLines( const PatternMatcher& matcher, const LogData::RawLines& rawLines,
                                  LineNumber chunkStart )
{
    LOG_DEBUG << "Filter lines at " << chunkStart;
    PartialSearchResults results;
    results.chunkStart = chunkStart;
    results.processedLines = LinesCount{ rawLines.endOfLines.size() };

    const auto& lines = rawLines.buildUtf8View();

    for ( auto offset = 0u; offset < lines.size(); ++offset ) {
        const auto& line = lines[ offset ];

        const auto hasMatch = matcher.hasMatch( line );

        if ( hasMatch ) {
            results.maxLength = qMax( results.maxLength, getUntabifiedLength( line ) );
            const auto lineNumber = chunkStart + LinesCount{ offset };
            results.matchingLines.add( lineNumber.get() );

            // LOG_INFO << "Match at " << lineNumber << ": " << line;
        }
    }
    return results;
}

} // namespace

SearchResults SearchData::takeCurrentResults() const
{
    UniqueLock lock( dataMutex_ );
    return SearchResults{ std::exchange( newMatches_, {} ), maxLength_, nbLinesProcessed_ };
}

void SearchData::addAll( LineLength length, const SearchResultArray& matches,
                         LinesCount matchedLines, LinesCount processedLines )
{
    UniqueLock lock( dataMutex_ );

    maxLength_ = qMax( maxLength_, length );
    nbLinesProcessed_ = qMax( nbLinesProcessed_, processedLines );
    nbMatches_ += matchedLines;

    newMatches_ |= matches;
}

LinesCount SearchData::getNbMatches() const
{
    SharedLock lock( dataMutex_ );
    return nbMatches_;
}

LineNumber SearchData::getLastProcessedLine() const
{
    SharedLock lock( dataMutex_ );
    return LineNumber{ nbLinesProcessed_.get() };
}

void SearchData::deleteMatch( LineNumber line )
{
    UniqueLock lock( dataMutex_ );
    matches_.remove( line.get() );
}

void SearchData::clear()
{
    UniqueLock locker( dataMutex_ );

    maxLength_ = LineLength( 0 );
    nbLinesProcessed_ = LinesCount( 0 );
    nbMatches_ = LinesCount( 0 );
    matches_ = {};
    newMatches_ = {};
}

LogFilteredDataWorker::LogFilteredDataWorker( const LogData& sourceLogData )
    : sourceLogData_( sourceLogData )
    , interruptRequested_( std::make_shared<AtomicFlag>() )
{
    operationsPool_.setMaxThreadCount( 1 );
}

LogFilteredDataWorker::~LogFilteredDataWorker() noexcept
{
    try {
        interrupt();
        ScopedLock locker( operationsMutex_ );
        operationsPool_.waitForDone();
        LOG_INFO << "LogFilteredDataWorker shutdown";
    } catch ( const std::exception& e ) {
        LOG_ERROR << "Failed to destroy LogFilteredDataWorker: " << e.what();
    }
}

void LogFilteredDataWorker::connectSignalsAndRun( SearchOperation* operationRequested )
{
    const auto requestedGeneration = operationRequested->searchGeneration();
    LOG_DEBUG << "Connecting search operation signals for generation " << requestedGeneration;

    const QPointer<LogFilteredDataWorker> worker( this );
    const auto progressConnection
        = connect( operationRequested, &SearchOperation::searchProgressed, operationRequested,
                   [ worker ]( LinesCount nbMatches, int percent, LineNumber initialLine,
                               uint64_t searchGeneration ) {
                       if ( !worker ) {
                           LOG_DEBUG << "Dropping search progress for destroyed worker, generation "
                                     << searchGeneration;
                           return;
                       }

                       QMetaObject::invokeMethod(
                           worker,
                           [ worker, nbMatches, percent, initialLine, searchGeneration ] {
                               if ( !worker ) {
                                   LOG_DEBUG
                                       << "Skipping queued search progress for destroyed worker, generation "
                                       << searchGeneration;
                                   return;
                               }
                               Q_EMIT worker->searchProgressed( nbMatches, percent, initialLine,
                                                                searchGeneration );
                           },
                           Qt::QueuedConnection );
                   },
                   Qt::DirectConnection );
    const auto failedConnection
        = connect( operationRequested, &SearchOperation::searchFailed, operationRequested,
                   [ worker ]( QString errorMessage, uint64_t searchGeneration ) {
                       if ( !worker ) {
                           LOG_DEBUG << "Dropping search failure for destroyed worker, generation "
                                     << searchGeneration;
                           return;
                       }

                       QMetaObject::invokeMethod(
                           worker,
                           [ worker, errorMessage = std::move( errorMessage ), searchGeneration ] {
                               if ( !worker ) {
                                   LOG_DEBUG
                                       << "Skipping queued search failure for destroyed worker, generation "
                                       << searchGeneration;
                                   return;
                               }
                               Q_EMIT worker->searchFailed( errorMessage, searchGeneration );
                           },
                           Qt::QueuedConnection );
                   },
                   Qt::DirectConnection );

    LOG_DEBUG << "Running search operation for generation " << requestedGeneration;
    operationRequested->run( searchData_ );
    LOG_DEBUG << "Search operation returned for generation " << requestedGeneration;

    const auto compiledRegexp = operationRequested->compiledRegexp();
    if ( compiledRegexp && compiledRegexp->isValid() ) {
        LOG_DEBUG << "Remembering compiled search expression for generation " << requestedGeneration;
        rememberCompiledRegexp( operationRequested->regexp(), compiledRegexp );
    }
    else {
        LOG_DEBUG << "Clearing compiled search expression for generation " << requestedGeneration;
        clearCompiledRegexp( operationRequested->regexp() );
    }

    LOG_DEBUG << "Posting deterministic search finish for generation " << requestedGeneration;
    QMetaObject::invokeMethod(
        this,
        [ worker, searchGeneration = requestedGeneration ] {
            if ( !worker ) {
                LOG_DEBUG << "Skipping queued search finish for destroyed worker, generation "
                          << searchGeneration;
                return;
            }
            Q_EMIT worker->searchFinished( searchGeneration );
        },
        Qt::QueuedConnection );

    // Only sever the connections we created above; leave any other connections
    // (e.g. ones made by callers/tests) untouched.
    QObject::disconnect( progressConnection );
    QObject::disconnect( failedConnection );
}

std::shared_ptr<AtomicFlag> LogFilteredDataWorker::beginOperation()
{
    if ( interruptRequested_ ) {
        interruptRequested_->set();
    }

    interruptRequested_ = std::make_shared<AtomicFlag>();
    return interruptRequested_;
}

std::shared_ptr<RegularExpression>
LogFilteredDataWorker::compiledRegexpFor( const RegularExpressionPattern& regExp,
                                          const std::shared_ptr<RegularExpression>& suppliedRegexp )
{
    if ( suppliedRegexp ) {
        if ( suppliedRegexp->isValid() ) {
            rememberCompiledRegexp( regExp, suppliedRegexp );
        }
        return suppliedRegexp;
    }

    ScopedLock locker( compiledRegexpMutex_ );
    if ( cachedCompiledRegExp_ && cachedCompiledRegExp_->isValid()
         && cachedRegExpPattern_ == regExp ) {
        LOG_DEBUG << "Reusing worker cached search expression";
        return cachedCompiledRegExp_;
    }

    return {};
}

void LogFilteredDataWorker::rememberCompiledRegexp(
    const RegularExpressionPattern& regExp,
    const std::shared_ptr<RegularExpression>& compiledRegexp )
{
    ScopedLock locker( compiledRegexpMutex_ );
    cachedRegExpPattern_ = regExp;
    cachedCompiledRegExp_ = compiledRegexp;
}

void LogFilteredDataWorker::clearCompiledRegexp( const RegularExpressionPattern& regExp )
{
    ScopedLock locker( compiledRegexpMutex_ );
    if ( cachedRegExpPattern_ == regExp ) {
        cachedRegExpPattern_ = {};
        cachedCompiledRegExp_.reset();
    }
}

void LogFilteredDataWorker::search( const RegularExpressionPattern& regExp,
                                    std::shared_ptr<RegularExpression> compiledRegexp,
                                    LineNumber startLine, LineNumber endLine,
                                    uint64_t searchGeneration )
{
    ScopedLock locker( operationsMutex_ );
    auto operationInterrupt = beginOperation();

    LOG_INFO << "Search requested";
    operationsPool_.start(
        createRunnable( [ this, regExp, compiledRegexp, startLine, endLine, searchGeneration,
                          operationInterrupt ] {
            auto operationRequested = std::make_unique<FullSearchOperation>(
                sourceLogData_, operationInterrupt, regExp,
                compiledRegexpFor( regExp, compiledRegexp ), startLine, endLine, searchGeneration );
            connectSignalsAndRun( operationRequested.get() );
        } ) );
}

void LogFilteredDataWorker::updateSearch( const RegularExpressionPattern& regExp,
                                          std::shared_ptr<RegularExpression> compiledRegexp,
                                          LineNumber startLine, LineNumber endLine,
                                          LineNumber position, uint64_t searchGeneration )
{
    ScopedLock locker( operationsMutex_ );
    auto operationInterrupt = beginOperation();

    LOG_INFO << "Search update requested from " << position.get();

    operationsPool_.start(
        createRunnable( [ this, regExp, compiledRegexp, startLine, endLine, position,
                          searchGeneration, operationInterrupt ] {
            auto operationRequested = std::make_unique<UpdateSearchOperation>(
                sourceLogData_, operationInterrupt, regExp,
                compiledRegexpFor( regExp, compiledRegexp ), startLine, endLine, position,
                searchGeneration );
            connectSignalsAndRun( operationRequested.get() );
        } ) );
}

void LogFilteredDataWorker::interrupt()
{
    LOG_INFO << "Search interruption requested";
    ScopedLock locker( operationsMutex_ );
    if ( interruptRequested_ ) {
        interruptRequested_->set();
    }
}

// This will do an atomic copy of the object
SearchResults LogFilteredDataWorker::getSearchResults() const
{
    return searchData_.takeCurrentResults();
}

//
// Operations implementation
//

SearchOperation::SearchOperation( const LogData& sourceLogData,
                                  std::shared_ptr<AtomicFlag> interruptRequested,
                                  const RegularExpressionPattern& regExp,
                                  std::shared_ptr<RegularExpression> compiledRegexp,
                                  LineNumber startLine, LineNumber endLine,
                                  uint64_t searchGeneration )

    : interruptRequested_( std::move( interruptRequested ) )
    , regexp_( regExp )
    , compiledRegexp_( std::move( compiledRegexp ) )
    , sourceLogData_( sourceLogData )
    , startLine_( startLine )
    , endLine_( endLine )
    , searchGeneration_( searchGeneration )

{
}

void SearchOperation::doSearch( SearchData& searchData, LineNumber initialLine )
{
    if ( !compiledRegexp_ ) {
        compiledRegexp_ = std::make_shared<RegularExpression>( regexp_ );
    }

    if ( !compiledRegexp_->isValid() ) {
        LOG_WARNING << "Skipping search: invalid expression " << compiledRegexp_->errorString();
        Q_EMIT searchFailed( compiledRegexp_->errorString(), searchGeneration_ );
        Q_EMIT searchFinished( searchGeneration_ );
        return;
    }

    const auto nbSourceLines = sourceLogData_.getNbLine();

    LOG_INFO << "Searching from line " << initialLine << " to " << nbSourceLines;

    using namespace std::chrono;
    high_resolution_clock::time_point t1 = high_resolution_clock::now();

    const auto& config = Configuration::get();
    const auto matchingThreadsCount = static_cast<uint32_t>( [ &config ]() {
        if ( !config.useParallelSearch() ) {
            return 1;
        }
        const auto configuredThreadPoolSize = config.searchThreadPoolSize();
        return qMax( 1, configuredThreadPoolSize == 0 ? tbb::info::default_concurrency()
                                                      : configuredThreadPoolSize );
    }() );

    LOG_INFO << "Using " << matchingThreadsCount << " matching threads";

    tbb::flow::graph searchGraph;

    if ( initialLine < startLine_ ) {
        initialLine = startLine_;
    }

    const auto endLine = qMin( LineNumber( nbSourceLines.get() ), endLine_ );
    const auto nbLinesInChunk = LinesCount(
        static_cast<LinesCount::UnderlyingType>( config.searchReadBufferSizeLines() ) );

    std::chrono::microseconds fileReadingDuration{ 0 };

    using BlockDataType = std::shared_ptr<SearchBlockData>;
    using BlockResultsType = std::shared_ptr<PartialSearchResults>;
    auto blockPrefetcher
        = tbb::flow::limiter_node<BlockDataType>( searchGraph, matchingThreadsCount * 3 );

    auto lineBlocksQueue = tbb::flow::buffer_node<BlockDataType>( searchGraph );

    using RegexMatcherNode
        = tbb::flow::function_node<BlockDataType, BlockResultsType, tbb::flow::rejecting>;

    using PatternMatcherPtr = std::unique_ptr<PatternMatcher>;
    using MatcherContext = std::tuple<PatternMatcherPtr, microseconds, RegexMatcherNode>;

    klogg::vector<MatcherContext> regexMatchers;
    regexMatchers.reserve( matchingThreadsCount );
    for ( auto index = 0u; index < matchingThreadsCount; ++index ) {
        auto matcher = compiledRegexp_->createMatcher();
        if ( !matcher ) {
            LOG_ERROR << "Skipping search: failed to create matcher #" << index;
            Q_EMIT searchFailed( tr( "failed to create matcher" ), searchGeneration_ );
            Q_EMIT searchFinished( searchGeneration_ );
            return;
        }

        regexMatchers.emplace_back(
            std::move( matcher ), microseconds{ 0 },
            RegexMatcherNode( searchGraph, 1,
                              [ &regexMatchers, index, this ]( const BlockDataType& blockData ) {
                                  auto searchResults = std::make_shared<PartialSearchResults>();

                    if ( *interruptRequested_ ) {
                        LOG_INFO << "Matcher " << index << " interrupted";
                                  searchResults->chunkStart = blockData->chunkStart;
                                  searchResults->processedLines
                            = LinesCount{ blockData->lines.endOfLines.size() };
                                  return searchResults;
                    }

                    const auto& matcherPtr = std::get<PatternMatcherPtr>( regexMatchers.at( index ) );
                    const auto matchStartTime = high_resolution_clock::now();

                                  *searchResults
                        = filterLines( *matcherPtr, blockData->lines, blockData->chunkStart );

                    const auto matchEndTime = high_resolution_clock::now();

                    microseconds& matchDuration
                        = std::get<microseconds>( regexMatchers.at( index ) );
                    matchDuration += duration_cast<microseconds>( matchEndTime - matchStartTime );
                                  LOG_DEBUG << "Searcher " << index << " block "
                                            << blockData->chunkStart
                              << " sending matches "
                                            << searchResults->matchingLines.cardinality();
                                  return searchResults;
                              } ) );
    }

    auto resultsQueue = tbb::flow::buffer_node<BlockResultsType>( searchGraph );

    const auto totalLines = endLine - initialLine;
    LinesCount totalProcessedLines = 0_lcount;
    LineLength maxLength = 0_length;
    LinesCount nbMatches = searchData.getNbMatches();
    auto reportedMatches = nbMatches;
    int reportedPercentage = 0;

    std::chrono::microseconds matchCombiningDuration{ 0 };

    auto matchProcessor
        = tbb::flow::function_node<BlockResultsType, tbb::flow::continue_msg,
                                   tbb::flow::rejecting>(
            searchGraph, 1, [ & ]( const BlockResultsType& matchResultsPtr ) {
                if ( *interruptRequested_ ) {
                    LOG_INFO << "Match processor interrupted";
                    return tbb::flow::continue_msg{};
                }

                if ( !matchResultsPtr ) {
                    return tbb::flow::continue_msg{};
                }

                const auto& matchResults = *matchResultsPtr;

                const auto matchProcessorStartTime = high_resolution_clock::now();

                if ( matchResults.processedLines.get() ) {

                    maxLength = qMax( maxLength, matchResults.maxLength );
                    const LinesCount matchesCount
                        = LinesCount( matchResults.matchingLines.cardinality() );
                    nbMatches += matchesCount;

                    const auto processedLines = LinesCount{ matchResults.chunkStart.get()
                                                            + matchResults.processedLines.get() };

                    totalProcessedLines += matchResults.processedLines;

                    // After each block, copy the data to shared data
                    // and update the client
                    searchData.addAll( maxLength, matchResults.matchingLines, matchesCount,
                                       processedLines );

                    LOG_DEBUG << "done Searching chunk starting at " << matchResults.chunkStart
                              << ", " << matchResults.processedLines << " lines read.";
                }

                const auto matchProcessorEndTime = high_resolution_clock::now();
                matchCombiningDuration += duration_cast<microseconds>( matchProcessorEndTime
                                                                       - matchProcessorStartTime );
                const int percentage
                    = calculateProgress( totalProcessedLines.get(), totalLines.get() );

                if ( percentage > reportedPercentage || nbMatches > reportedMatches ) {

                    Q_EMIT searchProgressed( nbMatches, std::min( 99, percentage ), initialLine,
                                             searchGeneration_ );

                    reportedPercentage = percentage;
                    reportedMatches = nbMatches;
                }

                return tbb::flow::continue_msg{};
            } );

    tbb::flow::make_edge( blockPrefetcher, lineBlocksQueue );

    for ( auto& regexMatcher : regexMatchers ) {
        tbb::flow::make_edge( lineBlocksQueue, std::get<RegexMatcherNode>( regexMatcher ) );
        tbb::flow::make_edge( std::get<RegexMatcherNode>( regexMatcher ), resultsQueue );
    }

    tbb::flow::make_edge( resultsQueue, matchProcessor );
    tbb::flow::make_edge( matchProcessor, blockPrefetcher.decrementer() );

    auto chunkStart = initialLine;
    while ( chunkStart < endLine && !*interruptRequested_ ) {
        const auto lineSourceStartTime = high_resolution_clock::now();
        LOG_DEBUG << "Reading chunk starting at " << chunkStart;

        const auto linesInChunk
            = LinesCount( qMin( nbLinesInChunk.get(), ( endLine - chunkStart ).get() ) );
        auto lines = sourceLogData_.getLinesRaw( chunkStart, linesInChunk );

        /*LOG_DEBUG << "Sending chunk starting at " << chunkStart << ", " <<
            lines.second.size()
                << " lines read.";*/
        auto blockData
            = std::make_shared<SearchBlockData>( SearchBlockData{ chunkStart, std::move( lines ) } );

        const auto lineSourceEndTime = high_resolution_clock::now();
        const auto chunkReadTime
            = duration_cast<microseconds>( lineSourceEndTime - lineSourceStartTime );

        /*LOG_DEBUG << "Sent chunk starting at " << chunkStart << ", " <<
        blockData->lines.second.size()
                << " lines read in " << static_cast<float>( chunkReadTime.count() )
        / 1000.f
                << " ms";*/

        chunkStart = chunkStart + nbLinesInChunk;
        fileReadingDuration += chunkReadTime;

        while ( !blockPrefetcher.try_put( blockData ) && !*interruptRequested_ ) {
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
    }

    searchGraph.wait_for_all();

    high_resolution_clock::time_point t2 = high_resolution_clock::now();
    const auto durationUs = duration_cast<microseconds>( t2 - t1 );
    const auto durationMs = duration_cast<milliseconds>( t2 - t1 );

    LOG_INFO << "Searching done, overall duration " << durationUs;
    LOG_INFO << "Line reading took " << fileReadingDuration;
    LOG_INFO << "Results combining took " << matchCombiningDuration;

    for ( const auto& regexMatcher : regexMatchers ) {
        LOG_INFO << "Matching took " << std::get<microseconds>( regexMatcher );
    }

    const auto totalFileSize = sourceLogData_.getFileSize();

    LOG_INFO << "Searching perf "
             << static_cast<uint64_t>(
                    std::floor( 1000.f * static_cast<float>( ( endLine - initialLine ).get() )
                                / static_cast<float>( durationMs.count() ) ) )
             << " lines/s";
    LOG_INFO << "Searching io perf "
             << ( 1000.f * static_cast<float>( totalFileSize )
                  / static_cast<float>( durationMs.count() ) )
                    / ( 1024 * 1024 )
             << " MiB/s";

    Q_EMIT searchProgressed( nbMatches, 100, initialLine, searchGeneration_ );
    Q_EMIT searchFinished( searchGeneration_ );
}

// Called in the worker thread's context
void FullSearchOperation::run( SearchData& searchData )
{
    try {
        // Clear the shared data
        searchData.clear();
        doSearch( searchData, 0_lnum );
    } catch ( const std::exception& err ) {
        const auto errorString = QString( "FullSearchOperation failed: %1" ).arg( err.what() );
        LOG_ERROR << errorString;
        dispatchToMainThread( [ errorString ]() {
            IssueReporter::askUserAndReportIssue( IssueTemplate::Exception, errorString );
        } );
        searchData.clear();
    }
}

// Called in the worker thread's context
void UpdateSearchOperation::run( SearchData& searchData )
{
    try {
        auto initialLine = qMax( searchData.getLastProcessedLine(), initialPosition_ );

        if ( initialLine.get() >= 1 ) {
            // We need to re-search the last line because it might have
            // been updated (if it was not LF-terminated)
            --initialLine;
            // In case the last line matched, we don't want it to match twice.
            searchData.deleteMatch( initialLine );
        }

        doSearch( searchData, initialLine );
    } catch ( const std::exception& err ) {
        const auto errorString = QString( "UpdateSearchOpertaion failed: %1" ).arg( err.what() );
        LOG_ERROR << errorString;
        dispatchToMainThread( [ errorString ]() {
            IssueReporter::askUserAndReportIssue( IssueTemplate::Exception, errorString );
        } );
        searchData.clear();
    }
}
