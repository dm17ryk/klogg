#pragma once

#include "communicationtypes.h"
#include <QHash>
#include <QMutex>
#include <QString>
#include <QUuid>
#include <QVector>
#include <functional>

class SerialEndpointLease {
public:
    const QString& canonicalKey() const
    {
        return key_;
    }
    quint64 leaseId() const
    {
        return id_;
    }

private:
    friend class StreamSourceRegistry;
    SerialEndpointLease( const QString& key, QUuid registry, quint64 id )
        : key_( key.constData(), key.size() )
        , registry_( registry )
        , id_( id )
    {
    }
    QString key_;
    QUuid registry_;
    quint64 id_;
};
struct SerialPortAlias {
    QString portName;
    QString systemLocation;
};

class StreamSourceRegistry {
public:
    static StreamSourceRegistry& get();

    bool isSerialPortActive( const QString& portName ) const;
    void registerSerialPort( const QString& portName );
    void unregisterSerialPort( const QString& portName );
    DataResult<SerialEndpointLease> tryAcquireSerialPort( const QString&, const StreamLimits& );
    cilogg::DataStatus releaseSerialPort( const SerialEndpointLease& );

private:
    friend struct StreamSourceRegistryTestAccess;
    using Inventory = std::function<QVector<SerialPortAlias>()>;
    using PathResolver = std::function<QString( const QString& )>;
    StreamSourceRegistry();
    StreamSourceRegistry( bool windows, Inventory, PathResolver );
    DataResult<QString> canonicalPortName( const QString& ) const;
    QString normalizedPortName( const QString& portName ) const;

    mutable QMutex mutex_;
    struct Entry {
        quint64 leaseId = 0;
    };
    QHash<QString, Entry> entries_;
    const QUuid registryId_ = QUuid::createUuid();
    quint64 nextLeaseId_ = 1;
    bool windows_;
    Inventory inventory_;
    PathResolver pathResolver_;
};
