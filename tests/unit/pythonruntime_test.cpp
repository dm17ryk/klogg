#include <catch2/catch.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "pythonruntime.h"

namespace {

bool touch( const QString& path )
{
    if ( !QDir().mkpath( QFileInfo( path ).absolutePath() ) ) {
        return false;
    }
    QFile file( path );
    return file.open( QIODevice::WriteOnly );
}

} // namespace

TEST_CASE( "Built application ships the Python runtime where it looks for it",
           "[python_runtime]" )
{
    // CILOGG_APP_DIR is the built klogg target's executable directory
    // (Contents/MacOS inside the bundle on macOS).
    const QString appDir = QStringLiteral( CILOGG_APP_DIR );
    const auto root = cilogg::pythonruntime::rootFor( appDir );
    INFO( "application dir: " << appDir.toStdString() );
    INFO( "candidates: "
          << cilogg::pythonruntime::rootCandidates( appDir ).join( ", " ).toStdString() );
    REQUIRE_FALSE( root.isEmpty() );

    const QDir runtime( root );
    CHECK( QFileInfo( runtime.filePath( "worker_bootstrap.py" ) ).isFile() );
    CHECK( QFileInfo( runtime.filePath( "scenario_bootstrap.py" ) ).isFile() );
    CHECK( QFileInfo( runtime.filePath( "klogg/__init__.py" ) ).isFile() );

#ifdef Q_OS_MACOS
    // Scripts belong to Resources; Contents/MacOS is signed as code.
    CHECK( root.endsWith( "/Contents/Resources/python_runtime" ) );
    CHECK_FALSE( QFileInfo::exists( QDir( appDir ).filePath( "python_runtime" ) ) );
#else
    CHECK( QDir::cleanPath( root )
           == QDir::cleanPath( QDir( appDir ).absoluteFilePath( "python_runtime" ) ) );
#endif
}

TEST_CASE( "Python runtime lookup needs the worker bootstrap", "[python_runtime]" )
{
    QTemporaryDir temp;
    REQUIRE( temp.isValid() );
#ifdef Q_OS_MACOS
    const QString appDir = temp.filePath( "CILogg.app/Contents/MacOS" );
    const QString expected = temp.filePath( "CILogg.app/Contents/Resources/python_runtime" );
#else
    const QString appDir = temp.filePath( "bin" );
    const QString expected = temp.filePath( "bin/python_runtime" );
#endif
    REQUIRE( QDir().mkpath( appDir ) );

    SECTION( "missing runtime yields an empty root" )
    {
        CHECK( cilogg::pythonruntime::rootFor( appDir ).isEmpty() );
    }

    SECTION( "a directory without worker_bootstrap.py is not a runtime" )
    {
        REQUIRE( QDir().mkpath( expected ) );
        CHECK( cilogg::pythonruntime::rootFor( appDir ).isEmpty() );
    }

    SECTION( "the platform location is found" )
    {
        REQUIRE( touch( QDir( expected ).filePath( "worker_bootstrap.py" ) ) );
        CHECK( QDir::cleanPath( cilogg::pythonruntime::rootFor( appDir ) )
               == QDir::cleanPath( expected ) );
    }

    SECTION( "bundled interpreters are searched inside the runtime first" )
    {
        const auto candidates = cilogg::pythonruntime::bundledInterpreterCandidates( appDir );
        REQUIRE_FALSE( candidates.isEmpty() );
        CHECK( QDir::cleanPath( candidates.front() )
                   .startsWith( QDir::cleanPath( cilogg::pythonruntime::rootCandidates( appDir )
                                                     .front() ) ) );
        CHECK( candidates.contains(
            QDir::cleanPath( QDir( appDir ).absoluteFilePath( "bin/python3" ) ) ) );
    }
}
