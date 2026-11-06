// SPDX-License-Identifier: GPL-3.0-or-later
#include "tsharkpreviewclient.h"
#include "log.h"
#include "previewdecodersettings.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <cmath>
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
    QStringList names;
#ifdef Q_OS_WIN
    // Native interpreters avoid batch-launcher overhead in the bounded worker startup.
    names << "python.exe" << "python3.exe";
#endif
    names << "python3" << "python";
    for ( const auto& name : names ) {
        const auto candidate = QStandardPaths::findExecutable( name );
        const QFileInfo info( candidate );
        const bool absolute = info.isAbsolute();
        const bool file = info.isFile();
        const bool executable = info.isExecutable();
        if ( absolute && file && executable ) {
            const auto suffix = info.suffix();
            const bool batchWrapper = suffix.compare( "bat", Qt::CaseInsensitive ) == 0
                                      || suffix.compare( "cmd", Qt::CaseInsensitive ) == 0;
            LOG_DEBUG << "Preview Python runtime selected source=path name=" << name
                      << " batch_wrapper=" << batchWrapper;
            return info.absoluteFilePath();
        }
        LOG_DEBUG << "Preview Python PATH candidate rejected name=" << name
                  << " found=" << !candidate.isEmpty() << " absolute=" << absolute
                  << " file=" << file << " executable=" << executable;
    }
    LOG_WARNING << "Preview Python PATH discovery found no executable runtime";
    return {};
}
} // namespace cilogg::preview::detail

namespace {
constexpr int ResponseLimit = 128 * 1024;
const QString Origin = QStringLiteral( "engine_data_source_unresolved" );
QVariantMap nativeFailure( const QString& reason )
{
    return { { "version", 1 },
             { "backend", "tshark_lua" },
             { "state", "failed" },
             { "reason", reason },
             { "available", false },
             { "complete", false },
             { "limited", false },
             { "offsetSpace", Origin },
             { "messageCompleteness", "not_reported" },
             { "messageIntegrity", "not_reported" },
             { "fields", QVariantList{} } };
}
QString pythonExecutable()
{
    const auto app = QCoreApplication::applicationDirPath();
    QStringList candidates;
    // An explicit native-host environment override matches PluginManager's runtime selection.
    candidates << qEnvironmentVariable( "CILOGG_SCRIPT_PYTHON" );
    for ( const auto* relative : { "python_runtime/python.exe", "python_runtime/python/python.exe",
                                   "python_runtime/bin/python3", "python_runtime/bin/python",
                                   "python_runtime/python/bin/python3", "python/python.exe",
                                   "bin/python3", "bin/python", "python/bin/python3" } )
        candidates << QDir( app ).filePath( relative );
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
    return cilogg::preview::detail::findPythonOnPath();
}
bool boundedString( const QJsonValue& value, int maximum = 65536 )
{
    return value.isString() && value.toString().size() <= maximum;
}
bool integer( const QJsonValue& value, double minimum, double maximum )
{
    const auto number = value.toDouble( -1 );
    return value.isDouble() && std::isfinite( number ) && std::floor( number ) == number
           && number >= minimum && number <= maximum;
}
bool validResponse( const QJsonObject& value, const QString& id )
{
    if ( !integer( value.value( "version" ), 1, 1 ) || value.value( "id" ) != id
         || value.value( "backend" ) != "tshark_lua"
         || ( value.value( "state" ) != "completed" && value.value( "state" ) != "failed" )
         || !boundedString( value.value( "reason" ), 128 )
         || !boundedString( value.value( "previewName" ), 128 )
         || value.value( "offsetSpace" ) != Origin
         || value.value( "messageCompleteness" ) != "not_reported"
         || value.value( "messageIntegrity" ) != "not_reported" )
        return false;
    for ( const auto* key : { "available", "complete", "limited" } )
        if ( !value.value( key ).isBool() )
            return false;
    const bool completed = value.value( "state" ) == "completed";
    if ( value.value( "available" ).toBool() != completed
         || ( !completed && value.value( "complete" ).toBool() )
         || ( value.value( "limited" ).toBool() && value.value( "complete" ).toBool() )
         || ( completed && value.value( "complete" ).toBool() == value.value( "limited" ).toBool() )
         || ( !completed && value.value( "reason" ).toString().isEmpty() )
         || ( completed
              && ( !value.contains( "enginePid" ) || !value.contains( "scriptSha256" ) ) ) )
        return false;
    const QStringList allowed{ "version",
                               "id",
                               "backend",
                               "state",
                               "reason",
                               "available",
                               "complete",
                               "limited",
                               "previewName",
                               "offsetSpace",
                               "messageCompleteness",
                               "messageIntegrity",
                               "fields",
                               "enginePid",
                               "scriptSha256" };
    for ( auto item = value.begin(); item != value.end(); ++item )
        if ( !allowed.contains( item.key() ) )
            return false;
    if ( value.contains( "enginePid" )
         && !integer( value.value( "enginePid" ), 1, 9007199254740991.0 ) )
        return false; // Metadata only; never used to find/terminate a process.
    if ( value.contains( "scriptSha256" ) ) {
        if ( !value.value( "scriptSha256" ).isArray()
             || value.value( "scriptSha256" ).toArray().size() > 8 )
            return false;
        static const QRegularExpression hash( "\\A[0-9a-f]{64}\\z" );
        for ( const auto& digest : value.value( "scriptSha256" ).toArray() )
            if ( !digest.isString() || !hash.match( digest.toString() ).hasMatch() )
                return false;
    }
    if ( !value.value( "fields" ).isArray() )
        return false;
    const auto fields = value.value( "fields" ).toArray();
    if ( fields.size() > 512 || ( !completed && !fields.isEmpty() ) )
        return false;
    int previousDepth = -1;
    for ( const auto& field : fields ) {
        if ( !field.isObject() )
            return false;
        const auto entry = field.toObject();
        if ( entry.size() != 5 || !integer( entry.value( "depth" ), 0, 32 )
             || !boundedString( entry.value( "id" ), 512 )
             || entry.value( "positionOrigin" ) != Origin || !entry.value( "attributes" ).isObject()
             || !entry.value( "columns" ).isArray()
             || entry.value( "columns" ).toArray().size() != 5 )
            return false;
        const int depth = entry.value( "depth" ).toInt();
        if ( depth > previousDepth + 1 )
            return false;
        previousDepth = depth;
        for ( const auto& column : entry.value( "columns" ).toArray() )
            if ( !boundedString( column ) )
                return false;
        const auto attributes = entry.value( "attributes" ).toObject();
        if ( attributes.size() > 64 )
            return false;
        for ( auto attr = attributes.begin(); attr != attributes.end(); ++attr )
            if ( attr.key().size() > 128 || !boundedString( attr.value() ) )
                return false;
        const auto columns = entry.value( "columns" ).toArray();
        const QList<QJsonValue> expected{
            attributes.value( "name" ).isUndefined() ? QJsonValue( "" )
                                                     : attributes.value( "name" ),
            attributes.contains( "show" )                  ? attributes.value( "show" )
            : attributes.value( "showname" ).isUndefined() ? QJsonValue( "" )
                                                           : attributes.value( "showname" ),
            attributes.value( "value" ).isUndefined() ? QJsonValue( "" )
                                                      : attributes.value( "value" ),
            attributes.value( "pos" ).isUndefined() ? QJsonValue( "" ) : attributes.value( "pos" ),
            attributes.value( "size" ).isUndefined() ? QJsonValue( "" ) : attributes.value( "size" )
        };
        for ( int i = 0; i < 5; ++i )
            if ( columns.at( i ) != expected.at( i ) )
                return false;
    }
    return true;
}

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

class TsharkPreviewClient::Session : public QObject {
public:
    Session( TsharkPreviewClient* pool, QObject* channel )
        : QObject( pool )
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
            LOG_DEBUG << "Preview worker owned pid=" << process->processId();
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
        const bool overflow = process->bytesAvailable() > 65536 - diagnostics;
        diagnostics += process->read( qMax<qint64>( 0, 65537 - diagnostics ) ).size();
        process->setReadChannel( QProcess::StandardOutput );
        if ( overflow || diagnostics > 65536 )
            retire( "diagnostic_limit" );
    }
    bool submit( const QVariantMap& profile, const QByteArray& payload, int timeoutMs,
                 const QString& identity, Completion callback )
    {
        id = identity;
        completion = std::move( callback );
        diagnostics = 0;
        timeout.start( timeoutMs );
        const auto body
            = QJsonDocument( QJsonObject::fromVariantMap(
                                 { { "version", 1 },
                                   { "id", id },
                                   { "op", "evaluate" },
                                   { "profile", profile },
                                   { "payloadBase64", QString::fromLatin1( payload.toBase64() ) },
                                   { "timeoutMs", timeoutMs } } ) )
                  .toJson( QJsonDocument::Compact );
        if ( body.size() > 256 * 1024 ) {
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
        for ( const auto* name : { "message_decoder_preview_worker.py", "message_decoder_wire.py",
                                   "workbench_process.py" } ) {
            if ( !QFile::copy( QString( ":/preview_decoder/" ) + name,
                               workspace.filePath( name ) ) ) {
                retire( "resource_unavailable" );
                return false;
            }
        }
        const auto python = pythonExecutable();
        if ( python.isEmpty() ) {
            retire( "python_unavailable" );
            return false;
        }
        LOG_DEBUG << "Preview worker starting request=" << id << " request_bytes=" << body.size();
        process->start( python, { "-I", "-S", "-u",
                                  workspace.filePath( "message_decoder_preview_worker.py" ),
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
             || !validResponse( document.object(), id ) ) {
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
                  << " diagnostic_bytes=" << diagnostics;
        if ( response.value( "state" ) != "completed" ) {
            auto done = std::move( completion );
            completion = {};
            retire( "worker_failure" );
            done( std::move( response ) );
        }
        else {
            auto done = std::move( completion );
            completion = {};
            done( std::move( response ) );
        }
    }
    void retire( const QString& reason )
    {
        if ( retired )
            return;
        retired = true;
        timeout.stop();
        process->terminateOwned();
        LOG_DEBUG << "Preview worker retiring reason=" << reason << " request=" << id;
        auto done = std::move( completion );
        completion = {};
        if ( done )
            done( nativeFailure( reason ) );
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

TsharkPreviewClient& TsharkPreviewClient::instance()
{
    static auto* client = new TsharkPreviewClient( QCoreApplication::instance() );
    return *client;
}
TsharkPreviewClient::TsharkPreviewClient( QObject* parent )
    : QObject( parent )
{
    initializePreviewDecoderResources();
    connect( &PreviewDecoderSettings::instance(), &PreviewDecoderSettings::changed, this,
             &TsharkPreviewClient::reset );
    connect( QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
             &TsharkPreviewClient::reset );
}
TsharkPreviewClient::~TsharkPreviewClient()
{
    const auto sessions = sessions_;
    for ( auto* session : sessions ) {
        QObject::disconnect( session, nullptr, this, nullptr );
        delete session;
    }
    sessions_.clear();
}
void TsharkPreviewClient::evaluate( QObject* owner, const QVariantMap& profile,
                                    const QByteArray& payload, int timeoutMs, const QString& id,
                                    Completion done )
{
    if ( !owner )
        owner = QCoreApplication::instance();
    if ( !owner || timeoutMs < 1 || timeoutMs > 10000 || payload.isEmpty()
         || payload.size() > 65536 ) {
        done( nativeFailure( !owner ? "application_unavailable"
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
        done( nativeFailure( "preview_busy" ) );
        return;
    }
    if ( !selected && warm.size() >= 2 ) {
        std::sort( warm.begin(), warm.end(),
                   []( Session* a, Session* b ) { return a->used < b->used; } );
        const auto idle
            = std::find_if( warm.begin(), warm.end(), []( Session* s ) { return !s->completion; } );
        if ( idle == warm.end() ) {
            done( nativeFailure( "preview_busy" ) );
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
    selected->submit( profile, payload, timeoutMs, id, std::move( done ) );
}
void TsharkPreviewClient::cancel( const QString& id, const QString& reason )
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
void TsharkPreviewClient::reset()
{
    const auto sessions = sessions_;
    for ( auto* session : sessions )
        session->retire( "configuration_changed" );
}
