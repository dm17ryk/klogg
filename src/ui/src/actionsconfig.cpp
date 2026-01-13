#include "actionsconfig.h"

#include "previewdecodeutils.h"

namespace {
ActionSequenceType parseSequenceType( const QString& text, bool* ok )
{
    if ( ok ) {
        *ok = true;
    }
    const auto normalized = text.trimmed().toLower();
    if ( normalized == "string" ) {
        return ActionSequenceType::String;
    }
    if ( normalized == "hexstring" ) {
        return ActionSequenceType::HexString;
    }
    if ( ok ) {
        *ok = false;
    }
    return ActionSequenceType::String;
}

ResponseMatchType parseMatchType( const QString& text, bool* ok )
{
    if ( ok ) {
        *ok = true;
    }
    const auto normalized = text.trimmed().toLower();
    if ( normalized == "string" ) {
        return ResponseMatchType::String;
    }
    if ( normalized == "hexstring" ) {
        return ResponseMatchType::HexString;
    }
    if ( normalized == "regex" ) {
        return ResponseMatchType::Regex;
    }
    if ( ok ) {
        *ok = false;
    }
    return ResponseMatchType::String;
}
} // namespace

QString actionSequenceTypeToString( ActionSequenceType type )
{
    switch ( type ) {
    case ActionSequenceType::HexString:
        return "hexString";
    case ActionSequenceType::String:
    default:
        return "string";
    }
}

ActionSequenceType actionSequenceTypeFromString( const QString& text, bool* ok )
{
    return parseSequenceType( text, ok );
}

QString responseMatchTypeToString( ResponseMatchType type )
{
    switch ( type ) {
    case ResponseMatchType::HexString:
        return "hexString";
    case ResponseMatchType::Regex:
        return "regex";
    case ResponseMatchType::String:
    default:
        return "string";
    }
}

ResponseMatchType responseMatchTypeFromString( const QString& text, bool* ok )
{
    return parseMatchType( text, ok );
}

ActionSequenceResult actionSequenceToBytes( const ActionSequence& sequence,
                                            const QMap<QString, QString>& substitutions,
                                            QStringList* missing )
{
    ActionSequenceResult result;
    const QString resolved
        = substitutions.isEmpty()
              ? sequence.value
              : resolveTemplateString( sequence.value, substitutions, missing );

    if ( sequence.type == ActionSequenceType::String ) {
        result.ok = true;
        result.bytes = resolved.toLatin1();
        return result;
    }

    const auto decoded = decodeHexStringToBytes( resolved );
    if ( !decoded.ok ) {
        result.error = decoded.error;
        return result;
    }
    result.ok = true;
    result.bytes = decoded.bytes;
    return result;
}
