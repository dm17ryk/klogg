#include "streamsession.h"

#include <QMetaObject>

StreamSession::StreamSession( SerialCaptureSettings settings, QObject* parent )
    : QObject( parent )
    , settings_( std::move( settings ) )
{
    worker_ = new SerialCaptureWorker( settings_ );
    worker_->moveToThread( &thread_ );

    connect( &thread_, &QThread::started, worker_, &SerialCaptureWorker::start );
    connect( worker_, &SerialCaptureWorker::finished, &thread_, &QThread::quit );
    connect( &thread_, &QThread::finished, this, [ this ]() {
        started_ = false;
        if ( worker_ ) {
            delete worker_;
            worker_ = nullptr;
        }
    } );
    connect( worker_, &SerialCaptureWorker::errorOccurred, this, &StreamSession::errorOccurred );
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

    started_ = false;
}

QString StreamSession::filePath() const
{
    return settings_.filePath;
}
