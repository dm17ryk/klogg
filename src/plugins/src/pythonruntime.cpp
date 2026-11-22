#include "pythonruntime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

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

QStringList pathInterpreterCandidates( const QStringList& searchPaths )
{
#ifdef Q_OS_WIN
    // "python3" with the PATHEXT suffixes also finds launcher scripts such as the pyenv-win shim
    // python3.bat, which starts Python through VBScript and can take half a minute: longer than a
    // plugin may take to initialize. Real executables are tried first.
    const auto names = { "python.exe", "python3.exe", "python", "python3" };
#else
    const auto names = { "python3", "python" };
#endif
    QStringList candidates;
    for ( const auto* name : names ) {
        candidates << QStandardPaths::findExecutable( QString::fromLatin1( name ), searchPaths );
    }
    candidates.removeAll( QString{} );
    candidates.removeDuplicates();
    return candidates;
}

bool isLauncherScript( const QString& path )
{
    const auto suffix = QFileInfo( path ).suffix();
    return suffix.compare( QLatin1String( "bat" ), Qt::CaseInsensitive ) == 0
           || suffix.compare( QLatin1String( "cmd" ), Qt::CaseInsensitive ) == 0;
}

} // namespace cilogg::pythonruntime
