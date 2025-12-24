#include "previewrepository.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "persistentinfo.h"

namespace {
QJsonValue toJsonValue( const PreviewValueExpr& expr )
{
    if ( !expr.isSet ) {
        return {};
    }
    if ( expr.isLiteral ) {
        return expr.literalValue;
    }
    return expr.expression;
}

QJsonValue toJsonValue( const PreviewCaptureRef& capture )
{
    if ( !capture.isSet ) {
        return {};
    }
    if ( capture.isIndex ) {
        return capture.index;
    }
    return capture.name;
}

QJsonObject fieldToJson( const PreviewFieldSpec& field )
{
    QJsonObject obj;
    obj.insert( "name", field.name );

    if ( field.source != PreviewFieldSource::Buffer ) {
        obj.insert( "source", previewFieldSourceToString( field.source ) );
    }

    const auto captureValue = toJsonValue( field.capture );
    if ( !captureValue.isUndefined() ) {
        obj.insert( "capture", captureValue );
    }

    const auto offsetValue = toJsonValue( field.offset );
    if ( !offsetValue.isUndefined() ) {
        obj.insert( "offset", offsetValue );
    }

    const auto widthValue = toJsonValue( field.width );
    if ( !widthValue.isUndefined() ) {
        obj.insert( "width", widthValue );
    }

    if ( field.type != PreviewBufferType::Bytes ) {
        obj.insert( "type", previewBufferTypeToString( field.type ) );
    }

    if ( !field.endianness.isEmpty() ) {
        obj.insert( "endianness", field.endianness );
    }

    if ( field.format != PreviewFormat::String ) {
        obj.insert( "format", previewFormatToString( field.format ) );
    }

    if ( !field.enumMap.isEmpty() ) {
        QJsonObject enumObj;
        for ( auto it = field.enumMap.begin(); it != field.enumMap.end(); ++it ) {
            enumObj.insert( it.key(), it.value() );
        }
        obj.insert( "enumMap", enumObj );
    }

    if ( !field.flagMap.isEmpty() ) {
        QJsonObject flagObj;
        for ( auto it = field.flagMap.begin(); it != field.flagMap.end(); ++it ) {
            flagObj.insert( it.key(), it.value() );
        }
        obj.insert( "flagMap", flagObj );
    }

    if ( !field.fields.isEmpty() ) {
        QJsonArray fieldArray;
        for ( const auto& child : field.fields ) {
            fieldArray.append( fieldToJson( child ) );
        }
        obj.insert( "fields", fieldArray );
    }

    if ( !field.bitfieldMap.isEmpty() ) {
        QJsonArray fieldArray;
        for ( const auto& child : field.bitfieldMap ) {
            fieldArray.append( fieldToJson( child ) );
        }
        obj.insert( "bitfieldMap", fieldArray );
    }

    return obj;
}

QJsonObject previewToJson( const PreviewDefinition& preview )
{
    QJsonObject obj;
    obj.insert( "name", preview.name );
    obj.insert( "regex", preview.regex );
    obj.insert( "enabled", preview.enabled );

    const auto bufferCaptureValue = toJsonValue( preview.bufferCapture );
    if ( !bufferCaptureValue.isUndefined() ) {
        obj.insert( "bufferCapture", bufferCaptureValue );
    }

    const auto offsetValue = toJsonValue( preview.offset );
    if ( !offsetValue.isUndefined() ) {
        obj.insert( "offset", offsetValue );
    }

    obj.insert( "type", previewBufferTypeToString( preview.type ) );
    obj.insert( "format", previewFormatToString( preview.format ) );

    if ( !preview.fields.isEmpty() ) {
        QJsonArray fieldArray;
        for ( const auto& field : preview.fields ) {
            fieldArray.append( fieldToJson( field ) );
        }
        obj.insert( "fields", fieldArray );
    }

    return obj;
}
} // namespace

PreviewParseResult PreviewRepository::load() const
{
    PreviewConfigParser parser;
    const auto path = storagePath();
    if ( !QFileInfo::exists( path ) ) {
        return {};
    }
    return parser.parseFile( path );
}

bool PreviewRepository::save( const QVector<PreviewDefinition>& previews ) const
{
    QJsonArray previewArray;
    for ( const auto& preview : previews ) {
        previewArray.append( previewToJson( preview ) );
    }

    QJsonObject root;
    root.insert( "version", 1 );
    root.insert( "previews", previewArray );

    const QJsonDocument doc( root );
    QSaveFile file( storagePath() );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
        return false;
    }
    file.write( doc.toJson( QJsonDocument::Indented ) );
    return file.commit();
}

QString PreviewRepository::storagePath() const
{
    const auto& settings = PersistentInfo::getSettings( app_settings{} );
    const QFileInfo settingsInfo( settings.fileName() );
    QDir dir = settingsInfo.absoluteDir();
    if ( !dir.exists() ) {
        dir.mkpath( "." );
    }
    return dir.filePath( "previews.json" );
}
