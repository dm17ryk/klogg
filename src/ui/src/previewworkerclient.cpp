// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewworkerclient.h"
#include "log.h"
#include "previewdecodersettings.h"
#include "pythonruntime.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

// Q_INIT_RESOURCE must resolve the generated global symbol for static-library linkage.
static void initializePreviewDecoderResources()
{
    Q_INIT_RESOURCE( preview_decoder );
}

namespace cilogg::preview::detail {
QString findPythonOnPath()
{
    // The python.exe rules of the plugins (#141): native interpreters before launcher scripts
    // such as a pyenv-win shim, Microsoft Store aliases last.
    for ( const auto& candidate : cilogg::pythonruntime::pathInterpreterCandidates() ) {
        const QFileInfo info( candidate );
        if ( info.isAbsolute() && info.isFile() && info.isExecutable() ) {
            LOG_DEBUG << "Preview Python runtime selected source=path launcher_script="
                      << cilogg::pythonruntime::isLauncherScript( candidate );
            return info.absoluteFilePath();
        }
    }
    LOG_WARNING << "Preview Python PATH discovery found no executable runtime";
    return {};
}

QString previewPython()
{
    QStringList candidates;
    // An explicit native-host environment override matches PluginManager's runtime selection.
    candidates << qEnvironmentVariable( "CILOGG_SCRIPT_PYTHON" );
    candidates << cilogg::pythonruntime::bundledInterpreterCandidates(
        QCoreApplication::applicationDirPath() );
    int candidateIndex = 0;
    for ( const auto& candidate : candidates ) {
        const QFileInfo info( candidate );
        const char* origin = candidateIndex == 0 ? "override" : "bundled";
        const bool absolute = info.isAbsolute();
        const bool file = info.isFile();
        const bool executable = info.isExecutable();
        if ( absolute && file && executable ) {
            LOG_DEBUG << "Preview Python runtime selected source=" << origin
                      << " candidate=" << candidateIndex;
            return info.absoluteFilePath();
        }
        LOG_DEBUG << "Preview Python candidate rejected source=" << origin
                  << " candidate=" << candidateIndex << " empty=" << candidate.isEmpty()
                  << " absolute=" << absolute << " file=" << file << " executable=" << executable;
        ++candidateIndex;
    }
    LOG_DEBUG << "Preview Python explicit/bundled candidates unavailable; resolving PATH";
    return findPythonOnPath();
}
} // namespace cilogg::preview::detail

namespace {
constexpr int RequestLimit = 256 * 1024;
constexpr int ResponseLimit = 128 * 1024;
constexpr int DiagnosticLimit = 64 * 1024;

class OwnedPreviewProcess : public QProcess {
public:
    explicit OwnedPreviewProcess( QObject* parent )
        : QProcess( parent )
    {
#if !defined( Q_OS_WIN ) && QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )
        setChildProcessModifier( [] {
            if ( ::setsid() == -1 )
                ::_exit( 127 );
        } );
#endif
    }
    ~OwnedPreviewProcess() override
    {
        terminateOwned();
        if ( state() != NotRunning )
            waitForFinished( 1000 ); // Bounded shutdown fallback only.
#ifdef Q_OS_WIN
        if ( job_ )
            CloseHandle( job_ );
#endif
    }
    bool establishOwnership()
    {
#ifdef Q_OS_WIN
        job_ = CreateJobObjectW( nullptr, nullptr );
        if ( !job_ )
            return false;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        const auto child = OpenProcess( PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                        static_cast<DWORD>( processId() ) );
        const bool ok = child
                        && SetInformationJobObject( job_, JobObjectExtendedLimitInformation,
                                                    &limits, sizeof( limits ) )
                        && AssignProcessToJobObject( job_, child );
        if ( child )
            CloseHandle( child );
        return ok;
#else
        const auto pid = static_cast<pid_t>( processId() );
        if ( pid <= 0 || ::getpgid( pid ) != pid )
            return false;
        group_ = pid;
        return true;
#endif
    }
    void terminateOwned()
    {
#ifdef Q_OS_WIN
        if ( job_ )
            TerminateJobObject( job_, 1 );
#else
        if ( group_ > 0 ) {
            ::kill( -group_, SIGKILL );
            group_ = 0;
        }
#endif
        if ( state() != NotRunning )
            kill();
    }
    bool descendantsStopped() const
    {
#ifdef Q_OS_WIN
        if ( !job_ )
            return true;
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
        return QueryInformationJobObject( job_, JobObjectBasicAccountingInformation, &info,
                                          sizeof( info ), nullptr )
               && info.ActiveProcesses == 0;
#else
        return true; // SIGKILL was sent to the verified owned group before parent reaping.
#endif
    }
#if !defined( Q_OS_WIN ) && QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 )
protected:
    void setupChildProcess() override
    {
        if ( ::setsid() == -1 )
            ::_exit( 127 );
    }
#endif
private:
#ifdef Q_OS_WIN
    HANDLE job_ = nullptr;
#else
    pid_t group_ = 0;
#endif
};
} // namespace

class PreviewWorkerClient::Session : public QObject {
public:
    Session( PreviewWorkerClient* pool, QObject* channel )
        : QObject( pool )
        , pool_( pool )
        , owner( channel )
        , process( new OwnedPreviewProcess( this ) )
    {
        connect( channel, &QObject::destroyed, this, [ this ] { retire( "cancelled" ); } );
        timeout.setSingleShot( true );
        connect( &timeout, &QTimer::timeout, this, [ this ] { retire( "timeout" ); } );
        connect( process, &QProcess::started, this, [ this ] {
            if ( retired ) {
                // Cancellation can race an asynchronous start; never leave the late child idle.
                process->terminateOwned();
                return;
            }
            if ( !process->establishOwnership() ) {
                LOG_WARNING << "Preview worker ownership rejected";
                retire( "process_ownership_failed" );
                return;
            }
            owned = true;
            LOG_DEBUG << "Preview worker owned backend=" << pool_->backend_.name
                      << " pid=" << process->processId();
            send();
        } );
        connect( process, &QProcess::readyReadStandardOutput, this, [ this ] { read(); } );
        connect( process, &QProcess::readyReadStandardError, this,
                 [ this ] { drainDiagnostics(); } );
        connect( process, &QProcess::errorOccurred, this, [ this ]( QProcess::ProcessError error ) {
            if ( error == QProcess::FailedToStart )
                retire( "worker_start_failed" );
        } );
        connect( process, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ), this,
                 [ this ]( int, QProcess::ExitStatus ) {
                     if ( !retired ) {
                         read();
                         retire( "worker_failure" );
                     }
                     cleanup();
                 } );
    }
    ~Session() override
    {
        // Destroy process/tree ownership before removing materialized resources.
        retired = true;
        completion = {};
        QObject::disconnect( process, nullptr, this, nullptr );
        delete process;
    }
    void drainDiagnostics()
    {
        if ( retired )
            return;
        process->setReadChannel( QProcess::StandardError );
        const bool overflow = process->bytesAvailable() > DiagnosticLimit - diagnostics;
        diagnostics
            += process->read( qMax<qint64>( 0, DiagnosticLimit + 1 - diagnostics ) ).size();
        process->setReadChannel( QProcess::StandardOutput );
        if ( overflow || diagnostics > DiagnosticLimit )
            retire( "diagnostic_limit" );
    }
    bool submit( const QVariantMap& fields, const QByteArray& payload, int timeoutMs,
                 const QString& identity, Completion callback )
    {
        id = identity;
        completion = std::move( callback );
        diagnostics = 0;
        timeout.start( timeoutMs );
        auto request = fields;
        request.insert( QVariantMap{ { "version", 1 },
                                     { "id", id },
                                     { "op", pool_->backend_.operation },
                                     { "payloadBase64", QString::fromLatin1( payload.toBase64() ) },
                                     { "timeoutMs", timeoutMs } } );
        const auto body
            = QJsonDocument( QJsonObject::fromVariantMap( request ) ).toJson( QJsonDocument::Compact );
        if ( body.size() > RequestLimit ) {
            retire( "request_limit" );
            return false;
        }
        char header[ 4 ];
        qToBigEndian<quint32>( static_cast<quint32>( body.size() ), header );
        pending = QByteArray( header, 4 ) + body;
        if ( process->state() == QProcess::Running ) {
            send();
            return true;
        }
        if ( !workspace.isValid() ) {
            retire( "workspace_unavailable" );
            return false;
        }
        for ( const auto& name : pool_->backend_.resources ) {
            if ( !QFile::copy( QStringLiteral( ":/preview_decoder/" ) + name,
                               workspace.filePath( name ) ) ) {
                retire( "resource_unavailable" );
                return false;
            }
        }
        const auto python = cilogg::preview::detail::previewPython();
        if ( python.isEmpty() ) {
            retire( "python_unavailable" );
            return false;
        }
        LOG_DEBUG << "Preview worker starting backend=" << pool_->backend_.name
                  << " request=" << id << " request_bytes=" << body.size();
        process->start( python, { "-I", "-S", "-u", workspace.filePath( pool_->backend_.script ),
                                  "--workspace", workspace.path() } );
        return true;
    }
    void send()
    {
        if ( retired || !owned || pending.isEmpty() )
            return;
        if ( process->bytesAvailable() || !output.isEmpty() ) {
            retire( "worker_protocol_error" );
            return;
        }
        if ( process->write( pending ) != pending.size() ) {
            retire( "worker_io_error" );
            return;
        }
        LOG_DEBUG << "Preview worker request sent id=" << id << " bytes=" << pending.size();
        pending.clear();
    }
    void read()
    {
        if ( retired ) {
            return;
        }
        if ( !completion ) {
            retire( "worker_protocol_error" );
            return;
        }
        if ( process->bytesAvailable() > ResponseLimit + 4 - output.size() ) {
            retire( "result_limit" );
            return;
        }
        output += process->read( ResponseLimit + 5 - output.size() );
        if ( output.size() < 4 )
            return;
        const auto size = qFromBigEndian<quint32>( output.constData() );
        if ( !size || size > ResponseLimit ) {
            retire( "result_limit" );
            return;
        }
        if ( output.size() < static_cast<qint64>( size ) + 4 )
            return;
        if ( output.size() != static_cast<qint64>( size ) + 4 ) {
            retire( "worker_protocol_error" );
            return;
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson( output.mid( 4 ), &error );
        if ( error.error != QJsonParseError::NoError || !document.isObject()
             || !pool_->backend_.validResponse( document.object(), id ) ) {
            retire( "worker_protocol_error" );
            return;
        }
        drainDiagnostics();
        if ( retired )
            return;
        auto response = document.object().toVariantMap();
        output.clear();
        timeout.stop();
        LOG_DEBUG << "Preview worker response id=" << id
                  << " fields=" << response.value( "fields" ).toList().size()
                  << " state=" << response.value( "state" ).toString()
                  << " reason=" << response.value( "reason" ).toString()
                  << " diagnostic_bytes=" << diagnostics;
        auto done = std::move( completion );
        completion = {};
        if ( response.value( "state" ) != "completed"
             && !pool_->backend_.survivedFailures.contains( response.value( "reason" ).toString() ) )
            retire( "worker_failure" );
        done( std::move( response ) );
    }
    void retire( const QString& reason )
    {
        if ( retired )
            return;
        retired = true;
        timeout.stop();
        process->terminateOwned();
        LOG_DEBUG << "Preview worker retiring backend=" << pool_->backend_.name
                  << " reason=" << reason << " request=" << id;
        auto done = std::move( completion );
        completion = {};
        if ( done )
            done( pool_->failure( reason ) );
        if ( process->state() == QProcess::NotRunning )
            cleanup();
    }
    void cleanup()
    {
        if ( process->state() != QProcess::NotRunning )
            return;
        if ( !process->descendantsStopped() ) {
            QTimer::singleShot( 20, this, [ this ] { cleanup(); } );
            return;
        }
        deleteLater();
    }
    PreviewWorkerClient* pool_;
    QPointer<QObject> owner;
    OwnedPreviewProcess* process;
    QTemporaryDir workspace;
    QTimer timeout;
    Completion completion;
    QByteArray pending, output;
    QString id;
    quint64 used = 0;
    qint64 diagnostics = 0;
    bool retired = false;
    bool owned = false;
};

PreviewWorkerClient::PreviewWorkerClient( QObject* parent, Backend backend )
    : QObject( parent )
    , backend_( std::move( backend ) )
{
    initializePreviewDecoderResources();
    connect( &PreviewDecoderSettings::instance(), &PreviewDecoderSettings::changed, this,
             &PreviewWorkerClient::reset );
    connect( QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
             &PreviewWorkerClient::reset );
}
PreviewWorkerClient::~PreviewWorkerClient()
{
    const auto sessions = sessions_;
    for ( auto* session : sessions ) {
        QObject::disconnect( session, nullptr, this, nullptr );
        delete session;
    }
    sessions_.clear();
}
QVariantMap PreviewWorkerClient::failure( const QString& reason ) const
{
    return { { "version", 1 },
             { "backend", backend_.name },
             { "state", "failed" },
             { "reason", reason },
             { "available", false },
             { "complete", false },
             { "limited", false },
             { "offsetSpace", backend_.offsetSpace },
             { "messageCompleteness", "not_reported" },
             { "messageIntegrity", "not_reported" },
             { "fields", QVariantList{} } };
}
void PreviewWorkerClient::submit( QObject* owner, const QVariantMap& fields,
                                  const QByteArray& payload, int timeoutMs, const QString& id,
                                  Completion done )
{
    if ( !owner )
        owner = QCoreApplication::instance();
    if ( !owner || timeoutMs < 1 || timeoutMs > 10000 || payload.isEmpty()
         || payload.size() > 65536 ) {
        done( failure( !owner ? "application_unavailable"
                       : payload.isEmpty() || payload.size() > 65536 ? "input_limit"
                                                                     : "invalid_timeout" ) );
        return;
    }
    Session* selected = nullptr;
    QList<Session*> warm;
    for ( auto* session : sessions_ ) {
        if ( session->retired )
            continue;
        warm << session;
        if ( session->owner == owner )
            selected = session;
    }
    if ( selected && selected->completion ) {
        done( failure( "preview_busy" ) );
        return;
    }
    if ( !selected && warm.size() >= 2 ) {
        std::sort( warm.begin(), warm.end(),
                   []( Session* a, Session* b ) { return a->used < b->used; } );
        const auto idle
            = std::find_if( warm.begin(), warm.end(), []( Session* s ) { return !s->completion; } );
        if ( idle == warm.end() ) {
            done( failure( "preview_busy" ) );
            return;
        }
        ( *idle )->retire( "idle_evicted" );
    }
    if ( !selected ) {
        selected = new Session( this, owner );
        sessions_ << selected;
        connect( selected, &QObject::destroyed, this,
                 [ this, selected ] { sessions_.removeAll( selected ); } );
    }
    selected->used = ++clock_;
    selected->submit( fields, payload, timeoutMs, id, std::move( done ) );
}
void PreviewWorkerClient::cancel( const QString& id, const QString& reason )
{
    const auto sessions = sessions_;
    for ( auto* session : sessions )
        if ( !session->retired && session->id == id ) {
            // Delivery may reject an overdue success after read() moved the callback.
            // The last identity still owns this channel until a new submit replaces it.
            LOG_DEBUG << "Preview worker cancellation id=" << id << " reason=" << reason
                      << " pending=" << static_cast<bool>( session->completion );
            session->retire( reason );
        }
}
void PreviewWorkerClient::reset()
{
    const auto sessions = sessions_;
    for ( auto* session : sessions )
        session->retire( "configuration_changed" );
}
