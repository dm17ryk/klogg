#include "plugincatalog.h"

#include "configpack.h"
#include "log.h"
#include "pluginmanifest.h"
#include "semanticversion.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QVersionNumber>
#include <map>
#include <vector>

namespace cilogg::plugins {
namespace {
const QStringList OptionalTextKeys{ "name", "description", "released", "license" };
const QStringList ContentTypes{ "previews", "actions", "highlights",
                                "filters",  "lua",     "python_decoders" };

QString describe( const QJsonObject& entry, int index )
{
    const auto id = entry.value( "id" ).toString();
    return id.isEmpty() ? QStringLiteral( "Entry %1" ).arg( index + 1 )
                        : QStringLiteral( "Entry %1 (%2)" ).arg( index + 1 ).arg( id.left( 128 ) );
}

bool validHttpsUrl( const QString& text )
{
    const QUrl url( text, QUrl::StrictMode );
    return url.isValid() && url.scheme() == "https" && !url.host().isEmpty()
           && url.userInfo().isEmpty();
}

// Why the entry is unusable, or empty when it is valid.
QString entryError( const QJsonObject& entry )
{
    // Anchored on the whole text: `$` alone would also accept a final line break.
    static const QRegularExpression idPattern(
        QRegularExpression::anchoredPattern( "[a-z0-9][a-z0-9._-]{0,127}" ) );
    static const QRegularExpression shaPattern(
        QRegularExpression::anchoredPattern( "[a-fA-F0-9]{64}" ) );
    if ( entry.value( "protocol_version" ).toDouble( -1 ) != ProtocolVersion ) {
        return QStringLiteral( "protocol_version must be %1" ).arg( ProtocolVersion );
    }
    if ( !idPattern.match( entry.value( "id" ).toString() ).hasMatch() ) {
        return QStringLiteral( "invalid id" );
    }
    if ( !isSemanticVersion( entry.value( "version" ).toString() ) ) {
        return QStringLiteral( "version is not a Semantic Versioning 2.0.0 version" );
    }
    if ( !validHttpsUrl( entry.value( "url" ).toString() ) ) {
        return QStringLiteral( "url must be an HTTPS address without user information" );
    }
    if ( !shaPattern.match( entry.value( "sha256" ).toString() ).hasMatch() ) {
        return QStringLiteral( "missing or invalid sha256" );
    }
    for ( const auto& key : OptionalTextKeys ) {
        if ( entry.contains( key ) && !entry.value( key ).isString() ) {
            return QStringLiteral( "%1 must be text" ).arg( key );
        }
    }
    if ( entry.contains( "homepage" ) ) {
        const QUrl homepage( entry.value( "homepage" ).toString(), QUrl::StrictMode );
        if ( !entry.value( "homepage" ).isString() || !homepage.isValid()
             || ( homepage.scheme() != "https" && homepage.scheme() != "http" ) ) {
            return QStringLiteral( "homepage must be an HTTP or HTTPS address" );
        }
    }
    if ( entry.contains( "contents" ) ) {
        const auto contents = entry.value( "contents" );
        if ( !contents.isArray() ) {
            return QStringLiteral( "contents must be a list" );
        }
        for ( const auto& item : contents.toArray() ) {
            if ( !ContentTypes.contains( item.toString() ) ) {
                return QStringLiteral( "unknown contents type" );
            }
        }
    }
    if ( entry.contains( "min_host_version" )
         && !compareHostVersions( entry.value( "min_host_version" ).toString(), "0" ) ) {
        return QStringLiteral( "min_host_version must be a version number such as 26.4" );
    }
    return {};
}

void warn( CatalogRead& read, const QString& text )
{
    if ( read.warnings.size() < MaxCatalogWarnings ) {
        read.warnings.append( text );
    }
    else if ( read.warnings.size() == MaxCatalogWarnings ) {
        read.warnings.append( QStringLiteral( "More entries were left out." ) );
    }
}

QJsonObject hiddenEntry( const QJsonObject& entry, const QString& reason )
{
    return { { "id", entry.value( "id" ).toString().left( 128 ) },
             { "version", entry.value( "version" ).toString().left( MaxVersionLength ) },
             { "reason", reason } };
}
} // namespace

QString platformName()
{
#ifdef Q_OS_WIN
    return "windows";
#elif defined( Q_OS_MACOS )
    return "macos";
#else
    return "linux";
#endif
}

QString packageKind( const QJsonObject& json )
{
    return json.contains( "kind" ) ? json.value( "kind" ).toString() : QStringLiteral( "process" );
}

bool supportedKind( const QString& kind )
{
    return kind == QLatin1String( "process" ) || kind == QLatin1String( ConfigPackKind );
}

QString catalogEntryId( const QJsonObject& entry, const QString& repositoryId )
{
    auto published = entry;
    published.remove( "entry_id" );
    QCryptographicHash hash( QCryptographicHash::Sha256 );
    hash.addData( repositoryId.toUtf8() );
    hash.addData( QByteArrayView( "\n" ) );
    hash.addData( QJsonDocument( published ).toJson( QJsonDocument::Compact ) );
    return QString::fromLatin1( hash.result().toHex() );
}

std::optional<int> compareHostVersions( const QString& a, const QString& b )
{
    qsizetype leftEnd = 0;
    qsizetype rightEnd = 0;
    const auto left = QVersionNumber::fromString( a, &leftEnd );
    const auto right = QVersionNumber::fromString( b, &rightEnd );
    if ( left.isNull() || right.isNull() || leftEnd != a.size() || rightEnd != b.size() ) {
        return std::nullopt;
    }
    const auto order = QVersionNumber::compare( left, right );
    return order < 0 ? -1 : order > 0 ? 1 : 0;
}

QJsonObject newestEntry( const QList<QJsonObject>& entries )
{
    QJsonObject best;
    for ( const auto& entry : entries ) {
        if ( best.isEmpty()
             || compareSemanticVersions( entry.value( "version" ).toString(),
                                         best.value( "version" ).toString() )
                        .value_or( 0 )
                    > 0 ) {
            best = entry;
        }
    }
    return best;
}

CatalogRead readCatalog( const QByteArray& bytes, const QString& repositoryId,
                         const QString& hostVersion, const QString& os, const QString& arch )
{
    CatalogRead read;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson( bytes, &parseError );
    if ( parseError.error != QJsonParseError::NoError || !document.isObject()
         || document.object().value( "schema_version" ).toDouble( -1 ) != 1
         || !document.object().value( "plugins" ).isArray() ) {
        read.error = QStringLiteral( "Invalid plugin catalog (expected plugins.json with "
                                     "schema_version 1)." );
        return read;
    }
    const auto packages = document.object().value( "plugins" ).toArray();
    if ( packages.size() > MaxCatalogEntries ) {
        read.error
            = QStringLiteral( "Too many catalog entries (maximum %1)." ).arg( MaxCatalogEntries );
        return read;
    }
    std::vector<QJsonObject> accepted;
    for ( int index = 0; index < packages.size(); ++index ) {
        if ( !packages.at( index ).isObject() ) {
            warn( read, QStringLiteral( "Entry %1 is not an object." ).arg( index + 1 ) );
            continue;
        }
        const auto entry = packages.at( index ).toObject();
        const auto entryOs = entry.value( "os" ).toString();
        const auto entryArch = entry.value( "arch" ).toString();
        if ( ( entryOs != os && entryOs != "any" )
             || ( entryArch != arch && entryArch != "any" ) ) {
            continue; // another platform: not an error
        }
        if ( entry.contains( "kind" ) && !entry.value( "kind" ).isString() ) {
            warn( read, describe( entry, index ) + QStringLiteral( ": kind must be text." ) );
            continue;
        }
        const auto kind = packageKind( entry );
        if ( !supportedKind( kind ) ) {
            read.hidden.append( hiddenEntry(
                entry, QStringLiteral( "Kind \"%1\" is not supported by this CILogg version." )
                           .arg( kind.left( 64 ) ) ) );
            continue;
        }
        const auto error = entryError( entry );
        if ( !error.isEmpty() ) {
            warn( read, describe( entry, index ) + ": " + error + '.' );
            continue;
        }
        const auto minimum = entry.value( "min_host_version" ).toString();
        if ( !minimum.isEmpty() && compareHostVersions( hostVersion, minimum ).value_or( 0 ) < 0 ) {
            read.hidden.append( hiddenEntry(
                entry, QStringLiteral( "Needs CILogg %1 or newer." ).arg( minimum ) ) );
            continue;
        }
        auto usable = entry;
        usable[ "entry_id" ] = catalogEntryId( entry, repositoryId );
        usable[ "repository" ] = repositoryId;
        usable[ "kind" ] = kind;
        accepted.push_back( usable );
    }
    // Equal versions of one ID must name the same package: otherwise none of them is offered.
    // For valid Semantic Versions, equal precedence means the same text before any build
    // metadata, so grouping by that text finds them without comparing every pair.
    std::vector<bool> dropped( accepted.size(), false );
    std::map<QString, std::vector<size_t>> groups;
    for ( size_t i = 0; i < accepted.size(); ++i ) {
        const auto& entry = accepted[ i ];
        groups[ entry.value( "id" ).toString() + '\n'
                + entry.value( "version" ).toString().section( '+', 0, 0 ) ]
            .push_back( i );
    }
    for ( const auto& [ key, members ] : groups ) {
        Q_UNUSED( key );
        if ( members.size() < 2 ) {
            continue;
        }
        QSet<QString> distinct;
        for ( const auto index : members ) {
            distinct.insert( accepted[ index ].value( "url" ).toString() + '\n'
                             + accepted[ index ].value( "sha256" ).toString().toLower() );
        }
        const auto& first = accepted[ members.front() ];
        if ( distinct.size() > 1 ) {
            warn( read,
                  QStringLiteral( "%1 %2: entries with the same version but different "
                                  "packages; all of them are left out." )
                      .arg( first.value( "id" ).toString(), first.value( "version" ).toString() ) );
        }
        // The same package listed more than once is offered once.
        for ( size_t position = distinct.size() > 1 ? size_t( 0 ) : size_t( 1 );
              position < members.size(); ++position ) {
            dropped[ members[ position ] ] = true;
        }
    }
    for ( size_t i = 0; i < accepted.size(); ++i ) {
        if ( !dropped[ i ] ) {
            read.entries.append( accepted[ i ] );
        }
    }
    LOG_DEBUG << "Plugin catalog read repository=" << repositoryId.toStdString()
              << " entries=" << read.entries.size() << " hidden=" << read.hidden.size()
              << " warnings=" << read.warnings.size();
    return read;
}
} // namespace cilogg::plugins
