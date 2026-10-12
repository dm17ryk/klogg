#include "pluginstorage.h"
#include "log.h"
#include "pluginmanifest.h"
#include <QDirIterator>
#include <QFileInfo>
#include <limits>
#include <utility>

namespace cilogg::plugins {
CaptureStorage::CaptureStorage( QString directory )
    : directory_( std::move( directory ) )
{
}

QJsonArray CaptureStorage::fields() const
{
    QJsonArray result;
    const auto current = limits();
    for ( auto it = current.cbegin(); it != current.cend(); ++it ) {
        result.append( QJsonObject{ { "id", it.key() },
                                    { "type", "integer" },
                                    { "minimum", 0 },
                                    { "maximum", 9007199254740991.0 },
                                    { "default", QJsonValue::fromVariant( it.value() ) } } );
    }
    return result;
}

QVariantMap CaptureStorage::limits() const
{
    return { { "max_capture_bytes", captureLimit_ },
             { "max_total_bytes", totalLimit_ },
             { "min_free_bytes", freeLimit_ } };
}

QString CaptureStorage::setLimits( const QVariantMap& values )
{
    const auto previous = limits();
    for ( auto it = values.cbegin(); it != values.cend(); ++it ) {
        if ( !previous.contains( it.key() ) ) {
            return "Unknown capture storage limit: " + it.key();
        }
    }
    auto json = QJsonObject::fromVariantMap( values );
    const auto error = validateValues( fields(), &json );
    if ( !error.isEmpty() ) {
        return error;
    }
    const auto capture = static_cast<qint64>( json.value( "max_capture_bytes" ).toDouble() );
    const auto total = static_cast<qint64>( json.value( "max_total_bytes" ).toDouble() );
    if ( capture > 0 && total > 0 && capture > total ) {
        return "Total capture limit must be at least the per-capture limit.";
    }
    captureLimit_ = capture;
    totalLimit_ = total;
    freeLimit_ = static_cast<qint64>( json.value( "min_free_bytes" ).toDouble() );
    LOG_DEBUG << "Plugin storage limits capture=" << captureLimit_ << " total=" << totalLimit_
              << " free=" << freeLimit_;
    return {};
}

QVariantMap CaptureStorage::status( qint64 reserved ) const
{
    return { { "directory", directory_ },
             { "limits", limits() },
             { "fields", fields().toVariantList() },
             { "captured_bytes", capturedBytes_ },
             { "reserved_bytes", reserved },
             { "available_bytes", availableBytes_ } };
}

QString CaptureStorage::checkWrite( qint64 captureSize, qint64 bytes, qint64 reserved ) const
{
    if ( captureLimit_ > 0
         && ( captureSize > captureLimit_ || bytes > captureLimit_ - captureSize ) ) {
        return "Plugin per-capture disk limit reached.";
    }
    if ( totalLimit_ > 0
         && ( capturedBytes_ > totalLimit_ || reserved > totalLimit_ - capturedBytes_
              || bytes > totalLimit_ - capturedBytes_ - reserved ) ) {
        return "Plugin total capture disk limit reached.";
    }
    if ( freeLimit_ > 0 ) {
        if ( availableBytes_ < 0 ) {
            return "Cannot determine free space on the plugin capture volume.";
        }
        if ( availableBytes_ < freeLimit_ || reserved > availableBytes_ - freeLimit_
             || bytes > availableBytes_ - freeLimit_ - reserved ) {
            return "Plugin capture volume free-space reserve reached.";
        }
    }
    return {};
}

qint64 CaptureStorage::converterReservation() const
{
    return captureLimit_ > 0 ? captureLimit_
           : totalLimit_ > 0 ? qMax( qint64( 0 ), totalLimit_ - capturedBytes_ )
                             : 0;
}

void CaptureStorage::refreshSpace()
{
    if ( volume_.rootPath().isEmpty() ) {
        volume_.setPath( directory_ );
    }
    else {
        volume_.refresh();
    }
    availableBytes_ = volume_.isValid() && volume_.isReady() ? volume_.bytesAvailable() : -1;
}

void CaptureStorage::addBytes( qint64 delta )
{
    if ( delta > 0 ) {
        capturedBytes_ = delta > std::numeric_limits<qint64>::max() - capturedBytes_
                             ? std::numeric_limits<qint64>::max()
                             : capturedBytes_ + delta;
        if ( availableBytes_ >= 0 ) {
            availableBytes_ = qMax( qint64( 0 ), availableBytes_ - delta );
        }
    }
    else {
        capturedBytes_ = qMax( qint64( 0 ), capturedBytes_ + delta );
    }
}

void CaptureStorage::replaceUsage( qint64 bytes )
{
    capturedBytes_ = qMax( qint64( 0 ), bytes );
}

qint64 CaptureStorage::directoryUsage( const QString& directory, const QSet<QString>& excluded )
{
    qint64 total = 0;
    QDirIterator files( directory, QDir::Files | QDir::Hidden | QDir::NoSymLinks );
    while ( files.hasNext() ) {
        files.next();
        if ( excluded.contains( files.fileInfo().absoluteFilePath() ) ) {
            continue;
        }
        const auto size = qMax( qint64( 0 ), files.fileInfo().size() );
        if ( size > std::numeric_limits<qint64>::max() - total ) {
            return std::numeric_limits<qint64>::max();
        }
        total += size;
    }
    return total;
}
} // namespace cilogg::plugins
