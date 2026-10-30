// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysisjob.h"
#include "log.h"
#include "logdata.h"
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QRunnable>
#include <QTextCodec>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <limits>
#include <map>
#include <mutex>

namespace {
constexpr qint64 GlobalResultBytes = 64 * 1024 * 1024;
constexpr qint64 RowBytes = 4 * 1024 * 1024;
class WorkerMemorySummary {
public:
    bool record( const AnalysisWorkerMemory& value )
    {
        using Availability = AnalysisWorkerMemory::Availability;
        switch ( value.availability ) {
        case Availability::NotReported:
        case Availability::Unsupported:
            if ( value.peakRssBytes != 0 || value.nativeError != 0 )
                return false;
            break;
        case Availability::Unavailable:
            if ( value.peakRssBytes != 0 )
                return false;
            break;
        case Availability::Measured:
            if ( value.peakRssBytes == 0 || value.nativeError != 0 )
                return false;
            break;
        default:
            return false;
        }
        auto& counter = value.availability == Availability::Measured ? measured_ : missing_;
        if ( counter == std::numeric_limits<quint64>::max() )
            return false;
        ++counter;
        reported_ = reported_ || value.availability != Availability::NotReported;
        unavailable_ = unavailable_ || value.availability == Availability::Unavailable;
        unsupported_ = unsupported_ || value.availability == Availability::Unsupported;
        peak_ = std::max( peak_, value.peakRssBytes );
        if ( value.availability != Availability::NotReported ) {
            lastObservation_ = value.terminationObservation ? "termination_request" : "response";
            lastError_ = value.nativeError;
        }
        return true;
    }
    bool hasReport() const
    {
        return reported_;
    }
    QVariantMap status() const
    {
        const auto state = measured_ > 0  ? ( missing_ > 0 ? "partial" : "measured" )
                           : unavailable_ ? "unavailable"
                           : unsupported_ ? "unsupported"
                                          : "not_reported";
        return { { "status", state },
                 { "backend", measured_ > 0 || unavailable_ ? "windows_peak_working_set" : "none" },
                 { "unit", "bytes" },
                 { "scope", "largest_single_worker_high_water_through_observation" },
                 { "peakRssBytes",
                   measured_ > 0 ? QVariant( QString::number( peak_ ) ) : QVariant{} },
                 { "measuredObservations", QString::number( measured_ ) },
                 { "missingObservations", QString::number( missing_ ) },
                 { "lastObservation", lastObservation_ },
                 { "lastNativeError",
                   lastError_ ? QVariant( QString::number( lastError_ ) ) : QVariant{} } };
    }

private:
    quint64 peak_ = 0;
    quint64 measured_ = 0;
    quint64 missing_ = 0;
    quint32 lastError_ = 0;
    bool reported_ = false;
    bool unavailable_ = false;
    bool unsupported_ = false;
    QString lastObservation_ = "none";
};
qint64 rowBytes( const QVariantMap& row )
{
    return QJsonDocument( QJsonObject::fromVariantMap( row ) )
        .toJson( QJsonDocument::Compact )
        .size();
}
bool terminal( const QString& state )
{
    return state == "completed" || state == "cancelled" || state == "failed";
}
AnalysisOutput rawOutput( const AnalysisBatch& batch )
{
    AnalysisOutput output;
    for ( const auto& line : batch.lines ) {
        output.rows.push_back( QVariantMap{ { "lineNumber", QVariant::fromValue( line.line + 1 ) },
                                            { "text", line.text } } );
    }
    return output;
}
} // namespace

struct AnalysisJobService::Private {
    struct Pending {
        QVariantList rows;
        qint64 bytes = 0;
    };
    struct Job {
        QString id = QUuid::createUuid().toString( QUuid::WithoutBraces );
        AnalysisRequest request;
        LogData* source = nullptr; // Protected by the service's owner-thread lease.
        std::weak_ptr<LogData> weakSource;
        bool sourceClosed = false;
        AnalysisProcessorFactory factory;
        mutable std::mutex mutex;
        std::condition_variable credit;
        std::atomic_bool cancelled{ false };
        QString state = "queued";
        QString reason;
        QString cancelReason = "cancelled";
        bool complete = false;
        bool workerDone = false;
        bool released = false;
        quint64 scanned = 0;
        qint64 pendingBytes = 0;
        qint64 storedBytes = 0;
        qint64 finishedAt = -1;
        QVariantMap metrics;
        WorkerMemorySummary workerMemory;
        QVariantMap aggregation;
        QVariantMap provenance;
        qint64 createdAt = 0;
        QVariantMap errorDetails;
        QString backend;
        bool compiled = false;
        qint64 readingUs = 0;
        qint64 deliveryUs = 0;
        qint64 creditUs = 0;
        std::deque<Pending> pending;
        QVariantList rows;
        std::map<QString, int> cursors;
        std::map<int, QString> nextCursors;
        QDeadlineTimer deadline;
    };
    std::map<QString, std::shared_ptr<Job>> jobs;
    std::map<QString, std::shared_ptr<LogData>> leases;
    QThreadPool pool;
    QTimer timer;
    QElapsedTimer clock;
    std::function<qint64()> retentionClock;
    qint64 elapsed() const
    {
        return retentionClock ? retentionClock() : clock.elapsed();
    }
    bool stopping = false;

    void detachLease( const QString& id, std::vector<std::shared_ptr<LogData>>& retired )
    {
        const auto lease = leases.find( id );
        if ( lease != leases.end() ) {
            retired.push_back( std::move( lease->second ) );
            leases.erase( lease );
        }
    }

    static void run( const std::shared_ptr<Job>& job )
    {
        AnalysisProcessor processor;
        std::unique_ptr<ValueCountJob> counts;
        std::unique_ptr<QTextDecoder> decoder;
        quint64 nextLine = 0;
        quint64 selected = 0;
        qint64 selectedBytes = 0;
        qsizetype textOffset = 0;
        QString reason;
        bool done = false;
        try {
            processor = job->factory ? job->factory() : AnalysisProcessor{};
            if ( job->request.valueCounts ) {
                counts = std::make_unique<ValueCountJob>( *job->request.valueCounts,
                    job->request.limits.resultRecords,
                    std::min( job->request.limits.pendingBytes, job->request.limits.resultBytes ) );
            }
            bool initialize = static_cast<bool>( processor );
            if ( ( job->request.scope == "selection" || job->request.scope == "matches" )
                 && job->request.ranges.empty() ) {
                done = true;
            }
            while ( !job->cancelled && !job->deadline.hasExpired() && ( !done || initialize ) ) {
                QElapsedTimer reading;
                reading.start();
                AnalysisBatch batch;
                QStringList decoded;
                const auto firstLine = nextLine;
                const bool initializing = initialize;
                initialize = false;
                if ( !initializing ) {
                    if ( job->source ) {
                        auto endLine
                            = static_cast<quint64>( job->request.snapshot.lineCount.get() );
                        const bool sampleLimited = job->request.scope == "sample" && endLine > 1000;
                        if ( sampleLimited )
                            endLine = 1000;
                        if ( !job->request.ranges.empty() ) {
                            endLine = std::min( endLine, job->request.ranges.back().second + 1 );
                        }
                        if ( nextLine >= endLine ) {
                            if ( sampleLimited )
                                reason = "sample_limit";
                            break;
                        }
                        auto limits = job->request.limits;
                        limits.batchLines = static_cast<int>( std::min<quint64>(
                            static_cast<quint64>( limits.batchLines ), endLine - nextLine ) );
                        const auto block = job->source->readAnalysisBlock( job->request.snapshot,
                                                                           nextLine, limits );
                        if ( block.retry ) {
                            QThread::msleep( 5 );
                            continue;
                        }
                        if ( !block.error.isEmpty() ) {
                            reason = block.error;
                            break;
                        }
                        if ( !decoder ) {
                            auto* codec = QTextCodec::codecForName( block.codec );
                            if ( !codec ) {
                                reason = "unsupported_encoding";
                                break;
                            }
                            decoder.reset( codec->makeDecoder() );
                            std::lock_guard<std::mutex> lock( job->mutex );
                            job->provenance[ "encoding" ] = QString::fromLatin1( block.codec );
                        }
                        const auto text = decoder->toUnicode( block.bytes );
                        decoded = text.split( '\n' );
                        if ( text.endsWith( '\n' ) ) {
                            decoded.removeLast();
                        }
                        nextLine = block.nextLine;
                        if ( decoded.size() != static_cast<qsizetype>( nextLine - firstLine ) ) {
                            reason = "decode_error";
                            break;
                        }
                    }
                    else {
                        const auto& input = job->request.text;
                        if ( textOffset >= input.size() ) {
                            break;
                        }
                        qint64 bytes = 0;
                        while ( textOffset < input.size()
                                && decoded.size() < job->request.limits.batchLines ) {
                            const auto newline = input.indexOf( '\n', textOffset );
                            const auto end = newline < 0 ? input.size() : newline;
                            if ( end - textOffset > job->request.limits.recordBytes ) {
                                reason = "record_limit";
                                done = true;
                                break;
                            }
                            const auto line = input.mid( textOffset, end - textOffset );
                            const qint64 size = line.toUtf8().size() + ( newline < 0 ? 0 : 1 );
                            if ( size > job->request.limits.recordBytes ) {
                                reason = "record_limit";
                                done = true;
                                break;
                            }
                            if ( bytes + size > job->request.limits.batchBytes ) {
                                break;
                            }
                            decoded.push_back( line );
                            bytes += size;
                            textOffset = end + ( newline < 0 ? 0 : 1 );
                            ++nextLine;
                        }
                    }
                    for ( int index = 0; index < decoded.size(); ++index ) {
                        const auto line = firstLine + static_cast<quint64>( index );
                        const auto& text = decoded.at( index );
                        if ( !job->source
                             && text.toUtf8().size() > job->request.limits.recordBytes ) {
                            reason = "record_limit";
                            done = true;
                            break;
                        }
                        const auto& ranges = job->request.ranges;
                        const auto included = ( ranges.empty() && job->request.scope != "selection"
                                                && job->request.scope != "matches" )
                                              || std::any_of( ranges.begin(), ranges.end(),
                                                              [ line ]( const auto& range ) {
                                                                  return line >= range.first
                                                                         && line <= range.second;
                                                              } );
                        if ( !included ) {
                            continue;
                        }
                        const auto bytes = static_cast<qint64>( text.size() ) * 2;
                        if ( job->request.scope == "sample"
                             && ( selected >= 1000 || selectedBytes + bytes > 4 * 1024 * 1024 ) ) {
                            reason = "sample_limit";
                            done = true;
                            break;
                        }
                        batch.lines.push_back( { line, text } );
                        ++selected;
                        selectedBytes += bytes;
                    }
                }
                {
                    std::lock_guard<std::mutex> lock( job->mutex );
                    job->readingUs += reading.nsecsElapsed() / 1000;
                }
                if ( batch.lines.empty() && !initializing ) {
                    continue;
                }
                const auto remaining = static_cast<int>(
                    std::min<qint64>( job->request.limits.batchTimeoutMs,
                                      std::max<qint64>( 0, job->deadline.remainingTime() ) ) );
                batch.jobRemainingMs
                    = static_cast<int>( std::max<qint64>( 0, job->deadline.remainingTime() ) );
                auto output = processor ? processor( batch, job->cancelled, remaining )
                                              : rawOutput( batch );
                const bool malformedReason = output.error.size() > 64;
                const bool fatal
                    = malformedReason
                      || ( !output.error.isEmpty() && !output.error.endsWith( "_limit" ) );
                if ( !output.error.isEmpty() ) {
                    reason = malformedReason ? "invalid_worker_output" : output.error;
                    done = true;
                }
                if ( job->source ) {
                    const auto current = job->source->sourceSnapshot();
                    if ( !current.isOpen() || current.sourceId != job->request.snapshot.sourceId
                         || current.generation != job->request.snapshot.generation ) {
                        reason = "stale_source";
                        break;
                    }
                }
                if ( counts && !fatal && !initializing ) {
                    bool invalid = false;
                    for ( const auto& row : output.rows ) {
                        if ( row.userType() != QMetaType::QVariantMap ) {
                            reason = "invalid_worker_output";
                            invalid = true;
                            break;
                        }
                        if ( rowBytes( row.toMap() ) > RowBytes ) {
                            reason = "result_limit";
                            invalid = true;
                            break;
                        }
                    }
                    if ( invalid ) {
                        LOG_DEBUG << "Value count rejected processor rows id=" << job->id
                                  << " reason=" << reason;
                        break;
                    }
                    const auto countError = counts->consume( output.rows, job->cancelled );
                    output.rows.clear();
                    {
                        std::lock_guard<std::mutex> lock( job->mutex );
                        job->aggregation = counts->status();
                    }
                    if ( !countError.isEmpty() ) {
                        reason = countError;
                        done = true;
                        LOG_DEBUG << "Value count reduction stopped id=" << job->id
                                  << " reason=" << reason;
                    }
                }
                Pending pending;
                for ( const auto& value : output.rows ) {
                    if ( fatal || initializing ) {
                        reason = initializing ? "invalid_worker_output" : reason;
                        done = true;
                        break;
                    }
                    if ( value.userType() != QMetaType::QVariantMap ) {
                        reason = "invalid_worker_output";
                        done = true;
                        break;
                    }
                    const auto bytes = rowBytes( value.toMap() );
                    if ( bytes > RowBytes
                         || pending.bytes + bytes > job->request.limits.pendingBytes ) {
                        reason = "result_limit";
                        done = true;
                        break;
                    }
                    pending.rows.push_back( value );
                    pending.bytes += bytes;
                }
                std::unique_lock<std::mutex> lock( job->mutex );
                if ( !job->workerMemory.record( output.workerMemory ) ) {
                    reason = "invalid_worker_output";
                    done = true;
                    LOG_DEBUG << "Invalid worker memory observation id=" << job->id;
                }
                else if ( output.workerMemory.availability
                          != AnalysisWorkerMemory::Availability::NotReported ) {
                    LOG_DEBUG << "Analysis worker memory id=" << job->id << " availability="
                              << static_cast<int>( output.workerMemory.availability )
                              << " peakBytes=" << output.workerMemory.peakRssBytes
                              << " nativeError=" << output.workerMemory.nativeError
                              << " terminating=" << output.workerMemory.terminationObservation;
                }
                if ( !output.backend.isEmpty() ) {
                    if ( output.backend != "Qt QRegularExpression" ) {
                        reason = "invalid_worker_output";
                        done = true;
                    }
                    else {
                        job->backend = output.backend;
                        if ( initializing && output.error.isEmpty() ) {
                            job->compiled = true;
                        }
                    }
                }
                if ( rowBytes( output.errorDetails ) > 4096 ) {
                    reason = "invalid_worker_output";
                    done = true;
                }
                else if ( !output.errorDetails.isEmpty() ) {
                    job->errorDetails = output.errorDetails;
                }
                for ( auto it = output.metrics.cbegin(); it != output.metrics.cend(); ++it ) {
                    bool valid = false;
                    const auto amount = it.value().toLongLong( &valid );
                    const auto previous = job->metrics.value( it.key() ).toLongLong();
                    if ( ( it.key() != "compileUs" && it.key() != "matchUs"
                           && it.key() != "startupUs" )
                         || !valid || amount < 0
                         || amount > std::numeric_limits<qint64>::max() - previous ) {
                        reason = "invalid_worker_output";
                        done = true;
                        break;
                    }
                    job->metrics[ it.key() ] = previous + amount;
                }
                if ( fatal || ( done && reason == "invalid_worker_output" ) ) {
                    break;
                }
                job->scanned += static_cast<quint64>( batch.lines.size() );
                QElapsedTimer credit;
                credit.start();
                while ( !job->cancelled && !job->deadline.hasExpired()
                        && ( job->pendingBytes + pending.bytes > job->request.limits.pendingBytes
                             || job->pending.size() >= 8 ) ) {
                    job->credit.wait_for( lock, std::chrono::milliseconds( 10 ) );
                }
                job->creditUs += credit.nsecsElapsed() / 1000;
                if ( job->cancelled || job->deadline.hasExpired() ) {
                    break;
                }
                if ( !pending.rows.isEmpty() ) {
                    job->pendingBytes += pending.bytes;
                    job->pending.push_back( std::move( pending ) );
                }
            }
            if ( counts ) {
                Pending finalCounts;
                for ( const auto& row : counts->results() ) {
                    const auto bytes = rowBytes( row.toMap() );
                    if ( bytes > RowBytes
                         || finalCounts.bytes + bytes > job->request.limits.pendingBytes ) {
                        reason = "result_limit";
                        LOG_DEBUG << "Value count final delivery limit id=" << job->id
                                  << " bytes=" << finalCounts.bytes << " nextBytes=" << bytes;
                        break;
                    }
                    finalCounts.rows.push_back( row );
                    finalCounts.bytes += bytes;
                }
                std::lock_guard<std::mutex> lock( job->mutex );
                job->aggregation = counts->status();
                if ( !finalCounts.rows.isEmpty() ) {
                    job->pendingBytes += finalCounts.bytes;
                    job->pending.push_back( std::move( finalCounts ) );
                }
            }
        } catch ( const std::exception& error ) {
            LOG_ERROR << "Analysis worker failed id=" << job->id << " error=" << error.what();
            reason = "worker_failure";
        }
        // Completion covers the full worker callable lifetime, including its destructors.
        processor = {};
        decoder.reset();
        std::lock_guard<std::mutex> lock( job->mutex );
        if ( job->deadline.hasExpired() ) {
            reason = "timeout";
        }
        job->reason = reason;
        job->workerDone = true;
        LOG_DEBUG << "Analysis scan stopped id=" << job->id << " reason=" << reason
                  << " scanned=" << job->scanned << " cancelled=" << job->cancelled.load();
    }
};

AnalysisJobService::AnalysisJobService( QObject* parent, std::function<qint64()> retentionClock )
    : QObject( parent )
    , d_( std::make_unique<Private>() )
{
    d_->retentionClock = std::move( retentionClock );
    d_->pool.setMaxThreadCount( 2 );
    d_->clock.start();
    d_->timer.setInterval( 20 );
    connect( &d_->timer, &QTimer::timeout, this, &AnalysisJobService::poll );
    d_->timer.start();
}
AnalysisJobService::~AnalysisJobService()
{
    d_->stopping = true;
    d_->timer.stop();
    for ( const auto& entry : d_->jobs ) {
        entry.second->cancelled = true;
        entry.second->credit.notify_all();
    }
    d_->pool.waitForDone();
    auto retired = std::move( d_->leases );
    d_->leases.clear();
    d_->jobs.clear();
    // destroyed observers may reenter release/start while d_ is still valid.
    retired.clear();
}
QString AnalysisJobService::start( AnalysisRequest request, AnalysisProcessorFactory factory,
                                   QString* error )
{
    Q_ASSERT( QThread::currentThread() == thread() );
    const auto& limits = request.limits;
    int active = 0;
    for ( const auto& entry : d_->jobs ) {
        if ( !terminal( entry.second->state ) ) {
            ++active;
        }
    }
    QString failure;
    if ( request.valueCounts
         && !parseValueCountOptions( { { "capture", request.valueCounts->capture },
                                      { "subpattern", request.valueCounts->subpattern },
                                      { "caseSensitive", request.valueCounts->caseSensitive },
                                      { "missing", request.valueCounts->includeMissing ? "include" : "skip" },
                                      { "maxValues", request.valueCounts->maxValues } } ) ) {
        failure = "invalid_count_options";
    }
    else if ( d_->stopping ) {
        failure = "service_stopping";
    }
    else if ( active >= 10 ) {
        failure = "queue_limit";
    }
    else if ( request.ranges.size() > 10000
              || rowBytes( request.sourceDescription ) > 256 * 1024 ) {
        failure = "scope_limit";
    }
    else if ( request.scope != "sample" && request.scope != "selection"
              && request.scope != "matches" && request.scope != "whole_file"
              && request.scope != "text" ) {
        failure = "invalid_scope";
    }
    else if ( limits.batchLines < 1 || limits.batchLines > 256 || limits.batchBytes < 1
              || limits.batchBytes > 1024 * 1024 || limits.recordBytes < 1
              || limits.recordBytes > limits.batchBytes || limits.pendingBytes < RowBytes
              || limits.pendingBytes > 4 * 1024 * 1024 || limits.resultBytes < 1
              || limits.resultBytes > 16 * 1024 * 1024 || limits.resultRecords < 1
              || limits.resultRecords > 10000 || limits.batchTimeoutMs < 1
              || limits.batchTimeoutMs > 30000 || limits.timeoutMs < 1
              || limits.timeoutMs > 600000 ) {
        failure = "invalid_limits";
    }
    else if ( request.source && ( !request.snapshot.available || !request.snapshot.isOpen() ) ) {
        failure = "source_not_ready";
    }
    else if ( !request.source
              && ( request.scope != "text" || request.text.size() > 2 * 1024 * 1024 ) ) {
        failure = "invalid_input";
    }
    if ( failure.isEmpty() ) {
        const auto lines
            = request.source
                  ? static_cast<quint64>( request.snapshot.lineCount.get() )
                  : static_cast<quint64>( request.text.count( '\n' ) )
                        + ( !request.text.isEmpty() && !request.text.endsWith( '\n' ) ? 1u : 0u );
        std::optional<quint64> previous;
        for ( const auto& range : request.ranges ) {
            if ( range.first > range.second || range.second >= lines
                 || ( previous && range.first <= *previous ) ) {
                failure = "invalid_scope";
                break;
            }
            previous = range.second;
        }
    }
    if ( !failure.isEmpty() ) {
        LOG_DEBUG << "Analysis rejected reason=" << failure;
        if ( error ) {
            *error = failure;
        }
        return {};
    }
    const auto timeoutMs = limits.timeoutMs;
    // Bound job metadata as well as output bytes, including zero-result jobs.
    while ( d_->jobs.size() >= 26 ) {
        auto oldest = d_->jobs.end();
        for ( auto it = d_->jobs.begin(); it != d_->jobs.end(); ++it ) {
            if ( terminal( it->second->state )
                 && ( oldest == d_->jobs.end()
                      || it->second->finishedAt < oldest->second->finishedAt ) ) {
                oldest = it;
            }
        }
        if ( oldest == d_->jobs.end() ) {
            break;
        }
        d_->jobs.erase( oldest );
    }
    auto job = std::make_shared<Private::Job>();
    job->source = request.source.get();
    job->weakSource = request.source;
    if ( request.source ) {
        d_->leases[ job->id ] = std::move( request.source );
    }
    job->request = std::move( request );
    job->createdAt = d_->elapsed();
    const auto& snapshot = job->request.snapshot;
    job->provenance
        = { { "type", job->source ? "file" : "text" },
            { "sourceId",
              job->source ? snapshot.sourceId.toString( QUuid::WithoutBraces ) : job->id },
            { "generation", job->source ? QString::number( snapshot.generation ) : "1" },
            { "revision", job->source ? QString::number( snapshot.revision ) : "1" },
            { "indexedBytes", QString::number( snapshot.indexedBytes ) },
            { "encoding", job->source ? job->request.sourceDescription.value( "encoding" )
                                      : QVariant( "QString UTF-16" ) } };
    if ( !job->source )
        job->provenance[ "textUtf16Units" ] = job->request.text.size();
    const auto inlineCompleteLines = static_cast<quint64>( job->request.text.count( '\n' ) );
    const auto inlineProvisional
        = !job->request.text.isEmpty() && !job->request.text.endsWith( '\n' );
    job->provenance[ "physicalLineCount" ]
        = QVariant::fromValue( job->source ? snapshot.lineCount.get()
                                           : inlineCompleteLines + ( inlineProvisional ? 1 : 0 ) );
    job->provenance[ "completePhysicalLineCount" ] = QVariant::fromValue(
        job->source ? snapshot.completeLineCount.get() : inlineCompleteLines );
    job->provenance[ "provisionalFinalLine" ]
        = job->source ? snapshot.provisionalLineStart.has_value() : inlineProvisional;
    job->factory = std::move( factory );
    job->deadline = QDeadlineTimer( timeoutMs );
    const auto id = job->id;
    d_->jobs.emplace( id, std::move( job ) );
    LOG_DEBUG << "Analysis queued id=" << id << " scope=" << d_->jobs.at( id )->request.scope;
    return id;
}
void AnalysisJobService::poll()
{
    // A final lease can synchronously emit destroyed and reenter this service.
    // Keep destruction outside every registry iterator and job lock below.
    std::vector<std::shared_ptr<LogData>> retired;
    int running = 0;
    for ( const auto& entry : d_->jobs ) {
        if ( entry.second->state == "running" ) {
            ++running;
        }
    }
    for ( auto it = d_->jobs.begin(); it != d_->jobs.end(); ) {
        const auto job = it->second;
        bool notify = false;
        {
            QElapsedTimer delivery;
            delivery.start();
            std::lock_guard<std::mutex> lock( job->mutex );
            if ( job->state == "queued" && job->cancelled ) {
                job->state = "cancelled";
                job->reason = job->cancelReason;
                job->finishedAt = d_->elapsed();
                notify = true;
            }
            if ( job->state == "queued" && running < 2 ) {
                job->state = "running";
                ++running;
                notify = true;
                d_->pool.start( QRunnable::create( [ job ] { Private::run( job ); } ) );
                LOG_DEBUG << "Analysis started id=" << job->id;
            }
            if ( job->state == "running" && job->source ) {
                const auto current = job->source->sourceSnapshot();
                if ( !current.isOpen() || current.generation != job->request.snapshot.generation ) {
                    job->cancelReason = "stale_source";
                    job->cancelled = true;
                    LOG_DEBUG << "Analysis rejected queued stale batch id=" << job->id;
                }
            }
            if ( !job->pending.empty() ) {
                auto pending = std::move( job->pending.front() );
                job->pending.pop_front();
                job->pendingBytes -= pending.bytes;
                job->credit.notify_all();
                for ( const auto& value : pending.rows ) {
                    auto row = value.toMap();
                    row[ "jobId" ] = job->id;
                    row[ "requestRevision" ] = QString::number( job->request.requestRevision );
                    row[ "source" ] = job->provenance;
                    const auto bytes = rowBytes( row );
                    if ( job->cancelled ) {
                        break;
                    }
                    if ( bytes > RowBytes || job->rows.size() >= job->request.limits.resultRecords
                         || job->storedBytes + bytes > job->request.limits.resultBytes ) {
                        job->cancelled = true;
                        job->cancelReason = "result_limit";
                        job->credit.notify_all();
                        break;
                    }
                    qint64 globalBytes = 0;
                    for ( const auto& other : d_->jobs ) {
                        globalBytes += other.second->storedBytes;
                    }
                    while ( globalBytes + bytes > GlobalResultBytes ) {
                        auto victim = d_->jobs.end();
                        for ( auto candidate = d_->jobs.begin(); candidate != d_->jobs.end();
                              ++candidate ) {
                            if ( candidate->second != job && terminal( candidate->second->state )
                                 && ( victim == d_->jobs.end()
                                      || candidate->second->finishedAt
                                             < victim->second->finishedAt ) ) {
                                victim = candidate;
                            }
                        }
                        if ( victim == d_->jobs.end() ) {
                            break;
                        }
                        LOG_DEBUG << "Analysis result eviction id=" << victim->first;
                        globalBytes -= victim->second->storedBytes;
                        d_->detachLease( victim->first, retired );
                        d_->jobs.erase( victim );
                    }
                    if ( globalBytes + bytes > GlobalResultBytes ) {
                        job->cancelled = true;
                        job->cancelReason = "result_limit";
                        job->credit.notify_all();
                        break;
                    }
                    job->rows.push_back( row );
                    job->storedBytes += bytes;
                }
                notify = true;
            }
            if ( job->state == "running" && job->workerDone && job->pending.empty() ) {
                if ( job->cancelled ) {
                    job->reason = job->cancelReason;
                }
                const auto limited = job->reason.endsWith( "_limit" );
                job->complete = job->reason.isEmpty() && !job->request.scopeLimited;
                if ( job->request.scopeLimited && job->reason.isEmpty() ) {
                    job->reason = "scope_limit";
                }
                job->state
                    = job->cancelled
                              && ( job->reason == "cancelled" || job->reason == "source_closed" )
                          ? "cancelled"
                          : ( job->reason.isEmpty() || limited || job->reason == "scope_limit"
                                  ? "completed"
                                  : "failed" );
                job->finishedAt = d_->elapsed();
                notify = true;
                LOG_DEBUG << "Analysis finished id=" << job->id << " state=" << job->state
                          << " reason=" << job->reason << " rows=" << job->rows.size();
            }
            job->deliveryUs += delivery.nsecsElapsed() / 1000;
        }
        const auto expired = terminal( job->state )
                             && ( job->released || d_->elapsed() - job->finishedAt >= 600000 );
        // run() sets workerDone only after its last source access. Workers own
        // no shared LogData pointer, so final destruction stays on this thread.
        if ( terminal( job->state ) ) {
            d_->detachLease( job->id, retired );
            job->source = nullptr;
            job->request.text.clear();
            job->request.text.squeeze();
            job->request.ranges.clear();
            job->request.ranges.shrink_to_fit();
            job->factory = {};
        }
        if ( expired ) {
            it = d_->jobs.erase( it );
        }
        else {
            ++it;
        }
        if ( notify ) {
            // A direct observer may release jobs; do not call it while iterating the registry.
            const auto id = job->id;
            QMetaObject::invokeMethod(
                this, [ this, id ] { Q_EMIT changed( id ); }, Qt::QueuedConnection );
        }
    }
}
QVariantMap AnalysisJobService::status( const QString& id ) const
{
    const auto found = d_->jobs.find( id );
    if ( found == d_->jobs.end() ) {
        return { { "reason", "job_expired" } };
    }
    const auto job = found->second;
    const auto source = job->weakSource.lock();
    std::lock_guard<std::mutex> lock( job->mutex );
    const auto& snapshot = job->request.snapshot;
    const auto current = source ? source->sourceSnapshot() : SourceSnapshot{};
    const auto historical
        = !snapshot.sourceId.isNull()
          && ( job->sourceClosed || !source || current.generation != snapshot.generation );
    auto metrics = job->metrics;
    if ( job->workerMemory.hasReport() ) {
        metrics[ "workerMemory" ] = job->workerMemory.status();
    }
    return { { "jobId", id },
             { "kind", job->request.valueCounts ? "value_counts" : "regex" },
             { "aggregation", job->aggregation },
             { "state", job->state },
             { "reason", job->reason },
             { "complete", job->complete },
             { "scope", job->request.scope },
             { "requestRevision", QString::number( job->request.requestRevision ) },
             { "scannedLines", QVariant::fromValue( job->scanned ) },
             { "resultRecords", job->rows.size() },
             { "pendingBytes", job->pendingBytes },
             { "retainedBytes", job->storedBytes },
             { "pendingBatches", static_cast<int>( job->pending.size() ) },
             { "sourceId", job->provenance.value( "sourceId" ) },
             { "sourceGeneration", job->provenance.value( "generation" ) },
             { "sourceRevision", job->provenance.value( "revision" ) },
             { "source", job->provenance },
             { "sourceDescription", job->request.sourceDescription },
             { "physicalLineCount", job->provenance.value( "physicalLineCount" ) },
             { "completePhysicalLineCount", job->provenance.value( "completePhysicalLineCount" ) },
             { "provisionalFinalLine", job->provenance.value( "provisionalFinalLine" ) },
             { "elapsedMs",
               ( job->finishedAt >= 0 ? job->finishedAt : d_->elapsed() ) - job->createdAt },
             { "indexedBytes", QString::number( snapshot.indexedBytes ) },
             { "metrics", metrics },
             { "timing", QVariantMap{ { "sourceReadUs", job->readingUs },
                                      { "resultDeliveryUs", job->deliveryUs },
                                      { "backpressureUs", job->creditUs } } },
             { "captureBackend", job->backend },
             { "compiled", job->compiled },
             { "errorDetails", job->errorDetails },
             { "historical", historical },
             { "newerDataAvailable",
               source && !historical && current.revision != snapshot.revision },
             { "limits", QVariantMap{ { "batchLines", job->request.limits.batchLines },
                                      { "batchBytes", job->request.limits.batchBytes },
                                      { "recordBytes", job->request.limits.recordBytes },
                                      { "pendingBytes", job->request.limits.pendingBytes },
                                      { "resultBytes", job->request.limits.resultBytes },
                                      { "resultRecords", job->request.limits.resultRecords },
                                      { "batchTimeoutMs", job->request.limits.batchTimeoutMs },
                                      { "timeoutMs", job->request.limits.timeoutMs },
                                      { "runningJobs", 2 },
                                      { "queuedJobs", 8 },
                                      { "globalResultBytes", GlobalResultBytes },
                                      { "expiryMs", 600000 },
                                      { "sampleLines", 1000 },
                                      { "sampleDecodedBytes", 4 * 1024 * 1024 },
                                      { "pageRecords", 1000 },
                                      { "pageBytes", 8 * 1024 * 1024 },
                                      { "scopeRanges", 10000 },
                                      { "sourceMetadataBytes", 256 * 1024 },
                                      { "patternBytes", 65536 },
                                      { "booleanSubpatterns", 64 },
                                      { "captureGroups", 128 },
                                      { "batchMatchOccurrences", 10000 },
                                      { "workerFrameBytes", 8 * 1024 * 1024 },
                                      { "workerDiagnosticBytes", 65536 },
                                      { "workerStartupMs", 30000 } } } };
}
QVariantMap AnalysisJobService::results( const QString& id, const QString& cursor, int count )
{
    const auto found = d_->jobs.find( id );
    if ( found == d_->jobs.end() ) {
        return { { "reason", "job_expired" } };
    }
    const auto& job = found->second;
    std::lock_guard<std::mutex> lock( job->mutex );
    int offset = 0;
    if ( !cursor.isEmpty() ) {
        const auto position = job->cursors.find( cursor );
        if ( position == job->cursors.end() ) {
            return { { "reason", "invalid_cursor" } };
        }
        offset = position->second;
    }
    count = std::clamp( count, 1, 1000 );
    QVariantList page;
    qint64 bytes = 0;
    while ( offset < job->rows.size() && page.size() < count ) {
        const auto value = job->rows.at( offset );
        const auto size = rowBytes( value.toMap() );
        if ( bytes + size > 8 * 1024 * 1024 - 4096 ) {
            break;
        }
        page.push_back( value );
        bytes += size;
        ++offset;
    }
    QString next;
    if ( offset < job->rows.size() || !terminal( job->state ) ) {
        const auto existing = job->nextCursors.find( offset );
        if ( existing != job->nextCursors.end() ) {
            next = existing->second;
        }
        else {
            next = QUuid::createUuid().toString( QUuid::WithoutBraces );
            job->cursors[ next ] = offset;
            job->nextCursors[ offset ] = next;
        }
    }
    return { { "jobId", id },
             { "rows", page },
             { "requestRevision", QString::number( job->request.requestRevision ) },
             { "source", job->provenance },
             { "nextCursor", next },
             { "state", job->state },
             { "complete", job->complete },
             { "reason", job->reason } };
}
QVariantMap AnalysisJobService::valueCountRecord( const QString& id, int index ) const
{
    const auto found = d_->jobs.find( id );
    if ( found == d_->jobs.end() )
        return { { "reason", "job_expired" } };
    const auto& job = found->second;
    std::lock_guard<std::mutex> lock( job->mutex );
    if ( !job->request.valueCounts )
        return { { "reason", "not_value_counts" } };
    if ( !terminal( job->state ) )
        return { { "reason", "job_not_finished" } };
    if ( index < 0 || index >= job->rows.size() )
        return { { "reason", "invalid_result_index" } };
    return { { "jobId", id }, { "rowIndex", index }, { "row", job->rows.at( index ) },
             { "complete", job->complete }, { "reason", job->reason } };
}

QVariantMap AnalysisJobService::exportValueCounts( const QString& id, const QString& path,
                                                 bool allowPartial )
{
    const auto found = d_->jobs.find( id );
    if ( found == d_->jobs.end() )
        return { { "reason", "job_expired" } };
    const auto& job = found->second;
    std::lock_guard<std::mutex> lock( job->mutex );
    if ( !job->request.valueCounts )
        return { { "reason", "not_value_counts" } };
    if ( !terminal( job->state ) )
        return { { "reason", "job_not_finished" } };
    if ( !job->complete && !allowPartial )
        return { { "reason", "incomplete_job" } };
    if ( path.isEmpty() || path.size() > 32768 || path.contains( QChar::Null )
         || !QFileInfo( path ).isAbsolute() )
        return { { "reason", "invalid_export_path" } };
    const auto sourcePath = job->request.sourceDescription.value( "filePath" ).toString();
    const auto target = QFileInfo( path );
#ifdef Q_OS_WIN
    constexpr auto pathCase = Qt::CaseInsensitive;
#else
    constexpr auto pathCase = Qt::CaseSensitive;
#endif
    if ( !sourcePath.isEmpty()
         && ( target.absoluteFilePath().compare( QFileInfo( sourcePath ).absoluteFilePath(), pathCase ) == 0
              || ( !target.canonicalFilePath().isEmpty()
                   && target.canonicalFilePath().compare( QFileInfo( sourcePath ).canonicalFilePath(), pathCase ) == 0 ) ) )
        return { { "reason", "source_is_export_target" } };
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) ) {
        LOG_DEBUG << "Value count export open failed id=" << id << " path=" << path
                  << " error=" << file.errorString();
        return { { "reason", "io_error" } };
    }
    const auto quote = []( QString value ) {
        value.replace( '"', "\"\"" );
        return '"' + value + '"';
    };
    const QByteArray header = "value,is_missing,count,percentage,denominator,first_line,"
                              "source_id,generation,complete,reason\r\n";
    qint64 written = header.size();
    if ( file.write( header ) != header.size() )
        return { { "reason", "io_error" } };
    for ( const auto& value : job->rows ) {
        const auto row = value.toMap();
        QStringList fields{ row.value( "value" ).toString(),
            row.value( "missing" ).toBool() ? "true" : "false",
            row.value( "count" ).toString(),
            QString::number( row.value( "percentage" ).toDouble(), 'g', 17 ),
            row.value( "denominator" ).toString(), row.value( "lineNumber" ).toString(),
            job->provenance.value( "sourceId" ).toString(),
            job->provenance.value( "generation" ).toString(),
            job->complete ? "true" : "false", job->reason };
        for ( auto& field : fields )
            field = quote( field );
        const auto bytes = ( fields.join( ',' ) + "\r\n" ).toUtf8();
        if ( bytes.size() > 16 * 1024 * 1024 - written )
            return { { "reason", "export_limit" } };
        if ( file.write( bytes ) != bytes.size() )
            return { { "reason", "io_error" } };
        written += bytes.size();
    }
    if ( !file.commit() ) {
        LOG_DEBUG << "Value count export commit failed id=" << id << " path=" << path
                  << " error=" << file.errorString();
        return { { "reason", "io_error" } };
    }
    LOG_DEBUG << "Value count CSV exported id=" << id << " path=" << path
              << " rows=" << job->rows.size() << " bytes=" << written
              << " complete=" << job->complete << " reason=" << job->reason;
    return { { "jobId", id }, { "filePath", path }, { "exportedRows", job->rows.size() },
             { "bytes", written }, { "complete", job->complete }, { "reason", job->reason } };
}

bool AnalysisJobService::cancel( const QString& id )
{
    const auto found = d_->jobs.find( id );
    if ( found == d_->jobs.end() ) {
        return false;
    }
    found->second->cancelled = true;
    found->second->credit.notify_all();
    LOG_DEBUG << "Analysis cancellation requested id=" << id;
    return true;
}
bool AnalysisJobService::release( const QString& id )
{
    std::vector<std::shared_ptr<LogData>> retired;
    const auto found = d_->jobs.find( id );
    if ( found == d_->jobs.end() ) {
        return false;
    }
    if ( terminal( found->second->state ) ) {
        d_->detachLease( id, retired );
        d_->jobs.erase( found );
    }
    else {
        found->second->released = true;
        cancel( id );
    }
    return true;
}
void AnalysisJobService::cancelSource( const LogData* source )
{
    if ( !source ) {
        LOG_DEBUG << "Analysis source close ignored for empty source";
        return;
    }
    for ( const auto& entry : d_->jobs ) {
        if ( entry.second->weakSource.lock().get() == source ) {
            entry.second->sourceClosed = true;
            if ( !terminal( entry.second->state ) ) {
                entry.second->cancelReason = "source_closed";
                cancel( entry.first );
            }
        }
    }
}
std::optional<SourceLocation> AnalysisJobService::location( const QString& id, quint64 line,
                                                            std::optional<SourceSpan> span ) const
{
    const auto found = d_->jobs.find( id );
    if ( found == d_->jobs.end() ) {
        return {};
    }
    const auto& request = found->second->request;
    const auto source = found->second->weakSource.lock();
    if ( !source || found->second->sourceClosed || line >= request.snapshot.lineCount.get() ) {
        return {};
    }
    const auto position = request.snapshot.location(
        LineNumber{ static_cast<LineNumber::UnderlyingType>( line ) }, span );
    return position && source->sourceSnapshot().accepts( *position ) ? position : std::nullopt;
}
