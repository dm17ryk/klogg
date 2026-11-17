#include "semanticversion.h"

#include <QRegularExpression>
#include <algorithm>
#include <utility>

namespace cilogg::plugins {
namespace {
// The regular expression suggested by semver.org, with ASCII classes only. Anchored on the whole
// text: `$` alone would also accept a final line break.
const QRegularExpression& versionPattern()
{
    static const QRegularExpression pattern( QRegularExpression::anchoredPattern(
        "(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)"
        "(?:-((?:0|[1-9][0-9]*|[0-9]*[a-zA-Z-][0-9a-zA-Z-]*)"
        "(?:\\.(?:0|[1-9][0-9]*|[0-9]*[a-zA-Z-][0-9a-zA-Z-]*))*))?"
        "(?:\\+([0-9a-zA-Z-]+(?:\\.[0-9a-zA-Z-]+)*))?" ) );
    return pattern;
}

bool numeric( const QString& identifier )
{
    return !identifier.isEmpty()
           && std::all_of( identifier.begin(), identifier.end(),
                           []( QChar c ) { return c >= '0' && c <= '9'; } );
}

// Numbers without leading zeros: the longer one is larger, then the text decides.
int compareNumbers( const QString& a, const QString& b )
{
    if ( a.size() != b.size() ) {
        return a.size() < b.size() ? -1 : 1;
    }
    return a.compare( b );
}

int sign( int value )
{
    return value < 0 ? -1 : value > 0 ? 1 : 0;
}
} // namespace

std::optional<SemanticVersion> SemanticVersion::parse( const QString& text )
{
    if ( text.size() > MaxVersionLength ) {
        return std::nullopt;
    }
    const auto match = versionPattern().match( text );
    if ( !match.hasMatch() ) {
        return std::nullopt;
    }
    SemanticVersion version;
    version.major = match.captured( 1 );
    version.minor = match.captured( 2 );
    version.patch = match.captured( 3 );
    if ( match.capturedLength( 4 ) > 0 ) {
        version.prerelease = match.captured( 4 ).split( '.' );
    }
    version.build = match.captured( 5 );
    return version;
}

bool isSemanticVersion( const QString& text )
{
    return SemanticVersion::parse( text ).has_value();
}

std::optional<int> compareSemanticVersions( const QString& a, const QString& b )
{
    const auto left = SemanticVersion::parse( a );
    const auto right = SemanticVersion::parse( b );
    if ( !left || !right ) {
        return std::nullopt;
    }
    for ( const auto& [ x, y ] : { std::make_pair( left->major, right->major ),
                                   std::make_pair( left->minor, right->minor ),
                                   std::make_pair( left->patch, right->patch ) } ) {
        const auto order = compareNumbers( x, y );
        if ( order != 0 ) {
            return sign( order );
        }
    }
    // A pre-release is lower than its release.
    if ( left->prerelease.isEmpty() || right->prerelease.isEmpty() ) {
        return left->prerelease.isEmpty() == right->prerelease.isEmpty() ? 0
               : left->prerelease.isEmpty()                              ? 1
                                                                         : -1;
    }
    const auto common = std::min( left->prerelease.size(), right->prerelease.size() );
    for ( qsizetype index = 0; index < common; ++index ) {
        const auto& x = left->prerelease.at( index );
        const auto& y = right->prerelease.at( index );
        const bool xNumeric = numeric( x );
        const bool yNumeric = numeric( y );
        int order = 0;
        if ( xNumeric && yNumeric ) {
            order = compareNumbers( x, y );
        }
        else if ( xNumeric != yNumeric ) {
            order = xNumeric ? -1 : 1; // numeric identifiers sort before text
        }
        else {
            order = x.compare( y ); // ASCII order
        }
        if ( order != 0 ) {
            return sign( order );
        }
    }
    return left->prerelease.size() == right->prerelease.size()  ? 0
           : left->prerelease.size() < right->prerelease.size() ? -1
                                                                : 1;
}
} // namespace cilogg::plugins
