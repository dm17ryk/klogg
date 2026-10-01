#include "actionexpression.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QDate>
#include <QDateTime>
#include <QMetaType>
#include <QRegularExpression>
#include <QSet>
#include <QTime>
#include <QUrl>

#include "log.h"

namespace {
ActionExpressionResult success( QVariant value )
{
    ActionExpressionResult result;
    result.ok = true;
    result.value = std::move( value );
    return result;
}

ActionExpressionResult failure( const QString& error )
{
    ActionExpressionResult result;
    result.error = error;
    return result;
}

bool isNull( const QVariant& value )
{
    return !value.isValid() || value.isNull();
}

bool isIntegerType( int typeId )
{
    return typeId == QMetaType::Bool || typeId == QMetaType::Char
           || typeId == QMetaType::SChar || typeId == QMetaType::UChar
           || typeId == QMetaType::Short || typeId == QMetaType::UShort
           || typeId == QMetaType::Int || typeId == QMetaType::UInt
           || typeId == QMetaType::Long || typeId == QMetaType::ULong
           || typeId == QMetaType::LongLong || typeId == QMetaType::ULongLong;
}

bool isNumeric( const QVariant& value )
{
    return isIntegerType( value.typeId() ) || value.typeId() == QMetaType::Float
           || value.typeId() == QMetaType::Double;
}

bool toInteger( const QVariant& value, qlonglong* output )
{
    if ( output == nullptr || isNull( value ) ) {
        return false;
    }
    bool ok = false;
    qlonglong parsed = 0;
    if ( isIntegerType( value.typeId() ) ) {
        parsed = value.toLongLong( &ok );
    }
    else if ( value.typeId() == QMetaType::Float || value.typeId() == QMetaType::Double ) {
        const auto number = value.toDouble( &ok );
        if ( ok && std::isfinite( number )
             && number >= static_cast<double>( std::numeric_limits<qlonglong>::min() )
             && number <= static_cast<double>( std::numeric_limits<qlonglong>::max() ) ) {
            parsed = static_cast<qlonglong>( number );
        }
        else {
            ok = false;
        }
    }
    else {
        auto text = value.toString().trimmed();
        int base = 10;
        bool negative = false;
        if ( text.startsWith( QLatin1Char( '-' ) ) ) {
            negative = true;
            text.remove( 0, 1 );
        }
        if ( text.startsWith( QStringLiteral( "0x" ), Qt::CaseInsensitive ) ) {
            base = 16;
            text.remove( 0, 2 );
        }
        else if ( text.startsWith( QStringLiteral( "0b" ), Qt::CaseInsensitive ) ) {
            base = 2;
            text.remove( 0, 2 );
        }
        parsed = text.toLongLong( &ok, base );
        if ( ok && negative ) {
            parsed = -parsed;
        }
    }
    if ( ok ) {
        *output = parsed;
    }
    return ok;
}

bool toNumber( const QVariant& value, double* output )
{
    if ( output == nullptr || isNull( value ) ) {
        return false;
    }
    bool ok = false;
    const auto parsed = value.toDouble( &ok );
    if ( ok && std::isfinite( parsed ) ) {
        *output = parsed;
        return true;
    }
    return false;
}

QString valueToString( const QVariant& value )
{
    if ( isNull( value ) ) {
        return {};
    }
    if ( value.typeId() == QMetaType::QByteArray ) {
        return QString::fromLatin1( value.toByteArray() );
    }
    if ( value.typeId() == QMetaType::QVariantList ) {
        QStringList parts;
        for ( const auto& item : value.toList() ) {
            parts.push_back( valueToString( item ) );
        }
        return parts.join( QLatin1Char( ',' ) );
    }
    if ( value.typeId() == QMetaType::Bool ) {
        return value.toBool() ? QStringLiteral( "true" ) : QStringLiteral( "false" );
    }
    if ( value.typeId() == QMetaType::Double ) {
        return QString::number( value.toDouble(), 'g', 15 );
    }
    return value.toString();
}

bool truthy( const QVariant& value )
{
    if ( isNull( value ) ) {
        return false;
    }
    if ( value.typeId() == QMetaType::Bool ) {
        return value.toBool();
    }
    if ( isNumeric( value ) ) {
        return value.toDouble() != 0.0;
    }
    if ( value.typeId() == QMetaType::QVariantList ) {
        return !value.toList().isEmpty();
    }
    if ( value.typeId() == QMetaType::QByteArray ) {
        return !value.toByteArray().isEmpty();
    }
    return !value.toString().isEmpty();
}

ActionExpressionResult requireArgumentCount( const QString& name,
                                             const QVector<QVariant>& arguments,
                                             int minimum,
                                             int maximum = -1 )
{
    if ( arguments.size() < minimum || ( maximum >= 0 && arguments.size() > maximum ) ) {
        const auto expected = maximum < 0 || maximum == minimum
                                  ? QString::number( minimum )
                                  : QStringLiteral( "%1..%2" ).arg( minimum ).arg( maximum );
        return failure( QStringLiteral( "%1 expects %2 argument(s), got %3." )
                            .arg( name, expected )
                            .arg( arguments.size() ) );
    }
    return success( {} );
}

ActionExpressionResult bytesFromValue( const QVariant& value )
{
    if ( value.typeId() == QMetaType::QByteArray ) {
        return success( value.toByteArray() );
    }
    if ( value.typeId() == QMetaType::QVariantList ) {
        QByteArray bytes;
        for ( const auto& item : value.toList() ) {
            qlonglong number = 0;
            if ( !toInteger( item, &number ) || number < 0 || number > 255 ) {
                return failure( QStringLiteral( "Byte list items must be in range 0..255." ) );
            }
            bytes.append( static_cast<char>( number ) );
        }
        return success( bytes );
    }
    return success( valueToString( value ).toUtf8() );
}

QByteArray packUnsigned( quint64 value, int width, bool littleEndian )
{
    QByteArray bytes( width, '\0' );
    for ( int index = 0; index < width; ++index ) {
        const auto shift = littleEndian ? index * 8 : ( width - index - 1 ) * 8;
        bytes[index] = static_cast<char>( ( value >> shift ) & 0xffU );
    }
    return bytes;
}

quint16 crc16( const QByteArray& bytes, quint16 initial, quint16 polynomial, bool reflected )
{
    quint16 crc = initial;
    for ( const auto byte : bytes ) {
        if ( reflected ) {
            crc ^= static_cast<quint8>( byte );
            for ( int bit = 0; bit < 8; ++bit ) {
                crc = ( crc & 1U ) != 0U ? static_cast<quint16>( ( crc >> 1U ) ^ polynomial )
                                         : static_cast<quint16>( crc >> 1U );
            }
        }
        else {
            crc ^= static_cast<quint16>( static_cast<quint8>( byte ) << 8U );
            for ( int bit = 0; bit < 8; ++bit ) {
                crc = ( crc & 0x8000U ) != 0U
                          ? static_cast<quint16>( ( crc << 1U ) ^ polynomial )
                          : static_cast<quint16>( crc << 1U );
            }
        }
    }
    return crc;
}

quint32 crc32Value( const QByteArray& bytes )
{
    quint32 crc = 0xffffffffU;
    for ( const auto byte : bytes ) {
        crc ^= static_cast<quint8>( byte );
        for ( int bit = 0; bit < 8; ++bit ) {
            crc = ( crc & 1U ) != 0U ? ( crc >> 1U ) ^ 0xedb88320U : crc >> 1U;
        }
    }
    return crc ^ 0xffffffffU;
}

enum class TokenKind {
    End,
    Number,
    String,
    Identifier,
    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    Comma,
    Question,
    Colon,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Bang,
    Tilde,
    Ampersand,
    Pipe,
    Caret,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    EqualEqual,
    BangEqual,
    AndAnd,
    OrOr,
    ShiftLeft,
    ShiftRight
};

struct Token {
    TokenKind kind = TokenKind::End;
    QString text;
    QVariant value;
    int offset = 0;
};

class Lexer {
  public:
    Lexer( const QString& expression, const ActionExpressionLimits& limits )
        : expression_( expression )
        , limits_( limits )
    {
    }

    QVector<Token> tokenize( QString* error )
    {
        QVector<Token> tokens;
        while ( position_ < expression_.size() ) {
            if ( tokens.size() >= limits_.maximumTokens ) {
                setError( error, QStringLiteral( "Expression exceeds the token limit." ) );
                return {};
            }
            const auto character = expression_.at( position_ );
            if ( character.isSpace() ) {
                ++position_;
                continue;
            }
            if ( character.isDigit() ) {
                tokens.push_back( number( error ) );
                if ( error != nullptr && !error->isEmpty() ) {
                    return {};
                }
                continue;
            }
            if ( character == QLatin1Char( '"' ) || character == QLatin1Char( '\'' ) ) {
                tokens.push_back( string( error ) );
                if ( error != nullptr && !error->isEmpty() ) {
                    return {};
                }
                continue;
            }
            if ( character.isLetter() || character == QLatin1Char( '_' ) ) {
                tokens.push_back( identifier() );
                continue;
            }
            const auto token = punctuation( error );
            if ( error != nullptr && !error->isEmpty() ) {
                return {};
            }
            tokens.push_back( token );
        }
        tokens.push_back( { TokenKind::End, {}, {}, position_ } );
        return tokens;
    }

  private:
    void setError( QString* error, const QString& message ) const
    {
        if ( error != nullptr ) {
            *error = message;
        }
    }

    Token number( QString* error )
    {
        const auto start = position_;
        bool hexadecimal = false;
        bool binary = false;
        bool decimalPoint = false;
        if ( position_ + 1 < expression_.size() && expression_.at( position_ ) == QLatin1Char( '0' )
             && ( expression_.at( position_ + 1 ) == QLatin1Char( 'x' )
                  || expression_.at( position_ + 1 ) == QLatin1Char( 'X' ) ) ) {
            hexadecimal = true;
            position_ += 2;
            while ( position_ < expression_.size()
                    && ( expression_.at( position_ ).isDigit()
                         || ( expression_.at( position_ ).toLower() >= QLatin1Char( 'a' )
                              && expression_.at( position_ ).toLower() <= QLatin1Char( 'f' ) ) ) ) {
                ++position_;
            }
        }
        else if ( position_ + 1 < expression_.size()
                  && expression_.at( position_ ) == QLatin1Char( '0' )
                  && ( expression_.at( position_ + 1 ) == QLatin1Char( 'b' )
                       || expression_.at( position_ + 1 ) == QLatin1Char( 'B' ) ) ) {
            binary = true;
            position_ += 2;
            while ( position_ < expression_.size()
                    && ( expression_.at( position_ ) == QLatin1Char( '0' )
                         || expression_.at( position_ ) == QLatin1Char( '1' ) ) ) {
                ++position_;
            }
        }
        else {
            while ( position_ < expression_.size() ) {
                const auto character = expression_.at( position_ );
                if ( character.isDigit() ) {
                    ++position_;
                }
                else if ( character == QLatin1Char( '.' ) && !decimalPoint ) {
                    decimalPoint = true;
                    ++position_;
                }
                else {
                    break;
                }
            }
        }
        const auto text = expression_.mid( start, position_ - start );
        bool ok = false;
        QVariant value;
        if ( decimalPoint ) {
            value = text.toDouble( &ok );
        }
        else {
            auto digits = text;
            int base = 10;
            if ( hexadecimal || binary ) {
                digits.remove( 0, 2 );
                base = hexadecimal ? 16 : 2;
            }
            value = digits.toLongLong( &ok, base );
        }
        if ( !ok ) {
            setError( error, QStringLiteral( "Invalid number at offset %1." ).arg( start ) );
        }
        return { TokenKind::Number, text, value, start };
    }

    Token string( QString* error )
    {
        const auto start = position_;
        const auto quote = expression_.at( position_++ );
        QString value;
        bool closed = false;
        while ( position_ < expression_.size() ) {
            auto character = expression_.at( position_++ );
            if ( character == quote ) {
                closed = true;
                break;
            }
            if ( character != QLatin1Char( '\\' ) ) {
                value.append( character );
                continue;
            }
            if ( position_ >= expression_.size() ) {
                break;
            }
            character = expression_.at( position_++ );
            switch ( character.unicode() ) {
            case 'n': value.append( QLatin1Char( '\n' ) ); break;
            case 'r': value.append( QLatin1Char( '\r' ) ); break;
            case 't': value.append( QLatin1Char( '\t' ) ); break;
            case '\\': value.append( QLatin1Char( '\\' ) ); break;
            case '\'': value.append( QLatin1Char( '\'' ) ); break;
            case '"': value.append( QLatin1Char( '"' ) ); break;
            default:
                setError( error,
                          QStringLiteral( "Unsupported string escape at offset %1." )
                              .arg( position_ - 2 ) );
                return {};
            }
        }
        if ( !closed ) {
            setError( error, QStringLiteral( "Unterminated string at offset %1." ).arg( start ) );
        }
        return { TokenKind::String, expression_.mid( start, position_ - start ), value, start };
    }

    Token identifier()
    {
        const auto start = position_++;
        while ( position_ < expression_.size() ) {
            const auto character = expression_.at( position_ );
            if ( character.isLetterOrNumber() || character == QLatin1Char( '_' )
                 || character == QLatin1Char( '.' ) ) {
                ++position_;
            }
            else {
                break;
            }
        }
        return { TokenKind::Identifier, expression_.mid( start, position_ - start ), {}, start };
    }

    Token punctuation( QString* error )
    {
        const auto start = position_;
        const auto one = expression_.mid( position_, 1 );
        const auto two = expression_.mid( position_, 2 );
        static const QHash<QString, TokenKind> doubleTokens = {
            { QStringLiteral( "<=" ), TokenKind::LessEqual },
            { QStringLiteral( ">=" ), TokenKind::GreaterEqual },
            { QStringLiteral( "==" ), TokenKind::EqualEqual },
            { QStringLiteral( "!=" ), TokenKind::BangEqual },
            { QStringLiteral( "&&" ), TokenKind::AndAnd },
            { QStringLiteral( "||" ), TokenKind::OrOr },
            { QStringLiteral( "<<" ), TokenKind::ShiftLeft },
            { QStringLiteral( ">>" ), TokenKind::ShiftRight },
        };
        if ( doubleTokens.contains( two ) ) {
            position_ += 2;
            return { doubleTokens.value( two ), two, {}, start };
        }
        static const QHash<QChar, TokenKind> singleTokens = {
            { '(', TokenKind::LeftParen },   { ')', TokenKind::RightParen },
            { '[', TokenKind::LeftBracket }, { ']', TokenKind::RightBracket },
            { ',', TokenKind::Comma },       { '?', TokenKind::Question },
            { ':', TokenKind::Colon },       { '+', TokenKind::Plus },
            { '-', TokenKind::Minus },       { '*', TokenKind::Star },
            { '/', TokenKind::Slash },       { '%', TokenKind::Percent },
            { '!', TokenKind::Bang },        { '~', TokenKind::Tilde },
            { '&', TokenKind::Ampersand },   { '|', TokenKind::Pipe },
            { '^', TokenKind::Caret },       { '<', TokenKind::Less },
            { '>', TokenKind::Greater },
        };
        if ( singleTokens.contains( one.at( 0 ) ) ) {
            ++position_;
            return { singleTokens.value( one.at( 0 ) ), one, {}, start };
        }
        setError( error, QStringLiteral( "Unexpected character at offset %1." ).arg( start ) );
        return {};
    }

    const QString& expression_;
    const ActionExpressionLimits& limits_;
    int position_ = 0;
};

class Parser {
  public:
    Parser( QVector<Token> tokens,
            const QVariantMap& variables,
            const ActionExpressionLimits& limits )
        : tokens_( std::move( tokens ) )
        , variables_( variables )
        , limits_( limits )
    {
    }

    ActionExpressionResult parse()
    {
        auto result = conditional( 0 );
        if ( !result.ok ) {
            return result;
        }
        if ( current().kind != TokenKind::End ) {
            return error( QStringLiteral( "Unexpected token '%1'." ).arg( current().text ) );
        }
        return result;
    }

  private:
    using ParseFunction = ActionExpressionResult ( Parser::* )( int );

    const Token& current() const { return tokens_.at( position_ ); }
    const Token& previous() const { return tokens_.at( position_ - 1 ); }

    bool match( TokenKind kind )
    {
        if ( current().kind != kind ) {
            return false;
        }
        ++position_;
        return true;
    }

    ActionExpressionResult error( const QString& message ) const
    {
        return failure( QStringLiteral( "%1 At offset %2." ).arg( message ).arg( current().offset ) );
    }

    ActionExpressionResult depthError( int depth ) const
    {
        if ( depth <= limits_.maximumNestingDepth ) {
            return success( {} );
        }
        return failure( QStringLiteral( "Expression exceeds the nesting-depth limit." ) );
    }

    ActionExpressionResult conditional( int depth )
    {
        if ( const auto checked = depthError( depth ); !checked.ok ) {
            return checked;
        }
        auto condition = logicalOr( depth );
        if ( !condition.ok || !match( TokenKind::Question ) ) {
            return condition;
        }
        auto whenTrue = conditional( depth + 1 );
        if ( !whenTrue.ok ) {
            return whenTrue;
        }
        if ( !match( TokenKind::Colon ) ) {
            return error( QStringLiteral( "Expected ':' in conditional expression." ) );
        }
        auto whenFalse = conditional( depth + 1 );
        if ( !whenFalse.ok ) {
            return whenFalse;
        }
        return truthy( condition.value ) ? whenTrue : whenFalse;
    }

    ActionExpressionResult logicalOr( int depth )
    {
        auto left = logicalAnd( depth );
        while ( left.ok && match( TokenKind::OrOr ) ) {
            auto right = logicalAnd( depth );
            if ( !right.ok ) {
                return right;
            }
            left = success( truthy( left.value ) || truthy( right.value ) );
        }
        return left;
    }

    ActionExpressionResult logicalAnd( int depth )
    {
        auto left = bitwiseOr( depth );
        while ( left.ok && match( TokenKind::AndAnd ) ) {
            auto right = bitwiseOr( depth );
            if ( !right.ok ) {
                return right;
            }
            left = success( truthy( left.value ) && truthy( right.value ) );
        }
        return left;
    }

    ActionExpressionResult integerBinary( ActionExpressionResult left,
                                          int depth,
                                          ParseFunction next,
                                          const QSet<TokenKind>& operators )
    {
        while ( left.ok && operators.contains( current().kind ) ) {
            const auto operation = current().kind;
            ++position_;
            auto right = ( this->*next )( depth );
            if ( !right.ok ) {
                return right;
            }
            qlonglong lhs = 0;
            qlonglong rhs = 0;
            if ( !toInteger( left.value, &lhs ) || !toInteger( right.value, &rhs ) ) {
                return error( QStringLiteral( "Bitwise operators require integer operands." ) );
            }
            switch ( operation ) {
            case TokenKind::Pipe: lhs |= rhs; break;
            case TokenKind::Caret: lhs ^= rhs; break;
            case TokenKind::Ampersand: lhs &= rhs; break;
            case TokenKind::ShiftLeft:
                if ( rhs < 0 || rhs >= 64 ) {
                    return error( QStringLiteral( "Left-shift count must be in range 0..63." ) );
                }
                lhs <<= rhs;
                break;
            case TokenKind::ShiftRight:
                if ( rhs < 0 || rhs >= 64 ) {
                    return error( QStringLiteral( "Right-shift count must be in range 0..63." ) );
                }
                lhs >>= rhs;
                break;
            default: break;
            }
            left = success( lhs );
        }
        return left;
    }

    ActionExpressionResult bitwiseOr( int depth )
    {
        return integerBinary( bitwiseXor( depth ), depth, &Parser::bitwiseXor,
                              { TokenKind::Pipe } );
    }
    ActionExpressionResult bitwiseXor( int depth )
    {
        return integerBinary( bitwiseAnd( depth ), depth, &Parser::bitwiseAnd,
                              { TokenKind::Caret } );
    }
    ActionExpressionResult bitwiseAnd( int depth )
    {
        return integerBinary( equality( depth ), depth, &Parser::equality,
                              { TokenKind::Ampersand } );
    }

    ActionExpressionResult equality( int depth )
    {
        auto left = comparison( depth );
        while ( left.ok && ( current().kind == TokenKind::EqualEqual
                            || current().kind == TokenKind::BangEqual ) ) {
            const auto operation = current().kind;
            ++position_;
            auto right = comparison( depth );
            if ( !right.ok ) {
                return right;
            }
            bool equal = false;
            if ( isNull( left.value ) || isNull( right.value ) ) {
                equal = isNull( left.value ) && isNull( right.value );
            }
            else if ( isNumeric( left.value ) && isNumeric( right.value ) ) {
                equal = left.value.toDouble() == right.value.toDouble();
            }
            else {
                equal = left.value == right.value;
            }
            left = success( operation == TokenKind::EqualEqual ? equal : !equal );
        }
        return left;
    }

    ActionExpressionResult comparison( int depth )
    {
        auto left = shift( depth );
        while ( left.ok && ( current().kind == TokenKind::Less
                            || current().kind == TokenKind::LessEqual
                            || current().kind == TokenKind::Greater
                            || current().kind == TokenKind::GreaterEqual ) ) {
            const auto operation = current().kind;
            ++position_;
            auto right = shift( depth );
            if ( !right.ok ) {
                return right;
            }
            double lhs = 0;
            double rhs = 0;
            if ( !toNumber( left.value, &lhs ) || !toNumber( right.value, &rhs ) ) {
                return error( QStringLiteral( "Comparison operators require numeric operands." ) );
            }
            bool result = false;
            switch ( operation ) {
            case TokenKind::Less: result = lhs < rhs; break;
            case TokenKind::LessEqual: result = lhs <= rhs; break;
            case TokenKind::Greater: result = lhs > rhs; break;
            case TokenKind::GreaterEqual: result = lhs >= rhs; break;
            default: break;
            }
            left = success( result );
        }
        return left;
    }

    ActionExpressionResult shift( int depth )
    {
        return integerBinary( additive( depth ), depth, &Parser::additive,
                              { TokenKind::ShiftLeft, TokenKind::ShiftRight } );
    }

    ActionExpressionResult additive( int depth )
    {
        auto left = multiplicative( depth );
        while ( left.ok && ( current().kind == TokenKind::Plus
                            || current().kind == TokenKind::Minus ) ) {
            const auto operation = current().kind;
            ++position_;
            auto right = multiplicative( depth );
            if ( !right.ok ) {
                return right;
            }
            if ( operation == TokenKind::Plus
                 && ( left.value.typeId() == QMetaType::QString
                      || right.value.typeId() == QMetaType::QString ) ) {
                left = success( valueToString( left.value ) + valueToString( right.value ) );
                continue;
            }
            double lhs = 0;
            double rhs = 0;
            if ( !toNumber( left.value, &lhs ) || !toNumber( right.value, &rhs ) ) {
                return error( QStringLiteral( "Arithmetic operators require numeric operands." ) );
            }
            if ( isIntegerType( left.value.typeId() ) && isIntegerType( right.value.typeId() ) ) {
                qlonglong lhsInteger = 0;
                qlonglong rhsInteger = 0;
                toInteger( left.value, &lhsInteger );
                toInteger( right.value, &rhsInteger );
                left = success( operation == TokenKind::Plus ? lhsInteger + rhsInteger
                                                              : lhsInteger - rhsInteger );
            }
            else {
                left = success( operation == TokenKind::Plus ? lhs + rhs : lhs - rhs );
            }
        }
        return left;
    }

    ActionExpressionResult multiplicative( int depth )
    {
        auto left = unary( depth );
        while ( left.ok && ( current().kind == TokenKind::Star
                            || current().kind == TokenKind::Slash
                            || current().kind == TokenKind::Percent ) ) {
            const auto operation = current().kind;
            ++position_;
            auto right = unary( depth );
            if ( !right.ok ) {
                return right;
            }
            if ( operation == TokenKind::Percent ) {
                qlonglong lhs = 0;
                qlonglong rhs = 0;
                if ( !toInteger( left.value, &lhs ) || !toInteger( right.value, &rhs ) ) {
                    return error( QStringLiteral( "Modulo requires integer operands." ) );
                }
                if ( rhs == 0 ) {
                    return error( QStringLiteral( "Modulo by zero." ) );
                }
                left = success( lhs % rhs );
                continue;
            }
            double lhs = 0;
            double rhs = 0;
            if ( !toNumber( left.value, &lhs ) || !toNumber( right.value, &rhs ) ) {
                return error( QStringLiteral( "Arithmetic operators require numeric operands." ) );
            }
            if ( operation == TokenKind::Slash && rhs == 0.0 ) {
                return error( QStringLiteral( "Division by zero." ) );
            }
            left = success( operation == TokenKind::Star ? lhs * rhs : lhs / rhs );
        }
        return left;
    }

    ActionExpressionResult unary( int depth )
    {
        if ( match( TokenKind::Bang ) ) {
            auto value = unary( depth + 1 );
            return value.ok ? success( !truthy( value.value ) ) : value;
        }
        if ( match( TokenKind::Tilde ) ) {
            auto value = unary( depth + 1 );
            qlonglong number = 0;
            if ( !value.ok || !toInteger( value.value, &number ) ) {
                return value.ok ? error( QStringLiteral( "Bitwise not requires an integer." ) )
                                : value;
            }
            return success( ~number );
        }
        if ( match( TokenKind::Minus ) ) {
            auto value = unary( depth + 1 );
            double number = 0;
            if ( !value.ok || !toNumber( value.value, &number ) ) {
                return value.ok ? error( QStringLiteral( "Unary minus requires a number." ) )
                                : value;
            }
            if ( isIntegerType( value.value.typeId() ) ) {
                qlonglong integer = 0;
                toInteger( value.value, &integer );
                return success( -integer );
            }
            return success( -number );
        }
        if ( match( TokenKind::Plus ) ) {
            return unary( depth + 1 );
        }
        return primary( depth );
    }

    ActionExpressionResult primary( int depth )
    {
        if ( const auto checked = depthError( depth ); !checked.ok ) {
            return checked;
        }
        if ( match( TokenKind::Number ) || match( TokenKind::String ) ) {
            return success( previous().value );
        }
        if ( match( TokenKind::LeftParen ) ) {
            auto value = conditional( depth + 1 );
            if ( !value.ok ) {
                return value;
            }
            if ( !match( TokenKind::RightParen ) ) {
                return error( QStringLiteral( "Expected ')'." ) );
            }
            return value;
        }
        if ( match( TokenKind::LeftBracket ) ) {
            QVariantList values;
            if ( !match( TokenKind::RightBracket ) ) {
                do {
                    auto value = conditional( depth + 1 );
                    if ( !value.ok ) {
                        return value;
                    }
                    values.push_back( value.value );
                } while ( match( TokenKind::Comma ) );
                if ( !match( TokenKind::RightBracket ) ) {
                    return error( QStringLiteral( "Expected ']'." ) );
                }
            }
            return success( values );
        }
        if ( match( TokenKind::Identifier ) ) {
            const auto name = previous().text;
            if ( name.compare( QStringLiteral( "true" ), Qt::CaseInsensitive ) == 0 ) {
                return success( true );
            }
            if ( name.compare( QStringLiteral( "false" ), Qt::CaseInsensitive ) == 0 ) {
                return success( false );
            }
            if ( name.compare( QStringLiteral( "null" ), Qt::CaseInsensitive ) == 0 ) {
                return success( {} );
            }
            if ( !match( TokenKind::LeftParen ) ) {
                return success( variables_.value( name ) );
            }
            QVector<QVariant> arguments;
            if ( !match( TokenKind::RightParen ) ) {
                do {
                    if ( arguments.size() >= limits_.maximumFunctionArguments ) {
                        return error( QStringLiteral( "Function exceeds the argument limit." ) );
                    }
                    auto argument = conditional( depth + 1 );
                    if ( !argument.ok ) {
                        return argument;
                    }
                    arguments.push_back( argument.value );
                } while ( match( TokenKind::Comma ) );
                if ( !match( TokenKind::RightParen ) ) {
                    return error( QStringLiteral( "Expected ')' after function arguments." ) );
                }
            }
            LOG_DEBUG << "Action expression calling function " << name.toStdString()
                      << " with " << arguments.size() << " argument(s)";
            return ActionExpressionRegistry::instance().call( name, arguments );
        }
        return error( QStringLiteral( "Expected a value." ) );
    }

    QVector<Token> tokens_;
    const QVariantMap& variables_;
    const ActionExpressionLimits& limits_;
    int position_ = 0;
};
} // namespace

ActionExpressionRegistry& ActionExpressionRegistry::instance()
{
    static ActionExpressionRegistry registry;
    return registry;
}

ActionExpressionRegistry::ActionExpressionRegistry()
{
    registerBuiltins();
}

bool ActionExpressionRegistry::registerFunction( const QString& name,
                                                  ActionExpressionFunction function,
                                                  QString* errorMessage )
{
    const auto normalized = name.trimmed().toLower();
    if ( frozen_ ) {
        if ( errorMessage ) {
            *errorMessage = QStringLiteral( "The expression registry is frozen." );
        }
        LOG_WARNING << "Rejected late action expression function registration: "
                    << normalized.toStdString();
        return false;
    }
    if ( !normalized.contains( QLatin1Char( '.' ) ) ) {
        if ( errorMessage ) {
            *errorMessage = QStringLiteral( "Plugin functions must use a namespace." );
        }
        LOG_WARNING << "Rejected unnamespaced action expression plugin function: "
                    << normalized.toStdString();
        return false;
    }
    if ( normalized.isEmpty() || !function || functions_.contains( normalized ) ) {
        if ( errorMessage ) {
            *errorMessage = QStringLiteral( "The expression function name is invalid or duplicate." );
        }
        LOG_WARNING << "Rejected invalid or duplicate action expression function: "
                    << normalized.toStdString();
        return false;
    }
    functions_.insert( normalized, std::move( function ) );
    LOG_INFO << "Registered action expression plugin function " << normalized.toStdString();
    return true;
}

void ActionExpressionRegistry::freeze()
{
    if ( !frozen_ ) {
        LOG_INFO << "Freezing action expression function registry with " << functions_.size()
                 << " function(s)";
        frozen_ = true;
    }
}

bool ActionExpressionRegistry::isFrozen() const
{
    return frozen_;
}

bool ActionExpressionRegistry::contains( const QString& name ) const
{
    return functions_.contains( name.trimmed().toLower() );
}

ActionExpressionResult ActionExpressionRegistry::call(
    const QString& name,
    const QVector<QVariant>& arguments ) const
{
    const auto normalized = name.trimmed().toLower();
    const auto iterator = functions_.constFind( normalized );
    if ( iterator == functions_.constEnd() ) {
        LOG_WARNING << "Action expression referenced unknown function "
                    << normalized.toStdString();
        return failure( QStringLiteral( "Unknown function '%1'." ).arg( name ) );
    }
    auto result = iterator.value()( arguments );
    if ( !result.ok ) {
        LOG_WARNING << "Action expression function " << normalized.toStdString()
                    << " failed: " << result.error.toStdString();
    }
    return result;
}

void ActionExpressionRegistry::registerBuiltins()
{
    const auto add = [ this ]( const QString& name, ActionExpressionFunction function ) {
        functions_.insert( name, std::move( function ) );
    };
    add( QStringLiteral( "concat" ), []( const QVector<QVariant>& arguments ) {
        QString output;
        for ( const auto& argument : arguments ) {
            output.append( valueToString( argument ) );
        }
        return success( output );
    } );
    add( QStringLiteral( "str" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "str" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        return success( valueToString( arguments.front() ) );
    } );
    add( QStringLiteral( "int" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "int" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        qlonglong value = 0;
        return toInteger( arguments.front(), &value ) ? success( value )
                                                       : failure( QStringLiteral( "int requires an integer-compatible value." ) );
    } );
    add( QStringLiteral( "hex" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "hex" ), arguments, 1, 2 );
             !count.ok ) {
            return count;
        }
        qlonglong value = 0;
        qlonglong width = 0;
        if ( !toInteger( arguments.at( 0 ), &value )
             || ( arguments.size() == 2 && !toInteger( arguments.at( 1 ), &width ) )
             || width < 0 || width > 64 ) {
            return failure( QStringLiteral( "hex requires an integer and optional width 0..64." ) );
        }
        auto output = QString::number( static_cast<qulonglong>( value ), 16 ).toUpper();
        if ( width > output.size() ) {
            output = output.rightJustified( static_cast<int>( width ), QLatin1Char( '0' ) );
        }
        return success( output );
    } );
    add( QStringLiteral( "utf8" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "utf8" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        return success( valueToString( arguments.front() ).toUtf8() );
    } );
    add( QStringLiteral( "latin1" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "latin1" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        return success( valueToString( arguments.front() ).toLatin1() );
    } );
    add( QStringLiteral( "hex_bytes" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count
             = requireArgumentCount( QStringLiteral( "hex_bytes" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        auto text = valueToString( arguments.front() );
        text.remove( QRegularExpression( QStringLiteral( "[\\s,:;_-]" ) ) );
        if ( text.size() % 2 != 0
             || !QRegularExpression( QStringLiteral( "^[0-9A-Fa-f]*$" ) ).match( text ).hasMatch() ) {
            return failure( QStringLiteral( "hex_bytes requires an even number of hexadecimal digits." ) );
        }
        return success( QByteArray::fromHex( text.toLatin1() ) );
    } );
    add( QStringLiteral( "bytes" ), []( const QVector<QVariant>& arguments ) {
        QByteArray output;
        for ( const auto& argument : arguments ) {
            const auto converted = bytesFromValue( argument );
            if ( !converted.ok ) {
                return converted;
            }
            output.append( converted.value.toByteArray() );
        }
        return success( output );
    } );
    add( QStringLiteral( "join" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "join" ), arguments, 1, 2 );
             !count.ok ) {
            return count;
        }
        const auto separator = arguments.size() == 2 ? valueToString( arguments.at( 1 ) )
                                                     : QStringLiteral( "," );
        QStringList values;
        for ( const auto& item : arguments.front().toList() ) {
            values.push_back( valueToString( item ) );
        }
        return success( values.join( separator ) );
    } );
    add( QStringLiteral( "split" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "split" ), arguments, 2, 2 );
             !count.ok ) {
            return count;
        }
        QVariantList output;
        for ( const auto& part : valueToString( arguments.at( 0 ) )
                                     .split( valueToString( arguments.at( 1 ) ), Qt::KeepEmptyParts ) ) {
            output.push_back( part );
        }
        return success( output );
    } );
    add( QStringLiteral( "replace" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count
             = requireArgumentCount( QStringLiteral( "replace" ), arguments, 3, 3 );
             !count.ok ) {
            return count;
        }
        auto value = valueToString( arguments.at( 0 ) );
        value.replace( valueToString( arguments.at( 1 ) ), valueToString( arguments.at( 2 ) ) );
        return success( value );
    } );
    add( QStringLiteral( "upper" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "upper" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        return success( valueToString( arguments.front() ).toUpper() );
    } );
    add( QStringLiteral( "lower" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "lower" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        return success( valueToString( arguments.front() ).toLower() );
    } );
    add( QStringLiteral( "trim" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "trim" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        return success( valueToString( arguments.front() ).trimmed() );
    } );
    add( QStringLiteral( "len" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "len" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        if ( arguments.front().typeId() == QMetaType::QVariantList ) {
            return success( arguments.front().toList().size() );
        }
        if ( arguments.front().typeId() == QMetaType::QByteArray ) {
            return success( arguments.front().toByteArray().size() );
        }
        return success( valueToString( arguments.front() ).size() );
    } );
    add( QStringLiteral( "abs" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "abs" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        if ( isIntegerType( arguments.front().typeId() ) ) {
            qlonglong value = 0;
            if ( !toInteger( arguments.front(), &value )
                 || value == std::numeric_limits<qlonglong>::min() ) {
                return failure( QStringLiteral( "abs received an invalid integer." ) );
            }
            return success( qAbs( value ) );
        }
        double value = 0.0;
        return toNumber( arguments.front(), &value )
                   ? success( std::fabs( value ) )
                   : failure( QStringLiteral( "abs requires a number." ) );
    } );
    const auto rounding = [ &add ]( const QString& name, auto operation ) {
        add( name, [ name, operation ]( const QVector<QVariant>& arguments ) {
            if ( const auto count = requireArgumentCount( name, arguments, 1, 1 ); !count.ok ) {
                return count;
            }
            double value = 0.0;
            return toNumber( arguments.front(), &value )
                       ? success( static_cast<qlonglong>( operation( value ) ) )
                       : failure( QStringLiteral( "%1 requires a number." ).arg( name ) );
        } );
    };
    rounding( QStringLiteral( "round" ), []( double value ) { return std::round( value ); } );
    rounding( QStringLiteral( "floor" ), []( double value ) { return std::floor( value ); } );
    rounding( QStringLiteral( "ceil" ), []( double value ) { return std::ceil( value ); } );
    const auto numericAggregate = [ &add ]( const QString& name, bool findMinimum ) {
        add( name, [ name, findMinimum ]( const QVector<QVariant>& arguments ) {
            if ( const auto count = requireArgumentCount( name, arguments, 1 ); !count.ok ) {
                return count;
            }
            double result = 0.0;
            if ( !toNumber( arguments.front(), &result ) ) {
                return failure( QStringLiteral( "%1 requires numeric arguments." ).arg( name ) );
            }
            for ( qsizetype index = 1; index < arguments.size(); ++index ) {
                double value = 0.0;
                if ( !toNumber( arguments.at( index ), &value ) ) {
                    return failure( QStringLiteral( "%1 requires numeric arguments." ).arg( name ) );
                }
                result = findMinimum ? qMin( result, value ) : qMax( result, value );
            }
            return success( result );
        } );
    };
    numericAggregate( QStringLiteral( "min" ), true );
    numericAggregate( QStringLiteral( "max" ), false );
    add( QStringLiteral( "clamp" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "clamp" ), arguments, 3, 3 );
             !count.ok ) {
            return count;
        }
        double value = 0.0;
        double minimum = 0.0;
        double maximum = 0.0;
        if ( !toNumber( arguments.at( 0 ), &value )
             || !toNumber( arguments.at( 1 ), &minimum )
             || !toNumber( arguments.at( 2 ), &maximum ) || minimum > maximum ) {
            return failure( QStringLiteral( "clamp requires value, minimum, and maximum numbers." ) );
        }
        const auto clamped = qBound( minimum, value, maximum );
        return isIntegerType( arguments.at( 0 ).typeId() )
                   ? success( static_cast<qlonglong>( clamped ) )
                   : success( clamped );
    } );
    add( QStringLiteral( "contains" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count
             = requireArgumentCount( QStringLiteral( "contains" ), arguments, 2, 2 );
             !count.ok ) {
            return count;
        }
        if ( arguments.front().typeId() == QMetaType::QVariantList ) {
            return success( arguments.front().toList().contains( arguments.at( 1 ) ) );
        }
        if ( arguments.front().typeId() == QMetaType::QByteArray ) {
            const auto needle = bytesFromValue( arguments.at( 1 ) );
            return needle.ok
                       ? success( arguments.front().toByteArray().contains(
                             needle.value.toByteArray() ) )
                       : needle;
        }
        return success( valueToString( arguments.front() )
                            .contains( valueToString( arguments.at( 1 ) ) ) );
    } );
    add( QStringLiteral( "slice" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "slice" ), arguments, 2, 3 );
             !count.ok ) {
            return count;
        }
        qlonglong start = 0;
        qlonglong length = -1;
        if ( !toInteger( arguments.at( 1 ), &start )
             || ( arguments.size() == 3 && !toInteger( arguments.at( 2 ), &length ) )
             || start < 0 || length < -1 ) {
            return failure( QStringLiteral( "slice requires a nonnegative start and optional length." ) );
        }
        if ( arguments.front().typeId() == QMetaType::QVariantList ) {
            const auto list = arguments.front().toList();
            return success( list.mid( static_cast<qsizetype>( start ),
                                      static_cast<qsizetype>( length ) ) );
        }
        if ( arguments.front().typeId() == QMetaType::QByteArray ) {
            return success( arguments.front().toByteArray().mid( static_cast<qsizetype>( start ),
                                                                  static_cast<qsizetype>( length ) ) );
        }
        return success( valueToString( arguments.front() )
                            .mid( static_cast<qsizetype>( start ),
                                  static_cast<qsizetype>( length ) ) );
    } );
    add( QStringLiteral( "reverse" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "reverse" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        if ( arguments.front().typeId() == QMetaType::QVariantList ) {
            auto values = arguments.front().toList();
            std::reverse( values.begin(), values.end() );
            return success( values );
        }
        auto value = valueToString( arguments.front() );
        std::reverse( value.begin(), value.end() );
        return success( value );
    } );
    add( QStringLiteral( "unique" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "unique" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        QVariantList output;
        for ( const auto& value : arguments.front().toList() ) {
            if ( !output.contains( value ) ) {
                output.push_back( value );
            }
        }
        return success( output );
    } );
    add( QStringLiteral( "sort" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "sort" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        auto values = arguments.front().toList();
        std::stable_sort( values.begin(), values.end(), []( const QVariant& left, const QVariant& right ) {
            return valueToString( left ) < valueToString( right );
        } );
        return success( values );
    } );
    add( QStringLiteral( "ipv4_bytes" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count
             = requireArgumentCount( QStringLiteral( "ipv4_bytes" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        const auto parts = valueToString( arguments.front() ).split( QLatin1Char( '.' ) );
        if ( parts.size() != 4 ) {
            return failure( QStringLiteral( "ipv4_bytes requires an IPv4 address." ) );
        }
        QByteArray bytes;
        for ( const auto& part : parts ) {
            bool ok = false;
            const auto value = part.toInt( &ok );
            if ( !ok || value < 0 || value > 255 || QString::number( value ) != part ) {
                return failure( QStringLiteral( "ipv4_bytes requires an IPv4 address." ) );
            }
            bytes.append( static_cast<char>( value ) );
        }
        return success( bytes );
    } );
    add( QStringLiteral( "mac_bytes" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count
             = requireArgumentCount( QStringLiteral( "mac_bytes" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        auto value = valueToString( arguments.front() );
        value.remove( QRegularExpression( QStringLiteral( "[:-]" ) ) );
        if ( !QRegularExpression( QStringLiteral( "^[0-9A-Fa-f]{12}$" ) )
                  .match( value )
                  .hasMatch() ) {
            return failure( QStringLiteral( "mac_bytes requires a 6-byte MAC address." ) );
        }
        return success( QByteArray::fromHex( value.toLatin1() ) );
    } );
    const auto formatTemporal = [ &add ]( const QString& name, QMetaType::Type type ) {
        add( name, [ name, type ]( const QVector<QVariant>& arguments ) {
            if ( const auto count = requireArgumentCount( name, arguments, 2, 2 ); !count.ok ) {
                return count;
            }
            const auto format = valueToString( arguments.at( 1 ) );
            if ( type == QMetaType::QDate ) {
                const auto value = arguments.front().canConvert<QDate>()
                                       ? arguments.front().toDate()
                                       : QDate::fromString( valueToString( arguments.front() ), Qt::ISODate );
                return value.isValid() ? success( value.toString( format ) )
                                       : failure( QStringLiteral( "%1 received an invalid date." ).arg( name ) );
            }
            if ( type == QMetaType::QTime ) {
                const auto value = arguments.front().canConvert<QTime>()
                                       ? arguments.front().toTime()
                                       : QTime::fromString( valueToString( arguments.front() ), Qt::ISODate );
                return value.isValid() ? success( value.toString( format ) )
                                       : failure( QStringLiteral( "%1 received an invalid time." ).arg( name ) );
            }
            const auto value = arguments.front().canConvert<QDateTime>()
                                   ? arguments.front().toDateTime()
                                   : QDateTime::fromString( valueToString( arguments.front() ), Qt::ISODate );
            return value.isValid() ? success( value.toString( format ) )
                                   : failure( QStringLiteral( "%1 received an invalid date-time." ).arg( name ) );
        } );
    };
    formatTemporal( QStringLiteral( "date_format" ), QMetaType::QDate );
    formatTemporal( QStringLiteral( "time_format" ), QMetaType::QTime );
    formatTemporal( QStringLiteral( "datetime_format" ), QMetaType::QDateTime );
    const auto pack = [ &add ]( const QString& name, int width, bool littleEndian ) {
        add( name, [ name, width, littleEndian ]( const QVector<QVariant>& arguments ) {
            if ( const auto count = requireArgumentCount( name, arguments, 1, 1 ); !count.ok ) {
                return count;
            }
            qlonglong value = 0;
            if ( !toInteger( arguments.front(), &value ) ) {
                return failure( QStringLiteral( "%1 requires an integer." ).arg( name ) );
            }
            return success( packUnsigned( static_cast<quint64>( value ), width, littleEndian ) );
        } );
    };
    pack( QStringLiteral( "u8" ), 1, true );
    pack( QStringLiteral( "u16le" ), 2, true );
    pack( QStringLiteral( "u16be" ), 2, false );
    pack( QStringLiteral( "u32le" ), 4, true );
    pack( QStringLiteral( "u32be" ), 4, false );
    pack( QStringLiteral( "u64le" ), 8, true );
    pack( QStringLiteral( "u64be" ), 8, false );
    add( QStringLiteral( "bcd" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "bcd" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        auto digits = valueToString( arguments.front() );
        digits.remove( QRegularExpression( QStringLiteral( "\\D" ) ) );
        if ( digits.size() % 2 != 0 ) {
            digits.prepend( QLatin1Char( '0' ) );
        }
        QByteArray output;
        for ( int index = 0; index < digits.size(); index += 2 ) {
            output.append( static_cast<char>( ( digits.at( index ).digitValue() << 4 )
                                              | digits.at( index + 1 ).digitValue() ) );
        }
        return success( output );
    } );
    add( QStringLiteral( "base64" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "base64" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        const auto bytes = bytesFromValue( arguments.front() );
        return bytes.ok ? success( bytes.value.toByteArray().toBase64() ) : bytes;
    } );
    add( QStringLiteral( "base64_decode" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count
             = requireArgumentCount( QStringLiteral( "base64_decode" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        const auto decoded = QByteArray::fromBase64Encoding( valueToString( arguments.front() ).toLatin1() );
        return decoded.decodingStatus == QByteArray::Base64DecodingStatus::Ok
                   ? success( decoded.decoded )
                   : failure( QStringLiteral( "base64_decode received invalid Base64." ) );
    } );
    add( QStringLiteral( "url_encode" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count
             = requireArgumentCount( QStringLiteral( "url_encode" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        return success( QUrl::toPercentEncoding( valueToString( arguments.front() ) ) );
    } );
    add( QStringLiteral( "url_decode" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count
             = requireArgumentCount( QStringLiteral( "url_decode" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        return success( QUrl::fromPercentEncoding( valueToString( arguments.front() ).toLatin1() ) );
    } );
    add( QStringLiteral( "regex_match" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count
             = requireArgumentCount( QStringLiteral( "regex_match" ), arguments, 2, 2 );
             !count.ok ) {
            return count;
        }
        const QRegularExpression regex( valueToString( arguments.at( 1 ) ) );
        return regex.isValid()
                   ? success( regex.match( valueToString( arguments.at( 0 ) ) ).hasMatch() )
                   : failure( regex.errorString() );
    } );
    add( QStringLiteral( "regex_replace" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count
             = requireArgumentCount( QStringLiteral( "regex_replace" ), arguments, 3, 3 );
             !count.ok ) {
            return count;
        }
        const QRegularExpression regex( valueToString( arguments.at( 1 ) ) );
        if ( !regex.isValid() ) {
            return failure( regex.errorString() );
        }
        auto value = valueToString( arguments.at( 0 ) );
        return success( value.replace( regex, valueToString( arguments.at( 2 ) ) ) );
    } );
    add( QStringLiteral( "sum8" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "sum8" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        const auto bytes = bytesFromValue( arguments.front() );
        if ( !bytes.ok ) {
            return bytes;
        }
        quint8 sum = 0;
        for ( const auto byte : bytes.value.toByteArray() ) {
            sum = static_cast<quint8>( sum + static_cast<quint8>( byte ) );
        }
        return success( static_cast<qlonglong>( sum ) );
    } );
    add( QStringLiteral( "xor8" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "xor8" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        const auto bytes = bytesFromValue( arguments.front() );
        if ( !bytes.ok ) {
            return bytes;
        }
        quint8 checksum = 0;
        for ( const auto byte : bytes.value.toByteArray() ) {
            checksum ^= static_cast<quint8>( byte );
        }
        return success( static_cast<qlonglong>( checksum ) );
    } );
    add( QStringLiteral( "crc16_modbus" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count
             = requireArgumentCount( QStringLiteral( "crc16_modbus" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        const auto bytes = bytesFromValue( arguments.front() );
        return bytes.ok ? success( static_cast<qlonglong>( crc16( bytes.value.toByteArray(), 0xffffU,
                                                                  0xa001U, true ) ) )
                        : bytes;
    } );
    add( QStringLiteral( "crc16_ccitt" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count
             = requireArgumentCount( QStringLiteral( "crc16_ccitt" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        const auto bytes = bytesFromValue( arguments.front() );
        return bytes.ok ? success( static_cast<qlonglong>( crc16( bytes.value.toByteArray(), 0xffffU,
                                                                  0x1021U, false ) ) )
                        : bytes;
    } );
    add( QStringLiteral( "crc32" ), []( const QVector<QVariant>& arguments ) {
        if ( const auto count = requireArgumentCount( QStringLiteral( "crc32" ), arguments, 1, 1 );
             !count.ok ) {
            return count;
        }
        const auto bytes = bytesFromValue( arguments.front() );
        return bytes.ok ? success( static_cast<qulonglong>( crc32Value( bytes.value.toByteArray() ) ) )
                        : bytes;
    } );
    add( QStringLiteral( "lookup" ), []( const QVector<QVariant>& arguments ) {
        if ( arguments.size() < 3 || arguments.size() % 2 == 0 ) {
            return failure( QStringLiteral( "lookup expects value followed by key/value pairs." ) );
        }
        for ( int index = 1; index + 1 < arguments.size(); index += 2 ) {
            if ( arguments.front() == arguments.at( index ) ) {
                return success( arguments.at( index + 1 ) );
            }
        }
        return success( {} );
    } );
    LOG_INFO << "Registered " << functions_.size() << " built-in action expression functions";
}

ActionExpressionResult evaluateActionExpression( const QString& expression,
                                                 const QVariantMap& variables,
                                                 const ActionExpressionLimits& limits )
{
    LOG_DEBUG << "Evaluating action expression length=" << expression.size()
              << " variables=" << variables.size();
    if ( expression.size() > limits.maximumExpressionLength ) {
        LOG_WARNING << "Rejected action expression because it exceeds the length limit";
        return failure( QStringLiteral( "Expression exceeds the length limit." ) );
    }
    if ( expression.trimmed().isEmpty() ) {
        LOG_WARNING << "Rejected empty action expression";
        return failure( QStringLiteral( "Expression is empty." ) );
    }
    auto& registry = ActionExpressionRegistry::instance();
    registry.freeze();
    QString lexerError;
    Lexer lexer( expression, limits );
    auto tokens = lexer.tokenize( &lexerError );
    if ( !lexerError.isEmpty() ) {
        LOG_WARNING << "Action expression tokenization failed: " << lexerError.toStdString();
        return failure( lexerError );
    }
    Parser parser( std::move( tokens ), variables, limits );
    auto result = parser.parse();
    if ( result.ok ) {
        LOG_DEBUG << "Action expression evaluation succeeded with type " << result.value.typeId();
    }
    else {
        LOG_WARNING << "Action expression evaluation failed: " << result.error.toStdString();
    }
    return result;
}

ActionExpressionResult actionExpressionValueToBytes( const QVariant& value,
                                                     int maximumOutputBytes )
{
    auto converted = bytesFromValue( value );
    if ( !converted.ok ) {
        return converted;
    }
    const auto bytes = converted.value.toByteArray();
    if ( bytes.size() > maximumOutputBytes ) {
        LOG_WARNING << "Rejected action expression output because it exceeds the byte limit: "
                    << bytes.size();
        return failure( QStringLiteral( "Expression output exceeds the byte limit." ) );
    }
    return success( bytes );
}
