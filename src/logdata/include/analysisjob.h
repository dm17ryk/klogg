// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_ANALYSISJOB_H
#define CILOGG_ANALYSISJOB_H
#include "sourcelocation.h"
#include "valuecountjob.h"
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class LogData;
struct AnalysisLimits {
    int batchLines = 256;
    qint64 batchBytes = 1024 * 1024;
    qint64 recordBytes = 1024 * 1024;
    qint64 pendingBytes = 4 * 1024 * 1024;
    qint64 resultBytes = 16 * 1024 * 1024;
    int resultRecords = 10000;
    int batchTimeoutMs = 2000;
    int timeoutMs = 60000;
};
struct AnalysisRawBlock {
    QByteArray bytes;
    QByteArray codec;
    quint64 firstLine = 0;
    quint64 nextLine = 0;
    QString error;
    bool retry = false;
};
struct AnalysisRequest {
    std::shared_ptr<LogData> source;
    SourceSnapshot snapshot;
    QString scope = QStringLiteral( "sample" );
    QString text;
    QVariantMap sourceDescription;
    // Inclusive physical ranges; an empty list means every source line.
    std::vector<std::pair<quint64, quint64>> ranges;
    bool scopeLimited = false;
    quint64 requestRevision = 0;
    AnalysisLimits limits;
    std::optional<ValueCountOptions> valueCounts;
};
struct AnalysisLine {
    quint64 line = 0;
    QString text;
};
struct AnalysisBatch {
    std::vector<AnalysisLine> lines;
    int jobRemainingMs = 60000;
};
// Supervisor observations, never accepted from worker protocol messages.
struct AnalysisWorkerMemory {
    enum class Availability { NotReported, Unsupported, Unavailable, Measured };
    Availability availability = Availability::NotReported;
    quint64 peakRssBytes = 0;
    quint32 nativeError = 0;
    bool terminationObservation = false;
};
struct AnalysisOutput {
    QVariantList rows;
    QString error;
    QVariantMap metrics;
    QVariantMap errorDetails;
    QString backend;
    AnalysisWorkerMemory workerMemory{};
};
using AnalysisProcessor
    = std::function<AnalysisOutput( const AnalysisBatch&, const std::atomic_bool&, int )>;
// Construct/destroy on the worker thread. The first call has an empty batch and
// must initialize/compile even for empty scopes; it must not return result rows.
using AnalysisProcessorFactory = std::function<AnalysisProcessor()>;

class AnalysisJobService : public QObject {
    Q_OBJECT
public:
    // Optional monotonic retention clock permits deterministic expiry verification.
    explicit AnalysisJobService( QObject* parent = nullptr,
                                 std::function<qint64()> retentionClock = {} );
    ~AnalysisJobService() override;
    QString start( AnalysisRequest request, AnalysisProcessorFactory factory = {},
                   QString* error = nullptr );
    QVariantMap status( const QString& id ) const;
    QVariantMap results( const QString& id, const QString& cursor = {}, int count = 200 );
    QVariantMap valueCountRecord( const QString& id, int index ) const;
    QVariantMap exportValueCounts( const QString& id, const QString& path, bool allowPartial );
    bool cancel( const QString& id );
    bool release( const QString& id );
    void cancelSource( const LogData* source );
    std::optional<SourceLocation> location( const QString& id, quint64 line,
                                            std::optional<SourceSpan> span = {} ) const;
Q_SIGNALS:
    void changed( const QString& id );

private:
    struct Private;
    std::unique_ptr<Private> d_;
    void poll();
};
#endif
