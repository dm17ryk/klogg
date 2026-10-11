#pragma once

#include "pluginmanifest.h"
#include <QList>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <algorithm>

namespace cilogg::plugins {
class PluginProcess final : public QObject {
    Q_OBJECT
public:
    explicit PluginProcess( Manifest manifest, QObject* parent = nullptr );
    ~PluginProcess() override;
    void start( const QJsonObject& configuration, const QString& configDirectory,
                const QString& pythonExecutable );
    void request( const QString& id, const QString& method, const QJsonObject& params );
    void notify( const QString& method, const QJsonObject& params );
    void cancelRequest( const QString& id );
    void stop();
    bool waitForStopped( int milliseconds );
    QString state() const
    {
        return state_;
    }
    QString error() const
    {
        return error_;
    }
    QJsonObject descriptor() const
    {
        return descriptor_;
    }
    qint64 pid() const
    {
        return process_.processId();
    }
    void fail( const QString& error );
    bool isRunning() const
    {
        return process_.state() != QProcess::NotRunning;
    }
Q_SIGNALS:
    void changed();
    void message( const QJsonObject& message );
    void diagnostic( const QString& text );
    void stopped();

private:
    void send( const QJsonObject& message );
    void drain();
    void setState( const QString& state );
    Manifest manifest_;
    QProcess process_;
    QTimer deadline_;
    QByteArray input_;
    QList<QJsonObject> waiting_;
    QJsonObject descriptor_;
    QJsonObject initializeParams_;
    QString state_ = QStringLiteral( "stopped" );
    QString error_;
};
} // namespace cilogg::plugins
