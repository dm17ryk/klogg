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

#include <algorithm>
#include <exception>
#include <memory>
#include <qregularexpression.h>
#include <string>
#include <variant>
#include <unordered_map>

#include "configuration.h"
#include "containers.h"
#include "log.h"
#include "regularexpressionpattern.h"
#include "uuid.h"

#include "booleanevaluator.h"
#include "regularexpression.h"

namespace {

klogg::vector<QString> splitTopLevelOr( const QString& pattern )
{
    klogg::vector<QString> parts;
    int parenDepth = 0;
    int bracketDepth = 0;
    bool escaped = false;
    int lastSplit = 0;

    const auto flushPart = [&]( int endIndex ) {
        const auto part = pattern.mid( lastSplit, endIndex - lastSplit ).trimmed();
        if ( !part.isEmpty() ) {
            parts.push_back( part );
        }
    };

    for ( int i = 0; i < pattern.size(); ++i ) {
        const auto ch = pattern[ i ];

        if ( escaped ) {
            escaped = false;
            continue;
        }

        if ( ch == '\\' ) {
            escaped = true;
            continue;
        }

        if ( ch == '[' ) {
            ++bracketDepth;
        }
        else if ( ch == ']' && bracketDepth > 0 ) {
            --bracketDepth;
        }
        else if ( bracketDepth == 0 ) {
            if ( ch == '(' ) {
                ++parenDepth;
            }
            else if ( ch == ')' && parenDepth > 0 ) {
                --parenDepth;
            }
            else if ( ch == '|' && parenDepth == 0 ) {
                flushPart( i );
                lastSplit = i + 1;
            }
        }
    }

    flushPart( type_safe::narrow_cast<int>( pattern.size() ) );

    return parts;
}

QString normalizeExtendedBooleanOps( QString expression )
{
    // Convert xor/nand/nor/xnor into combinations of &, | and ! so they can be
    // passed to Hyperscan combination expressions.
    // The regex matches simple binary forms (token OP token) where token is
    // either p_<n>, a number, or a parenthesized subexpression.
    const QRegularExpression binaryOpRe(
        R"((\([^()]+\)|p_\d+|\d+)\s*(xor|nand|nor|xnor)\s*(\([^()]+\)|p_\d+|\d+))" );

    bool replaced = true;
    while ( replaced ) {
        replaced = false;
        QRegularExpressionMatch match = binaryOpRe.match( expression );
        if ( match.hasMatch() ) {
            const auto a = match.captured( 1 );
            const auto op = match.captured( 2 );
            const auto b = match.captured( 3 );
            QString replacement;
            if ( op == "xor" ) {
                replacement = QStringLiteral( "((%1)|(%2))&!((%1)&(%2))" ).arg( a, b );
            }
            else if ( op == "nand" ) {
                replacement = QStringLiteral( "!( (%1)&(%2) )" ).arg( a, b );
            }
            else if ( op == "nor" ) {
                replacement = QStringLiteral( "!( (%1)|(%2) )" ).arg( a, b );
            }
            else if ( op == "xnor" ) {
                replacement
                    = QStringLiteral( "!(((%1)|(%2))&!((%1)&(%2)))" ).arg( a, b );
            }

            expression.replace( match.capturedStart(), match.capturedLength(), replacement );
            replaced = true;
        }
    }

    return expression;
}

bool canUseDirectHsCombinationBit( const QString& expression )
{
    // HS combination callbacks are safe for monotonic boolean logic only.
    // Expressions with negation or parity-style operators can become true at
    // an intermediate offset and then false later, so we must use evaluator.
    const auto lower = expression.toLower();
    return !lower.contains( '!' ) && !lower.contains( "not" ) && !lower.contains( "xor" )
           && !lower.contains( "xnor" ) && !lower.contains( "nand" )
           && !lower.contains( "nor" );
}

klogg::vector<RegularExpressionPattern>
parseBooleanExpressions( QString& pattern, bool isCaseSensitive, bool isPlainText )
{
    if ( !pattern.contains( '"' ) ) {
        throw std::runtime_error( "Patterns must be enclosed in quotes" );
    }

    klogg::vector<RegularExpressionPattern> subPatterns;
    subPatterns.reserve( static_cast<size_t>( pattern.size() ) );

    int currentIndex = 0;
    int leftQuote = -1;
    int rightQuote = -1;

    while ( currentIndex < pattern.size() ) {
        leftQuote = type_safe::narrow_cast<int>( pattern.indexOf( QChar( '"' ), currentIndex ) );
        if ( leftQuote < 0 ) {
            break;
        }

        currentIndex = leftQuote + 1;
        if ( leftQuote > 0 && pattern[ leftQuote - 1 ] == QChar( '\\' ) ) {
            leftQuote = -1;
            continue;
        }

        while ( currentIndex < pattern.size() ) {
            rightQuote
                = type_safe::narrow_cast<int>( pattern.indexOf( QChar( '"' ), currentIndex ) );
            if ( rightQuote < 0 ) {
                break;
            }

            currentIndex = rightQuote + 1;
            if ( rightQuote > 0 && pattern[ rightQuote - 1 ] == QChar( '\\' ) ) {
                rightQuote = -1;
                continue;
            }

            break;
        }

        if ( rightQuote < 0 ) {
            break;
        }

        const auto subPatternLength = rightQuote - leftQuote - 1;
        auto subPattern = pattern.mid( leftQuote + 1, subPatternLength );
        subPattern.replace( "\\\"", "\"" );

        subPatterns.emplace_back( subPattern, isCaseSensitive, false, false, isPlainText );

        pattern.replace( leftQuote, subPatternLength + 2,
                         QString::fromStdString( subPatterns.back().id() ) );

        currentIndex = 0;
        leftQuote = -1;
        rightQuote = -1;
    }

    if ( pattern.contains( '"' ) ) {
        throw std::runtime_error( "Pattern has unmatched quotes" );
    }

    pattern = pattern.toLower();
    LOG_INFO << "Parsed pattern: " << pattern;
    QRegularExpression finalPatternCheck( "^(and|nand|or|nor|xor|xnor|not|[ ()!|&]|p_[0-9]+)+$" );
    if ( !finalPatternCheck.match( pattern ).hasMatch() ) {
        throw std::runtime_error( "Sub-patterns must be enclosed in quotes" );
    }

    return subPatterns;
}

} // namespace

RegularExpression::RegularExpression( const RegularExpressionPattern& pattern )
    : isInverse_( pattern.isExclude )
    , isBooleanCombination_( pattern.isBoolean )
    , expression_( pattern.pattern )
{
    try {
        if ( pattern.isBoolean ) {
            subPatterns_ = parseBooleanExpressions( expression_, pattern.isCaseSensitive,
                                                    pattern.isPlainText );
        }
        else {
            const auto topLevelOrParts = splitTopLevelOr( expression_ );
            if ( topLevelOrParts.size() > 1 ) {
                for ( const auto& part : topLevelOrParts ) {
                    subPatterns_.emplace_back( part, pattern.isCaseSensitive, pattern.isExclude,
                                               /*boolean*/ false, pattern.isPlainText );
                }
            }
            else {
                subPatterns_.emplace_back( pattern );
            }
            expression_ = QString::fromStdString( subPatterns_.front().id() );
        }

        std::string hsCombination;
        const bool combinationEligible = isBooleanCombination_ && subPatterns_.size() >= 2;
        if ( combinationEligible ) {
            // Try to build a Hyperscan combination expression for boolean logic.
            QString combination = expression_;
            combination.replace( "and", "&" );
            combination.replace( "or", "|" );
            combination.replace( "not", "!" );
            combination.replace( " ", "" );
            combination = normalizeExtendedBooleanOps( combination );

            if ( combination.contains( '&' ) || combination.contains( '|' ) ) {

            // Map p_<n> tokens to positional indices.
            std::unordered_map<QString, size_t> idToIndex;
            for ( size_t i = 0; i < subPatterns_.size(); ++i ) {
                idToIndex.emplace( QString::fromStdString( subPatterns_[ i ].id() ), i );
            }

            static const QRegularExpression tokenRe( "p_\\d+" );
            auto it = tokenRe.globalMatch( combination );
            while ( it.hasNext() ) {
                const auto m = it.next();
                const auto token = m.captured();
                if ( const auto found = idToIndex.find( token ); found != idToIndex.end() ) {
                    combination.replace( token, QString::number( found->second ) );
                }
            }

            hsCombination = combination.toStdString();
            hasHsCombination_ = !hsCombination.empty();
            combinationIndex_ = subPatterns_.size(); // last entry
            }
        }

        hsExpression_ = HsRegularExpression( subPatterns_, hsCombination );
        isValid_ = hsExpression_.isValid();
        errorString_ = hsExpression_.errorString();

        if ( hasHsCombination_ && !hsExpression_.isValid() ) {
            hasHsCombination_ = false;
            combinationIndex_ = 0;
            hsExpression_ = HsRegularExpression( subPatterns_ );
            isValid_ = hsExpression_.isValid();
            errorString_ = hsExpression_.errorString();
        }

        if ( isBooleanCombination_ && !hasHsCombination_ ) {
            BooleanExpressionEvaluator evaluator{ expression_.toStdString(), subPatterns_ };
            if ( !evaluator.isValid() ) {
                isValid_ = false;
                errorString_ = QString::fromStdString( evaluator.errorString() );
                return;
            }
        }

    } catch ( std::exception& err ) {
        isValid_ = false;
        errorString_ = err.what();
    }
}

bool RegularExpression::isValid() const
{
    return isValid_;
}

QString RegularExpression::errorString() const
{
    return errorString_;
}

std::unique_ptr<PatternMatcher> RegularExpression::createMatcher() const
{
    return std::make_unique<PatternMatcher>( *this );
}

namespace matching {

bool hasSingleMatch( std::string_view line, const MatcherVariant& matcher,
                     BooleanExpressionEvaluator* )
{
    const auto result
        = std::visit( [ &line ]( const auto& m ) { return m.match( line ); }, matcher );

    return std::any_of( result.cbegin(), result.cend(), []( auto value ) { return value > 0; } );
}

bool hasCombinedMatch( std::string_view line, const MatcherVariant& matcher,
                       BooleanExpressionEvaluator* evaluator )
{
    auto result = std::visit( [ &line ]( const auto& m ) { return m.match( line ); }, matcher );
    if ( evaluator ) {
        if ( result.size() > evaluator->variableCount() ) {
            result.resize( evaluator->variableCount() );
        }
        return evaluator->evaluate( result );
    }
    return false;
}

bool hasInverseSingleMatch( std::string_view line, const MatcherVariant& matcher,
                            BooleanExpressionEvaluator* evaluator )
{
    return !hasSingleMatch( line, matcher, evaluator );
}

bool hasInverseCombinedMatch( std::string_view line, const MatcherVariant& matcher,
                              BooleanExpressionEvaluator* evaluator )
{
    return !hasCombinedMatch( line, matcher, evaluator );
}

} // namespace matching

PatternMatcher::PatternMatcher( const RegularExpression& expression )
    : isInverse_( expression.isInverse_ )
    , isBooleanCombination_( expression.isBooleanCombination_ )
    , hasHsCombination_( expression.hasHsCombination_ )
    , combinationIndex_( expression.combinationIndex_ )
    , mainPatternId_( expression.subPatterns_.empty() ? std::string{} : expression.subPatterns_.front().id() )
    , matcher_( expression.hsExpression_.createMatcher() )
{
    const auto& config = Configuration::get();
    const auto useHyperscanEngine = config.regexpEngine() == RegexpEngine::Hyperscan;
    const bool forceQtMatcherForBooleanFallback = isBooleanCombination_ && !hasHsCombination_;
    if ( !useHyperscanEngine || forceQtMatcherForBooleanFallback ) {
        matcher_ = DefaultRegularExpressionMatcher( expression.subPatterns_ );
    }

    if ( expression.isBooleanCombination_ ) {
        evaluator_ = std::make_unique<BooleanExpressionEvaluator>(
            expression.expression_.toStdString(), expression.subPatterns_ );
    }

    const bool directHsCombinationSafe = canUseDirectHsCombinationBit( expression.expression_ );
    const bool useHsCombinationBit = hasHsCombination_ && useHyperscanEngine
                                     && config.useHsCombinationBit() && directHsCombinationSafe;

    if ( isBooleanCombination_ ) {
        // Boolean path: prefer HS combination bit if enabled, otherwise fall back to evaluator.
        hasMatchImpl_ = [this, useHsCombinationBit]( std::string_view line,
                                                     const MatcherVariant& matcher,
                                                     BooleanExpressionEvaluator* evaluator ) {
            auto result
                = std::visit( [ &line ]( const auto& m ) { return m.match( line ); }, matcher );

            if ( useHsCombinationBit && combinationIndex_ < result.size()
                 && result[ combinationIndex_ ] > 0 ) {
                return true;
            }

            if ( evaluator ) {
                if ( result.size() > evaluator->variableCount() ) {
                    result.resize( evaluator->variableCount() );
                }
                return evaluator->evaluate( result );
            }

            return false;
        };

        if ( isInverse_ ) {
            const auto base = hasMatchImpl_;
            hasMatchImpl_ = [base]( std::string_view line, const MatcherVariant& matcher,
                                    BooleanExpressionEvaluator* evaluator ) {
                return !base( line, matcher, evaluator );
            };
        }
    }
    else {
        hasMatchImpl_ = isInverse_ ? matching::hasInverseSingleMatch : matching::hasSingleMatch;
    }
}

PatternMatcher::~PatternMatcher() = default;

bool PatternMatcher::hasMatch( std::string_view line ) const
{
    return hasMatchImpl_( line, matcher_, evaluator_.get() );
}

MultiRegularExpression::MultiRegularExpression(
    const klogg::vector<RegularExpressionPattern>& patterns )
    : patterns_( patterns )
{
    try {
        hsExpression_ = HsRegularExpression( patterns_ );
        isValid_ = hsExpression_.isValid();
        errorString_ = hsExpression_.errorString();

    } catch ( std::exception& err ) {
        isValid_ = false;
        errorString_ = err.what();
    }
}

std::unique_ptr<MultiPatternMatcher> MultiRegularExpression::createMatcher() const
{
    return std::make_unique<MultiPatternMatcher>( *this );
}

MultiPatternMatcher::MultiPatternMatcher( const MultiRegularExpression& expression )
    : matcher_( expression.hsExpression_.createMatcher() )
    , patterns_( expression.patterns_ )
{
}

MultiPatternMatcher::~MultiPatternMatcher() = default;

klogg::vector<std::pair<RegularExpressionPattern, bool>>
MultiPatternMatcher::match( std::string_view line ) const
{
    const auto result
        = std::visit( [ &line ]( const auto& m ) { return m.match( line ); }, matcher_ );

    klogg::vector<std::pair<RegularExpressionPattern, bool>> matchedPatterns;
    const auto count = std::min( result.size(), patterns_.size() );
    for ( size_t i = 0u; i < count; ++i ) {
        matchedPatterns.emplace_back( patterns_[ i ], result[ i ] );
    }

    return matchedPatterns;
}
