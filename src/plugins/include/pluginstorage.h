#pragma once

#include <QJsonArray>
#include <QSet>
#include <QStorageInfo>
#include <QVariantMap>

namespace cilogg::plugins {
class CaptureStorage {
public:
    explicit CaptureStorage( QString directory );
    QJsonArray fields() const;
    QVariantMap limits() const;
    QString setLimits( const QVariantMap& values );
    QVariantMap status( qint64 reserved ) const;
    QString checkWrite( qint64 captureSize, qint64 bytes, qint64 reserved ) const;
    qint64 converterReservation() const;
    void refreshSpace();
    void addBytes( qint64 delta );
    void replaceUsage( qint64 bytes );
    static qint64 directoryUsage( const QString& directory, const QSet<QString>& excluded = {} );
    const QString& directory() const
    {
        return directory_;
    }

private:
    QString directory_;
    QStorageInfo volume_;
    qint64 captureLimit_ = 1024LL * 1024 * 1024;
    qint64 totalLimit_ = 10LL * 1024 * 1024 * 1024;
    qint64 freeLimit_ = 512LL * 1024 * 1024;
    qint64 capturedBytes_ = 0;
    qint64 availableBytes_ = -1;
};
} // namespace cilogg::plugins
