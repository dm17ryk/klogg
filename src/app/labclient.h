#pragma once

#include <QObject>
#include <QVariantMap>

#include "labrequest.h"
#include "labtypes.h"

class LabClient : public QObject {
  public:
    explicit LabClient( QObject* parent = nullptr );

    bool loadToken( const QString& tokenFilePath, QString* errorMessage );

    QVariantMap submit( const QString& controllerUrl, const LabJobBundle& bundle, QString* errorMessage );
    QVariantMap queue( const QString& controllerUrl, QString* errorMessage );
    QVariantMap status( const QString& controllerUrl, const QString& jobId, QString* errorMessage );
    QVariantMap cancel( const QString& controllerUrl, const QString& jobId, QString* errorMessage );
    QVariantMap agents( const QString& controllerUrl, QString* errorMessage );
    QVariantMap artifacts( const QString& controllerUrl, const QString& jobId, QString* errorMessage );
    QVariantMap snapshot( const QString& controllerUrl, QString* errorMessage );

  private:
    QVariantMap performJsonRequest( const QString& controllerUrl, const QString& path,
                                    const QString& method, const QVariantMap& body,
                                    QString* errorMessage );

    QByteArray sharedToken_;
};
