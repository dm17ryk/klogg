#include "pythonruntime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace cilogg::pythonruntime {

QStringList rootCandidates( const QString& applicationDir )
{
    const QDir appDir( applicationDir );
    QStringList candidates;
#ifdef Q_OS_MACOS
    candidates << QDir::cleanPath( appDir.absoluteFilePath( "../Resources/python_runtime" ) );
#endif
    candidates << QDir::cleanPath( appDir.absoluteFilePath( "python_runtime" ) );
    return candidates;
}

QString rootFor( const QString& applicationDir )
{
    for ( const auto& candidate : rootCandidates( applicationDir ) ) {
        if ( QFileInfo( QDir( candidate ).filePath( "worker_bootstrap.py" ) ).isFile() ) {
            return candidate;
        }
    }
    return {};
}

QString root()
{
    return rootFor( QCoreApplication::applicationDirPath() );
}

QStringList bundledInterpreterCandidates( const QString& applicationDir )
{
    QStringList candidates;
    for ( const auto& runtimeRoot : rootCandidates( applicationDir ) ) {
        const QDir dir( runtimeRoot );
        for ( const auto* relative : { "python.exe", "python/python.exe", "bin/python3",
                                       "bin/python", "python/bin/python3" } ) {
            candidates << dir.filePath( relative );
        }
    }
    const QDir appDir( applicationDir );
    for ( const auto* relative :
          { "python/python.exe", "bin/python3", "bin/python", "python/bin/python3" } ) {
        candidates << QDir::cleanPath( appDir.absoluteFilePath( relative ) );
    }
    return candidates;
}

} // namespace cilogg::pythonruntime
