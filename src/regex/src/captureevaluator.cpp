// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "captureevaluator.h"
#include "booleanevaluator.h"
#include "booleanparser.h"
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <set>

namespace {
constexpr qint64 OutputBytes = 4 * 1024 * 1024;
constexpr int MatchRecords = 10000;
qint64 serializedBytes( const QVariantMap& value )
{
    return QJsonDocument( QJsonObject::fromVariantMap( value ) )
        .toJson( QJsonDocument::Compact )
        .size();
}
} // namespace

struct CaptureEvaluator::Private {
    std::vector<QRegularExpression> expressions;
    std::unique_ptr<BooleanExpressionEvaluator> boolean;
    QVariantMap compiled;
    bool inverse = false;
    bool includeUnmatched = false;
};

CaptureEvaluator::CaptureEvaluator( const QVariantMap& options )
    : d_( std::make_unique<Private>() )
{
    QElapsedTimer timer;
    timer.start();
    d_->compiled[ "backend" ] = "Qt QRegularExpression";
    d_->compiled[ "qtVersion" ] = qVersion();
    const auto fail = [ this, &timer ]( const QString& reason, const QString& message,
                                        qint64 offset = -1 ) {
        d_->compiled[ "reason" ] = reason;
        d_->compiled[ "errorDetails" ]
            = QVariantMap{ { "message", message.left( 2048 ) }, { "offsetUtf16", offset } };
        d_->compiled[ "metrics" ] = QVariantMap{ { "compileUs", timer.nsecsElapsed() / 1000 } };
    };
    const std::set<QString> allowed{ "pattern", "caseSensitive", "boolean",         "plainText",
                                     "exclude", "matchMode",     "includeUnmatched" };
    for ( auto it = options.cbegin(); it != options.cend(); ++it ) {
        if ( allowed.count( it.key() ) == 0 ) {
            fail( "unsupported_options", "Unknown pattern option" );
            return;
        }
        const bool stringValue = it.key() == "pattern" || it.key() == "matchMode";
        if ( it.value().userType() != ( stringValue ? QMetaType::QString : QMetaType::Bool ) ) {
            fail( "unsupported_options", "Incorrect pattern option type" );
            return;
        }
    }
    const auto pattern = options.value( "pattern" ).toString();
    if ( !options.contains( "pattern" ) || pattern.toUtf8().size() > 65536 ) {
        fail( "pattern_limit", "A pattern must be supplied and fit within 64 KiB" );
        return;
    }
    const auto modeName = options.value( "matchMode", "contains" ).toString();
    MatchMode mode = MatchMode::Contains;
    if ( modeName == "whole_word" ) {
        mode = MatchMode::WholeWord;
    }
    else if ( modeName == "whole_line" ) {
        mode = MatchMode::WholeLine;
    }
    else if ( modeName != "contains" ) {
        fail( "unsupported_options", "Unknown match mode" );
        return;
    }
    const bool isBoolean = options.value( "boolean", false ).toBool();
    if ( isBoolean && mode == MatchMode::WholeLine ) {
        fail( "unsupported_options", "Whole line mode cannot be combined with Boolean search" );
        return;
    }
    d_->inverse = options.value( "exclude", false ).toBool();
    d_->includeUnmatched = options.value( "includeUnmatched", false ).toBool();
    klogg::vector<RegularExpressionPattern> leaves;
    QString expression = pattern;
    try {
        if ( isBoolean ) {
            leaves = parseBooleanExpressions( expression,
                                              options.value( "caseSensitive", true ).toBool(),
                                              options.value( "plainText", false ).toBool(), mode );
        }
        else {
            leaves.emplace_back( pattern, options.value( "caseSensitive", true ).toBool(), false,
                                 false, options.value( "plainText", false ).toBool(), mode );
        }
        if ( leaves.empty() || leaves.size() > 64 ) {
            fail( "pattern_limit", "At most 64 Boolean subpatterns are supported" );
            return;
        }
        for ( const auto& leaf : leaves ) {
            auto flags = QRegularExpression::PatternOptions(
                QRegularExpression::UseUnicodePropertiesOption );
            if ( !leaf.isCaseSensitive ) {
                flags |= QRegularExpression::CaseInsensitiveOption;
            }
            // Deliberately separate from RegularExpressionPattern's search conversion:
            // that conversion disables ordinary captures and must remain unchanged.
            QRegularExpression regex( leaf.qtPattern(), flags );
            if ( !regex.isValid() ) {
                fail( "invalid_pattern", regex.errorString(), regex.patternErrorOffset() );
                auto details = d_->compiled.value( "errorDetails" ).toMap();
                details[ "offsetBasis" ] = "generated_qt_subpattern";
                details[ "subpatternIndex" ] = static_cast<int>( d_->expressions.size() );
                d_->compiled[ "errorDetails" ] = details;
                return;
            }
            if ( regex.captureCount() > 128 ) {
                fail( "pattern_limit", "At most 128 capture groups per subpattern are supported" );
                return;
            }
            regex.optimize();
            d_->expressions.push_back( std::move( regex ) );
        }
        if ( isBoolean ) {
            d_->boolean
                = std::make_unique<BooleanExpressionEvaluator>( expression.toStdString(), leaves );
            if ( !d_->boolean->isValid() ) {
                fail( "invalid_pattern", QString::fromStdString( d_->boolean->errorString() ) );
                return;
            }
        }
    } catch ( const std::exception& error ) {
        fail( "invalid_pattern", QString::fromUtf8( error.what() ) );
        return;
    }
    d_->compiled[ "backend" ] = "Qt QRegularExpression";
    d_->compiled[ "qtVersion" ] = qVersion();
    d_->compiled[ "metrics" ] = QVariantMap{ { "compileUs", timer.nsecsElapsed() / 1000 } };
}
CaptureEvaluator::~CaptureEvaluator() = default;
QVariantMap CaptureEvaluator::compilation() const
{
    return d_->compiled;
}

QVariantMap CaptureEvaluator::evaluate( const QVariantList& lines )
{
    if ( d_->compiled.contains( "reason" ) ) {
        return d_->compiled;
    }
    QElapsedTimer timer;
    timer.start();
    QVariantList rows;
    qint64 usedBytes = 0;
    int matchRecords = 0;
    QString reason;
    for ( const auto& value : lines ) {
        const auto input = value.toMap();
        const auto text = input.value( "text" ).toString();
        QVariantList subpatterns;
        std::string outcomes( d_->expressions.size(), '\0' );
        qint64 rowBudget = serializedBytes( { { "text", text } } ) + 1024;
        for ( size_t index = 0; index < d_->expressions.size(); ++index ) {
            const auto& regex = d_->expressions[ index ];
            const auto names = regex.namedCaptureGroups();
            QVariantList matches;
            auto iterator = regex.globalMatch( text );
            bool terminalEmptyMatch = false;
            while ( iterator.hasNext() ) {
                const auto match = iterator.next();
                terminalEmptyMatch
                    = match.capturedLength() == 0 && match.capturedStart() == text.size();
                outcomes[ index ] = 1;
                if ( ++matchRecords > MatchRecords ) {
                    reason = "result_limit";
                    break;
                }
                QVariantList groups;
                qint64 groupBytes = 0;
                for ( int group = 0; group <= regex.captureCount(); ++group ) {
                    const auto start = match.capturedStart( group );
                    const auto length = match.capturedLength( group );
                    // Preflight capture copies using worst-case JSON escaping.
                    groupBytes += 256 + ( start < 0 ? 0 : static_cast<qint64>( length ) * 6 );
                    if ( usedBytes + rowBudget + groupBytes > OutputBytes ) {
                        reason = "result_limit";
                        break;
                    }
                    groups.push_back( QVariantMap{
                        { "number", group },
                        { "name", names.value( group ) },
                        { "matched", start >= 0 },
                        { "value", start < 0 ? QVariant{} : QVariant( match.captured( group ) ) },
                        { "startUtf16", static_cast<qint64>( start ) },
                        { "lengthUtf16", static_cast<qint64>( length ) } } );
                }
                if ( !reason.isEmpty() ) {
                    break;
                }
                QVariantMap captured{
                    { "startUtf16", static_cast<qint64>( match.capturedStart() ) },
                    { "lengthUtf16", static_cast<qint64>( match.capturedLength() ) },
                    { "groups", groups }
                };
                rowBudget += serializedBytes( captured ) + 1;
                matches.push_back( captured );
            }
            if ( !iterator.isValid() && !terminalEmptyMatch && reason.isEmpty() ) {
                reason = "match_resource_limit";
            }
            subpatterns.push_back( QVariantMap{ { "index", static_cast<int>( index ) },
                                                { "matched", outcomes[ index ] != 0 },
                                                { "matches", matches } } );
            if ( !reason.isEmpty() ) {
                break;
            }
        }
        if ( !reason.isEmpty() ) {
            // Never expose an incompletely evaluated Boolean row as exact output.
            break;
        }
        const bool matched
            = ( d_->boolean ? d_->boolean->evaluate( outcomes ) : outcomes.front() != 0 )
              != d_->inverse;
        if ( matched || d_->includeUnmatched ) {
            QVariantMap row{ { "lineNumber", input.value( "lineNumber" ) },
                             { "text", text },
                             { "matched", matched },
                             { "subpatterns", subpatterns },
                             { "offsetConvention", "zero-based UTF-16" } };
            const auto bytes = serializedBytes( row );
            if ( usedBytes + bytes > OutputBytes ) {
                reason = "result_limit";
                break;
            }
            usedBytes += bytes;
            rows.push_back( row );
        }
    }
    return { { "rows", rows },
             { "reason", reason },
             { "backend", "Qt QRegularExpression" },
             { "metrics", QVariantMap{ { "matchUs", timer.nsecsElapsed() / 1000 } } } };
}
