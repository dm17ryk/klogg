#pragma once

#include "pluginmanifest.h"
#include <QList>
#include <QMap>
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
    // `environment` adds host variables (CILOGG_EXECUTABLE, CILOGG_INSTANCE_ID) to the child.
    void start( const QJsonObject& configuration, const QString& configDirectory,
                const QString& pythonExecutable, const QMap<QString, QString>& environment = {} );
    // The CILogg version announced in `initialize` (`host_version`); set before start().
    void setHostVersion( const QString& version )
    {
        hostVersion_ = version;
    }
    static constexpr int DefaultInitializationTimeoutMs = 10000;
    // How long the plugin may take to answer `initialize`; set before start().
    void setInitializationTimeout( int milliseconds )
    {
        initializationTimeoutMs_ = milliseconds;
    }
    // The program the last start() ran and its arguments, quoted where needed.
    QString commandLine() const;
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
    // The host-to-plugin methods the plugin announced in `features` (design 4.2); empty until
    // it is initialized.
    QStringList features() const
    {
        return features_;
    }
    qint64 pid() const
    {
        return process_.processId();
    }
    // Bytes written to the plugin's stdin that it has not consumed yet.
    qint64 pendingOutputBytes() const
    {
        return process_.bytesToWrite();
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
    // Every frame written to the plugin (`outgoing`) and every well-formed frame read from it, for
    // the golden-frame tests and the protocol log (docs/plugin-sdk-advanced.md sections 16, 20).
    void frameTransferred( bool outgoing, const QJsonObject& frame );

private:
    void send( const QJsonObject& message );
    void drain();
    void setState( const QString& state );
    // A timestamped line for the plugin's diagnostics (what the host did, next to its stderr).
    void note( const QString& text );
    QString initializationTimeoutError() const;
    Manifest manifest_;
    QString program_;
    qint64 stderrBytes_ = 0;
    int initializationTimeoutMs_ = DefaultInitializationTimeoutMs;
    QProcess process_;
    QTimer deadline_;
    QByteArray input_;
    QList<QJsonObject> waiting_;
    QJsonObject descriptor_;
    QStringList features_;
    QJsonObject initializeParams_;
    QString hostVersion_;
    QString state_ = QStringLiteral( "stopped" );
    QString error_;
};
} // namespace cilogg::plugins
