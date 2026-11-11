#pragma once
#include "pluginprocess.h"
#include "pluginrepository.h"
#include "pluginstorage.h"
#include <QElapsedTimer>
#include <QFile>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QVariantMap>
#include <deque>
#include <map>
#include <memory>
#include <vector>

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
    QVariantMap storageStatus() const;
    QString dataRoot() const
    {
        return dataRoot_;
    }

    // Plugin views and message boxes (docs/plugin-views.md). The UI owns the widgets; the
    // manager owns the protocol, the open-view registry and the open-message limit.
    QJsonArray viewDefinitions( const QString& pluginId ) const;
    Result openView( const QString& pluginId, const QString& viewId, const QVariantMap& context );
    void closeView( const QString& pluginId, const QString& viewId, const QString& windowId );
    bool sendViewEvent( const QString& pluginId, const QString& viewId, const QString& windowId,
                        const QJsonObject& event );
    void finishMessage( const QString& pluginId, const QString& messageId, const QString& button,
                        bool timedOut );
    void noteActiveWindow( const QString& windowId );
    QVariantMap viewState( const QString& pluginId, const QString& viewId ) const;
    void setViewState( const QString& pluginId, const QString& viewId, const QVariantMap& state );
    QString instanceId() const
    {
        return instanceId_;
    }
    void setInstanceId( const QString& id );
    // Grace period between cancel_plugin_job and the forced `cancelled` state (default 10 s).
    void setCancelGraceMs( int milliseconds );
    // At most `count` new message boxes per plugin within `windowMs` (default 10 per 5 s).
    void setMessageRateLimit( int count, int windowMs );
    // Process state of a plugin ("stopped" when it has no process); empty when unknown.
    QString pluginState( const QString& pluginId ) const;

Q_SIGNALS:
    void changed();
    void fileReady( const QString& path, bool follow, const QVariantMap& context );
    void panelUpdated( const QString& pluginId, const QJsonObject& panel,
                       const QVariantMap& context );
    void pluginStopped( const QString& pluginId );
    // view/update, view/show, view/hide, ui/message and ui/close_message from a plugin.
    // view/show and ui/message always carry the resolved target `window_id`.
    void viewRequest( const QString& pluginId, const QString& method, const QJsonObject& params );
    // The plugin (re)initialized and was sent view/opened for its open views.
    void pluginReady( const QString& pluginId );
    // The plugin was disabled or removed: close its views without a result.
    void viewsRevoked( const QString& pluginId );

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
        bool restartStarted = false;
        bool outputSupervised = false;
        qint64 outputReservation = 0;
        qint64 observedOutputBytes = 0;
    };
    struct Stream {
        QString pluginId;
        QString jobId;
        QString path;
        QVariantMap context;
        std::unique_ptr<QFile> file;
        quint64 nextSequence = 0;
    };
    struct OpenView {
        QString pluginId;
        QString viewId;
        QString windowId;
        QVariantMap context;
    };
    struct OpenMessage {
        QString pluginId;
        QString messageId;
        QString windowId;
    };
    void handleUiMessage( const QString& pluginId, const QString& method,
                          const QJsonObject& params );
    Result cancelJob( const QString& id, bool grace );
    void rejectMessage( const QString& pluginId, const QString& messageId, const QString& error );
    void sendViewOpened( PluginProcess* process, const OpenView& view );
    void revokeViews( const QString& pluginId );
    QString resolveWindow( const QString& requested ) const;
    bool declaresView( const QString& pluginId, const QString& viewId ) const;
    QString addJob( const QString& pluginId, const QString& action, const QVariantMap& context = {},
                    bool management = false );
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
    void rememberSecretFields( const QString& id, const QJsonArray& fields );
    qint64 reservedCaptureBytes() const;
    void refreshCaptureUsage();
    void recordCaptureBytes( qint64 delta );
    void monitorStorage();
    void failStorageJob( QString pluginId, QString jobId, QString error );
    Result configureStorage( const QVariantMap& limits );
    QString dataRoot_;
    QString shippedRoot_;
    QSettings settings_;
    CaptureStorage storage_;
    QNetworkAccessManager network_;
    QNetworkAccessManager* networkAccess_;
    std::map<QString, std::unique_ptr<Entry>> entries_;
    std::map<QString, Job> jobs_;
    std::map<QString, Stream> streams_;
    std::vector<OpenView> openViews_;
    std::vector<OpenMessage> messages_;
    QStringList activeWindows_; // most recently active first
    QString instanceId_;
    int cancelGraceMs_ = 10000;
    std::map<QString, std::deque<qint64>> messageTimes_;
    QElapsedTimer clock_;
    int messageRateCount_ = 10;
    int messageRateWindowMs_ = 5000;
    QJsonArray catalog_;
    QString catalogJobId_;
    bool storageScanActive_ = false;
    int storageTicks_ = 0;
    quint64 storageScanGeneration_ = 0;
    bool shuttingDown_ = false;
};
} // namespace cilogg::plugins
