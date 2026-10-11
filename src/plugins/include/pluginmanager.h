#pragma once
#include "pluginprocess.h"
#include "pluginrepository.h"
#include <QFile>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QVariantMap>
#include <map>
#include <memory>

namespace cilogg::plugins {
struct Result {
    QString error;
    QVariantMap payload;
    bool ok() const
    {
        return error.isEmpty();
    }
};

class PluginManager final : public QObject {
    Q_OBJECT
public:
    explicit PluginManager( const QString& dataRoot = {}, const QString& shippedRoot = {},
                            QObject* parent = nullptr, QNetworkAccessManager* network = nullptr );
    ~PluginManager() override;
    void discover();
    QVariantList plugins() const;
    QVariantList jobs() const;
    Result manage( const QString& operation, const QString& pluginId, const QVariantMap& options );
    Result invoke( const QString& pluginId, const QString& action, const QVariantMap& arguments,
                   const QVariantMap& context );
    Result job( const QString& id ) const;
    Result cancel( const QString& id );
    void closeFile( const QString& path, const QString& windowId = {} );
    void closeWindow( const QString& windowId );
    void shutdown();
    QString catalogUrl() const;
    QString dataRoot() const
    {
        return dataRoot_;
    }
Q_SIGNALS:
    void changed();
    void fileReady( const QString& path, bool follow, const QVariantMap& context );
    void panelUpdated( const QString& pluginId, const QJsonObject& panel,
                       const QVariantMap& context );
    void pluginStopped( const QString& pluginId );

private:
    struct Entry {
        Manifest manifest;
        std::unique_ptr<PluginProcess> process;
        QString diagnostics;
    };
    struct Job {
        QString id;
        QString pluginId;
        QString action;
        QString state = QStringLiteral( "pending" );
        QString error;
        QJsonObject params;
        QVariantMap context;
        QVariantMap result;
        QString outputPath;
        double progress = 0;
        bool cancelRequested = false;
        bool invocation = false;
    };
    struct Stream {
        QString pluginId;
        QString jobId;
        QString path;
        QVariantMap context;
        std::unique_ptr<QFile> file;
        quint64 nextSequence = 0;
    };
    QString addJob( const QString& pluginId, const QString& action,
                    const QVariantMap& context = {} );
    QVariantMap jobPayload( const Job& job ) const;
    void finishJob( const QString& id, const QString& error, const QVariantMap& result = {} );
    PluginProcess* ensureProcess( Entry& entry );
    void dispatchJob( const QString& id );
    void completeManagement( const QString& operation, const QString& pluginId,
                             const QString& jobId );
    void handleMessage( const QString& pluginId, const QJsonObject& frame );
    void processStopped( const QString& pluginId );
    void stopStream( const QString& id, bool notify );
    void install( const QString& source, const QString& jobId, const QString& expectedId = {},
                  const QString& expectedVersion = {} );
    void commitWhenStopped( const StagedPlugin& stage, const QString& jobId );
    void fetch( const QString& url, const QString& jobId, const QString& sha256 = {},
                const QString& expectedVersion = {} );
    void loadCatalog( const QByteArray& bytes, const QString& jobId );
    QString configurationKey( const QString& id ) const;
    QString dataRoot_;
    QString shippedRoot_;
    QSettings settings_;
    QNetworkAccessManager network_;
    QNetworkAccessManager* networkAccess_;
    std::map<QString, std::unique_ptr<Entry>> entries_;
    std::map<QString, Job> jobs_;
    std::map<QString, Stream> streams_;
    QJsonArray catalog_;
    bool shuttingDown_ = false;
};
} // namespace cilogg::plugins
