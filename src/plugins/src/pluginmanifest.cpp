#include "pluginmanifest.h"

#include "log.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <cmath>

namespace cilogg::plugins {
QString newId()
{
    return QUuid::createUuid().toString( QUuid::WithoutBraces );
}

bool safeRelativePath( const QString& path )
{
    if ( path.isEmpty() || QDir::isAbsolutePath( path ) || path.contains( '\\' )
         || path.contains( ':' ) || path.contains( QChar( 0 ) ) ) {
        return false;
    }
    for ( const auto& part : path.split( '/' ) ) {
        static const QRegularExpression deviceName(
            "^(?:CON|PRN|AUX|NUL|CONIN\\$|CONOUT\\$|COM[1-9]|LPT[1-9])(?:\\..*)?$",
            QRegularExpression::CaseInsensitiveOption );
        if ( part.isEmpty() || part == "." || part == ".." || part.endsWith( '.' )
             || part.endsWith( ' ' ) || deviceName.match( part ).hasMatch() ) {
            return false;
        }
        for ( const auto character : part ) {
            if ( character.unicode() < 32 || QStringLiteral( "<>\"|?*" ).contains( character ) ) {
                return false;
            }
        }
    }
    return true;
}

QString validateFields( const QJsonArray& fields )
{
    if ( fields.size() > 64 ) {
        return QStringLiteral( "Too many form fields." );
    }
    QSet<QString> names;
    const QStringList types{ "text",    "secret", "multiline", "number",   "integer",
                             "boolean", "choice", "file",      "directory" };
    for ( const auto& item : fields ) {
        const auto f = item.toObject();
        const auto name = f.value( "id" ).toString();
        if ( !item.isObject() || name.isEmpty() || names.contains( name )
             || !types.contains( f.value( "type" ).toString() ) ) {
            return QStringLiteral( "Invalid or duplicate form field: %1" ).arg( name );
        }
        names.insert( name );
        if ( name.size() > 128 || ( f.contains( "required" ) && !f.value( "required" ).isBool() )
             || ( f.contains( "minimum" ) && !f.value( "minimum" ).isDouble() )
             || ( f.contains( "maximum" ) && !f.value( "maximum" ).isDouble() )
             || ( f.contains( "minimum" ) && f.contains( "maximum" )
                  && f.value( "minimum" ).toDouble() > f.value( "maximum" ).toDouble() ) ) {
            return QStringLiteral( "Invalid field bounds or required flag: %1" ).arg( name );
        }
        if ( f.value( "type" ) == "choice"
             && ( !f.value( "choices" ).isArray() || f.value( "choices" ).toArray().isEmpty() ) ) {
            return QStringLiteral( "Choice field %1 requires choices." ).arg( name );
        }
    }
    return {};
}

QString validateValues( const QJsonArray& fields, QJsonObject* values )
{
    const auto schemaError = validateFields( fields );
    if ( !schemaError.isEmpty() ) {
        return schemaError;
    }
    for ( const auto& item : fields ) {
        const auto f = item.toObject();
        const auto key = f.value( "id" ).toString();
        if ( !values->contains( key ) && f.contains( "default" ) ) {
            values->insert( key, f.value( "default" ) );
        }
        const auto v = values->value( key );
        if ( v.isUndefined() ) {
            if ( f.value( "required" ).toBool() ) {
                return QStringLiteral( "Required field: %1" ).arg( key );
            }
            continue;
        }
        const auto type = f.value( "type" ).toString();
        bool valid = type == "boolean" ? v.isBool() : v.isString();
        if ( type == "number" || type == "integer" ) {
            valid = v.isDouble() && std::isfinite( v.toDouble() );
            if ( type == "integer" ) {
                valid = valid && std::floor( v.toDouble() ) == v.toDouble();
            }
            if ( f.contains( "minimum" ) ) {
                valid = valid && v.toDouble() >= f[ "minimum" ].toDouble();
            }
            if ( f.contains( "maximum" ) ) {
                valid = valid && v.toDouble() <= f[ "maximum" ].toDouble();
            }
        }
        if ( type == "choice" ) {
            valid = f.value( "choices" ).toArray().contains( v );
        }
        if ( f.value( "required" ).toBool() && v.isString() && v.toString().isEmpty() ) {
            valid = false;
        }
        if ( !valid ) {
            return QStringLiteral( "Invalid value for %1." ).arg( key );
        }
    }
    return {};
}

QString validateActions( const QJsonArray& actions, const QJsonArray& capabilities )
{
    if ( actions.size() > 128 ) {
        return QStringLiteral( "Too many plugin actions." );
    }
    QSet<QString> ids;
    for ( const auto& item : actions ) {
        const auto a = item.toObject();
        const auto id = a.value( "id" ).toString();
        if ( !item.isObject() || id.isEmpty() || ids.contains( id ) || id.size() > 128
             || !capabilities.contains( a.value( "capability" ) )
             || ( a.contains( "fields" ) && !a.value( "fields" ).isArray() ) ) {
            return QStringLiteral( "Invalid action: %1" ).arg( id );
        }
        const auto error = validateFields( a.value( "fields" ).toArray() );
        if ( !error.isEmpty() ) {
            return error;
        }
        ids.insert( id );
    }
    return {};
}

Manifest Manifest::read( const QString& directory )
{
    QFile file( QDir( directory ).filePath( "plugin.json" ) );
    if ( !file.open( QIODevice::ReadOnly ) || file.size() > MaxMessageBytes ) {
        Manifest m;
        m.directory = directory;
        m.error = QStringLiteral( "Cannot read plugin.json (maximum 1 MiB)." );
        return m;
    }
    return parse( file.readAll(), directory );
}

Manifest Manifest::parse( const QByteArray& bytes, const QString& directory )
{
    Manifest m;
    m.directory = directory;
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson( bytes, &parseError );
    m.json = doc.object();
    m.id = m.json.value( "id" ).toString();
    m.name = m.json.value( "name" ).toString();
    m.version = m.json.value( "version" ).toString();
    m.executable = m.json.value( "executable" ).toString();
#ifdef Q_OS_WIN
    if ( !m.executable.isEmpty() && m.executable != "python"
         && QFileInfo( m.executable ).suffix().isEmpty() ) {
        m.executable += ".exe";
    }
#endif
    static const QRegularExpression idPattern( "^[a-z0-9][a-z0-9._-]{0,127}$" );
    static const QRegularExpression versionPattern(
        "^[0-9]+\\.[0-9]+\\.[0-9]+(?:[-+][a-zA-Z0-9.-]+)?$" );
    if ( bytes.size() > MaxMessageBytes || parseError.error != QJsonParseError::NoError
         || !doc.isObject() || !idPattern.match( m.id ).hasMatch() || !safeRelativePath( m.id )
         || m.name.isEmpty() || !versionPattern.match( m.version ).hasMatch()
         || m.json.value( "protocol_version" ) != QJsonValue( ProtocolVersion )
         || ( m.json.contains( "arguments" ) && !m.json.value( "arguments" ).isArray() )
         || ( m.json.contains( "actions" ) && !m.json.value( "actions" ).isArray() )
         || ( m.json.contains( "configuration" ) && !m.json.value( "configuration" ).isArray() ) ) {
        m.error = QStringLiteral( "Invalid manifest identity, version, JSON or protocol version." );
    }
    const auto capabilities = m.json.value( "capabilities" ).toArray();
    if ( capabilities.isEmpty() ) {
        m.error = QStringLiteral( "No plugin capabilities." );
    }
    for ( const auto& capability : capabilities ) {
        if ( capability != "datasource" && capability != "converter" && capability != "tool" ) {
            m.error = QStringLiteral( "Unknown plugin capability." );
        }
    }
    // Python is an explicit interpreter choice, not a shell command.
    if ( m.executable != "python"
         && !safeRelativePath( m.json.value( "executable" ).toString() ) ) {
        m.error = QStringLiteral( "Executable must be a package-relative path or python." );
    }
    for ( const auto& argument : m.json.value( "arguments" ).toArray() ) {
        if ( !argument.isString() ) {
            m.error = QStringLiteral( "Arguments must be strings." );
        }
        m.arguments.append( argument.toString() );
    }
    const auto actionError = validateActions( m.json.value( "actions" ).toArray(), capabilities );
    const auto fieldError = validateFields( m.json.value( "configuration" ).toArray() );
    if ( !actionError.isEmpty() ) {
        m.error = actionError;
    }
    if ( !fieldError.isEmpty() ) {
        m.error = fieldError;
    }
    LOG_DEBUG << "Plugin manifest id=" << m.id.toStdString()
              << " directory=" << directory.toStdString() << " valid=" << m.valid()
              << " reason=" << m.error.toStdString();
    return m;
}
} // namespace cilogg::plugins
