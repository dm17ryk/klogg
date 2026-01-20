#include "streamsession.h"
#include <QDateTime>

#include <QMetaObject>

#include "streamsourceregistry.h"

#include "log.h"

StreamSession::StreamSession( SerialCaptureSettings settings )
    : QObject( nullptr )
    , settings_( std::move( settings ) )
{
    connect( &thread_, &QThread::finished, this, [ this ]() {
        setConnectionClosed();
        started_ = false;
        stopping_ = false;
    } );
}

StreamSession::~StreamSession()
{
    stop();
}

void StreamSession::start()
{
    if ( started_ ) {
        return;
    }

    setupWorker();

    stopping_ = false;
    started_ = true;
    connectionOpen_ = true;
    StreamSourceRegistry::get().registerSerialPort( settings_.portName );
    thread_.start();
}

void StreamSession::stop( bool waitForCompletion )
{
    if ( !started_ ) {
        return;
    }

    if ( stopping_ && !waitForCompletion ) {
        return;
    }

    stopping_ = true;

    if ( thread_.isRunning() ) {
        if ( worker_ ) {
            const auto connectionType
                = waitForCompletion ? Qt::BlockingQueuedConnection : Qt::QueuedConnection;
            const auto invoked = QMetaObject::invokeMethod( worker_, "stop", connectionType );
            if ( !invoked ) {
                LOG_ERROR << "Failed to invoke serial capture stop for "
                          << settings_.portName.toStdString();
            }
        }
        if ( !waitForCompletion ) {
            return;
        }
        thread_.quit();
        if ( !thread_.wait( 5000 ) ) {
            LOG_ERROR << "Timeout stopping serial capture for "
                      << settings_.portName.toStdString()
                      << ", waiting for thread to exit.";
            thread_.wait();
        }
    }

    setConnectionClosed();
    started_ = false;
}

void StreamSession::closeConnection()
{
    stop( false );
}

bool StreamSession::isConnectionOpen() const
{
    return connectionOpen_;
}

QString StreamSession::sourceDisplayName() const
{
    return settings_.portName;
}

QString StreamSession::filePath() const
{
    return settings_.filePath;
}

const SerialCaptureSettings& StreamSession::captureSettings() const
{
    return settings_;
}

void StreamSession::sendBytes( const QByteArray& data )
{
    if ( !worker_ || data.isEmpty() ) {
        return;
    }
    const auto invoked
        = QMetaObject::invokeMethod( worker_, "sendData", Qt::QueuedConnection,
                                     Q_ARG( QByteArray, data ) );
    if ( !invoked ) {
        LOG_ERROR << "Failed to invoke serial send for " << settings_.portName.toStdString();
    }
}

void StreamSession::setupWorker()
{
    if ( worker_ ) {
        return;
    }

    worker_ = new SerialCaptureWorker( settings_ );
    worker_->moveToThread( &thread_ );

    connect( &thread_, &QThread::started, worker_, &SerialCaptureWorker::start );
    connect( worker_, &SerialCaptureWorker::finished, &thread_, &QThread::quit );
    connect( worker_, &SerialCaptureWorker::finished, worker_, &QObject::deleteLater );
    connect( worker_, &QObject::destroyed, this, [ this ] { worker_ = nullptr; } );
    connect( worker_, &SerialCaptureWorker::errorOccurred, this,
             [ this ]( const QString& message ) { Q_EMIT errorOccurred( message ); } );
    connect( worker_, &SerialCaptureWorker::dataReceived, this,
             &StreamSession::handleDataReceived );
}

void StreamSession::setConnectionClosed()
{
    if ( !connectionOpen_ ) {
        return;
    }

    connectionOpen_ = false;
    stopping_ = false;
    lineBuffer_.clear();
    StreamSourceRegistry::get().unregisterSerialPort( settings_.portName );     
    Q_EMIT connectionClosed();
}

void StreamSession::handleDataReceived( const QByteArray& data )
{
    if ( stopping_ || data.isEmpty() ) {
        return;
    }

    lineBuffer_.append( data );
    int newlineIndex = lineBuffer_.indexOf( '\n' );
    while ( newlineIndex >= 0 ) {
        QByteArray lineBytes = lineBuffer_.left( newlineIndex );
        lineBuffer_.remove( 0, newlineIndex + 1 );
        if ( lineBytes.endsWith( '\r' ) ) {
            lineBytes.chop( 1 );
        }
        handleIncomingLine( lineBytes );
        newlineIndex = lineBuffer_.indexOf( '\n' );
    }
}

void StreamSession::appendToFile( const QByteArray& data )
{
    if ( !worker_ || data.isEmpty() ) {
        return;
    }
    const auto invoked
        = QMetaObject::invokeMethod( worker_, "appendToFile", Qt::QueuedConnection,
                                     Q_ARG( QByteArray, data ) );
    if ( !invoked ) {
        LOG_ERROR << "Failed to append to capture file for "
                  << settings_.portName.toStdString();
    }
}

void StreamSession::handleIncomingLine( const QByteArray& lineBytes )
{
    if ( lineBytes.isEmpty() ) {
        return;
    }

    if ( !ActionsManager::instance().autoResponsesEnabled() ) {
        return;
    }

    const auto& responses = ActionsManager::instance().responses();
    if ( responses.isEmpty() ) {
        return;
    }

    const QString lineText = QString::fromLatin1( lineBytes );
    for ( const auto& response : responses ) {
        if ( !response.enabled ) {
            continue;
        }

        bool matched = false;
        QMap<QString, QString> captures;

        switch ( response.match.type ) {
        case ResponseMatchType::String:
            matched = lineText.contains( response.match.value );
            break;
        case ResponseMatchType::HexString: {
            const auto decoded = decodeHexStringToBytes( response.match.value );
            if ( decoded.ok ) {
                matched = lineBytes.contains( decoded.bytes );
            }
            break;
        }
        case ResponseMatchType::Regex: {
            const auto regex = response.match.compiled.isValid()
                                   ? response.match.compiled
                                   : QRegularExpression( response.match.value );
            const auto match = regex.match( lineText );
            if ( match.hasMatch() ) {
                matched = true;
                const auto names = regex.namedCaptureGroups();
                for ( const auto& name : names ) {
                    if ( name.isEmpty() ) {
                        continue;
                    }
                    captures.insert( name, match.captured( name ) );
                }
                const auto texts = match.capturedTexts();
                for ( qsizetype i = 0; i < texts.size(); ++i ) {
                    captures.insert( QString::number( i ), texts.at( i ) );
                }
            }
            break;
        }
        }

        if ( !matched ) {
            continue;
        }

        const ActionSequence* sequence = nullptr;
        ActionSequence inlineSequence;
        if ( response.response.hasInlineAction ) {
            inlineSequence = response.response.inlineAction;
            sequence = &inlineSequence;
        }
        else if ( response.response.hasActionId ) {
            if ( const auto* action
                 = ActionsManager::instance().findActionById( response.response.actionId ) ) {
                sequence = &action->sequence;
            }
        }

        if ( sequence ) {
            QStringList missing;
            const auto result = actionSequenceToBytes( *sequence, captures, &missing );
            if ( result.ok ) {
                sendBytes( result.bytes );
            }
            else {
                LOG_WARNING << "Failed to encode response action for "
                            << response.name.toStdString() << ": "
                            << result.error.toStdString();
            }
        }

        if ( !response.response.comment.isEmpty() || response.response.linebreak ) {
            QStringList missing;
            QString comment = response.response.comment;
            if ( !captures.isEmpty() ) {
                comment = resolveTemplateString( comment, captures, &missing );
            }
            if ( response.response.timestamp ) {
                const auto timestamp = QDateTime::currentDateTime().toString( Qt::ISODateWithMs );
                comment = comment.isEmpty() ? timestamp : QString( "%1 %2" ).arg( timestamp, comment );
            }

            QByteArray output;
            if ( !comment.isEmpty() ) {
                output.append( comment.toLatin1() );
                output.append( "\r\n" );
            }
            if ( response.response.linebreak ) {
                output.append( "\r\n" );
            }
            appendToFile( output );
        }

        if ( response.response.snapshot ) {
            LOG_INFO << "Snapshot requested by response " << response.name.toStdString();
        }

        if ( response.response.stopCommunication ) {
            closeConnection();
            break;
        }
    }
}
            
