#include "streamsourceregistry.h"

#include <QMutexLocker>

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

    QMutexLocker locker( &mutex_ );
    return activeSerialPorts_.contains( normalized );
}

void StreamSourceRegistry::registerSerialPort( const QString& portName )
{
    const auto normalized = normalizedPortName( portName );
    if ( normalized.isEmpty() ) {
        return;
    }

    QMutexLocker locker( &mutex_ );
    activeSerialPorts_.insert( normalized );
}

void StreamSourceRegistry::unregisterSerialPort( const QString& portName )
{
    const auto normalized = normalizedPortName( portName );
    if ( normalized.isEmpty() ) {
        return;
    }

    QMutexLocker locker( &mutex_ );
    activeSerialPorts_.remove( normalized );
}

QString StreamSourceRegistry::normalizedPortName( const QString& portName ) const
{
    return portName.trimmed().toUpper();
}
