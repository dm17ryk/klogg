#include "pluginrepository.h"
#include "log.h"
#include "persistentinfo.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <karchive.h>
#include <kzip.h>
#include <memory>

namespace cilogg::plugins {
namespace {
constexpr qint64 MaxPackageBytes = 128LL * 1024 * 1024;
constexpr int MaxPackageEntries = 10000;
struct Budget {
    qint64 bytes = 0;
    int entries = 0;
};

bool containedDestination( const QString& root, const QString& destination )
{
    const QFileInfo info( destination );
    const auto parent = QDir( info.absolutePath() ).canonicalPath();
    auto normalized = info.canonicalFilePath();
    if ( normalized.isEmpty() && !parent.isEmpty() ) {
        normalized = QDir( parent ).filePath( info.fileName() );
    }
#ifdef Q_OS_WIN
    const auto sensitivity = Qt::CaseInsensitive;
#else
    const auto sensitivity = Qt::CaseSensitive;
#endif
    const bool contained = !normalized.isEmpty()
                           && QDir::cleanPath( normalized ).startsWith( root + '/', sensitivity );
    if ( !contained ) {
        LOG_WARNING << "Plugin package destination rejected root=" << root.toStdString()
                    << " destination=" << destination.toStdString();
    }
    return contained;
}

QString copyDirectory( const QString& source, const QString& destination, Budget& budget, int depth,
                       const QString& stagingRoot = {} )
{
    if ( depth > 32
         || ( !stagingRoot.isEmpty() && !containedDestination( stagingRoot, destination ) )
         || !QDir().mkpath( destination ) ) {
        return "Cannot create package directory.";
    }
    const auto root = stagingRoot.isEmpty() ? QDir( destination ).canonicalPath() : stagingRoot;
    for ( const auto& entry :
          QDir( source ).entryInfoList( QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden ) ) {
        if ( ++budget.entries > MaxPackageEntries || entry.isSymLink()
             || !safeRelativePath( entry.fileName() ) ) {
            return "Unsafe package entry or too many files.";
        }
        const auto target = QDir( destination ).filePath( entry.fileName() );
        if ( !containedDestination( root, target ) ) {
            return "Package destination escapes staging.";
        }
        if ( entry.isDir() ) {
            const auto error
                = copyDirectory( entry.absoluteFilePath(), target, budget, depth + 1, root );
            if ( !error.isEmpty() ) {
                return error;
            }
        }
        else if ( entry.isFile() ) {
            budget.bytes += entry.size();
            if ( budget.bytes > MaxPackageBytes
                 || !QFile::copy( entry.absoluteFilePath(), target ) ) {
                return "Cannot copy package or package exceeds 128 MiB.";
            }
            QFile::setPermissions( target, entry.permissions() );
        }
        else {
            return "Unsupported package entry.";
        }
    }
    return {};
}

QString extractDirectory( const KArchiveDirectory* source, const QString& destination,
                          Budget& budget, int depth, const QString& stagingRoot = {} )
{
    if ( depth > 32
         || ( !stagingRoot.isEmpty() && !containedDestination( stagingRoot, destination ) )
         || !QDir().mkpath( destination ) ) {
        return "Cannot create package directory.";
    }
    const auto root = stagingRoot.isEmpty() ? QDir( destination ).canonicalPath() : stagingRoot;
    for ( const auto& name : source->entries() ) {
        const auto* entry = source->entry( name );
        if ( ++budget.entries > MaxPackageEntries || !entry || !safeRelativePath( name )
             || name.contains( '/' ) || !entry->symLinkTarget().isEmpty() ) {
            return "Unsafe ZIP entry or too many files.";
        }
        const auto target = QDir( destination ).filePath( name );
        if ( !containedDestination( root, target ) ) {
            return "ZIP destination escapes staging.";
        }
        if ( entry->isDirectory() ) {
            const auto error = extractDirectory( static_cast<const KArchiveDirectory*>( entry ),
                                                 target, budget, depth + 1, root );
            if ( !error.isEmpty() ) {
                return error;
            }
        }
        else if ( entry->isFile() ) {
            const auto* file = static_cast<const KArchiveFile*>( entry );
            budget.bytes += file->size();
            if ( file->size() < 0 || budget.bytes > MaxPackageBytes ) {
                return "ZIP exceeds 128 MiB.";
            }
            std::unique_ptr<QIODevice> input( file->createDevice() );
            QFile output( target );
            if ( !input || !output.open( QIODevice::WriteOnly ) ) {
                return "Cannot extract ZIP file.";
            }
            qint64 written = 0;
            while ( !input->atEnd() ) {
                const auto bytes = input->read( 65536 );
                if ( bytes.isEmpty() || written + bytes.size() > file->size()
                     || output.write( bytes ) != bytes.size() ) {
                    return "ZIP read/write failure.";
                }
                written += bytes.size();
            }
            if ( written != file->size() ) {
                return "Truncated ZIP entry.";
            }
            output.close();
            // ZIP Unix mode's execute bits are meaningful for native plugin executables.
            if ( ( entry->permissions() & 0111 ) != 0 ) {
                QFile::setPermissions( target, QFile::permissions( target ) | QFile::ExeOwner
                                                   | QFile::ExeGroup | QFile::ExeOther );
            }
        }
        else {
            return "Unsupported ZIP entry.";
        }
    }
    return {};
}
} // namespace

QString PluginRepository::defaultDataRoot()
{
    const auto& appSettings = PersistentInfo::getSettings( app_settings{} );
    return dataRootForSettings( appSettings );
}

QString PluginRepository::dataRootForSettings( const QSettings& appSettings )
{
    const QFileInfo settings( appSettings.fileName() );
    const bool portable
        = appSettings.format() == QSettings::IniFormat && settings.suffix() == "conf";
    const auto root = portable
                          ? settings.absolutePath()
                          : QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
    return QDir( root ).filePath( "plugin-data" );
}

QString PluginRepository::shippedRoot()
{
#ifdef Q_OS_MAC
    return QDir( QCoreApplication::applicationDirPath() ).absoluteFilePath( "../PlugIns/cilogg" );
#else
    return QDir( QCoreApplication::applicationDirPath() ).filePath( "plugins" );
#endif
}

StagedPlugin PluginRepository::stage( const QString& source, const QString& installRoot )
{
    StagedPlugin result;
    if ( !QDir().mkpath( installRoot ) ) {
        result.error = "Cannot create plugin install directory.";
        return result;
    }
    QTemporaryDir temporary( QDir( installRoot ).filePath( ".stage-XXXXXX" ) );
    if ( !temporary.isValid() ) {
        result.error = "Cannot stage plugin installation.";
        return result;
    }
    const auto packagePath = QDir( temporary.path() ).filePath( "package" );
    Budget budget;
    const QFileInfo info( source );
    if ( info.isSymLink() ) {
        result.error = "Plugin source cannot be a symbolic link.";
    }
    else if ( info.isDir() ) {
        result.error = copyDirectory( source, packagePath, budget, 0 );
    }
    else if ( info.isFile() && info.size() <= MaxPackageBytes ) {
        KZip zip( source );
        if ( !zip.open( QIODevice::ReadOnly ) ) {
            result.error = "Cannot open plugin ZIP.";
        }
        else {
            result.error = extractDirectory( zip.directory(), packagePath, budget, 0 );
        }
    }
    else {
        result.error = "Plugin source missing or exceeds 128 MiB.";
    }
    if ( result.error.isEmpty() ) {
        result.manifest = Manifest::read( packagePath );
        result.error = result.manifest.error;
        if ( result.manifest.executable != "python"
             && !QFileInfo::exists( QDir( packagePath ).filePath( result.manifest.executable ) ) ) {
            result.error = "Plugin executable missing from package.";
        }
    }
    if ( result.error.isEmpty() ) {
        temporary.setAutoRemove( false );
        result.directory = temporary.path();
    }
    LOG_DEBUG << "Plugin stage source=" << source.toStdString() << " entries=" << budget.entries
              << " bytes=" << budget.bytes << " error=" << result.error.toStdString();
    return result;
}

QString PluginRepository::commit( const StagedPlugin& stage, const QString& installRoot )
{
    if ( !stage.error.isEmpty() || !stage.manifest.valid() ) {
        return stage.error;
    }
    QDir root( installRoot );
    const auto id = stage.manifest.id;
    const auto destination = root.filePath( id );
    const auto backup = root.filePath( ".backup-" + newId() );
    if ( QFileInfo( destination ).isSymLink() ) {
        return "Installed directory is a symbolic link.";
    }
    const bool hadOld = QFileInfo::exists( destination );
    if ( hadOld && !root.rename( destination, backup ) ) {
        return "Cannot stage existing plugin for update.";
    }
    if ( !root.rename( QDir( stage.directory ).filePath( "package" ), destination ) ) {
        const bool restored = !hadOld || root.rename( backup, destination );
        LOG_ERROR << "Plugin update rollback id=" << id.toStdString() << " restored=" << restored;
        return restored ? "Plugin update failed; previous version restored."
                        : "Plugin update failed; previous version retained in " + backup;
    }
    if ( hadOld && !QDir( backup ).removeRecursively() ) {
        LOG_WARNING << "Plugin backup cleanup failed " << backup.toStdString();
    }
    QDir( stage.directory ).removeRecursively();
    LOG_INFO << "Plugin package committed id=" << id.toStdString();
    return {};
}

QString PluginRepository::remove( const QString& id, const QString& installRoot )
{
    if ( !safeRelativePath( id ) || id.contains( '/' ) ) {
        return "Invalid plugin ID.";
    }
    const auto path = QDir( installRoot ).filePath( id );
    if ( QFileInfo( path ).isSymLink() ) {
        return "Installed directory is a symbolic link.";
    }
    if ( !QFileInfo::exists( path ) ) {
        return "No user-installed copy exists.";
    }
    return QDir( path ).removeRecursively() ? QString{}
                                            : QStringLiteral( "Cannot remove plugin files." );
}
} // namespace cilogg::plugins
