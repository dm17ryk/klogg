#include "labagentrunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include "kloggapp.h"
#include "labbundleutils.h"
#include "log.h"
#include "scenarioheadlessrunner.h"

namespace {

QString writeBundleToTempDir( const LabJobBundle& bundle, const QString& jobId,
                              QString* errorMessage )
{
    const auto root = QDir( QDir::tempPath() ).filePath( QStringLiteral( "klogg-lab-agent/%1" ).arg( jobId ) );
    QDir().mkpath( root );
    for ( const auto& file : bundle.files ) {
        const auto targetPath = QFileInfo( QDir( root ).filePath( file.relativePath ) ).absoluteFilePath();
        QDir().mkpath( QFileInfo( targetPath ).absolutePath() );
        QFile outputFile( targetPath );
        if ( !outputFile.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
            if ( errorMessage != nullptr ) {
                *errorMessage = QStringLiteral( "Failed to prepare %1: %2" )
                                    .arg( targetPath, outputFile.errorString() );
            }
            return {};
        }
        outputFile.write( file.content );
    }
    return root;
}

QByteArray loadToken( const QString& tokenFilePath, QString* errorMessage )
{
    QFile file( tokenFilePath );
    if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = QStringLiteral( "Failed to read token file %1: %2" )
                                .arg( tokenFilePath, file.errorString() );
        }
        return {};
    }
    return file.readAll().trimmed();
}

} // namespace

LabAgentRunner::LabAgentRunner( KloggApp& app, QObject* parent )
    : QObject( parent )
    , app_( app )
    , socket_( new QTcpSocket( this ) )
{
    connect( socket_, &QTcpSocket::connected, this, &LabAgentRunner::connected );
    connect( socket_, &QTcpSocket::disconnected, this, &LabAgentRunner::disconnected );
    connect( socket_, &QTcpSocket::readyRead, this, &LabAgentRunner::readyRead );
}

bool LabAgentRunner::start( const LabCliRequest& request, QString* errorMessage )
{
    sharedToken_ = loadToken( request.tokenFilePath, errorMessage );
    if ( sharedToken_.isEmpty() ) {
        return false;
    }

    const auto config = loadLabAgentConfig( request.agentConfigPath, errorMessage );
    if ( !config ) {
        return false;
    }
    config_ = *config;

    const QUrl controllerUrl( request.controllerUrl );
    const auto host = controllerUrl.host().trimmed();
    const auto port = controllerUrl.port( 0 );
    if ( host.isEmpty() || port <= 0 ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = QStringLiteral( "Invalid controller URL." );
        }
        return false;
    }

    socket_->connectToHost( host, static_cast<quint16>( port + 1 ) );
    if ( !socket_->waitForConnected( 5000 ) ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = socket_->errorString();
        }
        return false;
    }

    auto* timer = new QTimer( this );
    timer->setInterval( 3000 );
    connect( timer, &QTimer::timeout, this, &LabAgentRunner::sendHeartbeat );
    timer->start();
    return true;
}

void LabAgentRunner::connected()
{
    sendMessage( QVariantMap{ { QStringLiteral( "type" ), QStringLiteral( "register" ) },
                              { QStringLiteral( "token" ), sharedToken_ },
                              { QStringLiteral( "agent" ), labAgentConfigToVariantMap( config_ ) } } );
}

void LabAgentRunner::disconnected()
{
    QTimer::singleShot( 0, &app_, &QCoreApplication::quit );
}

void LabAgentRunner::readyRead()
{
    buffer_.append( socket_->readAll() );
    while ( true ) {
        const auto newlineIndex = buffer_.indexOf( '\n' );
        if ( newlineIndex < 0 ) {
            break;
        }

        const auto line = buffer_.left( newlineIndex ).trimmed();
        buffer_.remove( 0, newlineIndex + 1 );
        if ( line.isEmpty() ) {
            continue;
        }

        const auto document = QJsonDocument::fromJson( line );
        if ( document.isObject() ) {
            processMessage( document.object().toVariantMap() );
        }
    }
}

void LabAgentRunner::sendHeartbeat()
{
    sendMessage( QVariantMap{ { QStringLiteral( "type" ), QStringLiteral( "heartbeat" ) },
                              { QStringLiteral( "token" ), sharedToken_ },
                              { QStringLiteral( "agentId" ), config_.agentId } } );
}

void LabAgentRunner::processMessage( const QVariantMap& message )
{
    const auto type = message.value( QStringLiteral( "type" ) ).toString();
    if ( type == QStringLiteral( "assign_job" ) ) {
        runAssignedJob( message );
    }
    else if ( type == QStringLiteral( "cancel_job" ) ) {
        CommanderRequest request;
        request.action = CommanderAction::StopScenarioRun;
        app_.executeCommanderRequest( request );
    }
}

void LabAgentRunner::sendMessage( const QVariantMap& message )
{
    socket_->write( QJsonDocument::fromVariant( message ).toJson( QJsonDocument::Compact ) );
    socket_->write( "\n" );
    socket_->flush();
}

void LabAgentRunner::runAssignedJob( const QVariantMap& message )
{
    const auto jobId = message.value( QStringLiteral( "jobId" ) ).toString();
    const auto bundle = labJobBundleFromVariantMap( message.value( QStringLiteral( "bundle" ) ).toMap() );
    const auto bindings = message.value( QStringLiteral( "bindings" ) ).toList();

    QString errorMessage;
    const auto root = writeBundleToTempDir( bundle, jobId, &errorMessage );
    if ( root.isEmpty() ) {
        sendMessage( QVariantMap{ { QStringLiteral( "type" ), QStringLiteral( "job_complete" ) },
                                  { QStringLiteral( "token" ), sharedToken_ },
                                  { QStringLiteral( "jobId" ), jobId },
                                  { QStringLiteral( "status" ), QStringLiteral( "failed_infrastructure" ) },
                                  { QStringLiteral( "lastError" ), errorMessage } } );
        return;
    }

    QVariantMap deviceMapRoot;
    for ( const auto& bindingValue : bindings ) {
        const auto binding = bindingValue.toMap();
        deviceMapRoot.insert( binding.value( QStringLiteral( "logicalName" ) ).toString(), binding );
    }
    const auto deviceMapPath = QDir( root ).filePath( QStringLiteral( "device-map.json" ) );
    QFile deviceMapFile( deviceMapPath );
    if ( deviceMapFile.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
        deviceMapFile.write( QJsonDocument::fromVariant( QVariantMap{
                                 { QStringLiteral( "devices" ), deviceMapRoot } } )
                                 .toJson( QJsonDocument::Indented ) );
    }

    sendMessage( QVariantMap{ { QStringLiteral( "type" ), QStringLiteral( "job_update" ) },
                              { QStringLiteral( "token" ), sharedToken_ },
                              { QStringLiteral( "jobId" ), jobId },
                              { QStringLiteral( "state" ), QStringLiteral( "running" ) },
                              { QStringLiteral( "outputTail" ), QVariantList{} } } );

    ScenarioBatchRequest batchRequest;
    batchRequest.action = ScenarioBatchAction::Run;
    batchRequest.deviceMapFilePath = deviceMapPath;
    batchRequest.reportDirPath = QDir( root ).filePath( QStringLiteral( "artifacts" ) );
    if ( bundle.kind == QStringLiteral( "suite" ) ) {
        batchRequest.suiteFilePath = QDir( root ).filePath( bundle.suiteFile );
    }
    else {
        batchRequest.scenarioFilePath = QDir( root ).filePath( bundle.scenarioFile );
        if ( !bundle.argsJsonFile.isEmpty() ) {
            batchRequest.argsJsonFilePath = QDir( root ).filePath( bundle.argsJsonFile );
        }
    }

    ScenarioHeadlessRunner runner( app_ );
    const auto result = runner.run( batchRequest );

    QVariantList artifacts;
    const auto reportJsonFile = result.payload.value( QStringLiteral( "reportJsonFile" ) ).toString();
    const auto reportJunitFile = result.payload.value( QStringLiteral( "reportJunitFile" ) ).toString();
    for ( const auto& artifactPath : { reportJsonFile, reportJunitFile } ) {
        if ( artifactPath.isEmpty() ) {
            continue;
        }
        QFile file( artifactPath );
        if ( file.open( QIODevice::ReadOnly ) ) {
            artifacts.push_back( QVariantMap{
                { QStringLiteral( "name" ), QFileInfo( artifactPath ).fileName() },
                { QStringLiteral( "dataBase64" ),
                  QString::fromLatin1( file.readAll().toBase64( QByteArray::Base64Encoding ) ) },
            } );
        }
    }

    sendMessage( QVariantMap{
        { QStringLiteral( "type" ), QStringLiteral( "job_complete" ) },
        { QStringLiteral( "token" ), sharedToken_ },
        { QStringLiteral( "jobId" ), jobId },
        { QStringLiteral( "status" ),
          result.exitCode == 2 ? QStringLiteral( "failed_infrastructure" )
                               : ( result.exitCode == 0 ? QStringLiteral( "passed" )
                                                        : QStringLiteral( "failed" ) ) },
        { QStringLiteral( "lastError" ), result.message },
        { QStringLiteral( "reportPayload" ), result.payload },
        { QStringLiteral( "resolvedBindings" ), bindings },
        { QStringLiteral( "outputTail" ), QVariantList{} },
        { QStringLiteral( "artifacts" ), artifacts },
    } );
}
