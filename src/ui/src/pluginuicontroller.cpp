#include "pluginuicontroller.h"
#include "analysissource.h"
#include "configpack.h"
#include "packeditors.h"
#include "log.h"
#include "mainwindow.h"
#include "pluginsandpacks.h"
#include "pluginviewhost.h"
#include "previewdecodersettings.h"
#include "semanticversion.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
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
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>

namespace {
// The action form. With `source`, its first row is the shared source picker (U15b).
bool editValues( QWidget* parent, const QString& title, const QJsonArray& fields,
                 QJsonObject* values, QString* source = nullptr,
                 const std::function<QVariantList()>& sources = {} )
{
    QDialog dialog( parent );
    dialog.setObjectName( "pluginFormDialog" );
    dialog.setWindowTitle( title );
    auto* layout = new QVBoxLayout( &dialog );
    auto* form = new QFormLayout;
    layout->addLayout( form );
    AnalysisSourcePicker* picker = nullptr;
    if ( source ) {
        picker = new AnalysisSourcePicker( sources, SourceRules{}, &dialog );
        picker->setObjectName( "pluginFormSource" );
        picker->setAccessibleName( QObject::tr( "Run on" ) );
        picker->setSelection( *source );
        form->addRow( QObject::tr( "Run on" ), picker );
    }
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
        if ( picker ) {
            *source = picker->selection();
        }
        dialog.accept();
    } );
    return dialog.exec() == QDialog::Accepted;
}

// The size and modification time of every icon file a plugin's actions and views name, so that
// an update which only replaces an icon file still rebuilds the icons (refreshActions). A few
// file stats per plugin: cheap enough for every refresh.
QJsonArray iconFiles( const QVariantMap& plugin )
{
    QJsonArray files;
    const QDir directory( plugin.value( "directory" ).toString() );
    for ( const auto* key : { "actions", "views" } ) {
        for ( const auto& item : plugin.value( key ).toList() ) {
            const auto icon = item.toMap().value( "icon" ).toString();
            if ( icon.isEmpty() ) {
                continue;
            }
            const QFileInfo file( directory.filePath( icon ) );
            const bool exists = file.exists();
            files.append( QJsonArray{ icon, exists ? file.size() : qint64( -1 ),
                                      exists ? file.lastModified().toMSecsSinceEpoch() : 0 } );
        }
    }
    return files;
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
    browse->setToolTip( tr( "Browse the plugins of your plugin repositories" ) );
    menu_->addAction( manage );
    menu_->addAction( browse );
    // Plugins with a new version from their repository (design 3.7); hidden when there is none.
    updatesAction_ = new QAction( window->style()->standardIcon( QStyle::SP_BrowserReload ),
                                  tr( "Updates" ), this );
    updatesAction_->setObjectName( "pluginUpdatesAction" );
    updatesAction_->setToolTip( tr( "Plugins with a new version in the repository they were "
                                    "installed from" ) );
    updatesAction_->setVisible( false );
    menu_->addAction( updatesAction_ );
    connect( updatesAction_, &QAction::triggered, this,
             [ this ] { openPluginsWindow( PluginsTab::Installed ); } );
    menu_->addSeparator();
    // U15b: the source the plugin actions of this menu and the toolbar run on.
    auto* sourceRow = new QWidget( window );
    auto* sourceLayout = new QHBoxLayout( sourceRow );
    sourceLayout->setContentsMargins( 8, 2, 8, 2 );
    auto* sourceLabel = new QLabel( tr( "Run actions on" ), sourceRow );
    sourcePicker_ = new AnalysisSourcePicker(
        [ window = QPointer<MainWindow>( window ) ] {
            return window ? window->toolSources() : QVariantList{};
        },
        SourceRules{}, sourceRow );
    sourcePicker_->setObjectName( "pluginSourcePicker" );
    sourcePicker_->setAccessibleName( sourceLabel->text() );
    sourcePicker_->setToolTip( tr( "The tab, file or tabs that plugin actions run on" ) );
    sourceLabel->setBuddy( sourcePicker_ );
    sourceLayout->addWidget( sourceLabel );
    sourceLayout->addWidget( sourcePicker_, 1 );
    auto* sourceAction = new QWidgetAction( this );
    sourceAction->setObjectName( "pluginSourceAction" );
    sourceAction->setDefaultWidget( sourceRow );
    menu_->addAction( sourceAction );
    connect( menu_, &QMenu::aboutToShow, sourcePicker_, &AnalysisSourcePicker::refresh );
    menu_->addSeparator();
    if ( toolbar ) {
        toolbar->addAction( manage );
        toolbar->addAction( browse );
        toolbar->widgetForAction( manage )->setAccessibleName( manage->text() );
        toolbar->widgetForAction( browse )->setAccessibleName( browse->text() );
    }
    // One Plugins and Packs window: Manage opens its Installed tab, Browse its Available tab.
    connect( manage, &QAction::triggered, this,
             [ this ] { openPluginsWindow( PluginsTab::Installed ); } );
    connect( browse, &QAction::triggered, this,
             [ this ] { openPluginsWindow( PluginsTab::Available ); } );
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
    const auto windowId = window->pluginContext( {} ).value( "windowId" ).toString();
    // Plugin views (docks, windows, dialogs) and message boxes of this window.
    viewHost_ = new PluginViewHost(
        window, windowId,
        [ window = QPointer<MainWindow>( window ) ] {
            return window ? window->pluginContext( {} ) : QVariantMap{};
        },
        manager, executor_ );
    viewHost_->setSourceProviders(
        [ window = QPointer<MainWindow>( window ) ] {
            return window ? window->toolSources() : QVariantList{};
        },
        [ window = QPointer<MainWindow>( window ) ]( const QString& source, QString* error ) {
            return window ? window->pluginContextForSource( source, error ) : QVariantMap{};
        } );
    refreshActions();
    connect( window, &MainWindow::windowClosed, this, [ this ] {
        if ( viewHost_ ) {
            viewHost_->windowClosing();
        }
    } );
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
bool PluginUiController::confirmPackLua( QWidget* parent, const QString& id ) const
{
    if ( !manager_ ) {
        return false;
    }
    for ( const auto& value : manager_->plugins() ) {
        const auto p = value.toMap();
        if ( p.value( "id" ) == id ) {
            return pluginsui::confirmPackLua( parent, manager_, id, p.value( "name" ).toString(),
                                              p.value( "contents" ).toStringList(), true );
        }
    }
    return true;
}

void PluginUiController::editPackItem( QWidget* parent, const QString& packId,
                                       const QString& path )
{
    if ( manager_ ) {
        pluginsui::openPackItemEditor( parent, executor_, packId,
                                       manager_->manifest( packId ).value( "name" ).toString( packId ),
                                       path );
    }
}

int PluginUiController::pendingUpdateCount() const
{
    if ( !manager_ ) {
        return 0;
    }
    const auto updates = manager_->availableUpdates();
    return int( std::count_if( updates.cbegin(), updates.cend(), []( const QVariant& value ) {
        return !value.toMap().value( "skipped" ).toBool();
    } ) );
}

void PluginUiController::refreshUpdates()
{
    if ( !updatesAction_ ) {
        return;
    }
    const auto count = pendingUpdateCount();
    updatesAction_->setText( tr( "Updates (%1)…" ).arg( count ) );
    updatesAction_->setVisible( count > 0 );
}

void PluginUiController::refreshActions()
{
    if ( !window_ || !manager_ ) {
        return;
    }
    refreshUpdates();
    QJsonArray descriptions;
    const auto plugins = manager_->plugins();
    for ( const auto& value : plugins ) {
        const auto p = value.toMap();
        if ( p.value( "enabled" ).toBool() && p.value( "error" ).toString().isEmpty() ) {
            // Everything the menu entries and buttons are built from, icon files included.
            descriptions.append( QJsonObject{
                { "id", p.value( "id" ).toString() },
                { "name", p.value( "name" ).toString() },
                { "version", p.value( "version" ).toString() },
                { "directory", p.value( "directory" ).toString() },
                { "actions", QJsonArray::fromVariantList( p.value( "actions" ).toList() ) },
                { "views", QJsonArray::fromVariantList( p.value( "views" ).toList() ) },
                { "icons", iconFiles( p ) } } );
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
        // Views: a Plugins menu entry (menu, default true) and/or a toolbar button (toolbar, default
        // false) that open or raise the view in this window.
        for ( const auto& item : plugin.value( "views" ).toList() ) {
            const auto view = QJsonObject::fromVariantMap( item.toMap() );
            const bool inMenu = view.value( "menu" ).toBool( true );
            const bool inToolbar = view.value( "toolbar" ).toBool( false );
            if ( !inMenu && !inToolbar ) {
                continue;
            }
            const auto viewId = view.value( "id" ).toString();
            auto* action = new QAction( iconFor( plugin, view,
                                                 view.value( "kind" ) == "dialog"
                                                     ? QStyle::SP_MessageBoxInformation
                                                     : QStyle::SP_TitleBarNormalButton ),
                                        view.value( "title" ).toString( viewId ), this );
            action->setObjectName( "pluginViewAction_" + id + "_" + viewId );
            action->setToolTip( plugin.value( "name" ).toString() + ": " + action->text() );
            if ( inMenu ) {
                menu_->addAction( action );
            }
            if ( inToolbar && toolbar ) {
                toolbar->addAction( action );
                toolbar->widgetForAction( action )->setAccessibleName( action->text() );
            }
            actions_.append( action );
            connect( action, &QAction::triggered, this, [ this, id, viewId ] {
                QString error;
                if ( viewHost_ && !viewHost_->openView( id, viewId, {}, &error ) ) {
                    report( commanderFailure( CommanderResultCode::ExecutionFailed, error ) );
                }
            } );
        }
        for ( const auto& item : plugin.value( "actions" ).toList() ) {
            const auto definition = QJsonObject::fromVariantMap( item.toMap() );
            const bool inMenu = definition.value( "menu" ).toBool( true );
            const bool inToolbar = definition.value( "toolbar" ).toBool( true );
            if ( !inMenu && !inToolbar ) {
                continue; // still callable from Commander, CLI and MCP
            }
            const auto type = definition.value( "capability" ).toString();
            const auto standard = type == "datasource"  ? QStyle::SP_MediaPlay
                                  : type == "converter" ? QStyle::SP_FileDialogContentsView
                                                        : QStyle::SP_ComputerIcon;
            auto* action = new QAction(
                iconFor( plugin, definition, standard ),
                plugin.value( "name" ).toString() + ": "
                    + definition.value( "label" ).toString( definition.value( "id" ).toString() ),
                this );
            action->setObjectName( "pluginAction_" + id + "_"
                                   + definition.value( "id" ).toString() );
            action->setToolTip( action->text() );
            if ( inMenu ) {
                menu_->addAction( action );
            }
            if ( inToolbar && toolbar ) {
                toolbar->addAction( action );
                toolbar->widgetForAction( action )->setAccessibleName( action->text() );
            }
            actions_.append( action );
            connect( action, &QAction::triggered, this,
                     [ this, id, definition ] { invoke( id, definition ); } );
        }
    }
}

QIcon PluginUiController::iconFor( const QVariantMap& plugin, const QJsonObject& item,
                                   QStyle::StandardPixmap fallback )
{
    const auto generic = window_->style()->standardIcon( fallback );
    const auto icon = item.value( "icon" ).toString();
    if ( icon.isEmpty() ) {
        return generic;
    }
    const auto path = QDir( plugin.value( "directory" ).toString() ).filePath( icon );
    QIcon loaded( path );
    if ( QFileInfo::exists( path ) && !loaded.isNull() && !loaded.pixmap( 16, 16 ).isNull() ) {
        return loaded;
    }
    const auto key = plugin.value( "id" ).toString() + '|' + icon;
    if ( !iconWarnings_.contains( key ) ) {
        iconWarnings_.insert( key );
        LOG_WARNING << "Plugin icon not usable, using the generic icon: " << path.toStdString();
    }
    return generic;
}

QString PluginUiController::actionSource() const
{
    return sourcePicker_ ? sourcePicker_->selection() : QStringLiteral( "focused" );
}

void PluginUiController::invoke( const QString& id, const QJsonObject& action )
{
    QJsonObject arguments;
    const auto context = window_->pluginContext( {} );
    // A converter reads the focused tab's file unless another source is chosen; each job of
    // another source gets its own tab's file from its context.
    const auto focusedPath = QJsonValue::fromVariant( context.value( "filePath" ) );
    const bool converter = action.value( "capability" ) == "converter";
    if ( converter ) {
        arguments.insert( "input_path", focusedPath );
    }
    auto source = actionSource();
    const auto fields = action.value( "fields" ).toArray();
    if ( !fields.isEmpty()
         && !editValues( window_, action.value( "label" ).toString( "Plugin action" ), fields,
                         &arguments, &source, [ window = window_ ] {
                             return window ? window->toolSources() : QVariantList{};
                         } ) ) {
        return;
    }
    CommanderRequest request;
    request.action = CommanderAction::InvokePlugin;
    request.pluginId = id;
    request.pluginAction = action.value( "id" ).toString();
    if ( source == QLatin1String( "focused" ) ) {
        request.windowIndex = context.value( "windowIndex" ).toInt();
        request.tabId = context.value( "tabId" ).toString();
    }
    else {
        request.source = source;
        if ( converter && arguments.value( "input_path" ) == focusedPath ) {
            arguments.remove( "input_path" );
        }
    }
    request.pluginArguments = arguments.toVariantMap();
    LOG_DEBUG << "Plugin action from the menu plugin=" << id.toStdString()
              << " action=" << request.pluginAction.toStdString()
              << " source=" << source.toStdString();
    report( executor_( request ) );
}

void PluginUiController::openGuide()
{
    if ( window_ ) {
        window_->showDocumentation( QLatin1String( MainWindow::PluginRepositoriesGuideAnchor ) );
    }
}

void PluginUiController::openPluginsWindow( PluginsTab tab )
{
    if ( !manager_ || !window_ ) {
        return;
    }
    if ( !pluginsWindow_ ) {
        auto* dialog = new QDialog( window_ );
        dialog->setAttribute( Qt::WA_DeleteOnClose );
        dialog->setObjectName( "pluginsAndPacksDialog" );
        dialog->setWindowTitle( tr( "Plugins and Packs" ) );
        dialog->resize( 980, 640 );
        auto* layout = new QVBoxLayout( dialog );
        auto* tabs = new QTabWidget( dialog );
        tabs->setObjectName( "pluginsAndPacksTabs" );
        tabs->setAccessibleName( tr( "Plugins and Packs" ) );
        const auto guide = [ self = QPointer<PluginUiController>( this ) ] {
            if ( self ) {
                self->openGuide();
            }
        };
        tabs->addTab( createInstalledTab( tabs ), tr( "Installed" ) );
        tabs->addTab( pluginsui::createAvailableTab( tabs, manager_, executor_, guide ),
                      tr( "Available" ) );
        tabs->addTab( pluginsui::createRepositoriesTab( tabs, manager_, executor_, guide ),
                      tr( "Repositories" ) );
        layout->addWidget( tabs );
        pluginsWindow_ = dialog;
        LOG_DEBUG << "Plugins and Packs window created";
    }
    auto* tabs = pluginsWindow_->findChild<QTabWidget*>( "pluginsAndPacksTabs" );
    tabs->setCurrentIndex( int( tab ) );
    pluginsWindow_->show();
    pluginsWindow_->raise();
    pluginsWindow_->activateWindow();
}

QWidget* PluginUiController::createInstalledTab( QWidget* parent )
{
    auto* tab = new QWidget( parent );
    tab->setObjectName( "pluginsInstalledTab" );
    auto* layout = new QVBoxLayout( tab );
    auto* table = new QTableWidget( 0, 5, tab );
    table->setObjectName( "pluginManagerTable" );
    table->setAccessibleName( tr( "Installed plugins" ) );
    table->setHorizontalHeaderLabels(
        { tr( "Name" ), tr( "Kind" ), tr( "Version" ), tr( "Source" ), tr( "State" ) } );
    table->setSelectionBehavior( QAbstractItemView::SelectRows );
    table->setSelectionMode( QAbstractItemView::SingleSelection );
    table->setEditTriggers( QAbstractItemView::NoEditTriggers );
    table->verticalHeader()->setVisible( false );
    table->horizontalHeader()->setSectionResizeMode( QHeaderView::Stretch );
    layout->addWidget( table, 2 );
    auto* details = new QPlainTextEdit( tab );
    details->setReadOnly( true );
    details->setObjectName( "pluginDiagnostics" );
    details->setAccessibleName( tr( "Plugin details" ) );
    details->setMaximumBlockCount( 2000 );
    layout->addWidget( details, 1 );
    auto* storageUsage = new QLabel( tab );
    storageUsage->setObjectName( "pluginStorageUsage" );
    layout->addWidget( storageUsage );
    auto* status = new QLabel( tab );
    status->setObjectName( "pluginInstalledStatus" );
    status->setWordWrap( true );
    status->setTextFormat( Qt::PlainText ); // names and errors come from catalogs
    layout->addWidget( status );
    // Timers and job callbacks of this tab can outlive the controller.
    const QPointer<PluginUiController> self( this );
    auto* row = new QHBoxLayout;
    layout->addLayout( row );
    auto* secondRow = new QHBoxLayout;
    layout->addLayout( secondRow );
    const auto addButton
        = [ tab ]( QHBoxLayout* buttons, const QString& text, const QString& name ) {
              auto* button = new QPushButton( text, tab );
              button->setObjectName( name );
              button->setAccessibleName( QString( text ).remove( QChar( 0x2026 ) ).trimmed() );
              buttons->addWidget( button );
              return button;
          };
    const auto selectedId = [ table ] {
        return table->currentRow() >= 0 && table->item( table->currentRow(), 0 )
                   ? table->item( table->currentRow(), 0 )->data( Qt::UserRole ).toString()
                   : QString{};
    };
    const auto refresh = [ self, table, details, storageUsage, selectedId ] {
        if ( !self || !self->manager_ ) {
            return;
        }
        auto* manager = self->manager_.data();
        const auto storage = manager->storageStatus();
        storageUsage->setText(
            tr( "Captured: %1 bytes; reserved: %2 bytes; total limit: %3 bytes (0 = unlimited)" )
                .arg( storage.value( "captured_bytes" ).toLongLong() )
                .arg( storage.value( "reserved_bytes" ).toLongLong() )
                .arg( storage.value( "limits" ).toMap().value( "max_total_bytes" ).toLongLong() ) );
        const auto selected = selectedId();
        const auto list = manager->plugins();
        const auto catalog = manager->catalogEntries();
        table->setRowCount( static_cast<int>( list.size() ) );
        int index = 0;
        for ( const auto& value : list ) {
            const auto p = value.toMap();
            const auto id = p.value( "id" ).toString();
            const auto source = p.value( "source" ).toMap();
            QString state
                = !p.value( "error" ).toString().isEmpty()
                      ? tr( "Error: %1" ).arg( p.value( "error" ).toString() )
                  : p.value( "state" ) == "ready" ? tr( "Running" )
                  : p.value( "state" ) == "starting" || p.value( "state" ) == "initializing"
                      ? tr( "Starting" )
                  : p.value( "state" ) == "stopping" ? tr( "Stopping" )
                  : p.value( "state" ) == "failed"   ? tr( "Failed" )
                  : p.value( "enabled" ).toBool()    ? tr( "Enabled" )
                                                     : tr( "Disabled" );
            // An update only from the install source (get_plugins `update`); other repositories
            // are only mentioned.
            QStringList others;
            for ( const auto& item : catalog ) {
                const auto entry = item.toMap();
                if ( entry.value( "id" ) == id
                     && !( source.value( "type" ) == "repository"
                           && entry.value( "repository" ) == source.value( "repository" ) )
                     && !others.contains( entry.value( "repositoryName" ).toString() ) ) {
                    others.append( entry.value( "repositoryName" ).toString() );
                }
            }
            // Configuration packs (E16): what the pack's content still needs.
            if ( p.value( "kind" ) == "config-pack" && p.value( "error" ).toString().isEmpty() ) {
                const auto decoder = PreviewDecoderSettings::instance().configuration();
                if ( p.value( "luaFiles" ).toInt() > 0
                     && ( !decoder.value( "enabled" ).toBool()
                          || decoder.value( "tshark" ).toString().isEmpty() ) ) {
                    state += QStringLiteral( " · " ) + tr( "Needs TShark (Preview Decoder Settings)" );
                }
                if ( p.value( "pythonDecoders" ).toInt() > 0 ) {
                    state += QStringLiteral( " · " )
                             + tr( "Python decoders: requires a newer CILogg" );
                }
                // Local edits and an update that waits for a merge (design 3.3, 4.6).
                if ( p.value( "localEdits" ).toInt() > 0 ) {
                    state += QStringLiteral( " · " )
                             + tr( "Edited locally (%1)" ).arg( p.value( "localEdits" ).toInt() );
                }
                if ( !p.value( "needsMerge" ).toString().isEmpty() ) {
                    state += QStringLiteral( " · " )
                             + tr( "Update to %1 needs a merge" )
                                   .arg( p.value( "needsMerge" ).toString() );
                }
            }
            const auto update = p.value( "update" ).toMap();
            if ( !update.isEmpty() ) {
                state += QStringLiteral( " · " )
                         + ( update.value( "skipped" ).toBool()
                                 ? tr( "Update to %1 (skipped)" )
                                 : tr( "Update to %1" ) )
                               .arg( update.value( "version" ).toString() );
            }
            if ( !others.isEmpty() ) {
                state += QStringLiteral( " · " )
                         + tr( "Also offered by %1" ).arg( others.join( ", " ) );
            }
            const QStringList texts{ p.value( "name", id ).toString(), pluginsui::kindLabel( p ),
                                     p.value( "version" ).toString(), pluginsui::sourceLabel( p ),
                                     state };
            for ( int col = 0; col < texts.size(); ++col ) {
                auto* item = new QTableWidgetItem( texts[ col ] );
                item->setData( Qt::UserRole, id );
                item->setToolTip( texts[ col ] );
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
                + QString::fromUtf8( QJsonDocument::fromVariant( manager->jobs() )
                                         .toJson( QJsonDocument::Indented ) );
        details->setPlainText( info );
    };
    auto* updateButton = addButton( row, tr( "Update" ), "pluginManage_update" );
    updateButton->setToolTip( tr( "Update the selected plugin from the repository it was "
                                  "installed from" ) );
    connect( updateButton, &QPushButton::clicked, tab, [ this, self, tab, status, selectedId ] {
        const auto id = selectedId();
        if ( id.isEmpty() || !manager_ ) {
            return;
        }
        QVariantMap source;
        QString name = id;
        for ( const auto& value : manager_->plugins() ) {
            if ( value.toMap().value( "id" ) == id ) {
                source = value.toMap().value( "source" ).toMap();
                name = value.toMap().value( "name", id ).toString();
            }
        }
        if ( source.value( "type" ) != "repository" ) {
            status->setText( tr( "%1 was not installed from a repository. Install it from a "
                                 "repository on the Available tab to get updates." )
                                 .arg( name ) );
            return;
        }
        // Load the source repository's catalog first, then update from it.
        CommanderRequest catalog;
        catalog.action = CommanderAction::PluginManage;
        catalog.origin = CommanderOriginGui;
        catalog.pluginOperation = "catalog";
        catalog.pluginArguments = { { "repository", source.value( "repository" ) } };
        const auto started = executor_( catalog );
        if ( !started.ok() ) {
            status->setText( started.message );
            return;
        }
        status->setText(
            tr( "Checking %1 for updates…" ).arg( source.value( "repositoryName" ).toString() ) );
        pluginsui::followJob(
            tab, manager_, started.payload.value( "jobId" ).toString(),
            [ self, tab, status, id, name ]( const QVariantMap& job ) {
                if ( !self ) {
                    return;
                }
                if ( job.value( "state" ) != "completed" ) {
                    status->setText( tr( "Could not check for updates: %1" )
                                         .arg( job.value( "error" ).toString() ) );
                    return;
                }
                // The new version's Lua runs as soon as an enabled pack is updated (design 4.3).
                QString entryId;
                for ( const auto& value : self->manager_->plugins() ) {
                    if ( value.toMap().value( "id" ) == id ) {
                        entryId = value.toMap().value( "update" ).toMap().value( "entry_id" ).toString();
                    }
                }
                if ( !pluginsui::confirmPackLua( tab, self->manager_, id, name,
                                                 pluginsui::catalogContents( self->manager_, entryId ),
                                                 false ) ) {
                    status->setText( tr( "The update of %1 was not installed." ).arg( name ) );
                    return;
                }
                CommanderRequest request;
                request.action = CommanderAction::PluginManage;
                request.origin = CommanderOriginGui;
                request.pluginOperation = "update";
                request.pluginId = id;
                status->setText( tr( "Updating %1…" ).arg( name ) );
                // A pack with local edits may need the merge window (design 4.6).
                pluginsui::runPackAwareInstall(
                    tab, self->manager_, self->executor_, request, name,
                    [ status, name ]( const QVariantMap& update ) {
                        status->setText(
                            update.value( "state" ) == "completed"
                                ? tr( "Updated %1 to %2." )
                                      .arg( name, update.value( "result" )
                                                      .toMap()
                                                      .value( "version" )
                                                      .toString() )
                                : tr( "Could not update %1: %2" )
                                      .arg( name, update.value( "error" ).toString() ) );
                    } );
            } );
    } );
    // Skip this version: no notice and no automatic update until a newer one (design 3.7).
    auto* skipButton = addButton( row, tr( "Skip This Version" ), "pluginManage_skipUpdate" );
    skipButton->setToolTip( tr( "Hide the selected plugin's new version until a newer one "
                                "appears" ) );
    connect( skipButton, &QPushButton::clicked, tab, [ this, status, selectedId ] {
        const auto id = selectedId();
        if ( id.isEmpty() || !manager_ ) {
            return;
        }
        QString version;
        QString name = id;
        for ( const auto& value : manager_->plugins() ) {
            const auto p = value.toMap();
            if ( p.value( "id" ) == id ) {
                version = p.value( "update" ).toMap().value( "version" ).toString();
                name = p.value( "name", id ).toString();
            }
        }
        if ( version.isEmpty() ) {
            status->setText( tr( "%1 has no new version to skip." ).arg( name ) );
            return;
        }
        CommanderRequest request;
        request.action = CommanderAction::PluginManage;
        request.origin = CommanderOriginGui;
        request.pluginOperation = "skip_update";
        request.pluginId = id;
        request.pluginArguments = { { "version", version } };
        const auto result = executor_( request );
        status->setText( result.ok()
                             ? tr( "%1 %2 is skipped until a newer version appears." )
                                   .arg( name, version )
                             : result.message );
    } );
    auto* checkButton = addButton( row, tr( "Check for Updates" ), "pluginCheckUpdates" );
    checkButton->setToolTip( tr( "Check every enabled repository for new versions now" ) );
    connect( checkButton, &QPushButton::clicked, tab, [ this, self, tab, status ] {
        if ( !manager_ ) {
            return;
        }
        CommanderRequest request;
        request.action = CommanderAction::PluginManage;
        request.origin = CommanderOriginGui;
        request.pluginOperation = "check_updates";
        const auto started = executor_( request );
        if ( !started.ok() ) {
            status->setText( started.message );
            return;
        }
        status->setText( tr( "Checking the repositories for updates…" ) );
        pluginsui::followJob(
            tab, manager_, started.payload.value( "jobId" ).toString(),
            [ self, status ]( const QVariantMap& checked ) {
                if ( !self ) {
                    return;
                }
                if ( checked.value( "state" ) != "completed" ) {
                    status->setText( tr( "Could not check for updates: %1" )
                                         .arg( checked.value( "error" ).toString() ) );
                    return;
                }
                const auto updates = checked.value( "result" ).toMap().value( "updates" ).toList();
                status->setText( updates.isEmpty()
                                     ? tr( "The plugins from repositories are up to date." )
                                     : tr( "Updates available: %1." ).arg( updates.size() ) );
            } );
    } );
    const auto refreshUpdateButton = [ self, updateButton ] {
        if ( self ) {
            const auto count = self->pendingUpdateCount();
            updateButton->setText( count > 0 ? tr( "Update (%1)" ).arg( count ) : tr( "Update" ) );
        }
    };
    const QList<QPair<QString, QString>> operations{ { "enable", tr( "Enable" ) },
                                                     { "disable", tr( "Disable" ) },
                                                     { "configure", tr( "Configure…" ) },
                                                     { "restart", tr( "Restart" ) },
                                                     { "remove", tr( "Remove" ) } };
    QList<QPushButton*> processOnly;
    for ( const auto& [ operation, label ] : operations ) {
        auto* button = addButton( row, label, "pluginManage_" + operation );
        if ( operation == "configure" || operation == "restart" ) {
            processOnly.append( button );
        }
        connect(
            button, &QPushButton::clicked, tab, [ this, tab, managed = operation, selectedId ] {
                const auto id = selectedId();
                if ( id.isEmpty() || !manager_ ) {
                    return;
                }
                CommanderRequest r;
                r.action = CommanderAction::PluginManage;
                r.origin = CommanderOriginGui;
                r.pluginId = id;
                r.pluginOperation = managed;
                // Enabling a pack with Lua is the trust decision for its code (design 4.3).
                if ( managed == "enable" && !confirmPackLua( tab, id ) ) {
                    return;
                }
                // A pack's local edits and their history go with it (design 4.6).
                if ( managed == "remove" ) {
                    for ( const auto& value : manager_->plugins() ) {
                        const auto p = value.toMap();
                        const auto edits = p.value( "localEdits" ).toInt();
                        if ( p.value( "id" ) == id && edits > 0
                             && QMessageBox::question(
                                    tab, tr( "Remove Pack" ),
                                    tr( "Remove %1? Your edits of %2 of its items, and their "
                                        "history, are removed with it." )
                                        .arg( p.value( "name" ).toString(), QString::number( edits ) ) )
                                    != QMessageBox::Yes ) {
                            return;
                        }
                    }
                }
                if ( managed == "configure" ) {
                    for ( const auto& value : manager_->plugins() ) {
                        const auto p = value.toMap();
                        if ( p.value( "id" ) != id ) {
                            continue;
                        }
                        auto config = QJsonObject::fromVariantMap(
                            p.value( "configurationValues" ).toMap() );
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
                        if ( !editValues( tab, tr( "Configure Plugin" ), fields, &config ) ) {
                            return;
                        }
                        r.pluginArguments = { { "configuration", config.toVariantMap() } };
                    }
                }
                report( executor_( r ) );
            } );
    }
    // A configuration pack has no process and no settings: Configure and Restart do not apply.
    connect( table, &QTableWidget::itemSelectionChanged, tab, [ self, processOnly, selectedId ] {
        if ( !self || !self->manager_ ) {
            return;
        }
        const auto id = selectedId();
        const auto kind = cilogg::plugins::packageKind( self->manager_->manifest( id ) );
        for ( auto* button : processOnly ) {
            button->setEnabled( id.isEmpty() || kind != QLatin1String( "config-pack" ) );
        }
    } );
    row->addStretch( 1 );
    for ( const auto& source : QStringList{ "folder", "zip" } ) {
        auto* button = addButton(
            secondRow, source == "folder" ? tr( "Install Folder…" ) : tr( "Install ZIP…" ),
            "pluginInstall_" + source );
        connect( button, &QPushButton::clicked, tab, [ this, tab, source, status ] {
            const auto path = source == "folder"
                                  ? QFileDialog::getExistingDirectory( tab )
                                  : QFileDialog::getOpenFileName( tab, tr( "Plugin package" ), {},
                                                                  "ZIP (*.zip)" );
            if ( !path.isEmpty() ) {
                // A local package that replaces an enabled pack with Lua: asked first (design
                // 4.3). The manifest is only read for the question; the install checks it.
                const auto manifest = cilogg::plugins::PluginRepository::packageManifest( path );
                if ( cilogg::plugins::isConfigPack( manifest )
                     && !pluginsui::confirmPackLua(
                         tab, manager_, manifest.value( "id" ).toString(),
                         manifest.value( "name" ).toString(),
                         cilogg::plugins::packContents( manifest ), false ) ) {
                    return;
                }
                CommanderRequest r;
                r.action = CommanderAction::PluginManage;
                r.origin = CommanderOriginGui;
                r.pluginOperation = "install";
                r.pluginArguments = { { "path", path } };
                // Installing over an edited pack may need the merge window (design 4.6).
                const auto name = manifest.value( "name" ).toString( QFileInfo( path ).fileName() );
                pluginsui::runPackAwareInstall(
                    tab, manager_, executor_, r, name, [ status, name ]( const QVariantMap& job ) {
                        status->setText( job.value( "state" ) == "completed"
                                             ? tr( "Installed %1." ).arg( name )
                                             : tr( "Could not install %1: %2" )
                                                   .arg( name, job.value( "error" ).toString() ) );
                    } );
            }
        } );
    }
    // Configuration packs: their items, edited in place (design 4.6).
    auto* packFiles = addButton( secondRow, tr( "Pack Files…" ), "pluginManage_packFiles" );
    packFiles->setEnabled( false );
    connect( packFiles, &QPushButton::clicked, tab, [ this, tab, selectedId ] {
        const auto id = selectedId();
        if ( id.isEmpty() || !manager_ ) {
            return;
        }
        pluginsui::openPackFiles( tab, executor_, id,
                                  manager_->manifest( id ).value( "name" ).toString( id ) );
    } );
    connect( table, &QTableWidget::itemSelectionChanged, tab, [ self, packFiles, selectedId ] {
        if ( self && self->manager_ ) {
            packFiles->setEnabled( cilogg::plugins::isConfigPack(
                self->manager_->manifest( selectedId() ) ) );
        }
    } );
    auto* storage = addButton( secondRow, tr( "Storage Limits…" ), "pluginStorageLimits" );
    connect( storage, &QPushButton::clicked, tab, [ this, tab ] {
        if ( !manager_ ) {
            return;
        }
        const auto storageState = manager_->storageStatus();
        auto values = QJsonObject::fromVariantMap( storageState.value( "limits" ).toMap() );
        auto fields = QJsonArray::fromVariantList( storageState.value( "fields" ).toList() );
        for ( int index = 0; index < fields.size(); ++index ) {
            auto field = fields[ index ].toObject();
            const auto id = field.value( "id" ).toString();
            field[ "label" ] = id == "max_capture_bytes" ? tr( "Per-capture bytes (0: unlimited)" )
                               : id == "max_total_bytes"
                                   ? tr( "Total capture bytes (0: unlimited)" )
                                   : tr( "Free-space reserve bytes (0: disabled)" );
            fields[ index ] = field;
        }
        if ( editValues( tab, tr( "Plugin Capture Storage" ), fields, &values ) ) {
            CommanderRequest request;
            request.action = CommanderAction::PluginManage;
            request.origin = CommanderOriginGui;
            request.pluginOperation = "configure";
            request.pluginArguments = { { "storage_limits", values.toVariantMap() } };
            report( executor_( request ) );
        }
    } );
    auto* captures = addButton( secondRow, tr( "Open Captures Folder" ), "pluginCapturesFolder" );
    connect( captures, &QPushButton::clicked, tab, [ this ] {
        if ( manager_ ) {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile( manager_->storageStatus().value( "directory" ).toString() ) );
        }
    } );
    secondRow->addStretch( 1 );
    auto* timer = new QTimer( tab );
    timer->setInterval( 1000 );
    connect( timer, &QTimer::timeout, tab, refresh );
    connect( timer, &QTimer::timeout, tab, refreshUpdateButton );
    timer->start();
    refresh();
    refreshUpdateButton();
    return tab;
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
