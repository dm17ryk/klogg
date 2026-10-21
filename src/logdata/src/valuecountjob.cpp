// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "valuecountjob.h"
#include "log.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <vector>

namespace {
bool integer( const QVariant& value, int maximum, int& result )
{
    if ( value.userType() == QMetaType::Bool || value.userType() == QMetaType::QString )
        return false;
    bool ok = false;
    const auto number = value.toDouble( &ok );
    if ( !ok || !std::isfinite( number ) || number < 0 || number > maximum
         || number != std::floor( number ) )
        return false;
    result = static_cast<int>( number );
    return true;
}
} // namespace

std::optional<ValueCountOptions> parseValueCountOptions( const QVariantMap& values )
{
    ValueCountOptions result;
    for ( auto it = values.cbegin(); it != values.cend(); ++it ) {
        const auto key = it.key();
        const auto value = it.value();
        int number = 0;
        if ( key == "capture" ) {
            if ( value.userType() == QMetaType::QString ) {
                if ( value.toString().isEmpty() || value.toString().size() > 128 )
                    return {};
                result.capture = value;
            }
            else if ( integer( value, 128, number ) )
                result.capture = number;
            else
                return {};
        }
        else if ( key == "subpattern" && integer( value, 63, number ) )
            result.subpattern = number;
        else if ( key == "maxValues" && integer( value, 10000, number ) && number > 0 )
            result.maxValues = number;
        else if ( key == "caseSensitive" && value.userType() == QMetaType::Bool )
            result.caseSensitive = value.toBool();
        else if ( key == "missing" && value.userType() == QMetaType::QString
                  && ( value == "skip" || value == "include" ) )
            result.includeMissing = value == "include";
        else {
            LOG_DEBUG << "Value count option rejected key=" << key;
            return {};
        }
    }
    LOG_DEBUG << "Value count options accepted subpattern=" << result.subpattern
              << " namedCapture=" << ( result.capture.userType() == QMetaType::QString )
              << " caseSensitive=" << result.caseSensitive
              << " includeMissing=" << result.includeMissing << " maxValues=" << result.maxValues;
    return result;
}

ValueCountJob::ValueCountJob( ValueCountOptions options, int records, qint64 bytes )
    : options_( std::move( options ) )
    , maxValues_( std::min( records, options_.maxValues ) )
    , maxBytes_( bytes )
{
    LOG_DEBUG << "Value count reducer created maxValues=" << maxValues_
              << " maxBytes=" << maxBytes_;
}

QString ValueCountJob::add( const QVariantMap& group, const QVariant& line )
{
    const bool missing = !group.value( "matched" ).toBool();
    if ( missing && !options_.includeMissing ) {
        ++skipped_;
        return {};
    }
    auto value = missing ? QString{} : group.value( "value" ).toString();
    if ( !options_.caseSensitive )
        value = value.toCaseFolded();
    const auto key = ( missing ? QStringLiteral( "1" ) : QStringLiteral( "0" ) ) + value;
    auto found = entries_.find( key );
    if ( found == entries_.end() ) {
        if ( entries_.size() >= static_cast<size_t>( maxValues_ ) ) {
            LOG_DEBUG << "Value count unique limit reached entries=" << entries_.size()
                      << " counted=" << counted_ << " line=" << line.toULongLong();
            return "unique_value_limit";
        }
        // Covers the map node, key/display text, escaped output/filter and per-row
        // provenance. Conservative accounting bounds final pending rows as well.
        const auto size
            = QJsonDocument::fromVariant(
                  QVariantMap{ { "value", value },
                               { "filterPattern", QRegularExpression::escape( value ) } } )
                  .toJson( QJsonDocument::Compact )
                  .size();
        const qint64 cost = 2048 + static_cast<qint64>( value.size() ) * 4 + size * 2;
        if ( cost > maxBytes_ - bytes_ ) {
            LOG_DEBUG << "Value count memory limit reached bytes=" << bytes_
                      << " nextBytes=" << cost << " budget=" << maxBytes_;
            return "aggregation_memory_limit";
        }
        bytes_ += cost;
        found = entries_
                    .emplace( key, Entry{ value, missing, 0, line,
                                          group.value( "startUtf16", -1 ).toLongLong(),
                                          group.value( "lengthUtf16", 0 ).toLongLong() } )
                    .first;
    }
    if ( counted_ == std::numeric_limits<quint64>::max() )
        return "counter_limit";
    ++found->second.count;
    ++counted_;
    return {};
}

QString ValueCountJob::consume( const QVariantList& rows, const std::atomic_bool& cancelled )
{
    for ( const auto& rowValue : rows ) {
        if ( cancelled )
            return "cancelled";
        const auto row = rowValue.toMap();
        QVariantList matches;
        for ( const auto& subValue : row.value( "subpatterns" ).toList() ) {
            const auto sub = subValue.toMap();
            if ( sub.value( "index" ).toInt() == options_.subpattern ) {
                matches = sub.value( "matches" ).toList();
                break;
            }
        }
        if ( matches.isEmpty() ) {
            const auto error = add( {}, row.value( "lineNumber" ) );
            if ( !error.isEmpty() )
                return error;
        }
        for ( const auto& occurrence : matches ) {
            if ( cancelled )
                return "cancelled";
            QVariantMap selected;
            for ( const auto& groupValue : occurrence.toMap().value( "groups" ).toList() ) {
                const auto group = groupValue.toMap();
                const bool named = options_.capture.userType() == QMetaType::QString;
                if ( named ? group.value( "name" ) == options_.capture
                           : group.value( "number" ).toInt() == options_.capture.toInt() ) {
                    selected = group;
                    break;
                }
            }
            const auto error = add( selected, row.value( "lineNumber" ) );
            if ( !error.isEmpty() )
                return error;
        }
        ++rows_;
    }
    LOG_DEBUG << "Value count batch reduced rows=" << rows_ << " occurrences=" << counted_
              << " skippedMissing=" << skipped_ << " keys=" << entries_.size()
              << " accountedBytes=" << bytes_;
    return {};
}

QVariantList ValueCountJob::results() const
{
    std::vector<const Entry*> sorted;
    for ( const auto& item : entries_ )
        sorted.push_back( &item.second );
    std::sort( sorted.begin(), sorted.end(), []( const Entry* a, const Entry* b ) {
        if ( a->count != b->count )
            return a->count > b->count;
        if ( a->missing != b->missing )
            return !a->missing;
        return a->value < b->value;
    } );
    QVariantList result;
    for ( const auto* entry : sorted ) {
        result.push_back( QVariantMap{
            { "rowIndex", result.size() },
            { "value", entry->missing ? QVariant{} : QVariant( entry->value ) },
            { "missing", entry->missing },
            { "count", QString::number( entry->count ) },
            { "percentage", counted_ ? 100.0 * static_cast<double>( entry->count )
                                           / static_cast<double>( counted_ )
                                     : 0.0 },
            { "denominator", QString::number( counted_ ) },
            { "lineNumber", entry->line },
            { "startUtf16", entry->start },
            { "lengthUtf16", entry->length },
            { "filterPattern", entry->missing
                                   ? QVariant{}
                                   : QVariant( QRegularExpression::escape( entry->value ) ) },
            { "filterCaseSensitive", options_.caseSensitive },
            { "offsetConvention", "zero-based UTF-16" } } );
    }
    LOG_DEBUG << "Value count results finalized keys=" << result.size()
              << " denominator=" << counted_ << " bytes=" << bytes_;
    return result;
}

QVariantMap ValueCountJob::status() const
{
    return { { "countedOccurrences", QString::number( counted_ ) },
             { "skippedMissing", QString::number( skipped_ ) },
             { "processedRows", QString::number( rows_ ) },
             { "uniqueValues", static_cast<int>( entries_.size() ) },
             { "accountedBytes", bytes_ },
             { "maxBytes", maxBytes_ },
             { "maxValues", maxValues_ },
             { "denominator", "counted_occurrences" },
             { "capture", options_.capture },
             { "subpattern", options_.subpattern },
             { "caseSensitive", options_.caseSensitive },
             { "missing", options_.includeMissing ? "include" : "skip" } };
}
