#pragma once
#ifdef KLOGG_SHARED_SERIAL_WRITER
#include "streamsession_testaccess.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <future>
#include <mutex>
using namespace cilogg::communication;
struct SerialCaptureWorkerTestAccess {
    static void factory( SerialCaptureWorker& w, std::function<QSerialPort*( QObject* )> f )
    {
        w.portFactory_ = std::move( f );
    }
    static void prepare( SerialCaptureWorker& w )
    {
        auto policy = std::make_shared<StreamWritePolicyPublication>();
        policy->publish( { 1, 0, 0, true, false, false, false } );
        w.writeContext_
            = SerialWriteContext{ { "test-owner", 1 }, std::chrono::steady_clock::now(), policy };
    }
    static void drain( SerialCaptureWorker& w, qint64 n )
    {
        if ( w.port_ )
            Q_EMIT w.port_->bytesWritten( n );
    }
};
struct StreamSourceRegistryTestAccess {
    static std::unique_ptr<StreamSourceRegistry> isolated()
    {
        return std::unique_ptr<StreamSourceRegistry>( new StreamSourceRegistry(
            true, [] { return QVector<SerialPortAlias>{}; },
            []( const QString& p ) { return p; } ) );
    }
};
namespace cilogg_test {
struct PortState {
    std::mutex mutex;
    QByteArray sent;
    QByteArray banner;
    std::deque<qint64> returns;
    bool opensSuccessfully = true;
    bool autoDrain = true;
    bool synchronousFailure = false;
    std::atomic<int> opens{ 0 }, closes{ 0 }, calls{ 0 };
    std::shared_future<void> retirementGate;
    std::atomic<bool> retirementEntered{ false };
};
struct WaitForThreadRetirement {
    std::shared_ptr<PortState> state;
    explicit WaitForThreadRetirement( std::shared_ptr<PortState> value )
        : state( std::move( value ) )
    {
    }
    ~WaitForThreadRetirement()
    {
        state->retirementEntered.store( true );
        state->retirementGate.wait();
    }
};
class FakeSerialPort final : public QSerialPort {
public:
    FakeSerialPort( std::shared_ptr<PortState> state, QObject* parent )
        : QSerialPort( parent )
        , state_( std::move( state ) )
    {
    }
    bool open( OpenMode mode ) override
    {
        if ( state_->retirementGate.valid() ) {
            thread_local std::unique_ptr<WaitForThreadRetirement> retirement;
            retirement = std::make_unique<WaitForThreadRetirement>( state_ );
        }
        ++state_->opens;
        if ( !state_->opensSuccessfully )
            return false;
        QIODevice::open( mode | QIODevice::Unbuffered );
        incoming_ = state_->banner;
        if ( !incoming_.isEmpty() )
            Q_EMIT readyRead();
        return true;
    }
    void close() override
    {
        if ( isOpen() ) {
            QIODevice::close();
            ++state_->closes;
        }
    }
    qint64 bytesAvailable() const override
    {
        return incoming_.size();
    }
    void receive( const QByteArray& bytes )
    {
        incoming_.append( bytes );
        Q_EMIT readyRead();
    }

protected:
    qint64 readData( char* data, qint64 maximum ) override
    {
        const auto count = std::min( maximum, static_cast<qint64>( incoming_.size() ) );
        memcpy( data, incoming_.constData(), static_cast<size_t>( count ) );
        incoming_.remove( 0, static_cast<int>( count ) );
        return count;
    }
    qint64 writeData( const char* bytes, qint64 size ) override
    {
        ++state_->calls;
        qint64 count;
        {
            std::lock_guard<std::mutex> lock( state_->mutex );
            count = state_->returns.empty() ? size : state_->returns.front();
            if ( !state_->returns.empty() )
                state_->returns.pop_front();
            if ( count > 0 )
                state_->sent.append( bytes, static_cast<int>( count ) );
        }
        if ( count > 0 && state_->synchronousFailure ) {
            Q_EMIT bytesWritten( count );
            Q_EMIT errorOccurred( QSerialPort::ResourceError );
        }
        else if ( count > 0 && state_->autoDrain )
            QTimer::singleShot( 0, this, [ this, count ] { Q_EMIT bytesWritten( count ); } );
        return count;
    }

private:
    std::shared_ptr<PortState> state_;
    QByteArray incoming_;
};
struct OwnerFixture {
    std::shared_ptr<PortState> port = std::make_shared<PortState>();
    QTemporaryDir directory;
    std::unique_ptr<StreamSourceRegistry> registry = StreamSourceRegistryTestAccess::isolated();
    std::unique_ptr<StreamSession> session;
    explicit OwnerFixture( std::optional<SerialCaptureSettings> supplied = {} )
    {
        SerialCaptureSettings settings;
        if ( supplied )
            settings = *supplied;
        else {
            settings.portName = "TEST_OWNER";
            settings.filePath = directory.filePath( "capture.log" );
            settings.useForActions = true;
            settings.autoResponsesEnabled = true;
            settings.logTransmits = true;
        }
        session = std::make_unique<StreamSession>( settings );
        StreamSessionTestAccess::setOwnerFixture(
            *session, *registry, [ state = port ]( SerialCaptureSettings settings ) {
                auto* worker = new SerialCaptureWorker( std::move( settings ) );
                SerialCaptureWorkerTestAccess::factory( *worker, [ state ]( QObject* parent ) {
                    return new FakeSerialPort( state, parent );
                } );
                return worker;
            } );
    }
    bool ready()
    {
        return QTest::qWaitFor( [ & ] { return session->isConnectionOpen(); }, 5000 );
    }
    bool stopped()
    {
        return QTest::qWaitFor( [ & ] { return StreamSessionTestAccess::stopped( *session ); },
                                5000 );
    }
    ActionWriteStamp stamp( bool automatic = false )
    {
        return { automatic, session->actionsGeneration(), session->autoResponsesGeneration() };
    }
    QByteArray sent()
    {
        std::lock_guard<std::mutex> lock( port->mutex );
        return port->sent;
    }
};
} // namespace cilogg_test
#endif
