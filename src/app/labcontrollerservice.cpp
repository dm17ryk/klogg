#include "labcontrollerservice.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QSet>
#include <QUuid>

namespace {

QString nowUtcString()
{
    return QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs );
}

QSqlDatabase controllerDatabase( const QString& connectionName )
{
    return QSqlDatabase::database( connectionName );
}

bool execQuery( QSqlQuery* query, QString* errorMessage )
{
    if ( query->exec() ) {
        return true;
    }

    if ( errorMessage != nullptr ) {
        *errorMessage = query->lastError().text();
    }
    return false;
}

QString variantMapToJson( const QVariantMap& map )
{
    return QString::fromUtf8( QJsonDocument::fromVariant( map ).toJson( QJsonDocument::Compact ) );
}

QVariantMap jsonToVariantMap( const QString& value )
{
    const auto document = QJsonDocument::fromJson( value.toUtf8() );
    return document.isObject() ? document.object().toVariantMap() : QVariantMap{};
}

QString variantListToJson( const QVariantList& values )
{
    return QString::fromUtf8( QJsonDocument::fromVariant( values ).toJson( QJsonDocument::Compact ) );
}

QVariantList jsonToVariantList( const QString& value )
{
    const auto document = QJsonDocument::fromJson( value.toUtf8() );
    return document.isArray() ? document.array().toVariantList() : QVariantList{};
}

QVariantMap queryRowToMap( const QSqlQuery& query )
{
    QVariantMap result;
    const auto record = query.record();
    for ( int index = 0; index < record.count(); ++index ) {
        result.insert( record.fieldName( index ), query.value( index ) );
    }
    return result;
}

} // namespace

struct LabControllerService::AgentConnectionState {
    QTcpSocket* socket = nullptr;
    QByteArray buffer;
    QString agentId;
    LabAgentConfig config;
    QString assignedJobId;
    QDateTime lastHeartbeat;
};

LabControllerService::LabControllerService( QObject* parent )
    : QObject( parent )
    , httpServer_( new QTcpServer( this ) )
    , agentServer_( new QTcpServer( this ) )
{
    connect( httpServer_, &QTcpServer::newConnection, this, &LabControllerService::handleHttpConnection );
    connect( agentServer_, &QTcpServer::newConnection, this, &LabControllerService::handleAgentConnection );
}

LabControllerService::~LabControllerService()
{
    stop();
}

bool LabControllerService::start( const LabCliRequest& request, QString* errorMessage )
{
    if ( !loadToken( request.tokenFilePath, errorMessage ) ) {
        return false;
    }
    if ( !initializeDatabase( request.stateDirPath, errorMessage ) ) {
        return false;
    }

    const auto anyAddress = request.listenAddress == QStringLiteral( "0.0.0.0" )
                                ? QHostAddress::Any
                                : QHostAddress( request.listenAddress );
    if ( !httpServer_->listen( anyAddress, request.listenPort ) ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = httpServer_->errorString();
        }
        return false;
    }
    if ( !agentServer_->listen( anyAddress, static_cast<quint16>( request.listenPort + 1 ) ) ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = agentServer_->errorString();
        }
        return false;
    }

    auto* timer = new QTimer( this );
    timer->setInterval( heartbeatTimeoutMs_ / 3 );
    connect( timer, &QTimer::timeout, this, &LabControllerService::pruneStaleAgents );
    timer->start();
    return true;
}

void LabControllerService::stop()
{
    if ( httpServer_ != nullptr ) {
        httpServer_->close();
    }
    if ( agentServer_ != nullptr ) {
        agentServer_->close();
    }

    for ( auto it = agentConnections_.begin(); it != agentConnections_.end(); ++it ) {
        if ( it.key() != nullptr ) {
            it.key()->disconnectFromHost();
        }
        delete it.value();
    }
    agentConnections_.clear();

    if ( !databaseConnectionName_.isEmpty() ) {
        auto database = controllerDatabase( databaseConnectionName_ );
        database.close();
        QSqlDatabase::removeDatabase( databaseConnectionName_ );
        databaseConnectionName_.clear();
    }
}

QVariantMap LabControllerService::submitBundle( const LabJobBundle& bundle,
                                                const QString& requestedAgentLabel,
                                                QString* errorMessage )
{
    return submitBundleUnlocked( bundle, requestedAgentLabel, errorMessage );
}

QVariantMap LabControllerService::queueSnapshot() const
{
    return queueSnapshotUnlocked();
}

QVariantMap LabControllerService::agentsSnapshot() const
{
    return agentsSnapshotUnlocked();
}

QVariantMap LabControllerService::jobStatus( const QString& jobId, QString* errorMessage ) const
{
    return jobStatusUnlocked( jobId, errorMessage );
}

QVariantMap LabControllerService::cancelJob( const QString& jobId, QString* errorMessage )
{
    auto database = controllerDatabase( databaseConnectionName_ );
    QSqlQuery query( database );
    query.prepare( "UPDATE jobs SET state = ?, updated_at = ?, last_error = ? "
                   "WHERE job_id = ? AND state IN ('queued','assigned','running')" );
    query.addBindValue( QStringLiteral( "cancelled" ) );
    query.addBindValue( nowUtcString() );
    query.addBindValue( QStringLiteral( "Cancelled by operator." ) );
    query.addBindValue( jobId );
    if ( !execQuery( &query, errorMessage ) ) {
        return {};
    }

    if ( query.numRowsAffected() == 0 ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = QStringLiteral( "Job was not found or is already finished." );
        }
        return {};
    }

    for ( auto* state : agentConnections_ ) {
        if ( state != nullptr && state->assignedJobId == jobId && state->socket != nullptr ) {
            sendAgentMessage( state->socket,
                              QVariantMap{ { QStringLiteral( "type" ), QStringLiteral( "cancel_job" ) },
                                           { QStringLiteral( "jobId" ), jobId } } );
        }
    }

    return jobStatusUnlocked( jobId, errorMessage );
}

QVariantMap LabControllerService::artifactsPayload( const QString& jobId, QString* errorMessage ) const
{
    return artifactsPayloadUnlocked( jobId, errorMessage );
}

QString LabControllerService::stateDirPath() const
{
    return stateDirPath_;
}

bool LabControllerService::initializeDatabase( const QString& stateDirPath, QString* errorMessage )
{
    QDir().mkpath( stateDirPath );
    stateDirPath_ = QFileInfo( stateDirPath ).absoluteFilePath();
    databaseConnectionName_ = QStringLiteral( "lab_controller_%1" )
                                  .arg( QUuid::createUuid().toString( QUuid::WithoutBraces ) );

    auto database = QSqlDatabase::addDatabase( QStringLiteral( "QSQLITE" ), databaseConnectionName_ );
    database.setDatabaseName( QDir( stateDirPath_ ).filePath( QStringLiteral( "lab_controller.sqlite" ) ) );
    if ( !database.open() ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }

    {
        QSqlQuery query( database );
        if ( !query.exec(
                 "CREATE TABLE IF NOT EXISTS agents ("
                 "agent_id TEXT PRIMARY KEY,"
                 "display_name TEXT,"
                 "labels_json TEXT,"
                 "status TEXT,"
                 "last_heartbeat TEXT,"
                 "inventory_json TEXT,"
                 "last_error TEXT)" ) ) {
            if ( errorMessage != nullptr ) {
                *errorMessage = query.lastError().text();
            }
            return false;
        }
    }

    {
        QSqlQuery query( database );
        if ( !query.exec(
                 "CREATE TABLE IF NOT EXISTS jobs ("
                 "job_id TEXT PRIMARY KEY,"
                 "state TEXT,"
                 "kind TEXT,"
                 "suite_name TEXT,"
                 "suite_id TEXT,"
                 "scenario_file TEXT,"
                 "created_at TEXT,"
                 "updated_at TEXT,"
                 "requested_agent_label TEXT,"
                 "assigned_agent_id TEXT,"
                 "wait_reason TEXT,"
                 "last_error TEXT,"
                 "output_tail_json TEXT,"
                 "bundle_json TEXT,"
                 "required_devices_json TEXT,"
                 "report_payload_json TEXT,"
                 "artifact_dir TEXT,"
                 "resolved_bindings_json TEXT)" ) ) {
            if ( errorMessage != nullptr ) {
                *errorMessage = query.lastError().text();
            }
            return false;
        }
    }

    QDir( stateDirPath_ ).mkpath( QStringLiteral( "jobs" ) );
    return true;
}

bool LabControllerService::loadToken( const QString& tokenFilePath, QString* errorMessage )
{
    QFile file( tokenFilePath );
    if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = QStringLiteral( "Failed to read token file %1: %2" )
                                .arg( tokenFilePath, file.errorString() );
        }
        return false;
    }

    sharedToken_ = file.readAll().trimmed();
    if ( sharedToken_.isEmpty() ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = QStringLiteral( "Token file must not be empty." );
        }
        return false;
    }
    return true;
}

bool LabControllerService::insertOrUpdateAgent( const LabAgentConfig& config, QString* errorMessage )
{
    auto database = controllerDatabase( databaseConnectionName_ );
    QSqlQuery query( database );
    query.prepare(
        "INSERT OR REPLACE INTO agents "
        "(agent_id, display_name, labels_json, status, last_heartbeat, inventory_json, last_error) "
        "VALUES (?, ?, ?, ?, ?, ?, COALESCE((SELECT last_error FROM agents WHERE agent_id = ?), ''))" );
    query.addBindValue( config.agentId );
    query.addBindValue( config.displayName );
    QVariantList labels;
    for ( const auto& label : config.labels ) {
        labels.push_back( label );
    }
    query.addBindValue( variantListToJson( labels ) );
    query.addBindValue( QStringLiteral( "online" ) );
    query.addBindValue( nowUtcString() );
    query.addBindValue( variantMapToJson( labAgentConfigToVariantMap( config ) ) );
    query.addBindValue( config.agentId );
    return execQuery( &query, errorMessage );
}

bool LabControllerService::updateAgentHeartbeat( const QString& agentId, QString* errorMessage )
{
    auto database = controllerDatabase( databaseConnectionName_ );
    QSqlQuery query( database );
    query.prepare( "UPDATE agents SET status = ?, last_heartbeat = ?, last_error = '' WHERE agent_id = ?" );
    query.addBindValue( QStringLiteral( "online" ) );
    query.addBindValue( nowUtcString() );
    query.addBindValue( agentId );
    return execQuery( &query, errorMessage );
}

bool LabControllerService::updateAgentStatus( const QString& agentId, const QString& status,
                                              const QString& lastError, QString* errorMessage )
{
    auto database = controllerDatabase( databaseConnectionName_ );
    QSqlQuery query( database );
    query.prepare( "UPDATE agents SET status = ?, last_error = ?, last_heartbeat = ? WHERE agent_id = ?" );
    query.addBindValue( status );
    query.addBindValue( lastError );
    query.addBindValue( nowUtcString() );
    query.addBindValue( agentId );
    return execQuery( &query, errorMessage );
}

QVariantMap LabControllerService::jobPayloadById( const QString& jobId, QString* errorMessage ) const
{
    auto database = controllerDatabase( databaseConnectionName_ );
    QSqlQuery query( database );
    query.prepare( "SELECT * FROM jobs WHERE job_id = ?" );
    query.addBindValue( jobId );
    if ( !execQuery( &query, errorMessage ) ) {
        return {};
    }
    if ( !query.next() ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = QStringLiteral( "Job %1 was not found." ).arg( jobId );
        }
        return {};
    }
    return queryRowToMap( query );
}

QString LabControllerService::jobDirectoryPath( const QString& jobId ) const
{
    return QDir( stateDirPath_ ).filePath( QStringLiteral( "jobs/%1" ).arg( jobId ) );
}

QString LabControllerService::artifactDirectoryPath( const QString& jobId ) const
{
    return QDir( jobDirectoryPath( jobId ) ).filePath( QStringLiteral( "artifacts" ) );
}

QString LabControllerService::nextJobId() const
{
    return QStringLiteral( "job-%1" )
        .arg( QUuid::createUuid().toString( QUuid::WithoutBraces ) );
}

bool LabControllerService::writeBundleToDisk( const QString& jobId, const LabJobBundle& bundle,
                                              QString* errorMessage ) const
{
    const auto bundleRoot = QDir( jobDirectoryPath( jobId ) ).filePath( QStringLiteral( "bundle" ) );
    QDir().mkpath( bundleRoot );
    for ( const auto& file : bundle.files ) {
        const auto targetPath
            = QFileInfo( QDir( bundleRoot ).filePath( file.relativePath ) ).absoluteFilePath();
        QDir().mkpath( QFileInfo( targetPath ).absolutePath() );
        QFile outputFile( targetPath );
        if ( !outputFile.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
            if ( errorMessage != nullptr ) {
                *errorMessage = QStringLiteral( "Failed to write bundle file %1: %2" )
                                    .arg( targetPath, outputFile.errorString() );
            }
            return false;
        }
        outputFile.write( file.content );
    }
    return true;
}

QVariantMap LabControllerService::submitBundleUnlocked( const LabJobBundle& bundle,
                                                        const QString& requestedAgentLabel,
                                                        QString* errorMessage )
{
    const auto jobId = nextJobId();
    const auto createdAt = nowUtcString();
    auto database = controllerDatabase( databaseConnectionName_ );
    QDir().mkpath( artifactDirectoryPath( jobId ) );
    if ( !writeBundleToDisk( jobId, bundle, errorMessage ) ) {
        return {};
    }

    QVariantList requiredDevices;
    for ( const auto& device : bundle.requiredDevices ) {
        requiredDevices.push_back( device );
    }

    QSqlQuery query( database );
    query.prepare(
        "INSERT INTO jobs "
        "(job_id, state, kind, suite_name, suite_id, scenario_file, created_at, updated_at, "
        "requested_agent_label, assigned_agent_id, wait_reason, last_error, output_tail_json, "
        "bundle_json, required_devices_json, report_payload_json, artifact_dir, resolved_bindings_json) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, '', '', '', '[]', ?, ?, '{}', ?, '[]')" );
    query.addBindValue( jobId );
    query.addBindValue( QStringLiteral( "queued" ) );
    query.addBindValue( bundle.kind );
    query.addBindValue( bundle.suiteName );
    query.addBindValue( bundle.suiteId );
    query.addBindValue( bundle.scenarioFile );
    query.addBindValue( createdAt );
    query.addBindValue( createdAt );
    query.addBindValue( requestedAgentLabel );
    query.addBindValue( variantMapToJson( labJobBundleToVariantMap( bundle ) ) );
    query.addBindValue( variantListToJson( requiredDevices ) );
    query.addBindValue( artifactDirectoryPath( jobId ) );
    if ( !execQuery( &query, errorMessage ) ) {
        return {};
    }

    scheduleJobs();
    return jobStatusUnlocked( jobId, errorMessage );
}

QVariantList LabControllerService::eligibleBindings( const LabJobBundle& bundle,
                                                     const LabAgentConfig& config ) const
{
    QVariantList bindings;
    QSet<QString> usedPorts;

    for ( const auto& requiredDeviceName : bundle.requiredDevices ) {
        auto deviceIt = std::find_if( bundle.logicalDevices.cbegin(), bundle.logicalDevices.cend(),
                                      [ &requiredDeviceName ]( const LabLogicalDeviceDefinition& device ) {
                                          return device.name == requiredDeviceName;
                                      } );
        const auto requiredTags
            = deviceIt != bundle.logicalDevices.cend() ? deviceIt->capabilityTags : QStringList{};

        const auto portIt = std::find_if(
            config.ports.cbegin(), config.ports.cend(),
            [ &requiredTags, &usedPorts ]( const LabAgentPortDefinition& port ) {
                if ( usedPorts.contains( port.portName ) ) {
                    return false;
                }
                for ( const auto& tag : requiredTags ) {
                    if ( !port.capabilityTags.contains( tag, Qt::CaseInsensitive ) ) {
                        return false;
                    }
                }
                return true;
            } );

        if ( portIt == config.ports.cend() ) {
            return {};
        }

        usedPorts.insert( portIt->portName );
        LabResolvedBinding binding;
        binding.logicalName = requiredDeviceName;
        binding.portName = portIt->portName;
        binding.displayName = portIt->displayName;
        binding.capabilityTags = portIt->capabilityTags;
        binding.settings = portIt->settings;
        if ( binding.settings.portName.isEmpty() ) {
            binding.settings.portName = binding.portName;
        }
        bindings.push_back( labResolvedBindingToVariantMap( binding ) );
    }

    return bindings;
}

bool LabControllerService::assignJobToAgent( const QString& jobId, const QVariantMap& jobRecord,
                                             const QString& agentId, const LabAgentConfig& config,
                                             QString* errorMessage )
{
    const auto bundle = labJobBundleFromVariantMap(
        jsonToVariantMap( jobRecord.value( QStringLiteral( "bundle_json" ) ).toString() ) );
    const auto bindings = eligibleBindings( bundle, config );
    if ( bindings.isEmpty() && !bundle.requiredDevices.isEmpty() ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = QStringLiteral( "waiting for devices" );
        }
        return false;
    }

    AgentConnectionState* state = nullptr;
    for ( auto* connection : agentConnections_ ) {
        if ( connection != nullptr && connection->agentId == agentId ) {
            state = connection;
            break;
        }
    }
    if ( state == nullptr || state->socket == nullptr ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = QStringLiteral( "agent is offline" );
        }
        return false;
    }

    auto database = controllerDatabase( databaseConnectionName_ );
    QSqlQuery query( database );
    query.prepare( "UPDATE jobs SET state = ?, assigned_agent_id = ?, wait_reason = ?, "
                   "resolved_bindings_json = ?, updated_at = ? WHERE job_id = ?" );
    query.addBindValue( QStringLiteral( "assigned" ) );
    query.addBindValue( agentId );
    query.addBindValue( QString{} );
    query.addBindValue( variantListToJson( bindings ) );
    query.addBindValue( nowUtcString() );
    query.addBindValue( jobId );
    if ( !execQuery( &query, errorMessage ) ) {
        return false;
    }

    sendAgentMessage(
        state->socket,
        QVariantMap{
            { QStringLiteral( "type" ), QStringLiteral( "assign_job" ) },
            { QStringLiteral( "jobId" ), jobId },
            { QStringLiteral( "bundle" ), labJobBundleToVariantMap( bundle ) },
            { QStringLiteral( "bindings" ), bindings },
            { QStringLiteral( "artifactDir" ), artifactDirectoryPath( jobId ) },
        } );
    return true;
}

void LabControllerService::scheduleJobs()
{
    auto database = controllerDatabase( databaseConnectionName_ );
    QSqlQuery query( database );
    if ( !query.exec( "SELECT * FROM jobs WHERE state = 'queued' ORDER BY created_at ASC" ) ) {
        return;
    }

    while ( query.next() ) {
        const auto job = queryRowToMap( query );
        const auto jobId = job.value( QStringLiteral( "job_id" ) ).toString();
        const auto bundle = labJobBundleFromVariantMap(
            jsonToVariantMap( job.value( QStringLiteral( "bundle_json" ) ).toString() ) );
        QString errorMessage;
        QString waitReason = QStringLiteral( "waiting for devices" );
        bool assigned = false;

        for ( auto* connection : agentConnections_ ) {
            if ( connection == nullptr || connection->socket == nullptr || connection->agentId.isEmpty()
                 || !connection->assignedJobId.isEmpty() ) {
                continue;
            }
            if ( !bundle.agentLabel.isEmpty()
                 && !connection->config.labels.contains( bundle.agentLabel, Qt::CaseInsensitive ) ) {
                continue;
            }

            if ( assignJobToAgent( jobId, job, connection->agentId, connection->config, &errorMessage ) ) {
                connection->assignedJobId = jobId;
                assigned = true;
                break;
            }
            if ( !errorMessage.isEmpty() ) {
                waitReason = errorMessage;
            }
        }

        if ( !assigned ) {
            QSqlQuery updateQuery( database );
            updateQuery.prepare( "UPDATE jobs SET wait_reason = ?, updated_at = ? WHERE job_id = ?" );
            updateQuery.addBindValue( waitReason );
            updateQuery.addBindValue( nowUtcString() );
            updateQuery.addBindValue( jobId );
            QString ignored;
            execQuery( &updateQuery, &ignored );
        }
    }
}

QVariantMap LabControllerService::queueSnapshotUnlocked() const
{
    QVariantMap payload;
    QVariantList jobs;
    auto database = controllerDatabase( databaseConnectionName_ );
    QSqlQuery query( database );
    if ( query.exec( "SELECT job_id, state, suite_name, suite_id, scenario_file, created_at, updated_at, "
                     "requested_agent_label, assigned_agent_id, wait_reason, last_error "
                     "FROM jobs ORDER BY created_at ASC" ) ) {
        while ( query.next() ) {
            QVariantMap item;
            item.insert( QStringLiteral( "jobId" ), query.value( 0 ) );
            item.insert( QStringLiteral( "state" ), query.value( 1 ) );
            item.insert( QStringLiteral( "suiteName" ), query.value( 2 ) );
            item.insert( QStringLiteral( "suiteId" ), query.value( 3 ) );
            item.insert( QStringLiteral( "scenarioFile" ), query.value( 4 ) );
            item.insert( QStringLiteral( "createdAt" ), query.value( 5 ) );
            item.insert( QStringLiteral( "updatedAt" ), query.value( 6 ) );
            item.insert( QStringLiteral( "requestedAgentLabel" ), query.value( 7 ) );
            item.insert( QStringLiteral( "assignedAgentId" ), query.value( 8 ) );
            item.insert( QStringLiteral( "waitReason" ), query.value( 9 ) );
            item.insert( QStringLiteral( "lastError" ), query.value( 10 ) );
            jobs.push_back( item );
        }
    }
    payload.insert( QStringLiteral( "jobs" ), jobs );
    return payload;
}

QVariantMap LabControllerService::agentsSnapshotUnlocked() const
{
    QVariantMap payload;
    QVariantList agents;
    auto database = controllerDatabase( databaseConnectionName_ );
    QSqlQuery query( database );
    if ( query.exec( "SELECT agent_id, display_name, labels_json, status, last_heartbeat, inventory_json, last_error "
                     "FROM agents ORDER BY agent_id ASC" ) ) {
        while ( query.next() ) {
            QVariantMap item;
            item.insert( QStringLiteral( "agentId" ), query.value( 0 ) );
            item.insert( QStringLiteral( "displayName" ), query.value( 1 ) );
            item.insert( QStringLiteral( "labels" ), jsonToVariantList( query.value( 2 ).toString() ) );
            item.insert( QStringLiteral( "status" ), query.value( 3 ) );
            item.insert( QStringLiteral( "lastHeartbeat" ), query.value( 4 ) );
            item.insert( QStringLiteral( "inventory" ),
                         jsonToVariantMap( query.value( 5 ).toString() ) );
            item.insert( QStringLiteral( "lastError" ), query.value( 6 ) );
            agents.push_back( item );
        }
    }
    payload.insert( QStringLiteral( "agents" ), agents );
    return payload;
}

QVariantMap LabControllerService::jobStatusUnlocked( const QString& jobId, QString* errorMessage ) const
{
    const auto job = jobPayloadById( jobId, errorMessage );
    if ( job.isEmpty() ) {
        return {};
    }

    QVariantMap payload;
    payload.insert( QStringLiteral( "jobId" ), jobId );
    payload.insert( QStringLiteral( "state" ), job.value( QStringLiteral( "state" ) ) );
    payload.insert( QStringLiteral( "suiteName" ), job.value( QStringLiteral( "suite_name" ) ) );
    payload.insert( QStringLiteral( "suiteId" ), job.value( QStringLiteral( "suite_id" ) ) );
    payload.insert( QStringLiteral( "scenarioFile" ), job.value( QStringLiteral( "scenario_file" ) ) );
    payload.insert( QStringLiteral( "createdAt" ), job.value( QStringLiteral( "created_at" ) ) );
    payload.insert( QStringLiteral( "updatedAt" ), job.value( QStringLiteral( "updated_at" ) ) );
    payload.insert( QStringLiteral( "assignedAgentId" ),
                    job.value( QStringLiteral( "assigned_agent_id" ) ) );
    payload.insert( QStringLiteral( "waitReason" ), job.value( QStringLiteral( "wait_reason" ) ) );
    payload.insert( QStringLiteral( "lastError" ), job.value( QStringLiteral( "last_error" ) ) );
    payload.insert( QStringLiteral( "artifactDir" ), job.value( QStringLiteral( "artifact_dir" ) ) );
    payload.insert( QStringLiteral( "outputTail" ),
                    jsonToVariantList( job.value( QStringLiteral( "output_tail_json" ) ).toString() ) );
    payload.insert( QStringLiteral( "resolvedBindings" ),
                    jsonToVariantList( job.value( QStringLiteral( "resolved_bindings_json" ) ).toString() ) );
    payload.insert( QStringLiteral( "report" ),
                    jsonToVariantMap( job.value( QStringLiteral( "report_payload_json" ) ).toString() ) );
    return payload;
}

QVariantMap LabControllerService::artifactsPayloadUnlocked( const QString& jobId,
                                                            QString* errorMessage ) const
{
    const auto job = jobPayloadById( jobId, errorMessage );
    if ( job.isEmpty() ) {
        return {};
    }

    QVariantMap payload;
    payload.insert( QStringLiteral( "jobId" ), jobId );
    payload.insert( QStringLiteral( "artifactDir" ), job.value( QStringLiteral( "artifact_dir" ) ) );
    QVariantList artifacts;
    const QDir artifactDir( job.value( QStringLiteral( "artifact_dir" ) ).toString() );
    for ( const auto& fileInfo :
          artifactDir.entryInfoList( QDir::Files | QDir::NoDotAndDotDot, QDir::Name ) ) {
        QFile file( fileInfo.absoluteFilePath() );
        if ( !file.open( QIODevice::ReadOnly ) ) {
            continue;
        }
        QVariantMap item;
        item.insert( QStringLiteral( "name" ), fileInfo.fileName() );
        item.insert( QStringLiteral( "path" ), fileInfo.absoluteFilePath() );
        item.insert( QStringLiteral( "dataBase64" ),
                     QString::fromLatin1( file.readAll().toBase64( QByteArray::Base64Encoding ) ) );
        artifacts.push_back( item );
    }
    payload.insert( QStringLiteral( "artifacts" ), artifacts );
    return payload;
}

QVariantMap LabControllerService::loadBundlePayload( const QString& jobId ) const
{
    QString ignored;
    const auto job = jobPayloadById( jobId, &ignored );
    return jsonToVariantMap( job.value( QStringLiteral( "bundle_json" ) ).toString() );
}

QByteArray httpReasonPhrase( int statusCode )
{
    switch ( statusCode ) {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 404:
        return "Not Found";
    case 409:
        return "Conflict";
    case 500:
        return "Internal Server Error";
    default:
        return "Error";
    }
}

bool requestIsComplete( const QByteArray& request )
{
    const auto headerEnd = request.indexOf( "\r\n\r\n" );
    if ( headerEnd < 0 ) {
        return false;
    }

    const auto headers = request.left( headerEnd );
    const auto contentLengthIndex = headers.toLower().indexOf( "content-length:" );
    if ( contentLengthIndex < 0 ) {
        return true;
    }

    const auto lineEnd = headers.indexOf( "\r\n", contentLengthIndex );
    const auto line
        = headers.mid( contentLengthIndex, lineEnd < 0 ? -1 : lineEnd - contentLengthIndex );
    bool ok = false;
    const auto contentLength
        = line.mid( QByteArray( "content-length:" ).size() ).trimmed().toInt( &ok );
    if ( !ok ) {
        return true;
    }

    return request.size() >= headerEnd + 4 + contentLength;
}

QVariantMap parseJsonBody( const QByteArray& request )
{
    const auto headerEnd = request.indexOf( "\r\n\r\n" );
    if ( headerEnd < 0 ) {
        return {};
    }

    const auto body = request.mid( headerEnd + 4 );
    const auto document = QJsonDocument::fromJson( body );
    return document.isObject() ? document.object().toVariantMap() : QVariantMap{};
}

void LabControllerService::sendJsonResponse( QTcpSocket* socket, int statusCode,
                                             const QVariantMap& payload ) const
{
    if ( socket == nullptr ) {
        return;
    }

    const auto body = QJsonDocument::fromVariant( payload ).toJson( QJsonDocument::Compact );
    QByteArray response;
    response += "HTTP/1.1 ";
    response += QByteArray::number( statusCode );
    response += ' ';
    response += httpReasonPhrase( statusCode );
    response += "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ";
    response += QByteArray::number( body.size() );
    response += "\r\n\r\n";
    response += body;
    socket->write( response );
    socket->disconnectFromHost();
}

bool LabControllerService::authorizeHttpRequest( const QByteArray& headers,
                                                 QString* errorMessage ) const
{
    const auto lines = headers.split( '\n' );
    for ( const auto& rawLine : lines ) {
        const auto line = rawLine.trimmed();
        const auto lowerLine = line.toLower();
        if ( lowerLine.startsWith( "x-klogg-token:" )
             && line.mid( QByteArray( "X-Klogg-Token:" ).size() ).trimmed() == sharedToken_ ) {
            return true;
        }
        if ( lowerLine.startsWith( "authorization:" ) ) {
            const auto value = line.mid( QByteArray( "Authorization:" ).size() ).trimmed();
            if ( value.toLower().startsWith( "bearer " )
                 && value.mid( QByteArray( "Bearer " ).size() ) == sharedToken_ ) {
                return true;
            }
        }
    }

    if ( errorMessage != nullptr ) {
        *errorMessage = QStringLiteral( "Invalid or missing shared token." );
    }
    return false;
}

void LabControllerService::handleHttpConnection()
{
    while ( httpServer_->hasPendingConnections() ) {
        auto* socket = httpServer_->nextPendingConnection();
        connect( socket, &QTcpSocket::readyRead, this, [ this, socket ]() {
            auto buffer = socket->property( "labHttpBuffer" ).toByteArray();
            buffer.append( socket->readAll() );
            socket->setProperty( "labHttpBuffer", buffer );
            if ( requestIsComplete( buffer ) ) {
                processHttpRequest( socket, buffer );
            }
        } );
        connect( socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater );
    }
}

void LabControllerService::processHttpRequest( QTcpSocket* socket, const QByteArray& rawRequest )
{
    const auto headerEnd = rawRequest.indexOf( "\r\n\r\n" );
    const auto headerBytes = headerEnd < 0 ? rawRequest : rawRequest.left( headerEnd );
    QString authError;
    if ( !authorizeHttpRequest( headerBytes, &authError ) ) {
        sendJsonResponse( socket, 401,
                          QVariantMap{ { QStringLiteral( "error" ), authError } } );
        return;
    }

    const auto requestLineEnd = headerBytes.indexOf( "\r\n" );
    const auto requestLine = headerBytes.left( requestLineEnd );
    const auto parts = requestLine.split( ' ' );
    if ( parts.size() < 2 ) {
        sendJsonResponse( socket, 400,
                          QVariantMap{ { QStringLiteral( "error" ),
                                         QStringLiteral( "Invalid HTTP request line." ) } } );
        return;
    }

    const auto method = QString::fromLatin1( parts.at( 0 ) ).trimmed().toUpper();
    const auto url = QUrl( QString::fromLatin1( parts.at( 1 ) ) );
    const auto path = url.path();
    const auto query = QUrlQuery( url );
    QString errorMessage;

    if ( method == QStringLiteral( "POST" ) && path == QStringLiteral( "/api/lab/submit" ) ) {
        const auto body = parseJsonBody( rawRequest );
        const auto bundle = labJobBundleFromVariantMap( body.value( QStringLiteral( "bundle" ) ).toMap() );
        const auto payload
            = submitBundleUnlocked( bundle, body.value( QStringLiteral( "requestedAgentLabel" ) ).toString(),
                                    &errorMessage );
        if ( payload.isEmpty() ) {
            sendJsonResponse( socket, 400, QVariantMap{ { QStringLiteral( "error" ), errorMessage } } );
            return;
        }
        sendJsonResponse( socket, 201, payload );
        return;
    }

    if ( method == QStringLiteral( "GET" ) && path == QStringLiteral( "/api/lab/queue" ) ) {
        sendJsonResponse( socket, 200, queueSnapshotUnlocked() );
        return;
    }

    if ( method == QStringLiteral( "GET" ) && path == QStringLiteral( "/api/lab/status" ) ) {
        const auto payload = jobStatusUnlocked( query.queryItemValue( QStringLiteral( "jobId" ) ),
                                                &errorMessage );
        if ( payload.isEmpty() ) {
            sendJsonResponse( socket, 404, QVariantMap{ { QStringLiteral( "error" ), errorMessage } } );
            return;
        }
        sendJsonResponse( socket, 200, payload );
        return;
    }

    if ( method == QStringLiteral( "POST" ) && path == QStringLiteral( "/api/lab/cancel" ) ) {
        const auto body = parseJsonBody( rawRequest );
        const auto payload = cancelJob( body.value( QStringLiteral( "jobId" ) ).toString(),
                                        &errorMessage );
        if ( payload.isEmpty() ) {
            sendJsonResponse( socket, 404, QVariantMap{ { QStringLiteral( "error" ), errorMessage } } );
            return;
        }
        sendJsonResponse( socket, 200, payload );
        return;
    }

    if ( method == QStringLiteral( "GET" ) && path == QStringLiteral( "/api/lab/agents" ) ) {
        sendJsonResponse( socket, 200, agentsSnapshotUnlocked() );
        return;
    }

    if ( method == QStringLiteral( "GET" ) && path == QStringLiteral( "/api/lab/artifacts" ) ) {
        const auto payload = artifactsPayloadUnlocked(
            query.queryItemValue( QStringLiteral( "jobId" ) ), &errorMessage );
        if ( payload.isEmpty() ) {
            sendJsonResponse( socket, 404, QVariantMap{ { QStringLiteral( "error" ), errorMessage } } );
            return;
        }
        sendJsonResponse( socket, 200, payload );
        return;
    }

    if ( method == QStringLiteral( "GET" ) && path == QStringLiteral( "/api/lab/snapshot" ) ) {
        QVariantMap payload;
        payload.insert( QStringLiteral( "agents" ),
                        agentsSnapshotUnlocked().value( QStringLiteral( "agents" ) ) );
        payload.insert( QStringLiteral( "jobs" ),
                        queueSnapshotUnlocked().value( QStringLiteral( "jobs" ) ) );
        payload.insert( QStringLiteral( "stateDir" ), stateDirPath_ );
        sendJsonResponse( socket, 200, payload );
        return;
    }

    sendJsonResponse( socket, 404,
                      QVariantMap{ { QStringLiteral( "error" ), QStringLiteral( "Unknown endpoint." ) } } );
}

void LabControllerService::handleAgentConnection()
{
    while ( agentServer_->hasPendingConnections() ) {
        auto* socket = agentServer_->nextPendingConnection();
        auto* state = new AgentConnectionState();
        state->socket = socket;
        agentConnections_.insert( socket, state );
        connect( socket, &QTcpSocket::readyRead, this, &LabControllerService::readAgentSocket );
        connect( socket, &QTcpSocket::disconnected, this, &LabControllerService::handleAgentDisconnected );
    }
}

void LabControllerService::handleAgentDisconnected()
{
    auto* socket = qobject_cast<QTcpSocket*>( sender() );
    auto* state = connectionStateForSocket( socket );
    if ( state == nullptr ) {
        return;
    }

    QString ignored;
    updateAgentStatus( state->agentId, QStringLiteral( "offline" ),
                       QStringLiteral( "Agent disconnected." ), &ignored );
    if ( !state->assignedJobId.isEmpty() ) {
        auto database = controllerDatabase( databaseConnectionName_ );
        QSqlQuery query( database );
        query.prepare( "UPDATE jobs SET state = ?, updated_at = ?, last_error = ? "
                       "WHERE job_id = ? AND state IN ('assigned','running')" );
        query.addBindValue( QStringLiteral( "failed_infrastructure" ) );
        query.addBindValue( nowUtcString() );
        query.addBindValue( QStringLiteral( "Assigned agent disconnected before completion." ) );
        query.addBindValue( state->assignedJobId );
        execQuery( &query, &ignored );
    }

    agentConnections_.remove( socket );
    delete state;
    socket->deleteLater();
    scheduleJobs();
}

void LabControllerService::readAgentSocket()
{
    auto* socket = qobject_cast<QTcpSocket*>( sender() );
    auto* state = connectionStateForSocket( socket );
    if ( socket == nullptr || state == nullptr ) {
        return;
    }

    state->buffer.append( socket->readAll() );
    while ( true ) {
        const auto newlineIndex = state->buffer.indexOf( '\n' );
        if ( newlineIndex < 0 ) {
            break;
        }

        const auto line = state->buffer.left( newlineIndex ).trimmed();
        state->buffer.remove( 0, newlineIndex + 1 );
        if ( line.isEmpty() ) {
            continue;
        }

        const auto document = QJsonDocument::fromJson( line );
        if ( !document.isObject() ) {
            continue;
        }
        processAgentMessage( socket, document.object().toVariantMap() );
    }
}

void LabControllerService::pruneStaleAgents()
{
    const auto now = QDateTime::currentDateTimeUtc();
    for ( auto* state : agentConnections_ ) {
        if ( state == nullptr || state->agentId.isEmpty() || !state->lastHeartbeat.isValid() ) {
            continue;
        }

        if ( state->lastHeartbeat.msecsTo( now ) > heartbeatTimeoutMs_ && state->socket != nullptr ) {
            state->socket->disconnectFromHost();
        }
    }
}

void LabControllerService::processAgentMessage( QTcpSocket* socket, const QVariantMap& message )
{
    if ( socket == nullptr ) {
        return;
    }

    if ( message.value( QStringLiteral( "token" ) ).toByteArray() != sharedToken_ ) {
        socket->disconnectFromHost();
        return;
    }

    auto* state = connectionStateForSocket( socket );
    if ( state == nullptr ) {
        return;
    }

    const auto type = message.value( QStringLiteral( "type" ) ).toString();
    QString errorMessage;

    if ( type == QStringLiteral( "register" ) ) {
        state->config = labAgentConfigFromVariantMap( message.value( QStringLiteral( "agent" ) ).toMap() );
        state->agentId = state->config.agentId;
        state->lastHeartbeat = QDateTime::currentDateTimeUtc();
        insertOrUpdateAgent( state->config, &errorMessage );
        sendAgentMessage( socket,
                          QVariantMap{ { QStringLiteral( "type" ), QStringLiteral( "register_ack" ) },
                                       { QStringLiteral( "ok" ), errorMessage.isEmpty() },
                                       { QStringLiteral( "error" ), errorMessage } } );
        scheduleJobs();
        return;
    }

    if ( type == QStringLiteral( "heartbeat" ) ) {
        state->lastHeartbeat = QDateTime::currentDateTimeUtc();
        updateAgentHeartbeat( state->agentId, &errorMessage );
        return;
    }

    if ( type == QStringLiteral( "job_update" ) ) {
        QSqlQuery query( controllerDatabase( databaseConnectionName_ ) );
        query.prepare( "UPDATE jobs SET state = ?, updated_at = ?, last_error = ?, output_tail_json = ? "
                       "WHERE job_id = ?" );
        query.addBindValue( message.value( QStringLiteral( "state" ) ).toString() );
        query.addBindValue( nowUtcString() );
        query.addBindValue( message.value( QStringLiteral( "lastError" ) ).toString() );
        query.addBindValue( variantListToJson( message.value( QStringLiteral( "outputTail" ) ).toList() ) );
        query.addBindValue( message.value( QStringLiteral( "jobId" ) ).toString() );
        execQuery( &query, &errorMessage );
        return;
    }

    if ( type == QStringLiteral( "job_complete" ) ) {
        const auto jobId = message.value( QStringLiteral( "jobId" ) ).toString();
        const auto artifactDir = artifactDirectoryPath( jobId );
        QDir().mkpath( artifactDir );
        for ( const auto& artifactValue : message.value( QStringLiteral( "artifacts" ) ).toList() ) {
            const auto artifact = artifactValue.toMap();
            QFile file( QDir( artifactDir ).filePath( artifact.value( QStringLiteral( "name" ) ).toString() ) );
            if ( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
                file.write( QByteArray::fromBase64( artifact.value( QStringLiteral( "dataBase64" ) ).toByteArray(),
                                                   QByteArray::Base64Encoding ) );
            }
        }

        QSqlQuery query( controllerDatabase( databaseConnectionName_ ) );
        query.prepare( "UPDATE jobs SET state = ?, updated_at = ?, last_error = ?, output_tail_json = ?, "
                       "report_payload_json = ?, artifact_dir = ?, resolved_bindings_json = ? "
                       "WHERE job_id = ?" );
        query.addBindValue( message.value( QStringLiteral( "status" ) ).toString() );
        query.addBindValue( nowUtcString() );
        query.addBindValue( message.value( QStringLiteral( "lastError" ) ).toString() );
        query.addBindValue( variantListToJson( message.value( QStringLiteral( "outputTail" ) ).toList() ) );
        query.addBindValue( variantMapToJson( message.value( QStringLiteral( "reportPayload" ) ).toMap() ) );
        query.addBindValue( artifactDir );
        query.addBindValue( variantListToJson( message.value( QStringLiteral( "resolvedBindings" ) ).toList() ) );
        query.addBindValue( jobId );
        execQuery( &query, &errorMessage );

        state->assignedJobId.clear();
        scheduleJobs();
    }
}

void LabControllerService::sendAgentMessage( QTcpSocket* socket, const QVariantMap& message ) const
{
    if ( socket == nullptr ) {
        return;
    }

    socket->write( QJsonDocument::fromVariant( message ).toJson( QJsonDocument::Compact ) );
    socket->write( "\n" );
    socket->flush();
}

LabControllerService::AgentConnectionState* LabControllerService::connectionStateForSocket(
    QTcpSocket* socket )
{
    return agentConnections_.value( socket, nullptr );
}

const LabControllerService::AgentConnectionState* LabControllerService::connectionStateForSocket(
    QTcpSocket* socket ) const
{
    return agentConnections_.value( socket, nullptr );
}
