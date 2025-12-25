#include "previewconfigparser.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace {
QString contextPrefix( const QString& context, const QString& field )
{
    if ( context.isEmpty() ) {
        return field;
    }
    return QString( "%1.%2" ).arg( context, field );
}

PreviewValueExpr parseValueExpr( const QJsonValue& value,
                                 QStringList* warnings,
                                 const QString& context )
{
    PreviewValueExpr expr;
    if ( value.isUndefined() || value.isNull() ) {
        return expr;
    }

    expr.isSet = true;
    if ( value.isDouble() ) {
        expr.isLiteral = true;
        expr.literalValue = value.toInt();
        return expr;
    }
    if ( value.isString() ) {
        expr.expression = value.toString();
        return expr;
    }

    if ( warnings ) {
        warnings->push_back(
            QString( "Invalid value expression at %1." ).arg( context ) );
    }
    expr.isSet = false;
    return expr;
}

PreviewCaptureRef parseCaptureRef( const QJsonValue& value,
                                   QStringList* warnings,
                                   const QString& context )
{
    PreviewCaptureRef capture;
    if ( value.isUndefined() || value.isNull() ) {
        return capture;
    }

    capture.isSet = true;
    if ( value.isDouble() ) {
        capture.isIndex = true;
        capture.index = value.toInt();
        return capture;
    }
    if ( value.isString() ) {
        capture.name = value.toString();
        return capture;
    }

    if ( warnings ) {
        warnings->push_back(
            QString( "Invalid capture reference at %1." ).arg( context ) );
    }
    capture.isSet = false;
    return capture;
}

QStringList unknownKeys( const QJsonObject& object, const QSet<QString>& knownKeys )
{
    QStringList unknown;
    for ( const auto& key : object.keys() ) {
        if ( !knownKeys.contains( key ) ) {
            unknown.push_back( key );
        }
    }
    return unknown;
}

bool parseFieldSpec( const QJsonObject& object,
                     PreviewFieldSpec* out,
                     QStringList* errors,
                     QStringList* warnings,
                     const QString& context );

QVector<PreviewFieldSpec> parseFieldArray( const QJsonValue& value,
                                           QStringList* errors,
                                           QStringList* warnings,
                                           const QString& context )
{
    QVector<PreviewFieldSpec> fields;
    if ( !value.isArray() ) {
        if ( errors ) {
            errors->push_back( QString( "Expected array at %1." ).arg( context ) );
        }
        return fields;
    }

    const auto array = value.toArray();
    fields.reserve( array.size() );
    int index = 0;
    for ( const auto& item : array ) {
        if ( !item.isObject() ) {
            if ( errors ) {
                errors->push_back(
                    QString( "Expected object at %1[%2]." ).arg( context ).arg( index ) );
            }
            ++index;
            continue;
        }

        PreviewFieldSpec spec;
        if ( parseFieldSpec( item.toObject(), &spec, errors, warnings,
                             QString( "%1[%2]" ).arg( context ).arg( index ) ) ) {
            fields.push_back( std::move( spec ) );
        }
        ++index;
    }
    return fields;
}

bool parseFieldSpec( const QJsonObject& object,
                     PreviewFieldSpec* out,
                     QStringList* errors,
                     QStringList* warnings,
                     const QString& context )
{
    if ( !out ) {
        return false;
    }

    const auto nameValue = object.value( "name" );
    if ( !nameValue.isString() || nameValue.toString().trimmed().isEmpty() ) {
        if ( errors ) {
            errors->push_back( QString( "Missing field name at %1." ).arg( context ) );
        }
        return false;
    }

    out->name = nameValue.toString();

    const QSet<QString> knownKeys = { "name",    "source",     "capture",   "offset",
                                      "width",   "type",       "endianness","format",
                                      "enumMap", "flagMap",    "fields",    "bitfieldMap" };
    const auto extraKeys = unknownKeys( object, knownKeys );
    for ( const auto& key : extraKeys ) {
        if ( warnings ) {
            warnings->push_back( QString( "Unknown field property '%1' at %2." )
                                     .arg( key, context ) );
        }
    }

    if ( object.contains( "source" ) ) {
        bool ok = false;
        out->source = previewFieldSourceFromString( object.value( "source" ).toString(),
                                                    &ok );
        if ( !ok && warnings ) {
            warnings->push_back( QString( "Unknown field source at %1." ).arg( context ) );
        }
    }

    out->capture = parseCaptureRef( object.value( "capture" ), warnings,
                                    contextPrefix( context, "capture" ) );

    out->offset = parseValueExpr( object.value( "offset" ), warnings,
                                  contextPrefix( context, "offset" ) );
    out->width = parseValueExpr( object.value( "width" ), warnings,
                                 contextPrefix( context, "width" ) );

    if ( object.contains( "type" ) ) {
        bool ok = false;
        out->type = previewBufferTypeFromString( object.value( "type" ).toString(), &ok );
        if ( !ok && warnings ) {
            warnings->push_back( QString( "Unknown field type at %1." ).arg( context ) );
        }
    }

    if ( object.contains( "format" ) ) {
        bool ok = false;
        out->format = previewFormatFromString( object.value( "format" ).toString(), &ok );
        if ( !ok && warnings ) {
            warnings->push_back( QString( "Unknown field format at %1." ).arg( context ) );
        }
    }
    else if ( object.contains( "fields" ) ) {
        out->format = PreviewFormat::Fields;
    }

    if ( object.contains( "endianness" ) ) {
        out->endianness = object.value( "endianness" ).toString();
    }

    if ( object.contains( "enumMap" ) ) {
        const auto enumObj = object.value( "enumMap" ).toObject();
        for ( auto it = enumObj.begin(); it != enumObj.end(); ++it ) {
            out->enumMap.insert( it.key(), it.value().toString() );
        }
    }

    if ( object.contains( "flagMap" ) ) {
        const auto flagObj = object.value( "flagMap" ).toObject();
        for ( auto it = flagObj.begin(); it != flagObj.end(); ++it ) {
            out->flagMap.insert( it.key(), it.value().toString() );
        }
    }

    if ( out->source == PreviewFieldSource::Capture && !out->capture.isSet ) {
        if ( warnings ) {
            warnings->push_back( QString( "Missing capture for field %1." ).arg( context ) );
        }
    }

    if ( out->format == PreviewFormat::Enum && out->enumMap.isEmpty() ) {
        if ( warnings ) {
            warnings->push_back( QString( "Missing enumMap for field %1." ).arg( context ) );
        }
    }

    if ( out->format == PreviewFormat::Flags && out->flagMap.isEmpty() ) {
        if ( warnings ) {
            warnings->push_back( QString( "Missing flagMap for field %1." ).arg( context ) );
        }
    }

    if ( out->format == PreviewFormat::Fields ) {
        if ( !object.contains( "fields" ) ) {
            if ( errors ) {
                errors->push_back( QString( "Missing fields for %1." ).arg( context ) );
            }
            return false;
        }
        out->fields = parseFieldArray( object.value( "fields" ), errors, warnings,
                                       contextPrefix( context, "fields" ) );
    }

    if ( out->format == PreviewFormat::Bitfield ) {
        if ( !object.contains( "bitfieldMap" ) ) {
            if ( warnings ) {
                warnings->push_back(
                    QString( "Missing bitfieldMap for %1." ).arg( context ) );
            }
        }
        else {
            out->bitfieldMap = parseFieldArray( object.value( "bitfieldMap" ), errors,
                                                warnings,
                                                contextPrefix( context, "bitfieldMap" ) );
        }
    }

    return true;
}

bool parsePreviewDefinition( const QJsonObject& object,
                             PreviewDefinition* out,
                             QStringList* errors,
                             QStringList* warnings,
                             int index )
{
    if ( !out ) {
        return false;
    }

    const auto nameValue = object.value( "name" );
    const auto name = nameValue.toString();
    if ( !nameValue.isString() || name.trimmed().isEmpty() ) {
        if ( errors ) {
            errors->push_back( QString( "Missing preview name at index %1." ).arg( index ) );
        }
        return false;
    }

    const auto regexValue = object.contains( "regex" ) ? object.value( "regex" )
                                                       : object.value( "pattern" );
    if ( !regexValue.isString() || regexValue.toString().trimmed().isEmpty() ) {
        if ( errors ) {
            errors->push_back(
                QString( "Missing preview regex for '%1'." ).arg( name ) );
        }
        return false;
    }

    const QSet<QString> knownKeys
        = { "name",     "regex",  "pattern", "enabled", "bufferCapture",
            "offset",   "type",   "format",  "fields" };
    const auto extraKeys = unknownKeys( object, knownKeys );
    for ( const auto& key : extraKeys ) {
        if ( warnings ) {
            warnings->push_back(
                QString( "Unknown preview property '%1' for '%2'." ).arg( key, name ) );
        }
    }

    out->name = name;
    out->regex = regexValue.toString();
    out->compiled = QRegularExpression( out->regex );
    if ( !out->compiled.isValid() ) {
        if ( errors ) {
            errors->push_back(
                QString( "Invalid regex for '%1': %2" ).arg( name, out->compiled.errorString() ) );
        }
        return false;
    }

    if ( object.contains( "enabled" ) ) {
        out->hasEnabled = true;
        out->enabled = object.value( "enabled" ).toBool( true );
    }

    out->bufferCapture = parseCaptureRef( object.value( "bufferCapture" ), warnings,
                                          QString( "preview %1 bufferCapture" ).arg( name ) );
    out->offset = parseValueExpr( object.value( "offset" ), warnings,
                                  QString( "preview %1 offset" ).arg( name ) );

    if ( object.contains( "type" ) ) {
        bool ok = false;
        out->type = previewBufferTypeFromString( object.value( "type" ).toString(), &ok );
        if ( !ok && warnings ) {
            warnings->push_back(
                QString( "Unknown preview buffer type for '%1'." ).arg( name ) );
        }
    }

    if ( object.contains( "format" ) ) {
        bool ok = false;
        out->format = previewFormatFromString( object.value( "format" ).toString(), &ok );
        if ( !ok && warnings ) {
            warnings->push_back(
                QString( "Unknown preview format for '%1'." ).arg( name ) );
        }
    }

    if ( out->format == PreviewFormat::Fields ) {
        if ( !object.contains( "fields" ) ) {
            if ( errors ) {
                errors->push_back( QString( "Missing fields for '%1'." ).arg( name ) );
            }
            return false;
        }
        out->fields = parseFieldArray( object.value( "fields" ), errors, warnings,
                                       QString( "preview %1 fields" ).arg( name ) );
    }
    return true;
}
} // namespace

PreviewParseResult PreviewConfigParser::parseFile( const QString& path ) const
{
    PreviewParseResult result;
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        result.errors.push_back( QString( "Failed to open %1." ).arg( path ) );
        return result;
    }
    return parseJson( file.readAll() );
}

PreviewParseResult PreviewConfigParser::parseJson( const QByteArray& jsonBytes ) const
{
    PreviewParseResult result;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson( jsonBytes, &parseError );
    if ( parseError.error != QJsonParseError::NoError ) {
        result.errors.push_back(
            QString( "Invalid JSON: %1 at offset %2." )
                .arg( parseError.errorString() )
                .arg( parseError.offset ) );
        return result;
    }

    QJsonArray previewArray;
    if ( document.isArray() ) {
        previewArray = document.array();
    }
    else if ( document.isObject() ) {
        const auto rootObj = document.object();
        if ( rootObj.contains( "previews" ) && rootObj.value( "previews" ).isArray() ) {
            previewArray = rootObj.value( "previews" ).toArray();
        }
        else {
            result.errors.push_back( "Missing 'previews' array in JSON." );
            return result;
        }
    }
    else {
        result.errors.push_back( "Unsupported JSON root format." );
        return result;
    }

    result.previews.reserve( previewArray.size() );
    int index = 0;
    for ( const auto& item : previewArray ) {
        if ( !item.isObject() ) {
            result.errors.push_back( QString( "Preview entry %1 is not an object." )
                                         .arg( index ) );
            ++index;
            continue;
        }

        PreviewDefinition definition;
        if ( parsePreviewDefinition( item.toObject(), &definition, &result.errors,
                                     &result.warnings, index ) ) {
            result.previews.push_back( std::move( definition ) );
        }
        ++index;
    }

    return result;
}
