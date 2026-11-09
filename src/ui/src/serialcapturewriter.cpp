// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "serialcaptureworker.h"
#include <QDateTime>
#include <QLoggingCategory>
#include <QPointer>

Q_LOGGING_CATEGORY( serialCaptureWriterLog, "cilogg.communication.capture" )
using namespace cilogg::communication;

bool SerialCaptureWorker::isCurrentEpoch( quint64 generation, bool requireReady ) const
{
    if ( !writeContext_ || generation != writeContext_->epoch.generation || stopping_ )
        return false;
    const auto policy = writeContext_->policy->load();
    return policy && policy->connectionGeneration == generation
           && ( !requireReady || policy->connectionReady );
}
void SerialCaptureWorker::failChecked( const QString& message )
{
    qCDebug( serialCaptureWriterLog, "Serial owner failed; retiring backend" );
    QPointer<SerialCaptureWorker> guard( this );
    Q_EMIT errorOccurred( message );
    if ( guard )
        stopChecked();
}
void SerialCaptureWorker::startChecked()
{
    if ( stopping_ ) {
        qCDebug( serialCaptureWriterLog, "Start rejected: stopped" );
        return;
    }
    if ( !writeContext_ ) {
        failChecked( tr( "Serial endpoint %1 has no owner context." ).arg( settings_.portName ) );
        return;
    }
    if ( threadRetirement_ && !threadRetirement_->captureCurrentThread() ) {
        failChecked( tr( "Cannot observe serial worker thread completion." ) );
        return;
    }
    const auto epoch = writeContext_->epoch;
    if ( !isCurrentEpoch( epoch.generation, false ) ) {
        stopChecked();
        return;
    }
    if ( settings_.portName.trimmed().isEmpty() || settings_.filePath.isEmpty() ) {
        failChecked( tr( "Serial endpoint or capture file is missing." ) );
        return;
    }
    if ( !file_.open( QIODevice::WriteOnly | QIODevice::Append ) ) {
        failChecked( tr( "Failed to open serial capture file." ) );
        return;
    }
    port_ = portFactory_ ? portFactory_( this ) : new QSerialPort( this );
    port_->setPortName( settings_.portName );
    port_->setBaudRate( settings_.baudRate );
    port_->setDataBits( settings_.dataBits );
    port_->setParity( settings_.parity );
    port_->setStopBits( settings_.stopBits );
    port_->setFlowControl( settings_.flowControl );
    connect( port_, &QSerialPort::readyRead, this, &SerialCaptureWorker::onReadyRead );
    if ( !port_->open( QIODevice::ReadWrite ) ) {
        qCDebug( serialCaptureWriterLog, "Open failed generation=%llu error=%d",
                 static_cast<unsigned long long>( epoch.generation ),
                 static_cast<int>( port_->error() ) );
        failChecked( tr( "Failed to open serial endpoint %1 (error %2)." )
                         .arg( settings_.portName )
                         .arg( static_cast<int>( port_->error() ) ) );
        return;
    }
    if ( !isCurrentEpoch( epoch.generation, false ) ) {
        stopChecked();
        return;
    }
    auto driver = SerialWriteDriver::create( *port_, *writeContext_ );
    if ( !driver.value ) {
        failChecked( tr( "Failed to initialize serial write driver." ) );
        return;
    }
    writeDriver_ = std::move( *driver.value );
    connect( writeDriver_.get(), &SerialWriteDriver::writtenPrefix, this,
             &SerialCaptureWorker::captureWrittenPrefix, Qt::DirectConnection );
    connect( writeDriver_.get(), &SerialWriteDriver::snapshotReady, this,
             [ this ]( BackendWriteSnapshotPtr snapshot ) { Q_EMIT backendSnapshot( snapshot ); } );
    QPointer<SerialWriteDriver> safeDriver( writeDriver_.get() );
    connect(
        port_, &QSerialPort::errorOccurred, writeDriver_.get(),
        [ safeDriver, epoch ]( QSerialPort::SerialPortError error ) {
            if ( error != QSerialPort::NoError && safeDriver )
                safeDriver->backendError( epoch );
        },
        Qt::DirectConnection );
    connect( port_, &QSerialPort::errorOccurred, this, &SerialCaptureWorker::onError,
             Qt::QueuedConnection );
    atLineStart_ = true;
    flushCounter_ = 0;
    readyPublished_ = true;
    qCDebug( serialCaptureWriterLog, "Backend ready generation=%llu",
             static_cast<unsigned long long>( epoch.generation ) );
    QPointer<SerialCaptureWorker> guard( this );
    Q_EMIT backendReady();
    if ( guard )
        onReadyRead();
}
void SerialCaptureWorker::stopChecked()
{
    if ( closingStarted_ ) {
        qCDebug( serialCaptureWriterLog, "Repeated backend stop ignored" );
        return;
    }
    closingStarted_ = true;
    stopping_ = true;
    readyPublished_ = false;
    QPointer<SerialCaptureWorker> guard( this );
    if ( writeDriver_ )
        writeDriver_->beginClose( writeContext_->epoch );
    if ( port_ && port_->isOpen() )
        port_->close();
    if ( !guard )
        return;
    finishTxSegment();
    if ( !guard )
        return;
    if ( file_.isOpen() ) {
        file_.flush();
        file_.close();
    }
    if ( writeDriver_ )
        writeDriver_->backendClosed( writeContext_->epoch );
    else if ( writeContext_ )
        Q_EMIT backendSnapshot( BackendWriteSnapshotPtr::create( BackendWriteSnapshot{
            { writeContext_->epoch, 0 }, 1, {}, 0, UnsubmittedReason::None, false, true } ) );
    if ( !guard )
        return;
    qCDebug(
        serialCaptureWriterLog, "Backend resources retired generation=%llu",
        static_cast<unsigned long long>( writeContext_ ? writeContext_->epoch.generation : 0 ) );
    Q_EMIT finished();
}
void SerialCaptureWorker::submitWriteOffer( SerialWriteEnvelopePtr envelope, bool sensitive )
{
    if ( !envelope || !writeDriver_ || !writeContext_ || stopping_ ) {
        qCDebug( serialCaptureWriterLog, "Offer rejected: unavailable backend" );
        return;
    }
    const auto& id = envelope->offer.requestId();
    if ( !txRequestId_.isEmpty() && txRequestId_ != id ) {
        failChecked( tr( "Serial writer attempted interleaved requests." ) );
        return;
    }
    if ( txRequestId_.isEmpty() ) {
        txRequestId_ = id;
        txSensitive_ = sensitive;
        txRedactionLogged_ = false;
    }
    QPointer<SerialCaptureWorker> guard( this );
    const auto result = writeDriver_->offer( envelope );
    if ( !guard )
        return;
    if ( !result.ok ) {
        qCDebug( serialCaptureWriterLog, "Offer rejected reason=%s",
                 qPrintable( result.error.code ) );
        failChecked( tr( "Serial write driver rejected the offer." ) );
    }
}
void SerialCaptureWorker::acknowledgeWriteProgress( const WriteEpoch& epoch, quint64 sequence )
{
    if ( !writeDriver_ || !writeContext_ || epoch.endpointId != writeContext_->epoch.endpointId
         || epoch.generation != writeContext_->epoch.generation )
        return;
    writeDriver_->acknowledgeProgress( epoch, sequence );
}
void SerialCaptureWorker::finishTxSegment()
{
    if ( !txSegmentOpen_ )
        return;
    const bool newline = !txEndsInNewline_;
    txSegmentOpen_ = false;
    txEndsInNewline_ = true;
    atLineStart_ = true;
    qCDebug( serialCaptureWriterLog, "Closing TX presentation segment newline=%d", newline );
    if ( file_.isOpen() ) {
        if ( newline && file_.write( "\n", 1 ) != 1 ) {
            failChecked( tr( "Failed to finish serial TX capture." ) );
            return;
        }
        file_.flush();
    }
}
void SerialCaptureWorker::finalizeWritePresentation( const WriteEpoch& epoch, const QString& id )
{
    if ( !writeContext_ || epoch.generation != writeContext_->epoch.generation
         || epoch.endpointId != writeContext_->epoch.endpointId || id != txRequestId_ )
        return;
    QPointer<SerialCaptureWorker> guard( this );
    finishTxSegment();
    if ( !guard )
        return;
    txRequestId_.clear();
    txSensitive_ = false;
    txRedactionLogged_ = false;
    qCDebug( serialCaptureWriterLog, "Finalized logical TX presentation generation=%llu",
             static_cast<unsigned long long>( epoch.generation ) );
}
void SerialCaptureWorker::captureWrittenPrefix( const QByteArray& bytes, const QString& id )
{
    if ( bytes.isEmpty() || id != txRequestId_ || !writeContext_
         || !isCurrentEpoch( writeContext_->epoch.generation, false ) || !loggingEnabled_
         || !settings_.logTransmits || !file_.isOpen() ) {
        qCDebug( serialCaptureWriterLog, "TX capture skipped bytes=%lld",
                 static_cast<long long>( bytes.size() ) );
        return;
    }
    if ( txSensitive_ && txRedactionLogged_ )
        return;
    QByteArray output;
    if ( !txSegmentOpen_ ) {
        if ( !atLineStart_ )
            output.append( '\n' );
        if ( settings_.addTimestamps ) {
            output.append(
                QDateTime::currentDateTime().toString( settings_.timestampFormat ).toUtf8() );
            output.append( ' ' );
        }
        output.append( "[TX] - " );
    }
    const auto visible = txSensitive_ ? QByteArrayLiteral( "<redacted>" ) : bytes;
    output.append( visible );
    if ( file_.write( output ) != output.size() ) {
        failChecked( tr( "Failed to log serial TX prefix." ) );
        return;
    }
    txSegmentOpen_ = true;
    txEndsInNewline_ = visible.endsWith( '\n' );
    if ( txSensitive_ )
        txRedactionLogged_ = true;
    file_.flush();
    qCDebug( serialCaptureWriterLog, "Captured acknowledged TX prefix bytes=%lld redacted=%d",
             static_cast<long long>( bytes.size() ), txSensitive_ );
}
