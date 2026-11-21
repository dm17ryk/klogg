#include "pluginupdatenotices.h"
#include "diagnostics.h"
#include "log.h"
#include "pluginmanager.h"
#include <QAction>
#include <QApplication>
#include <QPushButton>

namespace {
// The Plugins menu's Manage Plugins… entry of a main window (PluginUiController).
constexpr auto ManagePluginsAction = "managePluginsAction";

QString pluginName( const QVariantMap& update )
{
    const auto name = update.value( "name" ).toString();
    return name.isEmpty() ? update.value( "id" ).toString() : name;
}

// Journal data: names and versions only, never an address or a credential.
QVariantMap journalData( const QVariantMap& update )
{
    return { { "plugin", update.value( "id" ) },
             { "from", update.value( "from" ) },
             { "to", update.value( "to" ) },
             { "repository", update.value( "repository" ) } };
}
} // namespace

PluginUpdateNotices::PluginUpdateNotices( cilogg::plugins::PluginManager* manager, QObject* parent )
    : QObject( parent )
    , manager_( manager )
{
    using cilogg::plugins::PluginManager;
    connect( manager, &PluginManager::updatesFound, this, &PluginUpdateNotices::showFound );
    connect( manager, &PluginManager::autoUpdated, this, &PluginUpdateNotices::autoUpdated );
    connect( manager, &PluginManager::autoUpdateFailed, this,
             &PluginUpdateNotices::autoUpdateFailed );
}

void PluginUpdateNotices::setInteractive( bool interactive )
{
    interactive_ = interactive;
}

QWidget* PluginUpdateNotices::noticeParent()
{
    auto* active = QApplication::activeWindow();
    if ( active && active->findChild<QAction*>( ManagePluginsAction ) ) {
        return active;
    }
    // The active window may be a dialog such as Plugins and Packs: use a main window.
    const auto windows = QApplication::topLevelWidgets();
    for ( auto* widget : windows ) {
        if ( widget->isVisible() && widget->findChild<QAction*>( ManagePluginsAction ) ) {
            return widget;
        }
    }
    return active;
}

void PluginUpdateNotices::openPlugins( QWidget* window )
{
    if ( auto* action = window ? window->findChild<QAction*>( ManagePluginsAction ) : nullptr ) {
        action->trigger();
    }
}

QMessageBox* PluginUpdateNotices::notice( QMessageBox::Icon icon, const QString& objectName,
                                          const QString& text )
{
    auto* parent = noticeParent();
    auto* box = new QMessageBox( icon, tr( "Plugin Updates" ), text, QMessageBox::NoButton, parent );
    box->setObjectName( objectName );
    // Names, versions and errors come from catalogs and downloads.
    box->setTextFormat( Qt::PlainText );
    box->setAttribute( Qt::WA_DeleteOnClose );
    box->setWindowModality( Qt::NonModal );
    return box;
}

void PluginUpdateNotices::showFound( const QVariantList& updates )
{
    if ( !interactive_ || updates.isEmpty() ) {
        return;
    }
    QPointer<cilogg::plugins::PluginManager> manager( manager_ );
    if ( updates.size() > MaxSeparateNotices ) {
        QStringList lines;
        for ( const auto& value : updates ) {
            const auto update = value.toMap();
            lines.append( QStringLiteral( "• %1 %2" ).arg( pluginName( update ),
                                                           update.value( "available" ).toString() ) );
        }
        auto* box = notice( QMessageBox::Information, "pluginUpdatesNotice",
                            tr( "%1 plugin updates are available:\n%2" )
                                .arg( updates.size() )
                                .arg( lines.join( '\n' ) ) );
        auto* open = box->addButton( tr( "Open Plugins" ), QMessageBox::AcceptRole );
        open->setObjectName( "pluginUpdateNoticeOpen" );
        box->addButton( QMessageBox::Close );
        connect( box, &QMessageBox::buttonClicked, box, [ box, open ]( QAbstractButton* button ) {
            if ( button == open ) {
                openPlugins( box->parentWidget() );
            }
        } );
        box->show();
        return;
    }
    for ( const auto& value : updates ) {
        const auto update = value.toMap();
        const auto id = update.value( "id" ).toString();
        const auto available = update.value( "available" ).toString();
        auto text = tr( "%1 %2 is available from \"%3\". Installed: %4." )
                        .arg( pluginName( update ), available,
                              update.value( "repositoryName" ).toString(),
                              update.value( "installed" ).toString() );
        if ( update.value( "majorChange" ).toBool() ) {
            text += '\n' + tr( "This is a new major version: it is never installed "
                               "automatically." );
        }
        auto* box = notice( QMessageBox::Information, "pluginUpdateNotice", text );
        box->setProperty( "pluginId", id );
        auto* open = box->addButton( tr( "Open Plugins" ), QMessageBox::AcceptRole );
        open->setObjectName( "pluginUpdateNoticeOpen" );
        auto* skip = box->addButton( tr( "Skip This Version" ), QMessageBox::DestructiveRole );
        skip->setObjectName( "pluginUpdateNoticeSkip" );
        box->addButton( QMessageBox::Close );
        connect( box, &QMessageBox::buttonClicked, box,
                 [ box, open, skip, manager, id, available ]( QAbstractButton* button ) {
                     if ( button == open ) {
                         openPlugins( box->parentWidget() );
                     }
                     else if ( button == skip && manager ) {
                         const auto result
                             = manager->manage( "skip_update", id, { { "version", available } },
                                                cilogg::plugins::RequestOriginGui );
                         if ( !result.ok() ) {
                             LOG_WARNING << "Skip this version failed: "
                                         << result.error.toStdString();
                         }
                     }
                 } );
        box->show();
    }
}

void PluginUpdateNotices::autoUpdated( const QVariantMap& update )
{
    const auto text = tr( "%1 was updated automatically from %2 to %3 (repository \"%4\")." )
                          .arg( pluginName( update ), update.value( "from" ).toString(),
                                update.value( "to" ).toString(),
                                update.value( "repositoryName" ).toString() );
    cilogg::diagnostics::ActivityJournal::record( QStringLiteral( "plugin" ), text,
                                                  journalData( update ) );
    if ( !interactive_ ) {
        return;
    }
    auto* box = notice( QMessageBox::Information, "pluginAutoUpdateNotice", text );
    box->addButton( QMessageBox::Close );
    box->show();
}

void PluginUpdateNotices::autoUpdateFailed( const QVariantMap& update )
{
    cilogg::diagnostics::ActivityJournal::record(
        QStringLiteral( "plugin" ),
        QStringLiteral( "Automatic update of %1 to %2 failed" )
            .arg( pluginName( update ), update.value( "to" ).toString() ),
        journalData( update ) );
    if ( !interactive_ ) {
        return;
    }
    // A pack update that changes items the user edited waits for the merge window (design 4.6).
    const bool needsMerge = update.value( "needsMerge" ).toBool();
    auto* box = needsMerge
                    ? notice( QMessageBox::Information, "pluginAutoUpdateMergeNotice",
                              tr( "%1 %2 changes items you edited, so it was not installed "
                                  "automatically. Open Plugins and click Update to merge it with "
                                  "your edits." )
                                  .arg( pluginName( update ), update.value( "to" ).toString() ) )
                    : notice( QMessageBox::Warning, "pluginAutoUpdateFailedNotice",
                              tr( "The automatic update of %1 to %2 did not work: %3\nCILogg tries "
                                  "again at the next update check." )
                                  .arg( pluginName( update ), update.value( "to" ).toString(),
                                        update.value( "error" ).toString() ) );
    auto* open = box->addButton( tr( "Open Plugins" ), QMessageBox::AcceptRole );
    open->setObjectName( "pluginUpdateNoticeOpen" );
    box->addButton( QMessageBox::Close );
    connect( box, &QMessageBox::buttonClicked, box, [ box, open ]( QAbstractButton* button ) {
        if ( button == open ) {
            openPlugins( box->parentWidget() );
        }
    } );
    box->show();
}
