#include "streamsession.h"

#include <QMetaObject>

#include "streamsourceregistry.h"

StreamSession::StreamSession( SerialCaptureSettings settings, QObject* parent )
    : QObject( parent )
    , settings_( std::move( settings ) )
{
    connect( &thread_, &QThread::finished, this, [ this ]() { started_ = false; } );
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
    connect( worker_, &SerialCaptureWorker::finished, this, [ this ] {
        setConnectionClosed();
        StreamSourceRegistry::get().unregisterSerialPort( settings_.portName );
    } );
    connect( worker_, &SerialCaptureWorker::errorOccurred, this, [ this ]( const QString& message ) {
        setConnectionClosed();
        StreamSourceRegistry::get().unregisterSerialPort( settings_.portName );
        Q_EMIT errorOccurred( message );
    } );
}

void StreamSession::setConnectionClosed()
{
    if ( !connectionOpen_ ) {
        return;
    }

    connectionOpen_ = false;
    Q_EMIT connectionClosed();
}
