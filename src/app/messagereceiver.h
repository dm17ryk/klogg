/*
 * Copyright (C) 2019 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MESSAGERECEIVER_H
#define MESSAGERECEIVER_H

#include <QtCore/QDir>
#include <QtCore/QCborValue>
#include <QtCore/QFile>
#include <QtCore/QFileDevice>
#include <QtCore/QFileInfo>

#include <QtCore/QJsonDocument>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariant>

#include "log.h"
#include "commander.h"
#include "klogg_version.h"

/*
 * Class receiving messages from another instance of klogg.
 * Messages are forwarded to the application by signals.
 */
class MessageReceiver final : public QObject {
    Q_OBJECT

  public:
    MessageReceiver()
        : QObject()
    {
    }

  Q_SIGNALS:
    void loadFile( const QString& filename );
    void activateWindow();
    void executeCommand( const CommanderRequest& request, const QString& resultPath );

  public Q_SLOTS:
    void receiveMessage( const QByteArray& message )
    {
        const auto data = QCborValue::fromCbor( message ).toVariant().toMap();

        LOG_INFO << "Message " << QJsonDocument::fromVariant(data).toJson();

        if ( data[ "version" ].toString() != kloggVersion() ) {
            return;
        }

        const auto ackPath = data.value( "ackPath" ).toString();
        const auto resultPath = data.value( "resultPath" ).toString();

        if ( data.value( "type" ).toString() == QLatin1String( "command" ) ) {
            handleCommandMessage( data, resultPath );
            return;
        }

        if ( !data.contains( "files" ) ) {
            LOG_WARNING << "Invalid message payload: missing files field";
            return;
        }

        const auto filesValue = data.value( "files" );
        if ( !filesValue.canConvert<QStringList>() ) {
            LOG_WARNING << "Invalid message payload: files must be a string list";
            return;
        }

        const QStringList filenames = filesValue.toStringList();

        bool didSomething = false;

        if ( data.value( "activate" ).toBool() || filenames.isEmpty() ) {
            Q_EMIT activateWindow();
            didSomething = true;
        }

        for ( const auto& f : filenames ) {
            if ( f.isEmpty() ) {
                continue;
            }

            Q_EMIT loadFile( f );
            didSomething = true;
        }

        if ( didSomething && !ackPath.isEmpty() ) {
            if ( !isValidAckPath( ackPath ) ) {
                LOG_WARNING << "Ignoring invalid ack path " << ackPath;
                return;
            }

            QFile ackFile( ackPath );
            if ( ackFile.open( QIODevice::WriteOnly | QIODevice::NewOnly,
                               QFileDevice::ReadOwner | QFileDevice::WriteOwner ) ) {
                ackFile.write( "ok" );
                ackFile.close();
            }
            else {
                LOG_WARNING << "Failed to create ack file " << ackPath << ackFile.errorString();
            }
        }
    }

  private:
    void handleCommandMessage( const QVariantMap& data, const QString& resultPath )
    {
        if ( !isValidTempPath( resultPath, QStringLiteral( "klogg_command_result_" ),
                               QStringLiteral( ".tmp" ) ) ) {
            LOG_WARNING << "Ignoring invalid result path " << resultPath;
            return;
        }

        const auto commandValue = data.value( QStringLiteral( "command" ) );
        if ( !commandValue.canConvert<QVariantMap>() ) {
            writeFailureResult( resultPath, QStringLiteral( "Invalid commander payload." ) );
            return;
        }

        QString errorMessage;
        const auto request = commanderRequestFromVariantMap( commandValue.toMap(), &errorMessage );
        if ( !request ) {
            writeFailureResult( resultPath, errorMessage.isEmpty()
                                                ? QStringLiteral( "Invalid commander payload." )
                                                : errorMessage );
            return;
        }

        Q_EMIT executeCommand( *request, resultPath );
    }

    static bool isPathEqual( const QString& left, const QString& right )
    {
#ifdef Q_OS_WIN
        return left.compare( right, Qt::CaseInsensitive ) == 0;
#else
        return left == right;
#endif
    }

    static bool isValidTempPath( const QString& path, const QString& prefix, const QString& suffix )
    {
        const QFileInfo info{ path };
        if ( !info.isAbsolute() ) {
            return false;
        }

        const auto expectedDir = QDir::cleanPath( QDir::tempPath() );
        const auto actualDir = QDir::cleanPath( info.absolutePath() );
        if ( !isPathEqual( actualDir, expectedDir ) ) {
            return false;
        }

        const auto fileName = info.fileName();
        if ( !fileName.startsWith( prefix ) || !fileName.endsWith( suffix ) ) {
            return false;
        }

        if ( info.exists() ) {
            return false;
        }

        return true;
    }

    static bool isValidAckPath( const QString& ackPath )
    {
        return isValidTempPath( ackPath, QStringLiteral( "klogg_activate_ack_" ),
                                QStringLiteral( ".tmp" ) );
    }

    static void writeFailureResult( const QString& resultPath, const QString& message )
    {
        const auto result = commanderFailure( CommanderResultCode::InvalidRequest, message );
        if ( !writeCommanderResult( resultPath, result ) ) {
            LOG_WARNING << "Failed to write commander result to " << resultPath;
        }
    }
};

#endif // MESSAGERECEIVER_H


