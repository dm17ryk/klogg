#include "previewdecodeutils.h"

#include <QtGlobal>

namespace {
struct HexNormalized {
    bool ok = false;
    QString digits;
    int digitCount = 0;
    QString error;
};

bool isWhitespaceOrSeparator( const QChar ch )
{
    return ch.isSpace() || ch == '_';
}

HexNormalized normalizeHexInput( const QString& input )
{
    HexNormalized normalized;
    auto trimmed = input.trimmed();
    if ( trimmed.startsWith( "0x", Qt::CaseInsensitive ) ) {
        trimmed = trimmed.mid( 2 );
    }

    QString digits;
    digits.reserve( trimmed.size() );
    int digitIndex = 0;
    for ( int i = 0; i < trimmed.size(); ++i ) {
        const auto ch = trimmed.at( i );
        if ( isWhitespaceOrSeparator( ch ) ) {
            continue;
        }
        if ( ch.isDigit() || ( ch.toLower() >= 'a' && ch.toLower() <= 'f' ) ) {
            digits.push_back( ch );
            ++digitIndex;
            continue;
        }
        normalized.error
            = QString( "Invalid hex digit '%1' at position %2." ).arg( ch ).arg( digitIndex + 1 );
        return normalized;
    }

    if ( digits.isEmpty() ) {
        normalized.error = "Hex string is empty.";
        return normalized;
    }

    normalized.ok = true;
    normalized.digits = digits;
    normalized.digitCount = digits.size();
    return normalized;
}

bool parseBinaryString( const QString& value, qint64* out )
{
    if ( !out ) {
        return false;
    }
    if ( value.isEmpty() ) {
        return false;
    }
    for ( const auto& ch : value ) {
        if ( ch != '0' && ch != '1' ) {
            return false;
        }
    }
    bool ok = false;
    *out = value.toLongLong( &ok, 2 );
    return ok;
}

bool parseSignedInteger( const QString& token, qint64* out )
{
    if ( !out ) {
        return false;
    }
    const auto trimmed = token.trimmed();
    if ( trimmed.startsWith( "0b", Qt::CaseInsensitive ) ) {
        return parseBinaryString( trimmed.mid( 2 ), out );
    }
    if ( parseBinaryString( trimmed, out ) ) {
        return true;
    }
    bool ok = false;
    *out = trimmed.toLongLong( &ok, 0 );
    return ok;
}
} // namespace

HexParseResult parseHexToU64AllowOddDigits( const QString& input )
{
    HexParseResult result;
    const auto normalized = normalizeHexInput( input );
    if ( !normalized.ok ) {
        result.error = normalized.error;
        return result;
    }

    result.digitCount = normalized.digitCount;
    if ( normalized.digitCount > 16 ) {
        result.error = QString( "Hex value too large (%1 digits)." ).arg( normalized.digitCount );
        return result;
    }

    bool ok = false;
    result.value = normalized.digits.toULongLong( &ok, 16 );
    if ( !ok ) {
        result.error = "Failed to parse hex value.";
        return result;
    }

    result.ok = true;
    return result;
}

HexDecodeResult decodeHexStringToBytes( const QString& input )
{
    HexDecodeResult result;
    const auto normalized = normalizeHexInput( input );
    if ( !normalized.ok ) {
        result.error = normalized.error;
        return result;
    }

    result.digitCount = normalized.digitCount;
    auto digits = normalized.digits;
    if ( digits.size() % 2 != 0 ) {
        digits.prepend( '0' );
    }

    result.bytes = QByteArray::fromHex( digits.toUtf8() );
    if ( result.bytes.isEmpty() && !digits.isEmpty() ) {
        result.error = "Failed to decode hex bytes.";
        return result;
    }

    result.ok = true;
    return result;
}

PreviewExpressionResult evaluatePreviewExpression( const PreviewValueExpr& expr,
                                                   const QMap<QString, qint64>& values )
{
    PreviewExpressionResult result;
    if ( !expr.isSet ) {
        result.ok = true;
        result.value = 0;
        return result;
    }
    if ( expr.isLiteral ) {
        result.ok = true;
        result.value = expr.literalValue;
        return result;
    }

    const auto expression = expr.expression.trimmed();
    if ( expression.isEmpty() ) {
        result.error = "Expression is empty.";
        return result;
    }

    qint64 total = 0;
    bool firstTerm = true;
    bool expectTerm = true;
    int sign = 1;
    int pos = 0;

    auto skipSpaces = [ &expression, &pos ]() {
        while ( pos < expression.size() && expression.at( pos ).isSpace() ) {
            ++pos;
        }
    };

    auto parseTerm = [ &expression, &pos, &values, &result ]( qint64* out ) {
        if ( !out ) {
            return false;
        }
        if ( pos >= expression.size() ) {
            return false;
        }

        if ( expression.at( pos ) == '{' ) {
            const int start = pos + 1;
            const int end = expression.indexOf( '}', start );
            if ( end < 0 ) {
                result.error = "Missing '}' in expression.";
                return false;
            }
            const auto key = expression.mid( start, end - start ).trimmed();
            if ( key.isEmpty() ) {
                result.error = "Empty variable name in expression.";
                return false;
            }
            if ( !values.contains( key ) ) {
                result.missingVariable = key;
                result.error = QString( "Missing variable %1." ).arg( key );
                return false;
            }
            *out = values.value( key );
            pos = end + 1;
            return true;
        }

        int start = pos;
        while ( pos < expression.size() ) {
            const auto ch = expression.at( pos );
            if ( ch.isSpace() || ch == '+' || ch == '-' ) {
                break;
            }
            ++pos;
        }
        const auto token = expression.mid( start, pos - start );
        if ( token.isEmpty() ) {
            result.error = "Expected numeric token.";
            return false;
        }
        if ( !parseSignedInteger( token, out ) ) {
            result.error = QString( "Invalid numeric token '%1'." ).arg( token );
            return false;
        }
        return true;
    };

    while ( pos < expression.size() ) {
        skipSpaces();
        if ( expectTerm ) {
            if ( pos < expression.size() ) {
                const auto ch = expression.at( pos );
                if ( ch == '+' || ch == '-' ) {
                    sign = ( ch == '-' ) ? -1 : 1;
                    ++pos;
                    skipSpaces();
                }
            }

            qint64 termValue = 0;
            if ( !parseTerm( &termValue ) ) {
                return result;
            }
            if ( firstTerm ) {
                total = sign * termValue;
                firstTerm = false;
            }
            else {
                total += sign * termValue;
            }
            expectTerm = false;
        }
        else {
            skipSpaces();
            if ( pos >= expression.size() ) {
                break;
            }
            const auto ch = expression.at( pos );
            if ( ch != '+' && ch != '-' ) {
                result.error = "Expected '+' or '-' in expression.";
                return result;
            }
            sign = ( ch == '-' ) ? -1 : 1;
            ++pos;
            expectTerm = true;
        }
    }

    if ( expectTerm ) {
        result.error = "Expression ends with an operator.";
        return result;
    }

    result.ok = true;
    result.value = total;
    return result;
}
