#include "streamsession.h"

#include <QMetaObject>

#include "streamsourceregistry.h"

StreamSession::StreamSession( SerialCaptureSettings settings, QObject* parent )
    : QObject( parent )
    , settings_( std::move( settings ) )
{
    worker_ = new SerialCaptureWorker( settings_ );
    worker_->moveToThread( &thread_ );

    connect( &thread_, &QThread::started, worker_, &SerialCaptureWorker::start );
    connect( worker_, &SerialCaptureWorker::finished, &thread_, &QThread::quit );
    connect( worker_, &SerialCaptureWorker::finished, this, [ this ] {
        setConnectionClosed();
        StreamSourceRegistry::get().unregisterSerialPort( settings_.portName );
    } );
    connect( &thread_, &QThread::finished, this, [ this ]() {
        started_ = false;
        if ( worker_ ) {
            delete worker_;
            worker_ = nullptr;
        }
    } );
    connect( worker_, &SerialCaptureWorker::errorOccurred, this, [ this ]( const QString& message ) {
        setConnectionClosed();
        StreamSourceRegistry::get().unregisterSerialPort( settings_.portName );
        Q_EMIT errorOccurred( message );
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

    started_ = true;
    connectionOpen_ = true;
    StreamSourceRegistry::get().registerSerialPort( settings_.portName );
    thread_.start();
}

void StreamSession::stop()
{
    if ( !started_ ) {
        return;
    }

    if ( thread_.isRunning() && worker_ ) {
        QMetaObject::invokeMethod( worker_, "stop", Qt::QueuedConnection );
        thread_.quit();
        thread_.wait( 2000 );
    }

    if ( !thread_.isRunning() && worker_ ) {
        delete worker_;
        worker_ = nullptr;
    }

    setConnectionClosed();
    StreamSourceRegistry::get().unregisterSerialPort( settings_.portName );
    started_ = false;
}

void StreamSession::closeConnection()
{
    stop();
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

void StreamSession::setConnectionClosed()
{
    if ( !connectionOpen_ ) {
        return;
    }

    connectionOpen_ = false;
    Q_EMIT connectionClosed();
}
