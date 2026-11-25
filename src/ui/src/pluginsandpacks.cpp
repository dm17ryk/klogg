#include "pluginsandpacks.h"

#include "log.h"
#include "packeditors.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
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
#include <algorithm>

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
    request.origin = CommanderOriginGui; // a sign-in from this window may prompt
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

// Columns of the Repositories table (docs/plugin-repositories.md 3.3).
enum RepositoryColumn {
    PositionColumn,
    EnabledColumn,
    NameColumn,
    AddressColumn,
    SignInColumn,
    StateColumn,
    LastCheckColumn,
    AutoUpdateColumn, // design 3.7: off by default
    RepositoryColumnCount
};

struct RepositoryForm {
    QString name;
    QString address;
    QString signIn = cilogg::plugins::SignInNone;
    QString branch;
    QString path;
    QString credentialSource = cilogg::plugins::CredentialSourceGitCredentialManager;
};

QString signInText( const QVariantMap& repository )
{
    if ( repository.value( "signIn" ).toString() != cilogg::plugins::SignInAzureDevOps ) {
        return tr( "None" );
    }
    return repository.value( "credentialSource" ).toString()
                   == cilogg::plugins::CredentialSourcePersonalAccessToken
               ? tr( "Azure DevOps (personal access token)" )
               : tr( "Azure DevOps (Git Credential Manager)" );
}

void showWarning( QWidget* parent, const QString& title, const QString& text )
{
    QMessageBox box( QMessageBox::Warning, title, text, QMessageBox::Ok, parent );
    box.setTextFormat( Qt::PlainText );
    box.exec();
}

// The Add and Edit dialog. Editing shows the address, sign-in, branch and path read-only:
// pointing at another catalog means adding a new repository (docs/plugin-repositories.md 3.1).
bool editRepository( QWidget* parent, const QString& title, RepositoryForm* values, bool editing,
                     const std::function<QString( const RepositoryForm& )>& apply )
{
    using namespace cilogg::plugins;
    QDialog dialog( parent );
    dialog.setObjectName( "pluginRepositoryDialog" );
    dialog.setWindowTitle( title );
    auto* layout = new QVBoxLayout( &dialog );
    auto* form = new QFormLayout;
    layout->addLayout( form );
    auto* nameEdit = new QLineEdit( values->name, &dialog );
    nameEdit->setObjectName( "pluginRepositoryName" );
    nameEdit->setAccessibleName( tr( "Repository name" ) );
    nameEdit->setMaxLength( PluginRepositories::MaxNameLength );
    form->addRow( tr( "Name" ), nameEdit );
    auto* signIn = new QComboBox( &dialog );
    signIn->setObjectName( "pluginRepositorySignIn" );
    signIn->setAccessibleName( tr( "Sign-in" ) );
    signIn->addItem( tr( "None (public HTTPS address)" ), SignInNone );
    signIn->addItem( tr( "Azure DevOps" ), SignInAzureDevOps );
    signIn->setCurrentIndex( std::max( 0, signIn->findData( values->signIn ) ) );
    signIn->setEnabled( !editing );
    form->addRow( tr( "Sign-in" ), signIn );
    auto* addressEdit = new QLineEdit( values->address, &dialog );
    addressEdit->setObjectName( "pluginRepositoryAddress" );
    addressEdit->setReadOnly( editing );
    form->addRow( tr( "Address" ), addressEdit );
    auto* branchEdit = new QLineEdit( values->branch, &dialog );
    branchEdit->setObjectName( "pluginRepositoryBranch" );
    branchEdit->setAccessibleName( tr( "Catalog branch" ) );
    branchEdit->setPlaceholderText( AzureDevOpsDefaultBranch );
    branchEdit->setReadOnly( editing );
    form->addRow( tr( "Branch" ), branchEdit );
    auto* pathEdit = new QLineEdit( values->path, &dialog );
    pathEdit->setObjectName( "pluginRepositoryPath" );
    pathEdit->setAccessibleName( tr( "Catalog path" ) );
    pathEdit->setPlaceholderText( AzureDevOpsDefaultPath );
    pathEdit->setReadOnly( editing );
    form->addRow( tr( "Path" ), pathEdit );
    auto* source = new QComboBox( &dialog );
    source->setObjectName( "pluginRepositoryCredentialSource" );
    source->setAccessibleName( tr( "Credential source" ) );
    source->addItem( tr( "Git Credential Manager" ), CredentialSourceGitCredentialManager );
    source->addItem( tr( "Personal access token" ), CredentialSourcePersonalAccessToken );
    source->setCurrentIndex( std::max( 0, source->findData( values->credentialSource ) ) );
    form->addRow( tr( "Credential" ), source );
    auto* note = new QLabel( &dialog );
    note->setWordWrap( true );
    layout->addWidget( note );
    const auto updateFields = [ = ] {
        const bool azure = signIn->currentData().toString() == SignInAzureDevOps;
        for ( auto* field : std::initializer_list<QWidget*>{ branchEdit, pathEdit, source } ) {
            field->setVisible( azure );
            if ( auto* label = form->labelForField( field ) ) {
                label->setVisible( azure );
            }
        }
        addressEdit->setAccessibleName( azure ? tr( "Azure DevOps repository address" )
                                              : tr( "Address of plugins.json" ) );
        addressEdit->setPlaceholderText(
            azure ? "https://dev.azure.com/<organization>/<project>/_git/<repository>"
                  : "https://example.com/plugins.json" );
        note->setText(
            editing ? tr( "The address of a repository cannot be changed. To use another address, "
                          "add a new repository and remove this one." )
            : azure ? tr( "Paste the repository address or a link to plugins.json copied from the "
                          "Azure DevOps web portal. Branch and path default to catalog and "
                          "/plugins.json. CILogg signs in through the Git Credential Manager you "
                          "use for Git, or with a personal access token with the Code (Read) "
                          "scope." )
                    : tr( "The HTTPS address of the repository's plugins.json. New repositories "
                          "are added at the end of the list." ) );
    };
    QObject::connect( signIn, qOverload<int>( &QComboBox::currentIndexChanged ), &dialog,
                      updateFields );
    updateFields();
    auto* buttons
        = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
    buttons->button( QDialogButtonBox::Ok )->setObjectName( "pluginRepositoryAccept" );
    buttons->button( QDialogButtonBox::Cancel )->setObjectName( "pluginRepositoryCancel" );
    layout->addWidget( buttons );
    QObject::connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
    QObject::connect( buttons, &QDialogButtonBox::accepted, &dialog, [ & ] {
        RepositoryForm entered;
        entered.name = nameEdit->text().trimmed();
        entered.address = addressEdit->text().trimmed();
        entered.signIn = signIn->currentData().toString();
        entered.branch = branchEdit->text().trimmed();
        entered.path = pathEdit->text().trimmed();
        entered.credentialSource = source->currentData().toString();
        const auto error = apply( entered );
        if ( !error.isEmpty() ) {
            showWarning( &dialog, title, error );
            return;
        }
        *values = entered;
        dialog.accept();
    } );
    dialog.resize( 600, dialog.sizeHint().height() );
    return dialog.exec() == QDialog::Accepted;
}

// Asks for a personal access token in a password field; empty when cancelled.
QString askToken( QWidget* parent, const QString& repositoryName )
{
    QDialog dialog( parent );
    dialog.setObjectName( "pluginRepositoryTokenDialog" );
    dialog.setWindowTitle( tr( "Sign In" ) );
    auto* layout = new QVBoxLayout( &dialog );
    auto* text = new QLabel(
        tr( "Personal access token for \"%1\". Create one in Azure DevOps (User settings > "
            "Personal access tokens) with the Code (Read) scope. CILogg keeps it in the "
            "operating system credential store, never in its settings." )
            .arg( repositoryName ),
        &dialog );
    text->setTextFormat( Qt::PlainText );
    text->setWordWrap( true );
    layout->addWidget( text );
    auto* token = new QLineEdit( &dialog );
    token->setObjectName( "pluginRepositoryToken" );
    token->setAccessibleName( tr( "Personal access token" ) );
    token->setEchoMode( QLineEdit::Password );
    layout->addWidget( token );
    auto* buttons
        = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
    buttons->button( QDialogButtonBox::Ok )->setObjectName( "pluginRepositoryTokenAccept" );
    layout->addWidget( buttons );
    QObject::connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
    QObject::connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
    dialog.resize( 520, dialog.sizeHint().height() );
    if ( dialog.exec() != QDialog::Accepted ) {
        return {};
    }
    const auto value = token->text().trimmed();
    token->clear();
    return value;
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
    if ( type == QLatin1String( "local" ) ) {
        // The folder or ZIP it was installed from, when CILogg recorded it.
        const auto path = source.value( "path" ).toString();
        if ( path.isEmpty() ) {
            return tr( "Local file" );
        }
        return ( source.value( "pathKind" ) == QLatin1String( "zip" ) ? tr( "Local ZIP: %1" )
                                                                      : tr( "Local folder: %1" ) )
            .arg( QDir::toNativeSeparators( path ) );
    }
    return tr( "Shipped with CILogg" );
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
    if ( state == cilogg::plugins::SignInRequiredState ) {
        return tr( "Sign-in needed" );
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
            // A failed refresh keeps the last catalog that loaded; say so when one is listed.
            setStatus(
                ( failed == 1
                      ? tr( "The enabled repository could not be loaded." )
                      : tr( "None of the %1 enabled repositories could be loaded." ).arg( failed ) )
                    + ( offered > 0 ? tr( " The list shows the last catalogs that loaded." )
                                    : QString{} ),
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
            // The new version's Lua runs at once when the installed pack is enabled.
            if ( !confirmPackLua( tab, plugins, id, name,
                                  catalogContents( plugins, options.value( "catalog_entry_id" )
                                                                .toString() ),
                                  false ) ) {
                return;
            }
            setStatus( tr( "Installing %1 %2…" ).arg( name, version ) );
            // A pack with local edits may need the merge window (design 4.6).
            runPackAwareInstall(
                tab, plugins, executor,
                manageRequest( installed && installedSource == repository ? "update" : "install",
                               options, id ),
                name, [ setStatus, render, name, version ]( const QVariantMap& job ) {
                           render( false );
                           setStatus( job.value( "state" ) == "completed"
                                          ? tr( "Installed %1 %2." ).arg( name, version )
                                                + jobWarningSuffix( job )
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

bool confirmPackLua( QWidget* parent, cilogg::plugins::PluginManager* manager, const QString& id,
                     const QString& name, const QStringList& contents, bool enabling )
{
    const bool lua = contents.contains( QStringLiteral( "lua" ) );
    // MD03b: a pack's Python decoders are local code too (design 4.4).
    const bool python = contents.contains( QStringLiteral( "python_decoders" ) );
    if ( !manager || ( !lua && !python ) ) {
        return true;
    }
    // A pack that is not enabled runs no code: installing or updating it is asked about when it
    // is enabled.
    if ( !enabling && !manager->enabledPacks().contains( id ) ) {
        return true;
    }
    const auto label = name.isEmpty() ? id : name;
    if ( !python ) {
        return confirm( parent, tr( "Configuration Pack Lua" ),
                        ( enabling ? tr( "\"%1\" contains Lua code that runs on this computer, with "
                                         "your permissions, whenever TShark decodes a preview. "
                                         "Enable it only if you trust its source." )
                                   : tr( "This version of \"%1\" contains Lua code that runs on "
                                         "this computer, with your permissions, whenever TShark "
                                         "decodes a preview, as soon as it is installed. Install "
                                         "it only if you trust its source." ) )
                            .arg( label ) );
    }
    const auto code = lua ? tr( "Lua and Python code" ) : tr( "Python code" );
    return confirm( parent, lua ? tr( "Configuration Pack Code" ) : tr( "Configuration Pack Python" ),
                    ( enabling ? tr( "\"%1\" contains %2 that runs on this computer, with your "
                                     "permissions, whenever it decodes a preview. Enable it only "
                                     "if you trust its source." )
                               : tr( "This version of \"%1\" contains %2 that runs on this "
                                     "computer, with your permissions, whenever it decodes a "
                                     "preview, as soon as it is installed. Install it only if "
                                     "you trust its source." ) )
                        .arg( label, code ) );
}

QString pythonDecodersState( const QVariantMap& plugin, const QVariantMap& hostFeatures )
{
    const auto count = plugin.value( "pythonDecoders" ).toInt();
    if ( count <= 0 ) {
        return {};
    }
    if ( !hostFeatures.contains( QStringLiteral( "python_decoders" ) ) ) {
        return tr( "Python decoders: requires a newer CILogg" );
    }
    return plugin.value( "pythonDecodersActive" ).toBool()
               ? tr( "Python decoders: %1" ).arg( count )
               : tr( "Python decoders: %1 (inactive)" ).arg( count );
}

QStringList catalogContents( cilogg::plugins::PluginManager* manager, const QString& entryId )
{
    if ( !manager || entryId.isEmpty() ) {
        return {};
    }
    for ( const auto& value : manager->catalogEntries() ) {
        const auto entry = value.toMap();
        if ( entry.value( "entry_id" ).toString() == entryId ) {
            return entry.value( "kind" ).toString() == QLatin1String( "config-pack" )
                       ? entry.value( "contents" ).toStringList()
                       : QStringList{};
        }
    }
    return {};
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
                                 tr( "Sign-in" ), tr( "State" ), tr( "Last check" ),
                                 tr( "Auto-update" ) } );
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
    auto* signInButton = addButton( row, tab, tr( "Sign In…" ), "pluginRepositorySignIn" );
    auto* signOutButton = addButton( row, tab, tr( "Sign Out" ), "pluginRepositorySignOut" );
    signOutButton->setToolTip( tr( "Delete the stored personal access token. The Git Credential "
                                   "Manager, which Git also uses, is not changed." ) );
    row->addStretch( 1 );
    auto* howTo = addButton( row, tab, tr( "How to…" ), "pluginRepositoriesHowTo" );
    howTo->setToolTip( tr( "How to pack a plugin, create a repository and use it" ) );
    QObject::connect( howTo, &QPushButton::clicked, tab, [ openGuide ] { openGuide(); } );

    const auto selectedId = [ table ] {
        const auto current = table->currentRow();
        return current >= 0 && table->item( current, NameColumn )
                   ? table->item( current, NameColumn )->data( IdRole ).toString()
                   : QString{};
    };
    const auto updateButtons = [ table, up, down, edit, remove, signInButton, signOutButton ] {
        const auto current = table->currentRow();
        up->setEnabled( current > 0 );
        down->setEnabled( current >= 0 && current + 1 < table->rowCount() );
        edit->setEnabled( current >= 0 );
        remove->setEnabled( current >= 0 );
        const auto* signInCell = current >= 0 ? table->item( current, SignInColumn ) : nullptr;
        const bool azure = signInCell && signInCell->data( RepositoryRole ).toBool();
        signInButton->setEnabled( azure );
        signOutButton->setEnabled( azure );
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
                                     signInText( repository ),
                                     repositoryStateText( repository ),
                                     lastCheckText( repository.value( "lastCheck" ).toString() ),
                                     QString{} };
            const auto state = repository.value( "state" ).toString();
            for ( int column = 0; column < cells.size(); ++column ) {
                auto* cell = new QTableWidgetItem( cells.at( column ) );
                cell->setData( IdRole, id );
                cell->setToolTip( column == StateColumn
                                          && ( state == "failed"
                                               || state == cilogg::plugins::SignInRequiredState )
                                      ? repository.value( "lastError" ).toString()
                                      : cells.at( column ) );
                if ( column == SignInColumn ) {
                    cell->setData( RepositoryRole, repository.value( "signIn" )
                                                       == cilogg::plugins::SignInAzureDevOps );
                    cell->setData( EntryRole, repository.value( "credentialSource" ) );
                }
                if ( column == EnabledColumn ) {
                    cell->setFlags( Qt::ItemIsEnabled | Qt::ItemIsSelectable
                                    | Qt::ItemIsUserCheckable );
                    cell->setCheckState( repository.value( "enabled" ).toBool() ? Qt::Checked
                                                                                : Qt::Unchecked );
                    cell->setData( Qt::AccessibleTextRole, repository.value( "enabled" ).toBool()
                                                               ? tr( "Enabled" )
                                                               : tr( "Disabled" ) );
                }
                if ( column == AutoUpdateColumn ) {
                    const bool automatic = repository.value( "autoUpdate" ).toBool();
                    cell->setFlags( Qt::ItemIsEnabled | Qt::ItemIsSelectable
                                    | Qt::ItemIsUserCheckable );
                    cell->setCheckState( automatic ? Qt::Checked : Qt::Unchecked );
                    cell->setData( Qt::AccessibleTextRole,
                                   automatic ? tr( "Auto-update on" ) : tr( "Auto-update off" ) );
                    cell->setToolTip( tr( "Install new versions within the installed major "
                                          "version of plugins installed from this repository" ) );
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
                          if ( item->column() != EnabledColumn
                               && item->column() != AutoUpdateColumn ) {
                              return;
                          }
                          const auto id = item->data( IdRole ).toString();
                          const bool checked = item->checkState() == Qt::Checked;
                          const auto option = item->column() == EnabledColumn
                                                  ? QStringLiteral( "enabled" )
                                                  : QStringLiteral( "auto_update" );
                          // The table is rebuilt afterwards: not inside its own signal.
                          QTimer::singleShot( 0, tab, [ run, selectedId, id, option, checked ] {
                              run( "repository_update", { { "repository", id }, { option, checked } },
                                   selectedId() );
                          } );
                      } );
    QObject::connect( manager, &cilogg::plugins::PluginManager::changed, tab,
                      [ refresh, selectedId ] { refresh( selectedId() ); } );
    const auto recordOf = [ plugins ]( const QString& id ) {
        if ( plugins ) {
            for ( const auto& value : plugins->repositories() ) {
                if ( value.toMap().value( "id" ) == id ) {
                    return value.toMap();
                }
            }
        }
        return QVariantMap{};
    };
    // Sign In (design 3.5): the Git Credential Manager may show its own window, because this
    // request comes from the GUI; a personal access token is typed into a password field.
    const auto startSignIn = [ tab, plugins, executor, status, refresh ](
                                 const QString& id, const QString& name, const QString& source,
                                 std::function<void( bool )> finished ) {
        QVariantMap options{ { "repository", id }, { "method", source } };
        if ( source == cilogg::plugins::CredentialSourcePersonalAccessToken ) {
            const auto token = askToken( tab, name );
            if ( token.isEmpty() ) {
                if ( finished ) {
                    finished( false );
                }
                return;
            }
            options.insert( "token", token );
        }
        status->setText( tr( "Signing in to \"%1\"…" ).arg( name ) );
        const auto result = executor( manageRequest( "repository_sign_in", options ) );
        if ( !result.ok() ) {
            status->setText( result.message );
            if ( finished ) {
                finished( false );
            }
            return;
        }
        followJob( tab, plugins, result.payload.value( "jobId" ).toString(),
                   [ status, refresh, id, name, finished ]( const QVariantMap& job ) {
                       const auto outcome = job.value( "result" ).toMap();
                       const bool signedIn = job.value( "state" ) == "completed"
                                             && outcome.value( "signedIn" ).toBool();
                       if ( signedIn ) {
                           status->setText(
                               outcome.value( "sessionOnly" ).toBool()
                                   ? tr( "Signed in to \"%1\". No credential store is available: "
                                         "the token is kept until CILogg exits." )
                                         .arg( name )
                                   : tr( "Signed in to \"%1\"." ).arg( name ) );
                       }
                       else {
                           const auto error = job.value( "error" ).toString();
                           status->setText( tr( "Signing in to \"%1\" did not work: %2" )
                                                .arg( name, error.isEmpty()
                                                                ? outcome.value( "error" ).toString()
                                                                : error ) );
                       }
                       refresh( id );
                       if ( finished ) {
                           finished( signedIn );
                       }
                   } );
    };
    QObject::connect( add, &QPushButton::clicked, tab, [ tab, executor, run, startSignIn ] {
        RepositoryForm form;
        QString added;
        editRepository(
            tab, tr( "Add Repository" ), &form, false,
            [ executor, &added ]( const RepositoryForm& entered ) {
                QVariantMap options{ { "name", entered.name }, { "url", entered.address } };
                if ( entered.signIn == cilogg::plugins::SignInAzureDevOps ) {
                    options.insert( "sign_in", entered.signIn );
                    options.insert( "credential_source", entered.credentialSource );
                    if ( !entered.branch.isEmpty() ) {
                        options.insert( "branch", entered.branch );
                    }
                    if ( !entered.path.isEmpty() ) {
                        options.insert( "path", entered.path );
                    }
                }
                const auto result = executor( manageRequest( "repository_add", options ) );
                if ( result.ok() ) {
                    added = result.payload.value( "repository" ).toMap().value( "id" ).toString();
                }
                return result.ok() ? QString{} : result.message;
            } );
        if ( added.isEmpty() ) {
            return;
        }
        run( "repositories", {}, added );
        if ( form.signIn != cilogg::plugins::SignInAzureDevOps ) {
            return;
        }
        // Adding an Azure DevOps repository tests the address and the sign-in (design 3.3).
        startSignIn( added, form.name, form.credentialSource,
                     [ tab, run, added, name = form.name ]( bool signedIn ) {
                         if ( !signedIn
                              && !confirm( tab, tr( "Add Repository" ),
                                           tr( "Signing in to \"%1\" did not work. Keep the "
                                               "repository and sign in later?" )
                                               .arg( name ) ) ) {
                             run( "repository_remove", { { "repository", added } }, {} );
                         }
                     } );
    } );
    QObject::connect( edit, &QPushButton::clicked, tab,
                      [ tab, executor, run, selectedId, recordOf ] {
                          const auto id = selectedId();
                          const auto record = recordOf( id );
                          if ( record.isEmpty() ) {
                              return;
                          }
                          RepositoryForm form;
                          form.name = record.value( "name" ).toString();
                          form.address = record.value( "url" ).toString();
                          form.signIn = record.value( "signIn" ).toString();
                          form.branch = record.value( "branch" ).toString();
                          form.path = record.value( "path" ).toString();
                          form.credentialSource = record.value( "credentialSource" ).toString();
                          const auto previousSource = form.credentialSource;
                          if ( editRepository(
                                   tab, tr( "Edit Repository" ), &form, true,
                                   [ executor, id, previousSource ]( const RepositoryForm& entered ) {
                                       QVariantMap options{ { "repository", id },
                                                            { "name", entered.name } };
                                       if ( entered.signIn == cilogg::plugins::SignInAzureDevOps
                                            && entered.credentialSource != previousSource ) {
                                           options.insert( "credential_source",
                                                           entered.credentialSource );
                                       }
                                       const auto result = executor(
                                           manageRequest( "repository_update", options ) );
                                       return result.ok() ? QString{} : result.message;
                                   } ) ) {
                              run( "repositories", {}, id );
                          }
                      } );
    QObject::connect( signInButton, &QPushButton::clicked, tab,
                      [ selectedId, recordOf, startSignIn ] {
                          const auto record = recordOf( selectedId() );
                          if ( record.value( "signIn" ) == cilogg::plugins::SignInAzureDevOps ) {
                              startSignIn( record.value( "id" ).toString(),
                                           record.value( "name" ).toString(),
                                           record.value( "credentialSource" ).toString(), {} );
                          }
                      } );
    QObject::connect( signOutButton, &QPushButton::clicked, tab,
                      [ tab, plugins, executor, status, refresh, selectedId, recordOf ] {
                          const auto record = recordOf( selectedId() );
                          if ( record.value( "signIn" ) != cilogg::plugins::SignInAzureDevOps ) {
                              return;
                          }
                          const auto id = record.value( "id" ).toString();
                          const auto name = record.value( "name" ).toString();
                          const auto result = executor(
                              manageRequest( "repository_sign_out", { { "repository", id } } ) );
                          if ( !result.ok() ) {
                              status->setText( result.message );
                              return;
                          }
                          followJob( tab, plugins, result.payload.value( "jobId" ).toString(),
                                     [ status, refresh, id, name ]( const QVariantMap& job ) {
                                         status->setText(
                                             job.value( "state" ) == "completed"
                                                 ? tr( "Signed out of \"%1\"." ).arg( name )
                                                 : job.value( "error" ).toString() );
                                         refresh( id );
                                     } );
                      } );
    QObject::connect( remove, &QPushButton::clicked, tab, [ tab, table, run, selectedId ] {
        const auto id = selectedId();
        if ( id.isEmpty() ) {
            return;
        }
        const auto name = table->item( table->currentRow(), NameColumn )->text();
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
