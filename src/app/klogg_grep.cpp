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

#include <mimalloc.h>

#include "configuration.h"
#include "dispatch_to.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logger.h"
#include "persistentinfo.h"

#include "cli.h"

const bool PersistentInfo::ForcePortable = true;

int main( int argc, char* argv[] )
{
#ifdef KLOGG_USE_MIMALLOC
    mi_stats_reset();
#endif
    qRegisterMetaType<LinesCount>( "LinesCount" );
    qRegisterMetaType<LineNumber>( "LineNumber" );

    QCoreApplication app( argc, argv );
    CliParameters parameters( app, true );
    if ( parameters.exit_requested ) {
        std::cout << parameters.exit_message.toStdString();
        if ( !parameters.exit_message.endsWith( '\n' ) ) {
            std::cout << "\n";
        }
        return parameters.exit_code;
    }
    if ( parameters.parse_error ) {
        std::cerr << parameters.parse_error_message.toStdString();
        if ( !parameters.parse_error_message.endsWith( '\n' ) ) {
            std::cerr << "\n";
        }
        return EXIT_FAILURE;
    }

    logging::enableLogging( true, static_cast<logging::LogLevel>( parameters.log_level ) );

    auto configuration = Configuration::getSynced();

    LogData logData;
    auto filteredData = logData.getNewFilteredData();

    filteredData->connect(
        filteredData.get(), &LogFilteredData::searchProgressed,
        [ & ]( LinesCount nbMatches, int progress, LineNumber ) {
            if ( progress == 100 ) {

                const auto nbDisplayedLines = filteredData->getNbLine();
                LOG_INFO << "Search finished, got " << nbMatches.get() << " true matches and "
                         << nbDisplayedLines.get() << " displayed lines";

                const auto defaultChunkSize = 1000_lcount;
                for ( auto chunkStart = 0_lnum; chunkStart < nbDisplayedLines;
                      chunkStart = chunkStart + defaultChunkSize ) {
                    auto chunkSize = std::min( defaultChunkSize.get(),
                                               nbDisplayedLines.get() - chunkStart.get() );
                    auto lines = filteredData->getLines( chunkStart, LinesCount( chunkSize ) );
                    for ( const auto& l : lines ) {
                        std::cout << l.toStdString() << "\n";
                    }
                }

                exit( EXIT_SUCCESS );
            }
        } );

    logData.connect( &logData, &LogData::loadingFinished, [ & ]() {
        dispatchToMainThread( [ & ] {
            SearchOptions options;
            options.contextBefore = parameters.before_context;
            options.contextAfter = parameters.after_context;
            options.maxMatches = parameters.max_matches;
            filteredData->runSearch( RegularExpressionPattern( parameters.pattern, false, false,
                                                               false, false,
                                                               parameters.match_mode ),
                                     options );
        } );
    } );

    logData.attachFile( parameters.filenames.front() );
    return app.exec();
}
