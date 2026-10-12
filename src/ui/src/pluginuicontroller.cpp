#include "pluginuicontroller.h"
#include "log.h"
#include "mainwindow.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
bool editValues( QWidget* parent, const QString& title, const QJsonArray& fields,
                 QJsonObject* values )
{
    QDialog dialog( parent );
    dialog.setObjectName( "pluginFormDialog" );
    dialog.setWindowTitle( title );
    auto* layout = new QVBoxLayout( &dialog );
    auto* form = new QFormLayout;
    layout->addLayout( form );
    QMap<QString, std::function<QJsonValue()>> getters;
    for ( const auto& item : fields ) {
        const auto field = item.toObject();
        const auto id = field.value( "id" ).toString();
        const auto type = field.value( "type" ).toString();
        const auto initial
            = values->value( id ).isUndefined() ? field.value( "default" ) : values->value( id );
        QWidget* widget = nullptr;
        if ( type == "boolean" ) {
            auto* input = new QCheckBox( &dialog );
            input->setChecked( initial.toBool() );
            widget = input;
            getters[ id ] = [ input ] { return QJsonValue( input->isChecked() ); };
        }
        else if ( type == "number" || type == "integer" ) {
            auto* input = new QLineEdit( QString::number( initial.toDouble(), 'g', 17 ), &dialog );
            auto* validator = new QDoubleValidator( input );
            validator->setLocale( QLocale::c() );
            validator->setNotation( QDoubleValidator::ScientificNotation );
            input->setValidator( validator );
            widget = input;
            getters[ id ] = [ input ] {
                bool valid = false;
                const auto number = input->text().toDouble( &valid );
                return valid ? QJsonValue( number ) : QJsonValue( QJsonValue::Null );
            };
        }
        else if ( type == "choice" ) {
            auto* input = new QComboBox( &dialog );
            for ( const auto& choice : field.value( "choices" ).toArray() ) {
                input->addItem( choice.toVariant().toString(), choice.toVariant() );
            }
            input->setCurrentIndex( qMax( 0, input->findData( initial.toVariant() ) ) );
            widget = input;
            getters[ id ] = [ input ] { return QJsonValue::fromVariant( input->currentData() ); };
        }
        else if ( type == "multiline" ) {
            auto* input = new QPlainTextEdit( initial.toString(), &dialog );
            widget = input;
            getters[ id ] = [ input ] { return QJsonValue( input->toPlainText() ); };
        }
        else {
            auto* input = new QLineEdit( initial.toString(), &dialog );
            widget = input;
            getters[ id ] = [ input ] { return QJsonValue( input->text() ); };
            if ( type == "secret" ) {
                input->setEchoMode( QLineEdit::Password );
                if ( field.value( "keep_existing" ).toBool() ) {
                    input->setPlaceholderText(
                        QObject::tr( "Leave blank to keep the stored value" ) );
                    auto* clear = input->addAction(
                        dialog.style()->standardIcon( QStyle::SP_DialogResetButton ),
                        QLineEdit::TrailingPosition );
                    clear->setObjectName( "clearPluginSecret_" + id );
                    clear->setText( QObject::tr( "Clear stored secret" ) );
                    clear->setToolTip( clear->text() );
                    QObject::connect( input, &QLineEdit::textEdited, input, [ input ] {
                        input->setProperty( "clearStoredSecret", false );
                    } );
                    QObject::connect( clear, &QAction::triggered, input, [ input ] {
                        input->clear();
                        input->setProperty( "clearStoredSecret", true );
                    } );
                    getters[ id ] = [ input ] {
                        return input->text().isEmpty()
                                       && !input->property( "clearStoredSecret" ).toBool()
                                   ? QJsonValue( QJsonValue::Undefined )
                                   : QJsonValue( input->text() );
                    };
                }
            }
            if ( type == "file" || type == "directory" ) {
                auto* row = new QWidget( &dialog );
                auto* rowLayout = new QHBoxLayout( row );
                rowLayout->setContentsMargins( 0, 0, 0, 0 );
                rowLayout->addWidget( input );
                auto* browse = new QPushButton( QObject::tr( "Browse…" ), row );
                rowLayout->addWidget( browse );
                QObject::connect( browse, &QPushButton::clicked, &dialog, [ input, type, &dialog ] {
                    const auto path = type == "directory"
                                          ? QFileDialog::getExistingDirectory( &dialog )
                                          : QFileDialog::getOpenFileName( &dialog );
                    if ( !path.isEmpty() ) {
                        input->setText( path );
                    }
                } );
                widget = row;
                row->setProperty( "pluginPickerRow", true );
                input->setObjectName( "pluginField_" + id );
                input->setAccessibleName( field.value( "label" ).toString( id ) );
                browse->setObjectName( "pluginBrowse_" + id );
                browse->setAccessibleName(
                    QObject::tr( "Browse %1" ).arg( field.value( "label" ).toString( id ) ) );
            }
        }
        widget->setObjectName(
            ( widget->property( "pluginPickerRow" ).toBool() ? "pluginFieldRow_" : "pluginField_" )
            + id );
        widget->setAccessibleName( field.value( "label" ).toString( id ) );
        form->addRow( field.value( "label" ).toString( id ), widget );
    }
    auto* buttons
        = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
    buttons->button( QDialogButtonBox::Ok )->setObjectName( "pluginFormAccept" );
    buttons->button( QDialogButtonBox::Cancel )->setObjectName( "pluginFormCancel" );
    layout->addWidget( buttons );
    QObject::connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
    QObject::connect( buttons, &QDialogButtonBox::accepted, &dialog, [ & ] {
        auto updated = *values;
        for ( auto it = getters.cbegin(); it != getters.cend(); ++it ) {
            const auto value = it.value()();
            if ( value.isUndefined() ) {
                updated.remove( it.key() );
            }
            else {
                updated.insert( it.key(), value );
            }
        }
        const auto error = cilogg::plugins::validateValues( fields, &updated );
        if ( !error.isEmpty() ) {
            QMessageBox::warning( &dialog, title, error );
            return;
        }
        *values = updated;
        dialog.accept();
    } );
    return dialog.exec() == QDialog::Accepted;
}
} // namespace

PluginUiController::PluginUiController( MainWindow* window, cilogg::plugins::PluginManager* manager,
                                        Executor executor )
    : QObject( window )
    , window_( window )
    , manager_( manager )
    , executor_( std::move( executor ) )
{
    menu_ = window->menuBar()->addMenu( tr( "Plugins" ) );
    menu_->setObjectName( "pluginsMenu" );
    menu_->setAccessibleName( tr( "Plugins" ) );
    auto* toolbar = window->findChild<QToolBar*>( "mainToolBar" );
    auto* manage = new QAction( window->style()->standardIcon( QStyle::SP_FileDialogDetailedView ),
                                tr( "Manage Plugins…" ), this );
    manage->setObjectName( "managePluginsAction" );
    manage->setToolTip( tr( "Install, configure and manage plugins" ) );
    auto* browse = new QAction( window->style()->standardIcon( QStyle::SP_DriveNetIcon ),
                                tr( "Browse Plugins…" ), this );
    browse->setObjectName( "browsePluginsAction" );
    browse->setToolTip( tr( "Browse the online plugin catalog" ) );
    menu_->addAction( manage );
    menu_->addAction( browse );
    menu_->addSeparator();
    if ( toolbar ) {
        toolbar->addAction( manage );
        toolbar->addAction( browse );
        toolbar->widgetForAction( manage )->setAccessibleName( manage->text() );
        toolbar->widgetForAction( browse )->setAccessibleName( browse->text() );
    }
    connect( manage, &QAction::triggered, this, &PluginUiController::manageDialog );
    connect( browse, &QAction::triggered, this, &PluginUiController::browseDialog );
    auto* refresh = new QTimer( this );
    refresh->setSingleShot( true );
    refresh->setInterval( 200 );
    connect( manager, &cilogg::plugins::PluginManager::changed, refresh, [ refresh ] {
        if ( !refresh->isActive() ) {
            refresh->start();
        }
    } );
    connect( refresh, &QTimer::timeout, this, &PluginUiController::refreshActions );
    panelTimer_ = new QTimer( this );
    panelTimer_->setInterval( 100 );
    connect( panelTimer_, &QTimer::timeout, this, [ this ] {
        for ( int count = 0; count < 2 && !pendingPanels_.isEmpty(); ++count ) {
            const auto it = pendingPanels_.begin();
            const auto id = it.key().section( ':', 0, 0 );
            const auto update = it.value();
            pendingPanels_.erase( it );
            panel( id, update.first, update.second );
        }
        if ( pendingPanels_.isEmpty() ) {
            panelTimer_->stop();
        }
    } );
    connect( manager, &cilogg::plugins::PluginManager::panelUpdated, this,
             &PluginUiController::queuePanel );
    connect( manager, &cilogg::plugins::PluginManager::fileReady, this,
             [ this ]( const QString& path, bool follow, const QVariantMap& context ) {
                 if ( window_
                      && window_->pluginContext( {} ).value( "windowId" )
                             == context.value( "windowId" ) ) {
                     window_->loadInitialFile( path, follow );
                 }
             } );
    connect( manager, &cilogg::plugins::PluginManager::pluginStopped, this,
             [ this ]( const QString& id ) {
                 for ( auto it = pendingPanels_.begin(); it != pendingPanels_.end(); ) {
                     it = it.key().startsWith( id + ":" ) ? pendingPanels_.erase( it )
                                                          : std::next( it );
                 }
                 for ( auto it = panels_.begin(); it != panels_.end(); ) {
                     if ( it.key().startsWith( id + ":" ) ) {
                         if ( it.value() ) {
                             it.value()->deleteLater();
                         }
                         it = panels_.erase( it );
                     }
                     else {
                         ++it;
                     }
                 }
             } );
    refreshActions();
    const auto windowId = window->pluginContext( {} ).value( "windowId" ).toString();
    connect( window, &MainWindow::windowClosed, manager,
             [ manager, windowId ] { manager->closeWindow( windowId ); } );
    connect( window, &MainWindow::windowClosed, this, [ this ] {
        pendingPanels_.clear();
        panelTimer_->stop();
        if ( manager_ ) {
            disconnect( manager_, nullptr, this, nullptr );
        }
        window_.clear();
    } );
}
PluginUiController::~PluginUiController() = default;
void PluginUiController::closeFile( const QString& path )
{
    if ( manager_ ) {
        manager_->closeFile( path, window_
                                       ? window_->pluginContext( {} ).value( "windowId" ).toString()
                                       : QString{} );
    }
}
void PluginUiController::report( const CommanderResult& result )
{
    if ( !result.ok() && window_ ) {
        QMessageBox::warning( window_, tr( "Plugins" ), result.message );
    }
}
void PluginUiController::refreshActions()
{
    if ( !window_ || !manager_ ) {
        return;
    }
    QJsonArray descriptions;
    const auto plugins = manager_->plugins();
    for ( const auto& value : plugins ) {
        const auto p = value.toMap();
        if ( p.value( "enabled" ).toBool() && p.value( "error" ).toString().isEmpty() ) {
            descriptions.append( QJsonObject{
                { "id", p.value( "id" ).toString() },
                { "name", p.value( "name" ).toString() },
                { "actions", QJsonArray::fromVariantList( p.value( "actions" ).toList() ) } } );
        }
    }
    if ( descriptions == actionDescriptions_ ) {
        return;
    }
    actionDescriptions_ = descriptions;
    auto* toolbar = window_->findChild<QToolBar*>( "mainToolBar" );
    for ( auto* action : actions_ ) {
        menu_->removeAction( action );
        if ( toolbar ) {
            toolbar->removeAction( action );
        }
        action->deleteLater();
    }
    actions_.clear();
    for ( const auto& value : plugins ) {
        const auto plugin = value.toMap();
        if ( !plugin.value( "enabled" ).toBool()
             || !plugin.value( "error" ).toString().isEmpty() ) {
            continue;
        }
        const auto id = plugin.value( "id" ).toString();
        for ( const auto& item : plugin.value( "actions" ).toList() ) {
            const auto definition = QJsonObject::fromVariantMap( item.toMap() );
            const auto type = definition.value( "capability" ).toString();
            const auto icon = type == "datasource"  ? QStyle::SP_MediaPlay
                              : type == "converter" ? QStyle::SP_FileDialogContentsView
                                                    : QStyle::SP_ComputerIcon;
            auto* action = new QAction(
                window_->style()->standardIcon( icon ),
                plugin.value( "name" ).toString() + ": "
                    + definition.value( "label" ).toString( definition.value( "id" ).toString() ),
                this );
            action->setObjectName( "pluginAction_" + id + "_"
                                   + definition.value( "id" ).toString() );
            action->setToolTip( action->text() );
            menu_->addAction( action );
            if ( toolbar ) {
                toolbar->addAction( action );
                toolbar->widgetForAction( action )->setAccessibleName( action->text() );
            }
            actions_.append( action );
            connect( action, &QAction::triggered, this,
                     [ this, id, definition ] { invoke( id, definition ); } );
        }
    }
}
void PluginUiController::invoke( const QString& id, const QJsonObject& action )
{
    QJsonObject arguments;
    const auto context = window_->pluginContext( {} );
    if ( action.value( "capability" ) == "converter" ) {
        arguments.insert( "input_path", QJsonValue::fromVariant( context.value( "filePath" ) ) );
    }
    const auto fields = action.value( "fields" ).toArray();
    if ( !fields.isEmpty()
         && !editValues( window_, action.value( "label" ).toString( "Plugin action" ), fields,
                         &arguments ) ) {
        return;
    }
    CommanderRequest request;
    request.action = CommanderAction::InvokePlugin;
    request.pluginId = id;
    request.pluginAction = action.value( "id" ).toString();
    request.pluginArguments = arguments.toVariantMap();
    request.windowIndex = context.value( "windowIndex" ).toInt();
    request.tabId = context.value( "tabId" ).toString();
    report( executor_( request ) );
}

void PluginUiController::manageDialog()
{
    if ( !manager_ ) {
        return;
    }
    auto* dialog = new QDialog( window_ );
    dialog->setAttribute( Qt::WA_DeleteOnClose );
    dialog->setObjectName( "pluginManagerDialog" );
    dialog->setWindowTitle( tr( "Manage Plugins" ) );
    dialog->resize( 900, 600 );
    auto* layout = new QVBoxLayout( dialog );
    auto* table = new QTableWidget( 0, 4, dialog );
    table->setObjectName( "pluginManagerTable" );
    table->setHorizontalHeaderLabels(
        { tr( "Plugin" ), tr( "Version" ), tr( "Enabled" ), tr( "State" ) } );
    table->setSelectionBehavior( QAbstractItemView::SelectRows );
    table->setSelectionMode( QAbstractItemView::SingleSelection );
    table->setEditTriggers( QAbstractItemView::NoEditTriggers );
    table->horizontalHeader()->setSectionResizeMode( QHeaderView::Stretch );
    layout->addWidget( table );
    auto* details = new QPlainTextEdit( dialog );
    details->setReadOnly( true );
    details->setObjectName( "pluginDiagnostics" );
    details->setMaximumBlockCount( 2000 );
    layout->addWidget( details );
    auto* storageUsage = new QLabel( dialog );
    storageUsage->setObjectName( "pluginStorageUsage" );
    layout->addWidget( storageUsage );
    auto* row = new QHBoxLayout;
    layout->addLayout( row );
    const auto refresh = [ this, table, details, storageUsage ] {
        const auto storage = manager_->storageStatus();
        storageUsage->setText(
            tr( "Captured: %1 bytes; reserved: %2 bytes; total limit: %3 bytes (0 = unlimited)" )
                .arg( storage.value( "captured_bytes" ).toLongLong() )
                .arg( storage.value( "reserved_bytes" ).toLongLong() )
                .arg( storage.value( "limits" ).toMap().value( "max_total_bytes" ).toLongLong() ) );
        const auto selected
            = table->currentRow() >= 0
                  ? table->item( table->currentRow(), 0 )->data( Qt::UserRole ).toString()
                  : QString{};
        const auto list = manager_->plugins();
        table->setRowCount( static_cast<int>( list.size() ) );
        int index = 0;
        for ( const auto& value : list ) {
            const auto p = value.toMap();
            const auto id = p.value( "id" ).toString();
            const QStringList texts{ p.value( "name", id ).toString(),
                                     p.value( "version" ).toString(),
                                     p.value( "enabled" ).toBool() ? tr( "Yes" ) : tr( "No" ),
                                     p.value( "state" ).toString() };
            for ( int col = 0; col < 4; ++col ) {
                auto* item = new QTableWidgetItem( texts[ col ] );
                item->setData( Qt::UserRole, id );
                table->setItem( index, col, item );
            }
            if ( id == selected ) {
                table->selectRow( index );
            }
            ++index;
        }
        QString info;
        for ( const auto& p : list ) {
            if ( p.toMap().value( "id" ).toString() == selected ) {
                info = QJsonDocument::fromVariant( p ).toJson( QJsonDocument::Indented );
            }
        }
        info += "\nJobs:\n"
                + QString::fromUtf8( QJsonDocument::fromVariant( manager_->jobs() )
                                         .toJson( QJsonDocument::Indented ) );
        details->setPlainText( info );
    };
    const auto addButton = [ row, dialog ]( const QString& text, const QString& name ) {
        auto* button = new QPushButton( text, dialog );
        button->setObjectName( name );
        row->addWidget( button );
        return button;
    };
    for ( const auto& source : QStringList{ "folder", "zip" } ) {
        auto* button
            = addButton( source == "folder" ? tr( "Install Folder…" ) : tr( "Install ZIP…" ),
                         "pluginInstall_" + source );
        connect( button, &QPushButton::clicked, dialog, [ this, dialog, source ] {
            const auto path = source == "folder"
                                  ? QFileDialog::getExistingDirectory( dialog )
                                  : QFileDialog::getOpenFileName( dialog, tr( "Plugin package" ),
                                                                  {}, "ZIP (*.zip)" );
            if ( !path.isEmpty() ) {
                CommanderRequest r;
                r.action = CommanderAction::PluginManage;
                r.pluginOperation = "install";
                r.pluginArguments = { { "path", path } };
                report( executor_( r ) );
            }
        } );
    }
    for ( const auto& operation :
          QStringList{ "enable", "disable", "configure", "restart", "remove" } ) {
        auto* button = addButton( operation, "pluginManage_" + operation );
        connect( button, &QPushButton::clicked, dialog, [ this, table, operation, dialog ] {
            if ( table->currentRow() < 0 ) {
                return;
            }
            const auto id = table->item( table->currentRow(), 0 )->data( Qt::UserRole ).toString();
            CommanderRequest r;
            r.action = CommanderAction::PluginManage;
            r.pluginId = id;
            r.pluginOperation = operation;
            if ( operation == "configure" ) {
                for ( const auto& value : manager_->plugins() ) {
                    const auto p = value.toMap();
                    if ( p.value( "id" ) != id ) {
                        continue;
                    }
                    auto config
                        = QJsonObject::fromVariantMap( p.value( "configurationValues" ).toMap() );
                    auto fields
                        = QJsonArray::fromVariantList( p.value( "configuration" ).toList() );
                    for ( int index = 0; index < fields.size(); ++index ) {
                        auto field = fields[ index ].toObject();
                        if ( field.value( "type" ) == "secret"
                             && p.value( "configuredSecrets" )
                                    .toMap()
                                    .value( field.value( "id" ).toString() )
                                    .toBool() ) {
                            field[ "required" ] = false;
                            field[ "keep_existing" ] = true;
                            fields[ index ] = field;
                        }
                    }
                    if ( !editValues( dialog, tr( "Configure Plugin" ), fields, &config ) ) {
                        return;
                    }
                    r.pluginArguments = { { "configuration", config.toVariantMap() } };
                }
            }
            report( executor_( r ) );
        } );
    }
    auto* storage = addButton( tr( "Storage Limits…" ), "pluginStorageLimits" );
    connect( storage, &QPushButton::clicked, dialog, [ this, dialog ] {
        const auto status = manager_->storageStatus();
        auto values = QJsonObject::fromVariantMap( status.value( "limits" ).toMap() );
        auto fields = QJsonArray::fromVariantList( status.value( "fields" ).toList() );
        for ( int index = 0; index < fields.size(); ++index ) {
            auto field = fields[ index ].toObject();
            const auto id = field.value( "id" ).toString();
            field[ "label" ] = id == "max_capture_bytes" ? tr( "Per-capture bytes (0: unlimited)" )
                               : id == "max_total_bytes"
                                   ? tr( "Total capture bytes (0: unlimited)" )
                                   : tr( "Free-space reserve bytes (0: disabled)" );
            fields[ index ] = field;
        }
        if ( editValues( dialog, tr( "Plugin Capture Storage" ), fields, &values ) ) {
            CommanderRequest request;
            request.action = CommanderAction::PluginManage;
            request.pluginOperation = "configure";
            request.pluginArguments = { { "storage_limits", values.toVariantMap() } };
            report( executor_( request ) );
        }
    } );
    auto* captures = addButton( tr( "Open Captures Folder" ), "pluginCapturesFolder" );
    connect( captures, &QPushButton::clicked, dialog, [ this ] {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile( manager_->storageStatus().value( "directory" ).toString() ) );
    } );
    auto* timer = new QTimer( dialog );
    timer->setInterval( 1000 );
    connect( timer, &QTimer::timeout, dialog, refresh );
    timer->start();
    refresh();
    dialog->show();
}

void PluginUiController::browseDialog()
{
    auto* dialog = new QDialog( window_ );
    dialog->setAttribute( Qt::WA_DeleteOnClose );
    dialog->setObjectName( "pluginCatalogDialog" );
    dialog->setWindowTitle( tr( "Browse Plugins" ) );
    dialog->resize( 850, 400 );
    auto* layout = new QVBoxLayout( dialog );
    auto* url = new QLineEdit( manager_->catalogUrl(), dialog );
    url->setObjectName( "pluginCatalogUrl" );
    layout->addWidget( url );
    auto* fetch = new QPushButton( tr( "Fetch Catalog" ), dialog );
    fetch->setObjectName( "fetchPluginCatalog" );
    layout->addWidget( fetch );
    auto* table = new QTableWidget( 0, 3, dialog );
    table->setObjectName( "pluginCatalogTable" );
    table->setHorizontalHeaderLabels( { tr( "Plugin" ), tr( "Version" ), tr( "Description" ) } );
    table->setSelectionBehavior( QAbstractItemView::SelectRows );
    table->setEditTriggers( QAbstractItemView::NoEditTriggers );
    table->horizontalHeader()->setSectionResizeMode( QHeaderView::Stretch );
    layout->addWidget( table );
    auto* status = new QLabel( dialog );
    status->setWordWrap( true );
    status->setObjectName( "pluginCatalogStatus" );
    layout->addWidget( status );
    auto* install = new QPushButton( tr( "Install / Update Selected" ), dialog );
    install->setObjectName( "installCatalogPlugin" );
    layout->addWidget( install );
    auto currentJob = std::make_shared<QString>();
    connect( fetch, &QPushButton::clicked, dialog, [ this, url, currentJob, status ] {
        CommanderRequest r;
        r.action = CommanderAction::PluginManage;
        r.pluginOperation = "catalog";
        r.pluginArguments = { { "url", url->text() } };
        const auto result = executor_( r );
        if ( !result.ok() ) {
            status->setText( result.message );
            return;
        }
        *currentJob = result.payload.value( "jobId" ).toString();
        status->setText( tr( "Loading…" ) );
    } );
    connect( install, &QPushButton::clicked, dialog, [ this, table, currentJob, status ] {
        if ( table->currentRow() < 0 ) {
            return;
        }
        CommanderRequest r;
        r.action = CommanderAction::PluginManage;
        r.pluginOperation = "install";
        r.pluginId = table->item( table->currentRow(), 0 )->data( Qt::UserRole ).toString();
        r.pluginArguments
            = { { "catalog_entry_id",
                  table->item( table->currentRow(), 0 )->data( Qt::UserRole + 1 ).toString() } };
        const auto result = executor_( r );
        if ( !result.ok() ) {
            status->setText( result.message );
            return;
        }
        *currentJob = result.payload.value( "jobId" ).toString();
    } );
    auto* timer = new QTimer( dialog );
    timer->setInterval( 200 );
    connect( timer, &QTimer::timeout, dialog, [ this, table, status, currentJob ] {
        if ( !manager_ || currentJob->isEmpty() ) {
            return;
        }
        const auto j = manager_->job( *currentJob );
        const auto state = j.payload.value( "state" ).toString();
        status->setText( state + ": " + j.payload.value( "error" ).toString() );
        if ( state == "completed" && j.payload.value( "action" ) == "catalog" ) {
            const auto list = j.payload.value( "result" ).toMap().value( "plugins" ).toList();
            table->setRowCount( static_cast<int>( list.size() ) );
            int row = 0;
            for ( const auto& item : list ) {
                const auto p = item.toMap();
                const QStringList texts{ p.value( "name", p.value( "id" ) ).toString(),
                                         p.value( "version" ).toString(),
                                         p.value( "description" ).toString() };
                for ( int col = 0; col < 3; ++col ) {
                    auto* cell = new QTableWidgetItem( texts[ col ] );
                    cell->setData( Qt::UserRole, p.value( "id" ) );
                    cell->setData( Qt::UserRole + 1, p.value( "entry_id" ) );
                    table->setItem( row, col, cell );
                }
                ++row;
            }
        }
        if ( state != "pending" && state != "running" ) {
            currentJob->clear();
        }
    } );
    timer->start();
    dialog->show();
}

void PluginUiController::queuePanel( const QString& pluginId, const QJsonObject& definition,
                                     const QVariantMap& context )
{
    const auto id = definition.value( "id" ).toString();
    const auto key = pluginId + ":" + id;
    if ( id.isEmpty() || id.size() > 128
         || ( !pendingPanels_.contains( key ) && pendingPanels_.size() >= 32 ) ) {
        return;
    }
    pendingPanels_[ key ] = qMakePair( definition, context );
    if ( !panelTimer_->isActive() ) {
        panelTimer_->start();
    }
}

void PluginUiController::panel( const QString& pluginId, const QJsonObject& definition,
                                const QVariantMap& context )
{
    if ( !window_
         || window_->pluginContext( {} ).value( "windowId" ) != context.value( "windowId" ) ) {
        return;
    }
    const auto panelId = definition.value( "id" ).toString();
    const auto elements = definition.value( "elements" ).toArray();
    if ( panelId.isEmpty() || panelId.size() > 128 || elements.size() > 64 ) {
        return;
    }
    const auto key = pluginId + ":" + panelId;
    auto dock = panels_.value( key );
    if ( !dock ) {
        if ( panels_.size() >= 32 ) {
            return;
        }
        dock = new QDockWidget( window_ );
        dock->setObjectName( "pluginPanel_" + pluginId + "_" + panelId );
        dock->setAccessibleName( definition.value( "title" ).toString( panelId ) );
        panels_[ key ] = dock;
        window_->addDockWidget( Qt::BottomDockWidgetArea, dock );
    }
    dock->setWindowTitle( definition.value( "title" ).toString( panelId ) );
    auto* body = new QWidget( dock );
    auto* layout = new QVBoxLayout( body );
    int remainingCells = 4096;
    for ( const auto& item : elements ) {
        const auto element = item.toObject();
        const auto type = element.value( "type" ).toString();
        if ( type == "text" ) {
            auto* label = new QLabel( element.value( "text" ).toString(), body );
            label->setTextFormat( Qt::PlainText );
            label->setWordWrap( true );
            layout->addWidget( label );
        }
        else if ( type == "progress" ) {
            auto* progress = new QProgressBar( body );
            progress->setValue(
                static_cast<int>( qBound( 0.0, element.value( "value" ).toDouble(), 1.0 ) * 100 ) );
            layout->addWidget( progress );
        }
        else if ( type == "table" ) {
            const auto columns = element.value( "columns" ).toArray();
            const auto rows = element.value( "rows" ).toArray();
            const auto cellCount = rows.size() * columns.size();
            if ( columns.size() > 64 || rows.size() > 1000 || cellCount > remainingCells ) {
                LOG_WARNING << "Plugin panel table rejected id=" << pluginId.toStdString()
                            << " panel=" << panelId.toStdString() << " cells=" << cellCount;
                continue;
            }
            remainingCells -= int( cellCount );
            auto* table = new QTableWidget( static_cast<int>( rows.size() ),
                                            static_cast<int>( columns.size() ), body );
            table->setEditTriggers( QAbstractItemView::NoEditTriggers );
            QStringList headers;
            for ( const auto& col : columns ) {
                headers.append( col.toString() );
            }
            table->setHorizontalHeaderLabels( headers );
            for ( int row = 0; row < rows.size(); ++row ) {
                const auto cells = rows[ row ].toArray();
                for ( int col = 0; col < qMin( cells.size(), columns.size() ); ++col ) {
                    table->setItem( row, col,
                                    new QTableWidgetItem( cells[ col ].toVariant().toString() ) );
                }
            }
            layout->addWidget( table );
        }
        else if ( type == "button" ) {
            auto* button = new QPushButton( element.value( "label" ).toString(), body );
            layout->addWidget( button );
            connect( button, &QPushButton::clicked, this, [ this, pluginId, element, context ] {
                CommanderRequest r;
                r.action = CommanderAction::InvokePlugin;
                r.pluginId = pluginId;
                r.pluginAction = element.value( "action" ).toString();
                r.pluginArguments = element.value( "arguments" ).toObject().toVariantMap();
                r.windowIndex = context.value( "windowIndex" ).toInt();
                r.tabId = context.value( "tabId" ).toString();
                report( executor_( r ) );
            } );
        }
    }
    if ( dock->widget() ) {
        dock->widget()->deleteLater();
    }
    dock->setWidget( body );
    dock->show();
    LOG_DEBUG << "Plugin panel rendered id=" << pluginId.toStdString()
              << " panel=" << panelId.toStdString();
}
