// A stand-in for `git credential fill|approve|reject` in the E15b tests.
//
// FAKE_GIT_RECORD  file that gets one JSON line per call: arguments, standard input and the
//                  variables the credential rules set or remove.
// FAKE_GIT_MODE    answer to `fill`: bearer (authtype=Bearer), jwt (a JWT password), basic
//                  (user name and password), empty (no credential), fail (exit 1 with a message on
//                  standard error) or hang (no answer for two minutes).
// FAKE_GIT_TOKENS  comma-separated tokens; the n-th `fill` call returns the n-th (the last one
//                  repeats).
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <algorithm>
#include <cstdio>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {
QByteArray readStandardInput()
{
    QFile input;
    if ( !input.open( stdin, QIODevice::ReadOnly ) ) {
        return {};
    }
    return input.readAll();
}

int previousFills( const QString& recordPath )
{
    QFile record( recordPath );
    if ( !record.open( QIODevice::ReadOnly ) ) {
        return 0;
    }
    int count = 0;
    for ( const auto& line : record.readAll().split( '\n' ) ) {
        if ( QJsonDocument::fromJson( line ).object().value( "verb" ).toString() == "fill" ) {
            ++count;
        }
    }
    return count;
}

QByteArray environmentValue( const char* name )
{
    return qEnvironmentVariableIsSet( name ) ? qgetenv( name ) : QByteArray( "<unset>" );
}

QByteArray jwtFor( const QByteArray& token )
{
    const auto options = QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals;
    return QByteArray( R"({"alg":"RS256","typ":"JWT"})" ).toBase64( options ) + '.'
           + ( R"({"token":")" + token + R"("})" ).toBase64( options ) + ".c2ln";
}

void write( const QByteArray& bytes )
{
    std::fwrite( bytes.constData(), 1, static_cast<size_t>( bytes.size() ), stdout );
    std::fflush( stdout );
}
} // namespace

int main( int argc, char** argv )
{
#ifdef _WIN32
    // Like Git: LF on the wire, no text-mode CR LF translation.
    _setmode( _fileno( stdin ), _O_BINARY );
    _setmode( _fileno( stdout ), _O_BINARY );
#endif
    QCoreApplication app( argc, argv );
    const auto arguments = app.arguments().mid( 1 );
    const auto verb = arguments.isEmpty() ? QString{} : arguments.last();
    const auto input = readStandardInput();
    const auto recordPath = qEnvironmentVariable( "FAKE_GIT_RECORD" );
    const auto fills = verb == "fill" ? previousFills( recordPath ) : 0;

    if ( !recordPath.isEmpty() ) {
        QFile record( recordPath );
        if ( record.open( QIODevice::Append ) ) {
            const QJsonObject line{
                { "arguments", QJsonArray::fromStringList( arguments ) },
                { "verb", verb },
                { "stdin", QString::fromUtf8( input ) },
                { "GIT_TERMINAL_PROMPT", QString::fromUtf8( environmentValue( "GIT_TERMINAL_PROMPT" ) ) },
                { "GCM_INTERACTIVE", QString::fromUtf8( environmentValue( "GCM_INTERACTIVE" ) ) },
                { "GIT_ASKPASS", QString::fromUtf8( environmentValue( "GIT_ASKPASS" ) ) },
                { "SSH_ASKPASS", QString::fromUtf8( environmentValue( "SSH_ASKPASS" ) ) },
                { "GIT_CEILING_DIRECTORIES",
                  QString::fromUtf8( environmentValue( "GIT_CEILING_DIRECTORIES" ) ) },
                { "cwd", QDir::currentPath() } };
            record.write( QJsonDocument( line ).toJson( QJsonDocument::Compact ) + '\n' );
        }
    }
    if ( verb != "fill" ) {
        return 0;
    }

    const auto mode = qEnvironmentVariable( "FAKE_GIT_MODE", "basic" );
    const auto tokens = qgetenv( "FAKE_GIT_TOKENS" ).split( ',' );
    const auto token = tokens.isEmpty() ? QByteArray( "token" )
                                        : tokens.at( std::min( fills, int( tokens.size() ) - 1 ) );
    if ( mode == "hang" ) {
        QThread::sleep( 120 );
        return 0;
    }
    if ( mode == "fail" ) {
        std::fputs( "fatal: SECRET-STDERR the credential manager failed\n", stderr );
        return 1;
    }
    // Git echoes the request's attributes before the credential.
    QByteArray answer;
    for ( const auto& line : input.split( '\n' ) ) {
        if ( line.startsWith( "protocol=" ) || line.startsWith( "host=" )
             || line.startsWith( "path=" ) ) {
            answer += line + '\n';
        }
    }
    if ( mode == "bearer" ) {
        answer += "capability[]=authtype\nauthtype=Bearer\ncredential=" + token + '\n';
    }
    else if ( mode == "jwt" ) {
        answer += "username=user@example.com\npassword=" + jwtFor( token ) + '\n';
    }
    else if ( mode == "basic" ) {
        answer += "username=user\npassword=" + token + '\n';
    }
    write( answer );
    return 0;
}
