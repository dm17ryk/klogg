#include "pluginmanifest.h"

#include "configpack.h"
#include "log.h"
#include "plugincatalog.h"
#include "pluginfeatures.h"
#include "pluginmenuspec.h"
#include "semanticversion.h"
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

// Optional `toolbar` / `menu` booleans and an `icon` path inside the plugin folder (E14).
QString validatePresentation( const QJsonObject& item, const QString& id, bool allowMenu )
{
    for ( const auto key : { "toolbar", "menu" } ) {
        if ( !allowMenu && QLatin1String( key ) == QLatin1String( "menu" ) ) {
            continue; // views validate `menu` with their other flags
        }
        if ( item.contains( QLatin1String( key ) ) && !item.value( QLatin1String( key ) ).isBool() ) {
            return QStringLiteral( "Invalid %1 flag: %2" ).arg( QLatin1String( key ), id );
        }
    }
    if ( item.contains( "icon" ) ) {
        const auto icon = item.value( "icon" ).toString();
        static const QRegularExpression imageSuffix( "\\.(png|ico|svg)$",
                                                     QRegularExpression::CaseInsensitiveOption );
        if ( !item.value( "icon" ).isString() || icon.size() > 256 || !safeRelativePath( icon )
             || !imageSuffix.match( icon ).hasMatch() ) {
            return QStringLiteral( "Invalid icon (a .png, .ico or .svg path inside the plugin): %1" )
                .arg( id );
        }
    }
    // E17: every icon reference form; wins over `icon` on hosts that know it. `icon` itself stays
    // a plugin file, because an older host refuses anything else there.
    if ( item.contains( "icon_ref" ) ) {
        const auto error = validateIconReference( item.value( "icon_ref" ) );
        if ( !error.isEmpty() ) {
            return QStringLiteral( "Invalid icon_ref: %1 (%2)" ).arg( id, error );
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
        const auto presentation = validatePresentation( a, id, true );
        if ( !presentation.isEmpty() ) {
            return presentation;
        }
        ids.insert( id );
    }
    return {};
}

// E17f dialog buttons (docs/plugin-sdk-advanced.md section 29.3): `buttons`, `default` and
// `confirm_by_plugin` on dialog views only. An older host ignores the keys.
static QString validateDialogButtons( const QJsonObject& view, const QString& id,
                                      const QString& kind )
{
    const bool declared = view.contains( "buttons" ) || view.contains( "default" )
                          || view.contains( "confirm_by_plugin" );
    if ( !declared ) {
        return {};
    }
    if ( kind != QLatin1String( "dialog" ) ) {
        return QStringLiteral( "Only dialog views take buttons, default and confirm_by_plugin: %1" )
            .arg( id );
    }
    QStringList names;
    if ( view.contains( "buttons" ) ) {
        if ( !view.value( "buttons" ).isArray() ) {
            return QStringLiteral( "Invalid dialog buttons: %1" ).arg( id );
        }
        for ( const auto& value : view.value( "buttons" ).toArray() ) {
            const auto name = value.toString();
            if ( !value.isString() || !dialogButtonNames().contains( name )
                 || names.contains( name ) ) {
                return QStringLiteral( "Invalid dialog buttons (ok, cancel, apply, close, yes, no, "
                                       "each once): %1" )
                    .arg( id );
            }
            names.append( name );
        }
    }
    if ( view.contains( "default" )
         && ( !view.value( "default" ).isString()
              || !names.contains( view.value( "default" ).toString() ) ) ) {
        return QStringLiteral( "The dialog default must be one of its buttons: %1" ).arg( id );
    }
    if ( view.contains( "confirm_by_plugin" ) && !view.value( "confirm_by_plugin" ).isBool() ) {
        return QStringLiteral( "Invalid confirm_by_plugin flag: %1" ).arg( id );
    }
    return {};
}

QString validateViews( const QJsonArray& views )
{
    if ( views.size() > MaxViewsPerPlugin ) {
        return QStringLiteral( "Too many plugin views (maximum %1)." ).arg( MaxViewsPerPlugin );
    }
    const auto optionalBool = []( const QJsonObject& view, const char* key ) {
        return !view.contains( QLatin1String( key ) )
               || view.value( QLatin1String( key ) ).isBool();
    };
    const auto optionalSize = []( const QJsonObject& view, const char* key ) {
        const auto value = view.value( QLatin1String( key ) );
        return value.isUndefined()
               || ( value.isDouble() && value.toDouble() == std::floor( value.toDouble() )
                    && value.toDouble() >= 1 && value.toDouble() <= MaxViewSize );
    };
    QSet<QString> ids;
    for ( const auto& item : views ) {
        const auto view = item.toObject();
        const auto id = view.value( "id" ).toString();
        if ( !item.isObject() || id.isEmpty() || ids.contains( id ) || id.size() > 128
             || ( view.contains( "title" )
                  && ( !view.value( "title" ).isString()
                       || view.value( "title" ).toString().size() > 256 ) ) ) {
            return QStringLiteral( "Invalid view: %1" ).arg( id );
        }
        const auto kind = view.value( "kind" ).toString( "dock" );
        const auto area = view.value( "area" ).toString( "right" );
        if ( ( view.contains( "kind" ) && !view.value( "kind" ).isString() )
             || !QStringList{ "dock", "window", "dialog" }.contains( kind )
             || ( view.contains( "area" ) && !view.value( "area" ).isString() )
             || !QStringList{ "left", "right", "top", "bottom" }.contains( area ) ) {
            return QStringLiteral( "Invalid view kind or area: %1" ).arg( id );
        }
        if ( !optionalBool( view, "menu" ) || !optionalBool( view, "floating" )
             || !optionalBool( view, "modal" ) || !optionalSize( view, "width" )
             || !optionalSize( view, "height" ) ) {
            return QStringLiteral( "Invalid view flag or size: %1" ).arg( id );
        }
        const auto presentation = validatePresentation( view, id, false );
        if ( !presentation.isEmpty() ) {
            return presentation;
        }
        const auto dialog = validateDialogButtons( view, id, kind );
        if ( !dialog.isEmpty() ) {
            return dialog;
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
    auto m = parse( file.readAll(), directory );
    // A pack is only as valid as the files it lists (design 4.1).
    if ( m.valid() && isConfigPack( m.json ) ) {
        m.error = validatePackFiles( m.json, directory );
    }
    return m;
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
    // `kind` decides which rules apply: a configuration pack has no process (design 4.1).
    const bool pack = isConfigPack( m.json );
    m.executable = pack ? QString{} : m.json.value( "executable" ).toString();
#ifdef Q_OS_WIN
    if ( !m.executable.isEmpty() && m.executable != "python"
         && QFileInfo( m.executable ).suffix().isEmpty() ) {
        m.executable += ".exe";
    }
#endif
    static const QRegularExpression idPattern(
        QRegularExpression::anchoredPattern( "[a-z0-9][a-z0-9._-]{0,127}" ) );
    // One Semantic Versioning 2.0.0 implementation for manifests, catalogs and updates.
    if ( bytes.size() > MaxMessageBytes || parseError.error != QJsonParseError::NoError
         || !doc.isObject() || !idPattern.match( m.id ).hasMatch() || !safeRelativePath( m.id )
         || m.name.isEmpty() || !isSemanticVersion( m.version )
         || m.json.value( "protocol_version" ) != QJsonValue( ProtocolVersion )
         || ( m.json.contains( "arguments" ) && !m.json.value( "arguments" ).isArray() )
         || ( m.json.contains( "actions" ) && !m.json.value( "actions" ).isArray() )
         || ( m.json.contains( "configuration" ) && !m.json.value( "configuration" ).isArray() )
         || ( m.json.contains( "views" ) && !m.json.value( "views" ).isArray() ) ) {
        m.error = QStringLiteral( "Invalid manifest identity, version, JSON or protocol version." );
    }
    if ( pack ) {
        if ( m.error.isEmpty() ) {
            m.error = validatePackManifest( m.json );
        }
        LOG_DEBUG << "Plugin manifest (configuration pack) id=" << m.id.toStdString()
                  << " directory=" << directory.toStdString() << " valid=" << m.valid()
                  << " reason=" << m.error.toStdString();
        return m;
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
    const auto viewError = validateViews( m.json.value( "views" ).toArray() );
    if ( !actionError.isEmpty() ) {
        m.error = actionError;
    }
    if ( !viewError.isEmpty() ) {
        m.error = viewError;
    }
    if ( !fieldError.isEmpty() ) {
        m.error = fieldError;
    }
    // E17: the host-to-plugin methods the plugin handles and the host features it requires.
    const auto handlesError = validateHandles( m.json.value( "handles" ) );
    if ( !handlesError.isEmpty() ) {
        m.error = handlesError;
    }
    // E17b: menus, context menus and keyboard shortcuts (docs/plugin-sdk-advanced.md section 7).
    const auto menuError
        = validateMenus( m.json.value( "menus" ), declaredIds( m.json.value( "actions" ) ),
                         declaredIds( m.json.value( "views" ) ) );
    if ( !menuError.isEmpty() ) {
        m.error = menuError;
    }
    for ( const auto* key : { "actions", "views" } ) {
        for ( const auto& item : m.json.value( QLatin1String( key ) ).toArray() ) {
            const auto shortcut = item.toObject().value( "shortcut" );
            const auto error = shortcut.isUndefined() ? QString{} : validateShortcut( shortcut );
            if ( !error.isEmpty() ) {
                m.error = QStringLiteral( "%1 (%2)" ).arg(
                    error, item.toObject().value( "id" ).toString().left( 128 ) );
            }
        }
    }
    const auto shortcutCountError = validateShortcutCount( m.json );
    if ( !shortcutCountError.isEmpty() ) {
        m.error = shortcutCountError;
    }
    const auto requiresError = validateRequires( m.json.value( "requires" ), &m.missingFeatures );
    if ( !requiresError.isEmpty() ) {
        m.error = requiresError;
    }
    else if ( m.error.isEmpty() && !m.missingFeatures.isEmpty() ) {
        m.error = QStringLiteral( "Requires a newer CILogg (missing: %1)." )
                      .arg( m.missingFeatures.join( QStringLiteral( ", " ) ) );
    }
    // `kind` is read before anything else matters: a kind this host does not support is never
    // run, whatever else the manifest says (configuration packs come with E16).
    if ( m.json.contains( "kind" )
         && ( !m.json.value( "kind" ).isString() || !supportedKind( packageKind( m.json ) ) ) ) {
        m.error = QStringLiteral( "Unsupported plugin kind: %1." )
                      .arg( m.json.value( "kind" ).toVariant().toString().left( 64 ) );
    }
    LOG_DEBUG << "Plugin manifest id=" << m.id.toStdString()
              << " directory=" << directory.toStdString() << " valid=" << m.valid()
              << " reason=" << m.error.toStdString();
    return m;
}
} // namespace cilogg::plugins
