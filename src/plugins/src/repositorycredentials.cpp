#include "repositorycredentials.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

namespace cilogg::plugins {
void wipeSecret( QByteArray& bytes )
{
    if ( bytes.isEmpty() ) {
        return;
    }
    if ( bytes.isDetached() ) {
        volatile char* data = bytes.data();
        for ( qsizetype index = 0; index < bytes.size(); ++index ) {
            data[ index ] = 0;
        }
    }
    bytes.clear();
}

Credential::Credential( Source source, QByteArray authorization, QByteArray fillOutput )
    : source_( source )
    , authorization_( std::move( authorization ) )
    , fillOutput_( std::move( fillOutput ) )
{
}

Credential::~Credential()
{
    wipeSecret( authorization_ );
    wipeSecret( fillOutput_ );
}

namespace {
using Outcome = GitCredentialHelper::Outcome;
using GitDone = std::function<void( Outcome outcome, QByteArray answer )>;

// One `git -c core.askPass= credential <verb>` run with `input` on standard input.
void runGit( const QString& git, const char* verb, const QByteArray& input, bool interactive,
             QObject* context, GitDone done, int timeoutMs = 0 )
{
    if ( git.isEmpty() ) {
        QTimer::singleShot( 0, context, [ done ] { done( Outcome::GitNotFound, {} ); } );
        return;
    }
    // Never inside a repository: a .git or a repository config in the folder Git starts in could
    // name a credential helper that would then receive the credential. Each run gets a new, empty
    // folder of its own, and GIT_CEILING_DIRECTORIES (its parent) stops the search above it.
    auto folder = std::make_shared<QTemporaryDir>(
        QDir( QDir::tempPath() ).filePath( QStringLiteral( "cilogg-git-XXXXXX" ) ) );
    const auto directory
        = folder->isValid() ? QFileInfo( folder->path() ).canonicalFilePath() : QString{};
    if ( directory.isEmpty() ) {
        QTimer::singleShot( 0, context, [ done ] { done( Outcome::Failed, {} ); } );
        return;
    }
    auto* process = new QProcess( context );
    process->setProgram( git );
    process->setWorkingDirectory( directory );
    process->setArguments( { QStringLiteral( "-c" ), QStringLiteral( "core.askPass=" ),
                             QStringLiteral( "credential" ), QString::fromLatin1( verb ) } );
    process->setProcessEnvironment( GitCredentialHelper::environment( interactive, directory ) );
    // Messages from Git or the credential manager are never logged (design 3.6).
    process->setStandardErrorFile( QProcess::nullDevice() );

    auto answer = std::make_shared<QByteArray>();
    auto overflow = std::make_shared<bool>( false );
    auto timedOut = std::make_shared<bool>( false );
    auto completed = std::make_shared<bool>( false );
    const auto complete = [ process, folder, answer, completed, done ]( Outcome outcome ) {
        if ( *completed ) {
            return;
        }
        *completed = true;
        folder->remove();
        process->deleteLater();
        done( outcome, outcome == Outcome::Ok ? *answer : QByteArray{} );
        wipeSecret( *answer );
    };
    const auto readAnswer = [ process, answer, overflow ] {
        const auto room = GitCredentialHelper::MaxAnswerBytes + 1 - answer->size();
        if ( room > 0 ) {
            auto chunk = process->read( room );
            answer->append( chunk );
            wipeSecret( chunk );
        }
        if ( answer->size() > GitCredentialHelper::MaxAnswerBytes && !*overflow ) {
            *overflow = true;
            process->kill();
        }
    };
    QObject::connect( process, &QProcess::readyReadStandardOutput, process, readAnswer );
    QObject::connect( process, &QProcess::errorOccurred, process,
                      [ complete ]( QProcess::ProcessError error ) {
                          if ( error == QProcess::FailedToStart ) {
                              complete( Outcome::GitNotFound );
                          }
                      } );
    QObject::connect( process, qOverload<int, QProcess::ExitStatus>( &QProcess::finished ), process,
                      [ complete, readAnswer, overflow, timedOut ]( int code,
                                                                    QProcess::ExitStatus status ) {
                          readAnswer();
                          if ( *timedOut ) {
                              complete( Outcome::TimedOut );
                          }
                          else if ( *overflow || status != QProcess::NormalExit || code != 0 ) {
                              complete( Outcome::Failed );
                          }
                          else {
                              complete( Outcome::Ok );
                          }
                      } );
    auto* timer = new QTimer( process );
    timer->setSingleShot( true );
    QObject::connect( timer, &QTimer::timeout, process, [ process, timedOut ] {
        *timedOut = true;
        process->kill();
    } );
    timer->start( timeoutMs > 0     ? timeoutMs
                  : interactive ? GitCredentialHelper::InteractiveTimeoutMs
                                : GitCredentialHelper::SilentTimeoutMs );
    process->start( QIODevice::ReadWrite );
    if ( process->state() != QProcess::NotRunning ) {
        process->write( input );
        process->closeWriteChannel();
    }
}

QByteArray terminated( QByteArray input )
{
    if ( !input.endsWith( '\n' ) ) {
        input.append( '\n' );
    }
    input.append( '\n' );
    return input;
}
} // namespace

QString GitCredentialHelper::resolveGit( const QString& configured )
{
    const auto usable = []( const QString& path ) {
        const QFileInfo info( path );
        const auto suffix = info.suffix().toLower();
        return info.isAbsolute() && info.isFile() && info.isExecutable()
               && suffix != QLatin1String( "bat" ) && suffix != QLatin1String( "cmd" );
    };
    if ( !configured.trimmed().isEmpty() ) {
        // A configured path that cannot be used is reported, not replaced by another Git.
        return usable( configured.trimmed() ) ? QFileInfo( configured.trimmed() ).absoluteFilePath()
                                              : QString{};
    }
    const auto found = QStandardPaths::findExecutable( QStringLiteral( "git" ) );
    return !found.isEmpty() && usable( found ) ? QFileInfo( found ).absoluteFilePath() : QString{};
}

QProcessEnvironment GitCredentialHelper::environment( bool interactive,
                                                     const QString& workingDirectory )
{
    auto environment = QProcessEnvironment::systemEnvironment();
    for ( const auto& key : environment.keys() ) {
        if ( key.endsWith( QLatin1String( "ASKPASS" ), Qt::CaseInsensitive ) ) {
            environment.remove( key );
        }
    }
    // Repository discovery stops at the working directory: no repository config is read.
    environment.insert( QStringLiteral( "GIT_CEILING_DIRECTORIES" ),
                        QDir::toNativeSeparators( QFileInfo( workingDirectory ).absolutePath() ) );
    environment.remove( QStringLiteral( "GIT_DIR" ) );
    environment.remove( QStringLiteral( "GIT_WORK_TREE" ) );
    environment.insert( QStringLiteral( "GIT_TERMINAL_PROMPT" ), QStringLiteral( "0" ) );
    if ( !interactive ) {
        environment.insert( QStringLiteral( "GCM_INTERACTIVE" ), QStringLiteral( "never" ) );
    }
    return environment;
}

QByteArray GitCredentialHelper::fillRequest( const AzureDevOpsLocation& location )
{
    return "protocol=https\nhost=" + AzureDevOpsHost.toUtf8()
           + "\npath=" + location.credentialPath().toUtf8() + "\ncapability[]=authtype\n\n";
}

QString GitCredentialHelper::outcomeText( Outcome outcome )
{
    switch ( outcome ) {
    case Outcome::Ok:
        return {};
    case Outcome::GitNotFound:
        return QStringLiteral( "Git is not installed or the configured Git path cannot be used." );
    case Outcome::Failed:
        return QStringLiteral( "The Git credential manager could not sign in." );
    case Outcome::TimedOut:
        return QStringLiteral( "The Git credential manager did not answer in time." );
    case Outcome::NoCredential:
        return QStringLiteral( "The Git credential manager returned no credential." );
    }
    return {};
}

void GitCredentialHelper::fill( const QString& git, const AzureDevOpsLocation& location,
                                bool interactive, QObject* context, FillDone done, int timeoutMs )
{
    runGit(
        git, "fill", fillRequest( location ), interactive, context,
            [ done ]( Outcome outcome, QByteArray answer ) {
                if ( outcome != Outcome::Ok ) {
                    done( nullptr, outcome );
                    return;
                }
                auto authorization = authorizationFromCredential( parseCredentialFields( answer ) );
                if ( authorization.isEmpty() ) {
                    wipeSecret( answer );
                    done( nullptr, Outcome::NoCredential );
                    return;
                }
                auto credential = std::make_shared<const Credential>(
                    Credential::Source::GitCredentialManager, std::move( authorization ),
                    std::move( answer ) );
                done( credential, Outcome::Ok );
            },
        timeoutMs );
}

void GitCredentialHelper::approve( const QString& git, const Credential& credential,
                                   QObject* context, Done done )
{
    runGit( git, "approve", terminated( credential.fillOutput() ), false, context,
            [ done ]( Outcome outcome, const QByteArray& ) {
                if ( done ) {
                    done( outcome );
                }
            } );
}

void GitCredentialHelper::reject( const QString& git, const Credential& credential,
                                  QObject* context, Done done )
{
    runGit( git, "reject", terminated( credential.fillOutput() ), false, context,
            [ done ]( Outcome outcome, const QByteArray& ) {
                if ( done ) {
                    done( outcome );
                }
            } );
}

QString PatStore::storeName( const QString& repository )
{
    return QStringLiteral( "CILogg plugin repository " ) + repository;
}

MemoryPatStore::~MemoryPatStore()
{
    for ( auto& token : tokens_ ) {
        wipeSecret( token );
    }
}

void MemoryPatStore::read( const QString& repository, ReadDone done )
{
    const auto found = tokens_.constFind( repository );
    done( found == tokens_.constEnd() ? std::nullopt : std::optional<QByteArray>( *found ),
          Status{ {}, true } );
}

void MemoryPatStore::write( const QString& repository, const QByteArray& token, Done done )
{
    auto& stored = tokens_[ repository ];
    wipeSecret( stored );
    stored = token;
    done( Status{ {}, true } );
}

void MemoryPatStore::remove( const QString& repository, Done done )
{
    const auto found = tokens_.find( repository );
    if ( found != tokens_.end() ) {
        wipeSecret( *found );
        tokens_.erase( found );
    }
    done( Status{ {}, true } );
}

bool MemoryPatStore::contains( const QString& repository ) const
{
    return tokens_.contains( repository );
}

#ifndef CILOGG_HAVE_QTKEYCHAIN
std::unique_ptr<PatStore> createSystemPatStore()
{
    return std::make_unique<MemoryPatStore>();
}
#endif
} // namespace cilogg::plugins
