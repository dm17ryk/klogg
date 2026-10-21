// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_VALUECOUNTJOB_H
#define CILOGG_VALUECOUNTJOB_H
#include <QVariantMap>
#include <atomic>
#include <map>
#include <optional>

struct ValueCountOptions {
    QVariant capture = 0;
    int subpattern = 0;
    bool caseSensitive = true;
    bool includeMissing = false;
    int maxValues = 10000;
};
std::optional<ValueCountOptions> parseValueCountOptions( const QVariantMap& options );

// Single scan-thread reducer. Never compiles patterns or retains source bodies.
class ValueCountJob {
public:
    ValueCountJob( ValueCountOptions options, int records, qint64 bytes );
    QString consume( const QVariantList& rows, const std::atomic_bool& cancelled );
    QVariantList results() const;
    QVariantMap status() const;

private:
    struct Entry {
        QString value;
        bool missing = false;
        quint64 count = 0;
        QVariant line;
        qint64 start = -1;
        qint64 length = 0;
    };
    QString add( const QVariantMap& group, const QVariant& line );
    ValueCountOptions options_;
    int maxValues_;
    qint64 maxBytes_;
    qint64 bytes_ = 0;
    quint64 counted_ = 0;
    quint64 skipped_ = 0;
    quint64 rows_ = 0;
    std::map<QString, Entry> entries_;
};
#endif
