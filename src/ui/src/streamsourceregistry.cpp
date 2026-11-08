#include "streamsourceregistry.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QSerialPortInfo>
#include <QSet>
#include <limits>

Q_LOGGING_CATEGORY( streamRegistryLog, "cilogg.communication.registry" )
namespace {
DataError registryError( const char* code )
{
    return {
        QString::fromLatin1( code ), "Serial endpoint registry rejected the operation", {}, {}
    };
}
#ifdef Q_OS_WIN
constexpr bool windowsPlatform = true;
#else
constexpr bool windowsPlatform = false;
#endif
} // namespace
StreamSourceRegistry::StreamSourceRegistry()
    : StreamSourceRegistry(
          windowsPlatform,
          [] {
              QVector<SerialPortAlias> result;
              for ( const auto& port : QSerialPortInfo::availablePorts() )
                  result.push_back( { port.portName(), port.systemLocation() } );
              return result;
          },
          []( const QString& path ) {
              const auto canonical = QFileInfo( path ).canonicalFilePath();
              return canonical.isEmpty() ? QDir::cleanPath( path ) : canonical;
          } )
{
}
StreamSourceRegistry::StreamSourceRegistry( bool windows, Inventory inventory, PathResolver paths )
    : windows_( windows )
    , inventory_( std::move( inventory ) )
    , pathResolver_( std::move( paths ) )
{
}
DataResult<QString> StreamSourceRegistry::canonicalPortName( const QString& selector ) const
{
    const auto raw = selector.trimmed();
    if ( raw.isEmpty() || raw.size() > 4096 || raw.contains( QChar( 0 ) ) || raw.contains( '\r' )
         || raw.contains( '\n' ) )
        return { {}, registryError( "invalid_endpoint" ) };
    const auto normalize = [ this ]( QString path ) {
        path = path.trimmed();
        if ( windows_ ) {
            if ( path.startsWith( QStringLiteral( "\\\\.\\" ) )
                 || path.startsWith( QStringLiteral( "\\\\?\\" ) ) )
                path.remove( 0, 4 );
            return path.toUpper();
        }
        if ( !path.startsWith( '/' ) )
            path.prepend( "/dev/" );
        return QDir::cleanPath( path );
    };
    const auto keyFor = [ this, &normalize ]( const QString& path ) {
        auto normalized = normalize( path );
        if ( windows_ )
            return QStringLiteral( "serial-win:" ) + normalized;
        if ( pathResolver_ )
            normalized = pathResolver_( normalized );
        return QStringLiteral( "serial-unix:" ) + normalized;
    };
    const auto normalized = normalize( raw );
    QSet<QString> candidates;
    if ( inventory_ ) {
        for ( const auto& port : inventory_() ) {
            if ( ( !port.portName.isEmpty() && normalize( port.portName ) == normalized )
                 || ( !port.systemLocation.isEmpty()
                      && normalize( port.systemLocation ) == normalized ) )
                candidates.insert(
                    keyFor( port.systemLocation.isEmpty() ? port.portName : port.systemLocation ) );
        }
    }
    if ( candidates.size() > 1 )
        return { {}, registryError( "ambiguous_endpoint" ) };
    const auto key = candidates.isEmpty() ? keyFor( raw ) : *candidates.cbegin();
    if ( key.size() > 4120 )
        return { {}, registryError( "invalid_endpoint" ) };
    return { key, {} };
}
DataResult<SerialEndpointLease>
StreamSourceRegistry::tryAcquireSerialPort( const QString& selector, const StreamLimits& limits )
{
    const auto canonical = canonicalPortName( selector );
    if ( !canonical.value ) {
        qCDebug( streamRegistryLog, "Acquire rejected reason=%s",
                 qPrintable( canonical.error.code ) );
        return { {}, canonical.error };
    }
    DataResult<SerialEndpointLease> result;
    qsizetype count;
    {
        QMutexLocker lock( &mutex_ );
        if ( entries_.contains( *canonical.value ) )
            result.error = registryError( "endpoint_busy" );
        else if ( static_cast<quint64>( entries_.size() ) >= limits.maximumEndpoints()
                  || nextLeaseId_ == std::numeric_limits<quint64>::max() )
            result.error = registryError( "resource_limit" );
        else {
            const auto id = nextLeaseId_++;
            entries_.insert( *canonical.value, Entry{ id } );
            result.value = SerialEndpointLease( *canonical.value, registryId_, id );
        }
        count = entries_.size();
    }
    qCDebug( streamRegistryLog, "Acquire ok=%d lease=%llu endpoints=%lld reason=%s",
             result.value.has_value(),
             static_cast<unsigned long long>( result.value ? result.value->leaseId() : 0 ),
             static_cast<long long>( count ), qPrintable( result.error.code ) );
    return result;
}
DataStatus StreamSourceRegistry::releaseSerialPort( const SerialEndpointLease& lease )
{
    DataStatus result;
    qsizetype count;
    {
        QMutexLocker lock( &mutex_ );
        const auto found = entries_.find( lease.key_ );
        if ( lease.registry_ != registryId_ || lease.id_ == 0 || lease.id_ >= nextLeaseId_ )
            result.error = registryError( "foreign_lease" );
        else if ( found != entries_.end() && found->leaseId != lease.id_ )
            result.error = registryError( "stale_lease" );
        else {
            if ( found != entries_.end() )
                entries_.erase( found );
            result.ok = true;
        }
        count = entries_.size();
    }
    qCDebug( streamRegistryLog, "Release ok=%d lease=%llu endpoints=%lld reason=%s", result.ok,
             static_cast<unsigned long long>( lease.id_ ), static_cast<long long>( count ),
             qPrintable( result.error.code ) );
    return result;
}

StreamSourceRegistry& StreamSourceRegistry::get()
{
    static StreamSourceRegistry registry;
    return registry;
}

bool StreamSourceRegistry::isSerialPortActive( const QString& portName ) const
{
    const auto normalized = normalizedPortName( portName );
    if ( normalized.isEmpty() ) {
        return false;
    }

    bool active;
    {
        QMutexLocker locker( &mutex_ );
        active = entries_.contains( normalized );
    }
    qCDebug( streamRegistryLog, "Legacy active query result=%d", active );
    return active;
}

void StreamSourceRegistry::registerSerialPort( const QString& portName )
{
    const auto normalized = normalizedPortName( portName );
    if ( normalized.isEmpty() ) {
        return;
    }

    bool protectedLease;
    {
        QMutexLocker locker( &mutex_ );
        const auto found = entries_.constFind( normalized );
        protectedLease = found != entries_.cend() && found->leaseId != 0;
        if ( !protectedLease )
            entries_.insert( normalized, Entry{} );
    }
    qCDebug( streamRegistryLog, "Legacy register protectedLease=%d", protectedLease );
}

void StreamSourceRegistry::unregisterSerialPort( const QString& portName )
{
    const auto normalized = normalizedPortName( portName );
    if ( normalized.isEmpty() ) {
        return;
    }

    bool protectedLease;
    {
        QMutexLocker locker( &mutex_ );
        const auto found = entries_.find( normalized );
        protectedLease = found != entries_.end() && found->leaseId != 0;
        if ( found != entries_.end() && !protectedLease )
            entries_.erase( found );
    }
    qCDebug( streamRegistryLog, "Legacy unregister protectedLease=%d", protectedLease );
}

QString StreamSourceRegistry::normalizedPortName( const QString& portName ) const
{
    const auto result = canonicalPortName( portName );
    if ( !result.value )
        qCDebug( streamRegistryLog, "Canonicalization rejected reason=%s",
                 qPrintable( result.error.code ) );
    return result.value.value_or( QString{} );
}
