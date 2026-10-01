#include <QtGlobal>
#include <catch2/catch.hpp>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "configuration.h"
#include "filewatcher.h"
#include "log.h"

#ifdef Q_OS_WIN

#include <atomic>
#include <efsw/efsw.hpp>

namespace {

class NativeWatchListener final : public efsw::FileWatchListener {
public:
    void handleFileAction( efsw::WatchID watchId, const std::string& directory,
                           const std::string& filename, efsw::Action action,
                           const std::string& oldFilename ) override
    {
        Q_UNUSED( oldFilename );
        LOG_DEBUG << "Native watcher regression event: watch=" << watchId
                  << ", directory=" << directory << ", file=" << filename
                  << ", action=" << static_cast<int>( action );
        notifications.fetch_add( 1, std::memory_order_relaxed );
    }

    std::atomic<unsigned int> notifications{ 0 };

    void handleMissedFileActions( efsw::WatchID watchId, const std::string& directory ) override
    {
        LOG_DEBUG << "Native watcher regression failed completion: watch=" << watchId
                  << ", directory=" << directory;
        missedNotifications.fetch_add( 1, std::memory_order_relaxed );
    }

    std::atomic<unsigned int> missedNotifications{ 0 };
};

} // namespace

TEST_CASE( "Windows native watcher permits synchronous removal from its listener",
           "[logdata][filewatch]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    struct RemovingListener final : efsw::FileWatchListener {
        void handleFileAction( efsw::WatchID watchId, const std::string&, const std::string&,
                               efsw::Action, const std::string& ) override
        {
            LOG_DEBUG << "Native watcher regression synchronous removal: watch=" << watchId;
            owner->removeWatch( watchId );
            notifications.fetch_add( 1, std::memory_order_relaxed );
        }
        efsw::FileWatcher* owner = nullptr;
        std::atomic<unsigned int> notifications{ 0 };
    } listener;
    {
        efsw::FileWatcher watcher;
        listener.owner = &watcher;
        const auto watchId = watcher.addWatch( directory.path().toStdString(), &listener, false );
        REQUIRE( watchId > 0 );
        watcher.watch();
        QFile file( directory.filePath( QStringLiteral( "self-removal.txt" ) ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        REQUIRE( file.write( "change\n" ) == 7 );
        REQUIRE( file.flush() );
        file.close();
        QElapsedTimer timeout;
        timeout.start();
        while ( listener.notifications.load( std::memory_order_relaxed ) == 0
                && timeout.elapsed() < 5000 ) {
            QTest::qWait( 10 );
        }
        REQUIRE( listener.notifications.load( std::memory_order_relaxed ) > 0 );
    }
}

TEST_CASE( "Windows native watcher drains cancelled requests during removal and shutdown",
           "[logdata][filewatch]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    NativeWatchListener listener;
    const auto watchedPath = directory.path().toStdString();
    const auto changedPath = directory.filePath( QStringLiteral( "native-watch.txt" ) );

    {
        efsw::FileWatcher watcher;
        const auto initialWatch = watcher.addWatch( watchedPath, &listener, false );
        REQUIRE( initialWatch > 0 );
        watcher.watch();

        QFile changedFile( changedPath );
        REQUIRE( changedFile.open( QIODevice::WriteOnly ) );
        REQUIRE( changedFile.write( "initial\n" ) == 8 );
        REQUIRE( changedFile.flush() );
        changedFile.close();

        QElapsedTimer timeout;
        timeout.start();
        while ( listener.notifications.load( std::memory_order_relaxed ) == 0
                && timeout.elapsed() < 5000 ) {
            QTest::qWait( 10 );
        }
        REQUIRE( listener.notifications.load( std::memory_order_relaxed ) > 0 );
        watcher.removeWatch( initialWatch );

        // Churn one directory so cancelled OVERLAPPED addresses can be reused.
        // The listener and directory outlive the watcher and its completion queue.
        for ( unsigned int iteration = 0; iteration < 128; ++iteration ) {
            INFO( "Native watcher cancellation iteration " << iteration );
            const auto watchId = watcher.addWatch( watchedPath, &listener, false );
            LOG_DEBUG << "Native watcher regression registration: iteration=" << iteration
                      << ", watch=" << watchId;
            REQUIRE( watchId > 0 );
            REQUIRE( changedFile.open( QIODevice::WriteOnly | QIODevice::Append ) );
            REQUIRE( changedFile.write( "change\n" ) == 7 );
            REQUIRE( changedFile.flush() );
            changedFile.close();
            LOG_DEBUG << "Native watcher regression cancellation: watch=" << watchId;
            watcher.removeWatch( watchId );
        }

        // Leave a read pending to cover shutdown without explicit removeWatch.
        const auto finalWatch = watcher.addWatch( watchedPath, &listener, false );
        REQUIRE( finalWatch > 0 );
        LOG_DEBUG << "Native watcher regression shutdown with pending watch " << finalWatch;
    }

    REQUIRE( listener.notifications.load( std::memory_order_relaxed ) > 0 );
}

TEST_CASE( "Windows native watcher permits self removal during delayed deletion delivery",
           "[logdata][filewatch]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    QFile file( directory.filePath( QStringLiteral( "delayed-removal.txt" ) ) );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( "initial\n" ) == 8 );
    REQUIRE( file.flush() );
    file.close();
    struct RemovingListener final : efsw::FileWatchListener {
        void handleFileAction( efsw::WatchID watchId, const std::string&, const std::string&,
                               efsw::Action action, const std::string& ) override
        {
            if ( action == efsw::Actions::Delete ) {
                LOG_DEBUG << "Native watcher regression delayed removal: watch=" << watchId;
                owner->removeWatch( watchId );
                deletions.fetch_add( 1, std::memory_order_relaxed );
            }
        }
        efsw::FileWatcher* owner = nullptr;
        std::atomic<unsigned int> deletions{ 0 };
    } listener;
    {
        efsw::FileWatcher watcher;
        listener.owner = &watcher;
        const auto watchId = watcher.addWatch( directory.path().toStdString(), &listener, true,
                                               { { efsw::Option::ReportCrossDirectoryMoves, 1 } } );
        REQUIRE( watchId > 0 );
        watcher.watch();
        REQUIRE( file.remove() );
        QElapsedTimer timeout;
        timeout.start();
        while ( listener.deletions.load( std::memory_order_relaxed ) == 0
                && timeout.elapsed() < 5000 ) {
            QTest::qWait( 10 );
        }
        REQUIRE( listener.deletions.load( std::memory_order_relaxed ) > 0 );
    }
}

TEST_CASE( "Windows native watcher shuts down after its directory is removed",
           "[logdata][filewatch]" )
{
    QTemporaryDir parent;
    REQUIRE( parent.isValid() );
    const auto directory = parent.filePath( QStringLiteral( "removed-watch" ) );
    REQUIRE( QDir().mkdir( directory ) );
    NativeWatchListener listener;
    {
        efsw::FileWatcher watcher;
        const auto watchId = watcher.addWatch( directory.toStdString(), &listener, false );
        REQUIRE( watchId > 0 );
        watcher.watch();
        REQUIRE( QDir().rmdir( directory ) );
        QElapsedTimer timeout;
        timeout.start();
        while ( listener.missedNotifications.load( std::memory_order_relaxed ) == 0
                && timeout.elapsed() < 5000 ) {
            QTest::qWait( 10 );
        }
        REQUIRE( listener.missedNotifications.load( std::memory_order_relaxed ) > 0 );
        LOG_DEBUG << "Native watcher regression removing failed watch " << watchId;
        watcher.removeWatch( watchId );
    }
}

#endif

TEST_CASE( "File watching stays disabled until polling is explicitly enabled",
           "[logdata][filewatch]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto fileName = directory.filePath( QStringLiteral( "disabled-watch.txt" ) );
    QFile file( fileName );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.close();

    struct RestoreWatching {
        bool native = Configuration::get().nativeFileWatchEnabled();
        bool polling = Configuration::get().pollingEnabled();
        int interval = Configuration::get().pollIntervalMs();
        QString fileName;
        ~RestoreWatching()
        {
            auto& watcher = FileWatcher::getFileWatcher();
            watcher.removeFile( fileName );
            auto& config = Configuration::get();
            config.setNativeFileWatchEnabled( native );
            config.setPollingEnabled( polling );
            config.setPollIntervalMs( interval );
            watcher.updateConfiguration();
        }
    } restore;
    restore.fileName = fileName;

    auto& config = Configuration::get();
    config.setNativeFileWatchEnabled( false );
    config.setPollingEnabled( false );
    config.setPollIntervalMs( 10 );
    auto& watcher = FileWatcher::getFileWatcher();
    watcher.updateConfiguration();
    QSignalSpy notifications( &watcher, &FileWatcher::fileChanged );
    watcher.addFile( fileName );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Append ) );
    REQUIRE( file.write( "change\n" ) == 7 );
    REQUIRE( file.flush() );
    file.close();
    REQUIRE_FALSE( notifications.wait( 500 ) );
    REQUIRE( notifications.isEmpty() );

    config.setPollingEnabled( true );
    watcher.updateConfiguration();
    REQUIRE( notifications.wait( 2000 ) );
    REQUIRE( notifications.front().front().toString() == fileName );
}
