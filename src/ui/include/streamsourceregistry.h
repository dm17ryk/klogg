#pragma once

#include <QMutex>
#include <QSet>
#include <QString>

class StreamSourceRegistry {
  public:
    static StreamSourceRegistry& get();

    bool isSerialPortActive( const QString& portName ) const;
    void registerSerialPort( const QString& portName );
    void unregisterSerialPort( const QString& portName );

  private:
    StreamSourceRegistry() = default;
    QString normalizedPortName( const QString& portName ) const;

    mutable QMutex mutex_;
    QSet<QString> activeSerialPorts_;
};
