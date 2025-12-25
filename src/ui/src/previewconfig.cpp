#include "previewconfig.h"

namespace {
QString normalizedKey( const QString& value )
{
    return value.trimmed().toLower();
}
} // namespace

QString previewBufferTypeToString( PreviewBufferType type )
{
    switch ( type ) {
    case PreviewBufferType::String:
        return "string";
    case PreviewBufferType::HexString:
        return "hexString";
    case PreviewBufferType::Base64:
        return "base64";
    case PreviewBufferType::Bin:
        return "bin";
    case PreviewBufferType::Bytes:
        return "bytes";
    }
    return "string";
}

QString previewFormatToString( PreviewFormat format )
{
    switch ( format ) {
    case PreviewFormat::Fields:
        return "fields";
    case PreviewFormat::Match:
        return "match";
    case PreviewFormat::String:
        return "string";
    case PreviewFormat::Dig:
        return "dig";
    case PreviewFormat::Dec:
        return "dec";
    case PreviewFormat::Hex:
        return "hex";
    case PreviewFormat::Bin:
        return "bin";
    case PreviewFormat::Enum:
        return "enum";
    case PreviewFormat::Flags:
        return "flags";
    case PreviewFormat::Bitfield:
        return "bitfield";
    }
    return "fields";
}

QString previewFieldSourceToString( PreviewFieldSource source )
{
    switch ( source ) {
    case PreviewFieldSource::Buffer:
        return "buffer";
    case PreviewFieldSource::Capture:
        return "capture";
    }
    return "buffer";
}

PreviewBufferType previewBufferTypeFromString( const QString& value, bool* ok )
{
    const auto key = normalizedKey( value );
    if ( key == "string" ) {
        if ( ok ) {
            *ok = true;
        }
        return PreviewBufferType::String;
    }
    if ( key == "hexstring" ) {
        if ( ok ) {
            *ok = true;
        }
        return PreviewBufferType::HexString;
    }
    if ( key == "base64" ) {
        if ( ok ) {
            *ok = true;
        }
        return PreviewBufferType::Base64;
    }
    if ( key == "bin" || key == "binary" ) {
        if ( ok ) {
            *ok = true;
        }
        return PreviewBufferType::Bin;
    }
    if ( key == "bytes" ) {
        if ( ok ) {
            *ok = true;
        }
        return PreviewBufferType::Bytes;
    }
    if ( ok ) {
        *ok = false;
    }
    return PreviewBufferType::String;
}

PreviewFormat previewFormatFromString( const QString& value, bool* ok )
{
    const auto key = normalizedKey( value );
    if ( key == "fields" ) {
        if ( ok ) {
            *ok = true;
        }
        return PreviewFormat::Fields;
    }
    if ( key == "match" ) {
        if ( ok ) {
            *ok = true;
        }
        return PreviewFormat::Match;
    }
    if ( key == "string" ) {
        if ( ok ) {
            *ok = true;
        }
        return PreviewFormat::String;
    }
    if ( key == "dig" ) {
        if ( ok ) {
            *ok = true;
        }
        return PreviewFormat::Dig;
    }
    if ( key == "dec" ) {
        if ( ok ) {
            *ok = true;
        }
        return PreviewFormat::Dec;
    }
    if ( key == "hex" ) {
        if ( ok ) {
            *ok = true;
        }
        return PreviewFormat::Hex;
    }
    if ( key == "bin" ) {
        if ( ok ) {
            *ok = true;
        }
        return PreviewFormat::Bin;
    }
    if ( key == "enum" ) {
        if ( ok ) {
            *ok = true;
        }
        return PreviewFormat::Enum;
    }
    if ( key == "flags" ) {
        if ( ok ) {
            *ok = true;
        }
        return PreviewFormat::Flags;
    }
    if ( key == "bitfield" ) {
        if ( ok ) {
            *ok = true;
        }
        return PreviewFormat::Bitfield;
    }
    if ( ok ) {
        *ok = false;
    }
    return PreviewFormat::Fields;
}

PreviewFieldSource previewFieldSourceFromString( const QString& value, bool* ok )
{
    const auto key = normalizedKey( value );
    if ( key == "buffer" ) {
        if ( ok ) {
            *ok = true;
        }
        return PreviewFieldSource::Buffer;
    }
    if ( key == "capture" ) {
        if ( ok ) {
            *ok = true;
        }
        return PreviewFieldSource::Capture;
    }
    if ( ok ) {
        *ok = false;
    }
    return PreviewFieldSource::Buffer;
}
