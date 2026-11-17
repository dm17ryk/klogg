#include "pluginsandpacks.h"

#include "log.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace pluginsui {
namespace {
constexpr int IdRole = Qt::UserRole;
constexpr int EntryRole = Qt::UserRole + 1;
constexpr int RepositoryRole = Qt::UserRole + 2;
constexpr int InstalledSourceRole = Qt::UserRole + 3;
constexpr int InstalledRole = Qt::UserRole + 4;
constexpr int VersionRole = Qt::UserRole + 5;

QString tr( const char* text )
{
    return QCoreApplication::translate( "PluginsAndPacks", text );
}

CommanderRequest manageRequest( const QString& operation, const QVariantMap& options,
                                const QString& pluginId = {} )
{
    CommanderRequest request;
    request.action = CommanderAction::PluginManage;
    request.pluginOperation = operation;
    request.pluginId = pluginId;
    request.pluginArguments = options;
    return request;
}

QPushButton* addButton( QBoxLayout* row, QWidget* parent, const QString& text, const QString& name )
{
    auto* button = new QPushButton( text, parent );
    button->setObjectName( name );
    button->setAccessibleName( QString( text ).remove( QChar( 0x2026 ) ).trimmed() );
    row->addWidget( button );
    return button;
}

QTableWidget* createTable( QWidget* parent, const QString& name, const QStringList& headers )
{
    auto* table = new QTableWidget( 0, int( headers.size() ), parent );
    table->setObjectName( name );
    table->setHorizontalHeaderLabels( headers );
    table->setSelectionBehavior( QAbstractItemView::SelectRows );
    table->setSelectionMode( QAbstractItemView::SingleSelection );
    table->setEditTriggers( QAbstractItemView::NoEditTriggers );
    table->verticalHeader()->setVisible( false );
    table->horizontalHeader()->setStretchLastSection( true );
    return table;
}

// A Yes/No question whose text holds names from catalogs or settings: plain text, never HTML.
bool confirm( QWidget* parent, const QString& title, const QString& text )
{
    QMessageBox box( QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::No,
                     parent );
    box.setTextFormat( Qt::PlainText );
    box.setDefaultButton( QMessageBox::No );
    return box.exec() == QMessageBox::Yes;
}

QString lastCheckText( const QString& iso )
{
    const auto time = QDateTime::fromString( iso, Qt::ISODate );
    return time.isValid() ? QLocale().toString( time.toLocalTime(), QLocale::ShortFormat )
                          : tr( "Never" );
}

// The Add and Edit dialog. Editing shows the address read-only: pointing at another catalog
// means adding a new repository (docs/plugin-repositories.md 3.1).
bool editRepository( QWidget* parent, const QString& title, QString* name, QString* address,
                     bool editing,
                     const std::function<QString( const QString&, const QString& )>& apply )
{
    QDialog dialog( parent );
    dialog.setObjectName( "pluginRepositoryDialog" );
    dialog.setWindowTitle( title );
    auto* layout = new QVBoxLayout( &dialog );
    auto* form = new QFormLayout;
    layout->addLayout( form );
    auto* nameEdit = new QLineEdit( *name, &dialog );
    nameEdit->setObjectName( "pluginRepositoryName" );
    nameEdit->setAccessibleName( tr( "Repository name" ) );
    nameEdit->setMaxLength( cilogg::plugins::PluginRepositories::MaxNameLength );
    form->addRow( tr( "Name" ), nameEdit );
    auto* addressEdit = new QLineEdit( *address, &dialog );
    addressEdit->setObjectName( "pluginRepositoryAddress" );
    addressEdit->setAccessibleName( tr( "Address of plugins.json" ) );
    addressEdit->setPlaceholderText( "https://example.com/plugins.json" );
    addressEdit->setReadOnly( editing );
    form->addRow( tr( "Address" ), addressEdit );
    auto* note = new QLabel( editing ? tr( "The address of a repository cannot be changed. To use "
                                           "another address, add a new repository and remove "
                                           "this one." )
                                     : tr( "The HTTPS address of the repository's plugins.json. "
                                           "New repositories are added at the end of the list." ),
                             &dialog );
    note->setWordWrap( true );
    layout->addWidget( note );
    auto* buttons
        = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
    buttons->button( QDialogButtonBox::Ok )->setObjectName( "pluginRepositoryAccept" );
    buttons->button( QDialogButtonBox::Cancel )->setObjectName( "pluginRepositoryCancel" );
    layout->addWidget( buttons );
    QObject::connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
    QObject::connect( buttons, &QDialogButtonBox::accepted, &dialog, [ & ] {
        const auto error = apply( nameEdit->text().trimmed(), addressEdit->text().trimmed() );
        if ( !error.isEmpty() ) {
            QMessageBox box( QMessageBox::Warning, title, error, QMessageBox::Ok, &dialog );
            box.setTextFormat( Qt::PlainText );
            box.exec();
            return;
        }
        *name = nameEdit->text().trimmed();
        *address = addressEdit->text().trimmed();
        dialog.accept();
    } );
    dialog.resize( 560, dialog.sizeHint().height() );
    return dialog.exec() == QDialog::Accepted;
}
} // namespace

QString kindLabel( const QVariantMap& entry )
{
    const auto kind = entry.value( "kind", QStringLiteral( "process" ) ).toString();
    if ( kind == QLatin1String( "process" ) ) {
        return tr( "Plugins" );
    }
    auto contents = entry.value( "contents" ).toStringList();
    // Previews come with the Lua that decodes them; Lua and Python alone are decoders.
    const bool previews = contents.contains( "previews" );
    if ( previews ) {
        contents.removeAll( "lua" );
    }
    const bool decoders
        = !previews && ( contents.contains( "lua" ) || contents.contains( "python_decoders" ) );
    if ( decoders ) {
        contents.removeAll( "lua" );
        contents.removeAll( "python_decoders" );
        if ( contents.isEmpty() ) {
            return tr( "Decoders" );
        }
    }
    else if ( contents.size() == 1 ) {
        const auto& only = contents.first();
        if ( only == QLatin1String( "previews" ) ) {
            return tr( "Previewers" );
        }
        if ( only == QLatin1String( "actions" ) ) {
            return tr( "Actions and responses" );
        }
        if ( only == QLatin1String( "highlights" ) ) {
            return tr( "Highlighters" );
        }
        if ( only == QLatin1String( "filters" ) ) {
            return tr( "Filters" );
        }
    }
    return tr( "Configuration pack" );
}

bool matchesKind( const QVariantMap& entry, KindFilter filter )
{
    const bool process = entry.value( "kind", QStringLiteral( "process" ) ).toString()
                         == QLatin1String( "process" );
    const auto contents = entry.value( "contents" ).toStringList();
    switch ( filter ) {
    case KindFilter::All:
        return true;
    case KindFilter::Plugins:
        return process;
    case KindFilter::Previewers:
        return !process && contents.contains( "previews" );
    case KindFilter::Decoders:
        return !process && ( contents.contains( "lua" ) || contents.contains( "python_decoders" ) );
    case KindFilter::Actions:
        return !process && contents.contains( "actions" );
    case KindFilter::Highlighters:
        return !process && contents.contains( "highlights" );
    case KindFilter::Filters:
        return !process && contents.contains( "filters" );
    }
    return true;
}

QString sourceLabel( const QVariantMap& plugin )
{
    const auto source = plugin.value( "source" ).toMap();
    const auto type = source.value( "type" ).toString();
    if ( type == QLatin1String( "repository" ) ) {
        const auto name = source.value( "repositoryName" ).toString();
        return source.value( "available" ).toBool()
                   ? name
                   : tr( "%1 (source unavailable)" )
                         .arg( name.isEmpty() ? source.value( "repository" ).toString() : name );
    }
    return type == QLatin1String( "local" ) ? tr( "Local file" ) : tr( "Shipped with CILogg" );
}

QString repositoryStateText( const QVariantMap& repository )
{
    const auto state = repository.value( "state" ).toString();
    if ( state == QLatin1String( "disabled" ) ) {
        return tr( "Disabled" );
    }
    if ( state == QLatin1String( "empty" ) ) {
        return tr( "No plugins in this repository" );
    }
    if ( state == QLatin1String( "failed" ) ) {
        return tr( "Failed: %1" ).arg( repository.value( "lastError" ).toString() );
    }
    if ( state == QLatin1String( "loaded" ) ) {
        const auto count = repository.value( "pluginCount" ).toInt();
        auto text = count == 1 ? tr( "1 package" ) : tr( "%1 packages" ).arg( count );
        const auto warnings = repository.value( "warnings" ).toInt();
        if ( warnings > 0 ) {
            text += tr( "; %1 invalid entries left out" ).arg( warnings );
        }
        return text;
    }
    return tr( "Not checked yet" );
}

void followJob( QObject* owner, QPointer<cilogg::plugins::PluginManager> manager,
                const QString& jobId, std::function<void( const QVariantMap& )> done )
{
    auto* timer = new QTimer( owner );
    timer->setInterval( 100 );
    QObject::connect( timer, &QTimer::timeout, owner, [ timer, manager, jobId, done ] {
        if ( !manager ) {
            timer->deleteLater();
            return;
        }
        const auto job = manager->job( jobId );
        const auto state = job.payload.value( "state" ).toString();
        if ( !job.ok() || state == "completed" || state == "failed" || state == "cancelled" ) {
            timer->stop();
            timer->deleteLater();
            done( job.ok() ? job.payload
                           : QVariantMap{ { "state", "failed" }, { "error", job.error } } );
        }
    } );
    timer->start();
}

QWidget* createAvailableTab( QWidget* parent, cilogg::plugins::PluginManager* manager,
                             Executor executor, std::function<void()> openGuide )
{
    QPointer<cilogg::plugins::PluginManager> plugins( manager );
    auto* tab = new QWidget( parent );
    tab->setObjectName( "pluginsAvailableTab" );
    auto* layout = new QVBoxLayout( tab );
    auto* filterRow = new QHBoxLayout;
    layout->addLayout( filterRow );
    auto* kindLabelWidget = new QLabel( tr( "Kind" ), tab );
    filterRow->addWidget( kindLabelWidget );
    auto* kind = new QComboBox( tab );
    kind->setObjectName( "pluginKindFilter" );
    kind->setAccessibleName( tr( "Kind" ) );
    kind->addItem( tr( "All" ), int( KindFilter::All ) );
    kind->addItem( tr( "Plugins" ), int( KindFilter::Plugins ) );
    kind->addItem( tr( "Previewers" ), int( KindFilter::Previewers ) );
    kind->addItem( tr( "Decoders (Lua and Python)" ), int( KindFilter::Decoders ) );
    kind->addItem( tr( "Actions and responses" ), int( KindFilter::Actions ) );
    kind->addItem( tr( "Highlighters" ), int( KindFilter::Highlighters ) );
    kind->addItem( tr( "Filters" ), int( KindFilter::Filters ) );
    kindLabelWidget->setBuddy( kind );
    filterRow->addWidget( kind );
    auto* search = new QLineEdit( tab );
    search->setObjectName( "pluginCatalogSearch" );
    search->setAccessibleName( tr( "Search packages" ) );
    search->setPlaceholderText( tr( "Search" ) );
    search->setClearButtonEnabled( true );
    filterRow->addWidget( search, 1 );
    auto* fetch = addButton( filterRow, tab, tr( "Fetch" ), "fetchPluginCatalog" );
    fetch->setToolTip( tr( "Load the catalogs of all enabled repositories" ) );
    auto* table = createTable( tab, "pluginCatalogTable",
                               { tr( "Name" ), tr( "Kind" ), tr( "Version" ), tr( "Source" ),
                                 tr( "Installed" ), tr( "Description" ) } );
    table->setAccessibleName( tr( "Available packages" ) );
    layout->addWidget( table );
    auto* status = new QLabel( tab );
    status->setObjectName( "pluginCatalogStatus" );
    status->setAccessibleName( tr( "Catalog status" ) );
    status->setWordWrap( true );
    status->setTextFormat( Qt::RichText );
    status->setTextInteractionFlags( Qt::TextBrowserInteraction );
    QObject::connect( status, &QLabel::linkActivated, tab,
                      [ openGuide ]( const QString& ) { openGuide(); } );
    layout->addWidget( status );
    auto* buttonRow = new QHBoxLayout;
    layout->addLayout( buttonRow );
    buttonRow->addStretch( 1 );
    auto* install = addButton( buttonRow, tab, tr( "Install / Update" ), "installCatalogPlugin" );
    install->setToolTip( tr( "Install the selected package, or update it from its source" ) );
    auto running = std::make_shared<QString>();

    const auto setStatus = [ status ]( const QString& text, bool guideLink = false ) {
        auto html = text.toHtmlEscaped();
        if ( guideLink ) {
            html += QStringLiteral( " <a href=\"guide\">%1</a>" )
                        .arg( tr( "How to publish plugins" ).toHtmlEscaped() );
        }
        status->setText( html );
    };
    // Groups in repository order: a header row per enabled repository, then its packages.
    const auto render = [ plugins, table, kind, search, setStatus ]( bool reportStatus ) {
        if ( !plugins ) {
            return;
        }
        const auto selected = table->currentRow() >= 0 && table->item( table->currentRow(), 0 )
                                  ? table->item( table->currentRow(), 0 )->data( EntryRole )
                                  : QVariant{};
        const auto filter = KindFilter( kind->currentData().toInt() );
        const auto text = search->text().trimmed();
        const auto entries = plugins->catalogEntries();
        const auto list = plugins->repositories();
        QMap<QString, QString> names;
        for ( const auto& value : list ) {
            names.insert( value.toMap().value( "id" ).toString(),
                          value.toMap().value( "name" ).toString() );
        }
        table->clearSpans();
        table->setRowCount( 0 );
        int packages = 0;
        int offered = 0;
        int repositories = 0;
        int failed = 0;
        for ( const auto& value : list ) {
            const auto repository = value.toMap();
            if ( !repository.value( "enabled" ).toBool() ) {
                continue;
            }
            ++repositories;
            failed += repository.value( "state" ) == "failed" ? 1 : 0;
            const auto id = repository.value( "id" ).toString();
            const int header = table->rowCount();
            table->insertRow( header );
            auto* title = new QTableWidgetItem( repository.value( "name" ).toString()
                                                + QStringLiteral( " — " )
                                                + repositoryStateText( repository ) );
            auto font = title->font();
            font.setBold( true );
            title->setFont( font );
            title->setFlags( Qt::ItemIsEnabled );
            title->setData( RepositoryRole, id );
            table->setItem( header, 0, title );
            table->setSpan( header, 0, 1, table->columnCount() );
            for ( const auto& item : entries ) {
                const auto entry = item.toMap();
                if ( entry.value( "repository" ) != id ) {
                    continue;
                }
                ++offered;
                if ( !matchesKind( entry, filter ) ) {
                    continue;
                }
                const auto name = entry.value( "name", entry.value( "id" ) ).toString();
                if ( !text.isEmpty() && !name.contains( text, Qt::CaseInsensitive )
                     && !entry.value( "id" ).toString().contains( text, Qt::CaseInsensitive )
                     && !entry.value( "description" )
                             .toString()
                             .contains( text, Qt::CaseInsensitive ) ) {
                    continue;
                }
                ++packages;
                const auto installed = entry.value( "installed" ).toString();
                const auto installedSource = entry.value( "installedSource" ).toString();
                auto installedText = installed;
                if ( !installed.isEmpty() && installedSource != id ) {
                    // Installed from elsewhere: installing this row is a source switch.
                    installedText = installedSource == QLatin1String( "local" )
                                        ? tr( "%1 (local file)" ).arg( installed )
                                    : installedSource == QLatin1String( "shipped" )
                                        ? tr( "%1 (shipped with CILogg)" ).arg( installed )
                                        : tr( "%1 from %2" )
                                              .arg( installed, names.value( installedSource,
                                                                            installedSource ) );
                }
                const QStringList cells{ name,
                                         kindLabel( entry ),
                                         entry.value( "version" ).toString(),
                                         entry.value( "repositoryName" ).toString(),
                                         installedText,
                                         entry.value( "description" ).toString() };
                const int row = table->rowCount();
                table->insertRow( row );
                for ( int column = 0; column < cells.size(); ++column ) {
                    auto* cell = new QTableWidgetItem( cells.at( column ) );
                    cell->setData( IdRole, entry.value( "id" ) );
                    cell->setData( EntryRole, entry.value( "entry_id" ) );
                    cell->setData( RepositoryRole, id );
                    cell->setData( InstalledSourceRole, installedSource );
                    cell->setData( InstalledRole, installed );
                    cell->setData( VersionRole, entry.value( "version" ) );
                    cell->setToolTip( column == 0 ? entry.value( "id" ).toString()
                                                  : cells.at( column ) );
                    table->setItem( row, column, cell );
                }
                if ( selected.isValid() && entry.value( "entry_id" ) == selected ) {
                    table->selectRow( row );
                }
            }
        }
        table->resizeColumnsToContents();
        table->horizontalHeader()->setStretchLastSection( true );
        if ( !reportStatus ) {
            return;
        }
        if ( repositories == 0 ) {
            setStatus( tr( "No repositories are enabled. Add or enable one on the Repositories "
                           "tab." ),
                       true );
        }
        else if ( failed == repositories ) {
            setStatus(
                failed == 1
                    ? tr( "The enabled repository could not be loaded." )
                    : tr( "None of the %1 enabled repositories could be loaded." ).arg( failed ),
                true );
        }
        else if ( offered == 0 ) {
            setStatus( tr( "No plugins in the enabled repositories." ), true );
        }
        else {
            auto summary
                = ( packages == 1 ? tr( "1 package" ) : tr( "%1 packages" ).arg( packages ) )
                  + ( repositories == 1 ? tr( " from 1 repository" )
                                        : tr( " from %1 repositories" ).arg( repositories ) );
            if ( failed > 0 ) {
                summary += failed == 1 ? tr( "; 1 repository failed" )
                                       : tr( "; %1 repositories failed" ).arg( failed );
            }
            setStatus( summary + '.' );
        }
    };
    QObject::connect( kind, qOverload<int>( &QComboBox::currentIndexChanged ), tab,
                      [ render ] { render( false ); } );
    QObject::connect( search, &QLineEdit::textChanged, tab, [ render ] { render( false ); } );
    QObject::connect( manager, &cilogg::plugins::PluginManager::changed, tab, [ render, running ] {
        if ( running->isEmpty() ) {
            render( false );
        }
    } );
    QObject::connect(
        fetch, &QPushButton::clicked, tab,
        [ tab, plugins, executor, running, render, setStatus, fetch ] {
            const auto result = executor( manageRequest( "catalog", {} ) );
            if ( !result.ok() ) {
                setStatus( result.message );
                return;
            }
            if ( !plugins ) {
                return;
            }
            int enabled = 0;
            for ( const auto& value : plugins->repositories() ) {
                enabled += value.toMap().value( "enabled" ).toBool() ? 1 : 0;
            }
            setStatus( enabled == 1 ? tr( "Loading 1 repository…" )
                                    : tr( "Loading %1 repositories…" ).arg( enabled ) );
            fetch->setEnabled( false );
            *running = result.payload.value( "jobId" ).toString();
            followJob( tab, plugins, *running,
                       [ running, render, setStatus, fetch ]( const QVariantMap& job ) {
                           running->clear();
                           fetch->setEnabled( true );
                           if ( job.value( "state" ) == "cancelled" ) {
                               setStatus( tr( "Fetch was replaced by a newer one." ) );
                               return;
                           }
                           render( true );
                       } );
        } );
    QObject::connect(
        install, &QPushButton::clicked, tab, [ tab, plugins, executor, table, setStatus, render ] {
            const auto row = table->currentRow();
            const auto* first = row >= 0 ? table->item( row, 0 ) : nullptr;
            if ( !first || first->data( EntryRole ).toString().isEmpty() ) {
                setStatus( tr( "Select a package first." ) );
                return;
            }
            // Copied now: the table is rebuilt whenever the manager changes, also while the
            // confirmation below waits.
            const auto id = first->data( IdRole ).toString();
            const auto repository = first->data( RepositoryRole ).toString();
            const auto installedSource = first->data( InstalledSourceRole ).toString();
            const bool installed = !first->data( InstalledRole ).toString().isEmpty();
            const auto version = first->data( VersionRole ).toString();
            const auto name = first->text();
            QVariantMap options{ { "catalog_entry_id", first->data( EntryRole ) } };
            first = nullptr;
            const bool otherRepository = installed && !installedSource.isEmpty()
                                         && installedSource != QLatin1String( "local" )
                                         && installedSource != QLatin1String( "shipped" )
                                         && installedSource != repository;
            if ( otherRepository && plugins ) {
                QString from = installedSource;
                QString to = repository;
                for ( const auto& value : plugins->repositories() ) {
                    const auto record = value.toMap();
                    if ( record.value( "id" ) == installedSource ) {
                        from = record.value( "name" ).toString();
                    }
                    if ( record.value( "id" ) == repository ) {
                        to = record.value( "name" ).toString();
                    }
                }
                // Switch source: later updates come only from the new repository. Names come
                // from catalogs and settings, so they are shown as plain text.
                if ( !confirm( tab, tr( "Switch Source" ),
                               tr( "%1 was installed from \"%2\". Install it from \"%3\" "
                                   "instead? Updates will then come only from \"%3\"." )
                                   .arg( name, from, to ) ) ) {
                    return;
                }
                options.insert( "switch_source", true );
            }
            const auto result = executor( manageRequest(
                installed && installedSource == repository ? "update" : "install", options, id ) );
            if ( !result.ok() ) {
                setStatus( result.message );
                return;
            }
            setStatus( tr( "Installing %1 %2…" ).arg( name, version ) );
            followJob( tab, plugins, result.payload.value( "jobId" ).toString(),
                       [ setStatus, render, name, version ]( const QVariantMap& job ) {
                           render( false );
                           setStatus( job.value( "state" ) == "completed"
                                          ? tr( "Installed %1 %2." ).arg( name, version )
                                          : tr( "Could not install %1: %2" )
                                                .arg( name, job.value( "error" ).toString() ) );
                       } );
        } );
    render( false );
    setStatus( plugins && plugins->catalogEntries().isEmpty()
                   ? tr( "Press Fetch to load the catalogs of the enabled repositories." )
                   : QString{} );
    return tab;
}

QWidget* createRepositoriesTab( QWidget* parent, cilogg::plugins::PluginManager* manager,
                                Executor executor, std::function<void()> openGuide )
{
    QPointer<cilogg::plugins::PluginManager> plugins( manager );
    auto* tab = new QWidget( parent );
    tab->setObjectName( "pluginsRepositoriesTab" );
    auto* layout = new QVBoxLayout( tab );
    auto* table = createTable( tab, "pluginRepositoriesTable",
                               { tr( "#" ), tr( "Enabled" ), tr( "Name" ), tr( "Address" ),
                                 tr( "State" ), tr( "Last check" ) } );
    table->setAccessibleName( tr( "Plugin repositories" ) );
    layout->addWidget( table );
    auto* note = new QLabel(
        tr( "The order is a priority: the Available tab lists repositories in this order, and a "
            "new install of a plugin that several repositories offer takes the higher one. "
            "Installed plugins keep the repository they came from." ),
        tab );
    note->setWordWrap( true );
    layout->addWidget( note );
    auto* status = new QLabel( tab );
    status->setObjectName( "pluginRepositoriesStatus" );
    status->setWordWrap( true );
    status->setTextFormat( Qt::PlainText );
    layout->addWidget( status );
    auto* row = new QHBoxLayout;
    layout->addLayout( row );
    auto* add = addButton( row, tab, tr( "Add…" ), "pluginRepositoryAdd" );
    auto* edit = addButton( row, tab, tr( "Edit…" ), "pluginRepositoryEdit" );
    auto* remove = addButton( row, tab, tr( "Remove" ), "pluginRepositoryRemove" );
    auto* up = addButton( row, tab, tr( "Move Up" ), "pluginRepositoryUp" );
    auto* down = addButton( row, tab, tr( "Move Down" ), "pluginRepositoryDown" );
    row->addStretch( 1 );
    auto* howTo = addButton( row, tab, tr( "How to…" ), "pluginRepositoriesHowTo" );
    howTo->setToolTip( tr( "How to pack a plugin, create a repository and use it" ) );
    QObject::connect( howTo, &QPushButton::clicked, tab, [ openGuide ] { openGuide(); } );

    const auto selectedId = [ table ] {
        const auto current = table->currentRow();
        return current >= 0 && table->item( current, 2 )
                   ? table->item( current, 2 )->data( IdRole ).toString()
                   : QString{};
    };
    const auto updateButtons = [ table, up, down, edit, remove ] {
        const auto current = table->currentRow();
        up->setEnabled( current > 0 );
        down->setEnabled( current >= 0 && current + 1 < table->rowCount() );
        edit->setEnabled( current >= 0 );
        remove->setEnabled( current >= 0 );
    };
    const auto refresh = [ plugins, table, updateButtons ]( const QString& keep ) {
        if ( !plugins ) {
            return;
        }
        const QSignalBlocker blocker( table );
        const auto list = plugins->repositories();
        table->setRowCount( int( list.size() ) );
        int selectedRow = -1;
        for ( int index = 0; index < list.size(); ++index ) {
            const auto repository = list.at( index ).toMap();
            const auto id = repository.value( "id" ).toString();
            const QStringList cells{ QString::number( repository.value( "position" ).toInt() ),
                                     QString{},
                                     repository.value( "name" ).toString(),
                                     repository.value( "url" ).toString(),
                                     repositoryStateText( repository ),
                                     lastCheckText( repository.value( "lastCheck" ).toString() ) };
            for ( int column = 0; column < cells.size(); ++column ) {
                auto* cell = new QTableWidgetItem( cells.at( column ) );
                cell->setData( IdRole, id );
                cell->setToolTip( column == 4 && repository.value( "state" ) == "failed"
                                      ? repository.value( "lastError" ).toString()
                                      : cells.at( column ) );
                if ( column == 1 ) {
                    cell->setFlags( Qt::ItemIsEnabled | Qt::ItemIsSelectable
                                    | Qt::ItemIsUserCheckable );
                    cell->setCheckState( repository.value( "enabled" ).toBool() ? Qt::Checked
                                                                                : Qt::Unchecked );
                    cell->setData( Qt::AccessibleTextRole, repository.value( "enabled" ).toBool()
                                                               ? tr( "Enabled" )
                                                               : tr( "Disabled" ) );
                }
                table->setItem( index, column, cell );
            }
            if ( id == keep ) {
                selectedRow = index;
            }
        }
        if ( selectedRow >= 0 ) {
            table->selectRow( selectedRow );
        }
        table->resizeColumnsToContents();
        table->horizontalHeader()->setStretchLastSection( true );
        updateButtons();
    };
    const auto run
        = [ executor, status, refresh ]( const QString& operation, const QVariantMap& options,
                                         const QString& keep ) {
              const auto result = executor( manageRequest( operation, options ) );
              status->setText( result.ok() ? QString{} : result.message );
              refresh( keep );
              return result;
          };
    QObject::connect( table, &QTableWidget::itemSelectionChanged, tab, updateButtons );
    QObject::connect( table, &QTableWidget::itemChanged, tab,
                      [ tab, run, selectedId ]( QTableWidgetItem* item ) {
                          if ( item->column() != 1 ) {
                              return;
                          }
                          const auto id = item->data( IdRole ).toString();
                          const bool enabled = item->checkState() == Qt::Checked;
                          // The table is rebuilt afterwards: not inside its own signal.
                          QTimer::singleShot( 0, tab, [ run, selectedId, id, enabled ] {
                              run( "repository_update",
                                   { { "repository", id }, { "enabled", enabled } }, selectedId() );
                          } );
                      } );
    QObject::connect( manager, &cilogg::plugins::PluginManager::changed, tab,
                      [ refresh, selectedId ] { refresh( selectedId() ); } );
    QObject::connect( add, &QPushButton::clicked, tab, [ tab, executor, run ] {
        QString name;
        QString address;
        QString added;
        editRepository(
            tab, tr( "Add Repository" ), &name, &address, false,
            [ executor, &added ]( const QString& newName, const QString& url ) {
                const auto result = executor(
                    manageRequest( "repository_add", { { "name", newName }, { "url", url } } ) );
                if ( result.ok() ) {
                    added = result.payload.value( "repository" ).toMap().value( "id" ).toString();
                }
                return result.ok() ? QString{} : result.message;
            } );
        if ( !added.isEmpty() ) {
            run( "repositories", {}, added );
        }
    } );
    QObject::connect(
        edit, &QPushButton::clicked, tab, [ tab, plugins, executor, run, selectedId ] {
            const auto id = selectedId();
            if ( id.isEmpty() || !plugins ) {
                return;
            }
            QString name;
            QString address;
            for ( const auto& value : plugins->repositories() ) {
                if ( value.toMap().value( "id" ) == id ) {
                    name = value.toMap().value( "name" ).toString();
                    address = value.toMap().value( "url" ).toString();
                }
            }
            if ( editRepository( tab, tr( "Edit Repository" ), &name, &address, true,
                                 [ executor, id ]( const QString& newName, const QString& ) {
                                     const auto result = executor( manageRequest(
                                         "repository_update",
                                         { { "repository", id }, { "name", newName } } ) );
                                     return result.ok() ? QString{} : result.message;
                                 } ) ) {
                run( "repositories", {}, id );
            }
        } );
    QObject::connect( remove, &QPushButton::clicked, tab, [ tab, table, run, selectedId ] {
        const auto id = selectedId();
        if ( id.isEmpty() ) {
            return;
        }
        const auto name = table->item( table->currentRow(), 2 )->text();
        if ( confirm( tab, tr( "Remove Repository" ),
                      tr( "Remove the repository \"%1\"? Plugins installed from it stay "
                          "installed and get no updates until it is added again or their source "
                          "is switched." )
                          .arg( name ) ) ) {
            run( "repository_remove", { { "repository", id } }, {} );
        }
    } );
    for ( const auto& [ button, direction ] :
          { std::make_pair( up, QStringLiteral( "up" ) ),
            std::make_pair( down, QStringLiteral( "down" ) ) } ) {
        QObject::connect( button, &QPushButton::clicked, tab,
                          [ run, selectedId, towards = direction ] {
                              const auto id = selectedId();
                              if ( !id.isEmpty() ) {
                                  run( "repository_move",
                                       { { "repository", id }, { "direction", towards } }, id );
                              }
                          } );
    }
    refresh( {} );
    return tab;
}
} // namespace pluginsui
