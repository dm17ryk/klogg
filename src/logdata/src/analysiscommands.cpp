// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysiscommands.h"
#include "analysisregex.h"
#include "log.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <limits>
#include <set>

namespace {
AnalysisCommandResult failure( const QString& reason )
{
    LOG_DEBUG << "Analysis command rejected reason=" << reason;
    return { false, { { "reason", reason } } };
}
bool onlyKeys( const QVariantMap& arguments, const std::set<QString>& allowed )
{
    for ( auto it = arguments.cbegin(); it != arguments.cend(); ++it ) {
        if ( allowed.count( it.key() ) == 0 ) {
            return false;
        }
    }
    return true;
}
bool integer( const QVariant& value, qint64& output )
{
    if ( value.userType() == QMetaType::Bool || value.userType() == QMetaType::QString ) {
        return false;
    }
    bool valid = false;
    output = value.toLongLong( &valid );
    return valid && value.toDouble() == static_cast<double>( output );
}
} // namespace
AnalysisJobService* applicationAnalysisService( bool create )
{
    auto* app = QCoreApplication::instance();
    if ( !app ) {
        return nullptr;
    }
    auto* service
        = app->findChild<AnalysisJobService*>( "analysisJobService", Qt::FindDirectChildrenOnly );
    if ( !service && create ) {
        service = new AnalysisJobService( app );
        service->setObjectName( "analysisJobService" );
    }
    return service;
}

std::optional<std::vector<std::pair<quint64, quint64>>>
freezeAnalysisRanges( const std::vector<std::pair<quint64, quint64>>& ranges,
                      const std::function<quint64( quint64 )>& physicalRow, QString* error )
{
    std::vector<std::pair<quint64, quint64>> result;
    const auto fail =
        [ error ]( const char* reason ) -> std::optional<std::vector<std::pair<quint64, quint64>>> {
        if ( error )
            *error = QLatin1String( reason );
        return {};
    };
    if ( ranges.size() > 10000 )
        return fail( "scope_limit" );
    std::optional<quint64> previousOrdinal;
    for ( const auto& range : ranges ) {
        if ( range.first > range.second )
            return fail( "invalid_scope" );
        if ( previousOrdinal && range.first <= *previousOrdinal )
            return fail( "invalid_scope" );
        previousOrdinal = range.second;
        auto ordinal = range.first;
        for ( ;; ) {
            const auto first = physicalRow( ordinal );
            if ( !result.empty() && first <= result.back().second )
                return fail( "invalid_scope" );
            quint64 left = ordinal;
            auto right = range.second;
            while ( left < right ) {
                const auto middle = left + ( right - left ) / 2 + ( right - left ) % 2;
                const auto distance = middle - ordinal;
                if ( distance <= std::numeric_limits<quint64>::max() - first
                     && physicalRow( middle ) == first + distance )
                    left = middle;
                else
                    right = middle - 1;
            }
            const auto last = physicalRow( left );
            if ( last < first )
                return fail( "invalid_scope" );
            if ( !result.empty() && first - result.back().second == 1 ) {
                result.back().second = std::max( result.back().second, last );
            }
            else {
                if ( result.size() >= 10000 )
                    return fail( "scope_limit" );
                result.emplace_back( first, last );
            }
            if ( left == range.second )
                break;
            ordinal = left + 1;
        }
    }
    LOG_DEBUG << "Analysis scope frozen inputRanges=" << ranges.size()
              << " physicalRanges=" << result.size();
    return result;
}

AnalysisCommandResult executeAnalysisOperation( AnalysisJobService& service,
                                                const QString& operation,
                                                const QVariantMap& arguments,
                                                std::optional<AnalysisRequest> source,
                                                QString workerExecutable )
{
    if ( operation == "regex_evaluate" || operation == "value_counts" ) {
        auto keys = std::set<QString>{ "pattern", "options", "scope", "text", "requestRevision", "limits" };
        if ( operation == "value_counts" )
            keys.insert( "counts" );
        if ( !onlyKeys( arguments, keys )
             || arguments.value( "pattern" ).userType() != QMetaType::QString
             || ( arguments.contains( "options" )
                  && arguments.value( "options" ).userType() != QMetaType::QVariantMap )
             || ( arguments.contains( "scope" )
                  && arguments.value( "scope" ).userType() != QMetaType::QString )
             || ( arguments.contains( "limits" )
                  && arguments.value( "limits" ).userType() != QMetaType::QVariantMap ) ) {
            return failure( "invalid_request" );
        }
        const auto scope = arguments.value( "scope", "sample" ).toString();
        if ( scope != "sample" && scope != "selection" && scope != "matches"
             && scope != "whole_file" && scope != "text" ) {
            return failure( "invalid_scope" );
        }
        AnalysisRequest request;
        if ( scope == "text" ) {
            if ( arguments.value( "text" ).userType() != QMetaType::QString ) {
                return failure( "invalid_input" );
            }
            request.text = arguments.value( "text" ).toString();
        }
        else {
            if ( !source || !source->source ) {
                return failure( "source_not_found" );
            }
            if ( arguments.contains( "text" ) ) {
                return failure( "invalid_input" );
            }
            request = std::move( *source );
        }
        request.scope = scope;
        if ( operation == "value_counts" ) {
            if ( arguments.contains( "counts" )
                 && arguments.value( "counts" ).userType() != QMetaType::QVariantMap )
                return failure( "invalid_count_options" );
            request.valueCounts = parseValueCountOptions( arguments.value( "counts" ).toMap() );
            if ( !request.valueCounts )
                return failure( "invalid_count_options" );
        }
        if ( arguments.contains( "requestRevision" ) ) {
            const auto revision = arguments.value( "requestRevision" ).toString();
            bool valid = false;
            const auto parsed = revision.toULongLong( &valid );
            if ( !valid || revision.isEmpty()
                 || !std::all_of( revision.cbegin(), revision.cend(), []( QChar character ) {
                        return character >= QLatin1Char( '0' ) && character <= QLatin1Char( '9' );
                    } ) ) {
                return failure( "invalid_revision" );
            }
            request.requestRevision = parsed;
        }
        const auto limits = arguments.value( "limits" ).toMap();
        if ( !onlyKeys( limits,
                        { "batchLines", "batchBytes", "recordBytes", "pendingBytes", "resultBytes",
                          "resultRecords", "batchTimeoutMs", "timeoutMs" } ) ) {
            return failure( "invalid_limits" );
        }
        for ( auto it = limits.cbegin(); it != limits.cend(); ++it ) {
            qint64 value = 0;
            if ( !integer( it.value(), value ) || value < 1
                 || value > std::numeric_limits<int>::max() ) {
                return failure( "invalid_limits" );
            }
            const auto key = it.key();
            if ( key == "batchLines" )
                request.limits.batchLines = static_cast<int>( value );
            else if ( key == "batchBytes" )
                request.limits.batchBytes = value;
            else if ( key == "recordBytes" )
                request.limits.recordBytes = value;
            else if ( key == "pendingBytes" )
                request.limits.pendingBytes = value;
            else if ( key == "resultBytes" )
                request.limits.resultBytes = value;
            else if ( key == "resultRecords" )
                request.limits.resultRecords = static_cast<int>( value );
            else if ( key == "batchTimeoutMs" )
                request.limits.batchTimeoutMs = static_cast<int>( value );
            else if ( key == "timeoutMs" )
                request.limits.timeoutMs = static_cast<int>( value );
        }
        auto options = arguments.value( "options" ).toMap();
        if ( options.contains( "pattern" ) ) {
            return failure( "invalid_request" );
        }
        options[ "pattern" ] = arguments.value( "pattern" );
        QString error;
        const auto id = service.start(
            std::move( request ),
            makeRegexProcessor( std::move( options ), std::move( workerExecutable ) ), &error );
        return id.isEmpty() ? failure( error )
                            : AnalysisCommandResult{ true, service.status( id ) };
    }
    if ( operation == "get_value_count" || operation == "export_value_counts" ) {
        const bool exporting = operation == "export_value_counts";
        if ( !onlyKeys( arguments, exporting
                ? std::set<QString>{ "jobId", "filePath", "allowPartial" }
                : std::set<QString>{ "jobId", "rowIndex" } )
             || arguments.value( "jobId" ).userType() != QMetaType::QString
             || arguments.value( "jobId" ).toString().isEmpty()
             || arguments.value( "jobId" ).toString().size() > 128 )
            return failure( "invalid_request" );
        QVariantMap result;
        if ( exporting ) {
            if ( arguments.value( "filePath" ).userType() != QMetaType::QString
                 || ( arguments.contains( "allowPartial" )
                      && arguments.value( "allowPartial" ).userType() != QMetaType::Bool ) )
                return failure( "invalid_request" );
            result = service.exportValueCounts( arguments.value( "jobId" ).toString(),
                arguments.value( "filePath" ).toString(),
                arguments.value( "allowPartial" ).toBool() );
        }
        else {
            qint64 index = -1;
            if ( !integer( arguments.value( "rowIndex" ), index )
                 || index < 0 || index > std::numeric_limits<int>::max() )
                return failure( "invalid_result_index" );
            result = service.valueCountRecord( arguments.value( "jobId" ).toString(),
                                                static_cast<int>( index ) );
        }
        return { result.contains( "jobId" ), std::move( result ) };
    }
    if ( operation != "get_analysis_job" && operation != "get_analysis_results"
         && operation != "cancel_analysis_job" && operation != "release_analysis_job" ) {
        return failure( "unsupported_operation" );
    }
    if ( !onlyKeys( arguments, operation == "get_analysis_results"
                                   ? std::set<QString>{ "jobId", "cursor", "pageSize" }
                                   : std::set<QString>{ "jobId" } )
         || arguments.value( "jobId" ).userType() != QMetaType::QString ) {
        return failure( "invalid_request" );
    }
    const auto id = arguments.value( "jobId" ).toString();
    if ( id.isEmpty() || id.size() > 128 ) {
        return failure( "invalid_request" );
    }
    const auto state = service.status( id );
    if ( !state.contains( "jobId" ) ) {
        return failure( "job_expired" );
    }
    if ( operation == "get_analysis_job" ) {
        return { true, state };
    }
    if ( operation == "get_analysis_results" ) {
        if ( arguments.contains( "cursor" )
             && arguments.value( "cursor" ).userType() != QMetaType::QString ) {
            return failure( "invalid_cursor" );
        }
        const auto cursor = arguments.value( "cursor" ).toString();
        if ( cursor.size() > 128 ) {
            return failure( "invalid_cursor" );
        }
        qint64 count = 200;
        if ( arguments.contains( "pageSize" )
             && ( !integer( arguments.value( "pageSize" ), count )
                  || count < std::numeric_limits<int>::min()
                  || count > std::numeric_limits<int>::max() ) ) {
            return failure( "invalid_page_size" );
        }
        auto results = service.results( id, cursor, static_cast<int>( count ) );
        const bool valid = results.contains( "jobId" );
        return { valid, std::move( results ) };
    }
    if ( operation == "cancel_analysis_job" ) {
        service.cancel( id );
        return { true, { { "jobId", id }, { "cancelRequested", true } } };
    }
    service.release( id );
    return { true, { { "jobId", id }, { "released", true } } };
}
