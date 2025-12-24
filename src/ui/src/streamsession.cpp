#include "streamsession.h"

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
}

void StreamSession::setConnectionClosed()
{
    if ( !connectionOpen_ ) {
        return;
    }

    connectionOpen_ = false;
    stopping_ = false;
    StreamSourceRegistry::get().unregisterSerialPort( settings_.portName );
    Q_EMIT connectionClosed();
}
