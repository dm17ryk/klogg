// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysisregex.h"
#include "log.h"
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QtEndian>
#include <set>
#include <utility>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <psapi.h>
#endif

namespace {
constexpr qint64 FrameLimit = 8 * 1024 * 1024;
class RegexProcess {
public:
    RegexProcess( QVariantMap options, QString executable, QStringList arguments )
        : options_( std::move( options ) )
        , executable_( std::move( executable ) )
        , arguments_( std::move( arguments ) )
    {
        QObject::connect( &process_, &QProcess::readyReadStandardError, &process_, [ this ] {
            process_.setReadChannel( QProcess::StandardError );
            while ( process_.bytesAvailable() > 0 ) {
                const auto available = process_.bytesAvailable();
                if ( stderrBytes_ + available > 65536 ) {
                    protocolFailure_ = true;
                    process_.kill();
                    break;
                }
                stderrBytes_ += process_.read( qMin<qint64>( available, 16384 ) ).size();
            }
            process_.setReadChannel( QProcess::StandardOutput );
        } );
        QObject::connect( &process_, &QProcess::readyReadStandardOutput, &process_, [ this ] {
            if ( process_.bytesAvailable() > FrameLimit + 4 ) {
                protocolFailure_ = true;
                process_.kill();
            }
        } );
    }
    ~RegexProcess()
    {
        stop();
#ifdef Q_OS_WIN
        if ( memoryProcess_ ) {
            CloseHandle( memoryProcess_ );
        }
        if ( job_ ) {
            CloseHandle( job_ );
        }
#endif
    }
    AnalysisOutput process( const AnalysisBatch& batch, const std::atomic_bool& cancelled,
                            int timeoutMs )
    {
        QDeadlineTimer deadline( timeoutMs );
        AnalysisOutput output;
        const bool firstCall = !started_;
        bool awaitingReady = firstCall;
        QElapsedTimer startup;
        startup.start();
        if ( !started_ ) {
            deadline = QDeadlineTimer( qMin( batch.jobRemainingMs, 30000 ) );
            process_.setProcessChannelMode( QProcess::SeparateChannels );
            process_.start( executable_, arguments_ );
            while ( process_.state() == QProcess::Starting && !cancelled
                    && !deadline.hasExpired() ) {
                process_.waitForStarted( 10 );
            }
            output.metrics[ "startupUs" ] = startup.nsecsElapsed() / 1000;
            if ( cancelled || deadline.hasExpired() ) {
                output.error = cancelled ? "cancelled" : "timeout";
                stop();
                return finalizeOutput( std::move( output ) );
            }
            if ( process_.state() != QProcess::Running || !limitMemory() ) {
                output.error = "worker_failure";
                stop();
                return finalizeOutput( std::move( output ) );
            }
            started_ = true;
            LOG_DEBUG << "Analysis regex worker started pid=" << process_.processId();
        }
        const auto requestId = ++requestId_;
        QVariantMap message{ { "version", 1 }, { "requestId", requestId } };
        if ( !compiled_ ) {
            message[ "operation" ] = "compile";
            message[ "options" ] = options_;
        }
        else {
            QVariantList lines;
            for ( const auto& line : batch.lines ) {
                lines.push_back(
                    QVariantMap{ { "lineNumber", QVariant::fromValue( line.line + 1 ) },
                                 { "text", line.text } } );
            }
            message[ "operation" ] = "match";
            message[ "lines" ] = lines;
        }
        const auto bytes = QJsonDocument( QJsonObject::fromVariantMap( message ) )
                               .toJson( QJsonDocument::Compact );
        if ( bytes.size() > FrameLimit ) {
            output.error = "worker_resource_limit";
            stop();
            return finalizeOutput( std::move( output ) );
        }
        char header[ 4 ];
        qToBigEndian<quint32>( static_cast<quint32>( bytes.size() ), header );
        const auto sendRequest = [ & ] {
            if ( process_.bytesAvailable() != 0 ) {
                output.error = "invalid_worker_output";
                return false;
            }
            if ( process_.write( header, 4 ) != 4 || process_.write( bytes ) != bytes.size() ) {
                output.error = "io_error";
                return false;
            }
            return true;
        };
        if ( !awaitingReady && !sendRequest() ) {
            stop();
            return finalizeOutput( std::move( output ) );
        }
        QByteArray response;
        qint64 expected = -1;
        while ( !cancelled && !deadline.hasExpired() ) {
            if ( process_.bytesToWrite() ) {
                process_.waitForBytesWritten( 10 );
            }
            if ( process_.bytesAvailable() == 0 ) {
                process_.waitForReadyRead( 10 );
            }
            if ( protocolFailure_
                 || response.size() + process_.bytesAvailable() > FrameLimit + 4 ) {
                output.error = "invalid_worker_output";
                break;
            }
            const auto wanted = ( expected < 0 ? 4 : expected + 4 ) - response.size();
            response += process_.read( wanted );
            if ( response.size() >= 4 && expected < 0 ) {
                expected = qFromBigEndian<quint32>( response.constData() );
                if ( expected == 0 || expected > FrameLimit ) {
                    output.error = "invalid_worker_output";
                    break;
                }
            }
            if ( expected >= 0 && response.size() >= expected + 4 ) {
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson( response.mid( 4 ), &error );
                if ( response.size() != expected + 4 || error.error != QJsonParseError::NoError
                     || !document.isObject() ) {
                    output.error = "invalid_worker_output";
                    break;
                }
                const auto result = document.object().toVariantMap();
                const auto object = document.object();
                if ( awaitingReady ) {
                    if ( !object.value( "version" ).isDouble()
                         || object.value( "version" ).toInt() != 1
                         || object.value( "operation" ).toString() != "ready"
                         || object.value( "backend" ).toString() != "Qt QRegularExpression" ) {
                        output.error = "invalid_worker_output";
                        break;
                    }
                    output.metrics[ "startupUs" ] = startup.nsecsElapsed() / 1000;
                    deadline = QDeadlineTimer( qMin(
                        timeoutMs,
                        qMax( 0, batch.jobRemainingMs - static_cast<int>( startup.elapsed() ) ) ) );
                    awaitingReady = false;
                    response.clear();
                    expected = -1;
                    if ( !sendRequest() ) {
                        break;
                    }
                    continue;
                }
                const auto metricsObject = object.value( "metrics" ).toObject();
                const auto duration = metricsObject.value( compiled_ ? "matchUs" : "compileUs" );
                const bool hasError = object.value( "reason" ).isString()
                                      && !object.value( "reason" ).toString().isEmpty();
                const std::set<QString> reasons{ "",
                                                 "invalid_pattern",
                                                 "unsupported_options",
                                                 "pattern_limit",
                                                 "result_limit",
                                                 "match_resource_limit",
                                                 "worker_resource_limit",
                                                 "worker_failure" };
                bool valid
                    = object.value( "version" ).isDouble() && object.value( "version" ).toInt() == 1
                      && object.value( "backend" ).toString() == "Qt QRegularExpression"
                      && object.value( "requestId" ).isDouble()
                      && object.value( "requestId" ).toDouble() == requestId
                      && object.value( "metrics" ).isObject() && metricsObject.size() == 1
                      && duration.isDouble() && duration.toDouble() >= 0
                      && reasons.count( object.value( "reason" ).toString() ) != 0
                      && ( !object.contains( "reason" ) || object.value( "reason" ).isString() )
                      && ( !object.contains( "errorDetails" )
                           || object.value( "errorDetails" ).isObject() )
                      && ( !compiled_ || object.value( "rows" ).isArray() || hasError )
                      && ( !object.contains( "rows" ) || object.value( "rows" ).isArray() );
                const auto rows = object.value( "rows" ).toArray();
                valid = valid && rows.size() <= 10000 && ( compiled_ || rows.isEmpty() );
                for ( const auto& rowValue : rows ) {
                    const auto row = rowValue.toObject();
                    valid = valid && rowValue.isObject() && row.value( "lineNumber" ).isDouble()
                            && row.value( "text" ).isString() && row.value( "matched" ).isBool()
                            && row.value( "subpatterns" ).isArray()
                            && row.value( "offsetConvention" ).toString() == "zero-based UTF-16";
                }
                if ( !valid || process_.bytesAvailable() != 0 ) {
                    output.error = "invalid_worker_output";
                    break;
                }
                output.rows = result.value( "rows" ).toList();
                output.error = result.value( "reason" ).toString();
                output.errorDetails = result.value( "errorDetails" ).toMap();
                output.backend = result.value( "backend" ).toString();
                const auto metrics = result.value( "metrics" ).toMap();
                for ( auto it = metrics.cbegin(); it != metrics.cend(); ++it ) {
                    output.metrics[ it.key() ] = it.value();
                }
                compiled_ = output.error.isEmpty();
                return finalizeOutput( std::move( output ) );
            }
            if ( process_.state() == QProcess::NotRunning && process_.bytesAvailable() == 0 ) {
                output.error = "worker_failure";
                break;
            }
        }
        if ( output.error.isEmpty() ) {
            output.error = cancelled ? "cancelled" : "timeout";
        }
        if ( awaitingReady ) {
            output.metrics[ "startupUs" ] = startup.nsecsElapsed() / 1000;
        }
        output.errorDetails = { { "phase", awaitingReady ? "startup"
                                           : compiled_   ? "matching"
                                                         : "compilation" } };
        LOG_DEBUG << "Analysis regex worker stopped reason=" << output.error
                  << " received=" << response.size();
        stop();
        return finalizeOutput( std::move( output ) );
    }

private:
    AnalysisWorkerMemory observeMemory( bool terminating ) const
    {
        AnalysisWorkerMemory memory;
        memory.terminationObservation = terminating;
#ifdef Q_OS_WIN
        memory.availability = AnalysisWorkerMemory::Availability::Unavailable;
        memory.nativeError = memoryOpenError_;
        if ( memoryProcess_ ) {
            const auto state = WaitForSingleObject( memoryProcess_, 0 );
            if ( state == WAIT_TIMEOUT ) {
                PROCESS_MEMORY_COUNTERS counters{};
                counters.cb = static_cast<DWORD>( sizeof( counters ) );
                if ( GetProcessMemoryInfo( memoryProcess_, &counters, counters.cb ) ) {
                    if ( counters.PeakWorkingSetSize > 0 ) {
                        memory.availability = AnalysisWorkerMemory::Availability::Measured;
                        memory.peakRssBytes = counters.PeakWorkingSetSize;
                        memory.nativeError = 0;
                    }
                    else {
                        memory.nativeError = ERROR_INVALID_DATA;
                    }
                }
                else {
                    memory.nativeError = GetLastError();
                }
            }
            else {
                memory.nativeError = state == WAIT_FAILED ? GetLastError() : ERROR_PROCESS_ABORTED;
            }
        }
#else
        memory.availability = AnalysisWorkerMemory::Availability::Unsupported;
#endif
        LOG_DEBUG << "Analysis worker memory pid=" << process_.processId()
                  << " availability=" << static_cast<int>( memory.availability )
                  << " peakBytes=" << memory.peakRssBytes << " nativeError=" << memory.nativeError
                  << " terminating=" << terminating;
        return memory;
    }
    AnalysisOutput finalizeOutput( AnalysisOutput output ) const
    {
        output.workerMemory = stopObserved_ ? stoppingMemory_ : observeMemory( false );
        return output;
    }
    void stop()
    {
        if ( process_.state() != QProcess::NotRunning ) {
            stoppingMemory_ = observeMemory( true );
            stopObserved_ = true;
            LOG_DEBUG << "Reaping analysis regex worker pid=" << process_.processId();
            process_.kill();
            process_.waitForFinished( -1 );
        }
    }
    bool limitMemory()
    {
#ifdef Q_OS_WIN
        job_ = CreateJobObjectW( nullptr, nullptr );
        if ( !job_ ) {
            return false;
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags
            = JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        limits.ProcessMemoryLimit = 512ULL * 1024 * 1024;
        const auto child = OpenProcess( PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                        static_cast<DWORD>( process_.processId() ) );
        const bool accepted = child
                              && SetInformationJobObject( job_, JobObjectExtendedLimitInformation,
                                                          &limits, sizeof( limits ) )
                              && AssignProcessToJobObject( job_, child );
        if ( child ) {
            CloseHandle( child );
        }
        if ( accepted ) {
            memoryProcess_ = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
                                          static_cast<DWORD>( process_.processId() ) );
            if ( !memoryProcess_ ) {
                memoryOpenError_ = GetLastError();
            }
            else {
                BOOL member = FALSE;
                const auto queried = IsProcessInJob( memoryProcess_, job_, &member );
                if ( !queried || !member ) {
                    memoryOpenError_ = queried ? ERROR_INVALID_HANDLE : GetLastError();
                    CloseHandle( memoryProcess_ );
                    memoryProcess_ = nullptr;
                }
            }
            LOG_DEBUG << "Analysis worker memory handle pid=" << process_.processId()
                      << " accepted=" << ( memoryProcess_ != nullptr )
                      << " nativeError=" << memoryOpenError_;
        }
        return accepted;
#else
        return true; // The internal worker applies RLIMIT_AS before reading requests.
#endif
    }
    QVariantMap options_;
    QString executable_;
    QStringList arguments_;
    QProcess process_;
    bool started_ = false;
    bool compiled_ = false;
    bool protocolFailure_ = false;
    qint64 stderrBytes_ = 0;
    quint32 requestId_ = 0;
    bool stopObserved_ = false;
    AnalysisWorkerMemory stoppingMemory_{};
#ifdef Q_OS_WIN
    HANDLE job_ = nullptr;
    HANDLE memoryProcess_ = nullptr;
    quint32 memoryOpenError_ = ERROR_INVALID_HANDLE;
#endif
};
} // namespace
AnalysisProcessorFactory makeRegexProcessor( QVariantMap options, QString executable,
                                             QStringList arguments )
{
    if ( executable.isEmpty() ) {
        executable = QCoreApplication::applicationFilePath();
    }
    if ( arguments.isEmpty() ) {
        arguments = { "--internal-analysis-worker" };
    }
    return [ options = std::move( options ), executable = std::move( executable ),
             arguments = std::move( arguments ) ] {
        auto worker = std::make_shared<RegexProcess>( options, executable, arguments );
        return
            [ worker ]( const AnalysisBatch& batch, const std::atomic_bool& cancelled,
                        int timeoutMs ) { return worker->process( batch, cancelled, timeoutMs ); };
    };
}
