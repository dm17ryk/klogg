#include "streamsession.h"
#include <QDateTime>

#include <QMetaObject>
#include <QVariantMap>

#include "actionruntime.h"
#include "actionsmanager.h"
#include "previewdecodeutils.h"
#include "streamsourceregistry.h"

#include "log.h"

StreamSession::StreamSession( SerialCaptureSettings settings )
    : QObject( nullptr )
    , settings_( std::move( settings ) )
{
    connect( &thread_, &QThread::finished, this, [ this ]() {
        started_ = false;
        stopping_ = false;
        setConnectionClosed();
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
    lineBuffer_.clear();
    responseCounters_.clear();
    StreamSourceRegistry::get().registerSerialPort( settings_.portName );
    thread_.start();
    if ( worker_ ) {
        const auto invoked = QMetaObject::invokeMethod( worker_, "setLoggingEnabled",
                                                        Qt::QueuedConnection,
                                                        Q_ARG( bool, loggingEnabled_ ) );
        if ( !invoked ) {
            LOG_ERROR << "Failed to apply logging state for " << settings_.portName.toStdString();
        }
    }
    Q_EMIT connectionOpened();
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
    started_ = false;
    stopping_ = false;
    setConnectionClosed();
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

bool StreamSession::startNewCaptureFile( const QString& filePath, QString* errorMessage )
{
    if ( !started_ || !connectionOpen_ || worker_ == nullptr ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = tr( "No active stream connection." );
        }
        return false;
    }

    const auto oldFilePath = settings_.filePath;
    QString switchError;
    bool switched = false;
    const auto invoked = QMetaObject::invokeMethod(
        worker_,
        [ this, filePath, &switchError, &switched ] {
            switched = worker_->switchCaptureFile( filePath, &switchError );
        },
        Qt::BlockingQueuedConnection );
    if ( !invoked || !switched ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = invoked ? switchError : tr( "Failed to invoke capture file switch." );
        }
        return false;
    }

    settings_.filePath = filePath;
    lineBuffer_.clear();
    Q_EMIT captureFileChanged( oldFilePath, filePath );
    return true;
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

void StreamSession::notifyActionSend( int actionId, const QString& actionName, int stepIndex,
                                      const QByteArray& data )
{
    if ( actionId < 0 || data.isEmpty() ) {
        return;
    }

    Q_EMIT actionSent( actionId, actionName, stepIndex, data );
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
    connect( worker_, &SerialCaptureWorker::dataTransmitted, this,
             [ this ]( const QByteArray& data ) { Q_EMIT dataTransmitted( data ); } );
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

    Q_EMIT dataObserved( data );

    lineBuffer_.append( data );
    qsizetype newlineIndex = lineBuffer_.indexOf( '\n' );
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

bool StreamSession::isLoggingEnabled() const
{
    return loggingEnabled_;
}

void StreamSession::setLoggingEnabled( bool enabled )
{
    loggingEnabled_ = enabled;
    if ( worker_ ) {
        const auto invoked = QMetaObject::invokeMethod( worker_, "setLoggingEnabled",
                                                        Qt::QueuedConnection,
                                                        Q_ARG( bool, enabled ) );
        if ( !invoked ) {
            LOG_ERROR << "Failed to toggle logging state for " << settings_.portName.toStdString();
        }
    }
}

int StreamSession::responseCounter( int responseId ) const
{
    return responseCounters_.value( responseId, 0 );
}

QVariantList StreamSession::responseCounters() const
{
    QVariantList counters;
    const auto& responses = ActionsManager::instance().responses();
    for ( const auto& response : responses ) {
        QVariantMap counter;
        counter.insert( QStringLiteral( "responseId" ), response.id );
        counter.insert( QStringLiteral( "responseName" ), response.name );
        counter.insert( QStringLiteral( "count" ), responseCounter( response.id ) );
        counters.push_back( counter );
    }
    return counters;
}

void StreamSession::resetResponseCounter( int responseId )
{
    responseCounters_[ responseId ] = 0;
}

void StreamSession::resetAllResponseCounters()
{
    responseCounters_.clear();
}

void StreamSession::handleIncomingLine( const QByteArray& lineBytes )
{
    if ( lineBytes.isEmpty() ) {
        return;
    }

    Q_EMIT lineObserved( lineBytes );

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

        const auto match = matchResponseDefinition( response, lineBytes, lineText );
        if ( !match.matched ) {
            continue;
        }

        responseCounters_[ response.id ] = responseCounters_.value( response.id, 0 ) + 1;
        Q_EMIT responseMatched( response.id, response.name, responseCounters_.value( response.id ),
                                lineBytes, lineText );

        QString errorMessage;
        if ( !executeResponseDefinition( this, response, match.captures, &errorMessage ) ) {
            LOG_WARNING << "Failed to execute response action for "
                        << response.name.toStdString() << ": "
                        << errorMessage.toStdString();
        }

        if ( response.response.snapshot ) {
            LOG_INFO << "Snapshot requested by response " << response.name.toStdString();
        }

        if ( response.response.stopCommunication ) {
            break;
        }
    }
}
            
