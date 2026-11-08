#pragma once

#include "streamsession.h"
#include <QPointer>

// Simulate the session's logical connection state without opening a device or
// starting a worker. Tests observe scheduling signals, never physical transport.
struct StreamSessionTestAccess {
#ifdef KLOGG_SHARED_SERIAL_WRITER
    static void rejectResume( StreamSession& s )
    {
        s.capturePreflight_ = []( QString* error ) {
            if ( error )
                *error = QStringLiteral( "Test endpoint unavailable" );
            return false;
        };
    }
    static void
    setOwnerFixture( StreamSession& s, StreamSourceRegistry& registry,
                     std::function<SerialCaptureWorker*( SerialCaptureSettings )> factory )
    {
        s.registry_ = &registry;
        s.workerFactory_ = std::move( factory );
        s.capturePreflight_ = []( QString* ) { return true; };
    }
    static bool stopped( const StreamSession& s )
    {
        return !s.started_ && !s.thread_.isRunning() && !s.worker_;
    }
    static quint64 epoch( const StreamSession& s )
    {
        return s.workerGeneration_;
    }
    static QString endpoint( const StreamSession& s )
    {
        return s.writerEndpointId_;
    }
    static void ready( StreamSession& s, quint64 epoch )
    {
        s.acceptBackendReady( epoch );
    }
    static void data( StreamSession& s, quint64 epoch, const QByteArray& bytes, bool tx = false )
    {
        s.acceptBackendData( epoch, bytes, tx );
    }
    static void snapshot( StreamSession& s, cilogg::communication::BackendWriteSnapshotPtr p )
    {
        s.acceptWriteSnapshot( p );
    }
    static std::optional<cilogg::communication::WriteObservation> write( StreamSession& s,
                                                                         const QString& id )
    {
        return s.writer_ ? s.writer_->getWrite( id ).value
                         : std::optional<cilogg::communication::WriteObservation>{};
    }
    static void clock( StreamSession& s, std::function<qint64()> clock )
    {
        s.writerClock_ = std::move( clock );
    }
    static void tick( StreamSession& s )
    {
        s.driveWrites( true );
    }
    static void postWorker( StreamSession& s,
                            std::function<void( SerialCaptureWorker& )> operation )
    {
        QPointer<SerialCaptureWorker> worker( s.worker_ );
        if ( worker )
            QMetaObject::invokeMethod(
                worker,
                [ worker, operation ] {
                    if ( worker )
                        operation( *worker );
                },
                Qt::QueuedConnection );
    }
#endif
    static void openConnection( StreamSession& session )
    {
#ifdef KLOGG_SHARED_SERIAL_WRITER
        session.workerGeneration_ = 1;
        session.initializeWriter( 1 );
        session.backendRetired_ = false;
        session.closePublished_ = false;
#endif
        session.connectionOpen_ = true;
#ifdef KLOGG_SHARED_SERIAL_WRITER
        session.publishWritePolicy();
#endif
    }

    static void feedData( StreamSession& session, const QByteArray& data )
    {
        session.handleDataReceived( data );
    }
};
