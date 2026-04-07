#include "labclient.h"

#include <QEventLoop>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

LabClient::LabClient( QObject* parent )
    : QObject( parent )
{
}

bool LabClient::loadToken( const QString& tokenFilePath, QString* errorMessage )
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

QVariantMap LabClient::submit( const QString& controllerUrl, const LabJobBundle& bundle,
                               QString* errorMessage )
{
    return performJsonRequest( controllerUrl, QStringLiteral( "/api/lab/submit" ),
                               QStringLiteral( "POST" ),
                               QVariantMap{ { QStringLiteral( "bundle" ),
                                              labJobBundleToVariantMap( bundle ) },
                                            { QStringLiteral( "requestedAgentLabel" ),
                                              bundle.agentLabel } },
                               errorMessage );
}

QVariantMap LabClient::queue( const QString& controllerUrl, QString* errorMessage )
{
    return performJsonRequest( controllerUrl, QStringLiteral( "/api/lab/queue" ),
                               QStringLiteral( "GET" ), {}, errorMessage );
}

QVariantMap LabClient::status( const QString& controllerUrl, const QString& jobId,
                               QString* errorMessage )
{
    QUrl url( controllerUrl );
    url.setPath( QStringLiteral( "/api/lab/status" ) );
    QUrlQuery query;
    query.addQueryItem( QStringLiteral( "jobId" ), jobId );
    url.setQuery( query );
    return performJsonRequest( url.toString(), QString{}, QStringLiteral( "GET" ), {}, errorMessage );
}

QVariantMap LabClient::cancel( const QString& controllerUrl, const QString& jobId,
                               QString* errorMessage )
{
    return performJsonRequest( controllerUrl, QStringLiteral( "/api/lab/cancel" ),
                               QStringLiteral( "POST" ),
                               QVariantMap{ { QStringLiteral( "jobId" ), jobId } }, errorMessage );
}

QVariantMap LabClient::agents( const QString& controllerUrl, QString* errorMessage )
{
    return performJsonRequest( controllerUrl, QStringLiteral( "/api/lab/agents" ),
                               QStringLiteral( "GET" ), {}, errorMessage );
}

QVariantMap LabClient::artifacts( const QString& controllerUrl, const QString& jobId,
                                  QString* errorMessage )
{
    QUrl url( controllerUrl );
    url.setPath( QStringLiteral( "/api/lab/artifacts" ) );
    QUrlQuery query;
    query.addQueryItem( QStringLiteral( "jobId" ), jobId );
    url.setQuery( query );
    return performJsonRequest( url.toString(), QString{}, QStringLiteral( "GET" ), {}, errorMessage );
}

QVariantMap LabClient::snapshot( const QString& controllerUrl, QString* errorMessage )
{
    return performJsonRequest( controllerUrl, QStringLiteral( "/api/lab/snapshot" ),
                               QStringLiteral( "GET" ), {}, errorMessage );
}

QVariantMap LabClient::performJsonRequest( const QString& controllerUrl, const QString& path,
                                           const QString& method, const QVariantMap& body,
                                           QString* errorMessage )
{
    QUrl url( controllerUrl );
    if ( !path.isEmpty() ) {
        url.setPath( path );
    }

    QNetworkAccessManager manager;
    QNetworkRequest request( url );
    request.setHeader( QNetworkRequest::ContentTypeHeader, QStringLiteral( "application/json" ) );
    request.setRawHeader( "X-Klogg-Token", sharedToken_ );

    QNetworkReply* reply = nullptr;
    if ( method == QStringLiteral( "POST" ) ) {
        reply = manager.post( request, QJsonDocument::fromVariant( body ).toJson( QJsonDocument::Compact ) );
    }
    else {
        reply = manager.get( request );
    }

    QEventLoop loop;
    connect( reply, &QNetworkReply::finished, &loop, &QEventLoop::quit );
    loop.exec();

    const auto responseBytes = reply->readAll();
    const auto document = QJsonDocument::fromJson( responseBytes );
    const auto payload = document.isObject() ? document.object().toVariantMap() : QVariantMap{};
    if ( reply->error() != QNetworkReply::NoError ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = payload.value( QStringLiteral( "error" ) ).toString().isEmpty()
                                ? reply->errorString()
                                : payload.value( QStringLiteral( "error" ) ).toString();
        }
        reply->deleteLater();
        return {};
    }

    reply->deleteLater();
    return payload;
}
