// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pluginviewhost.h"
#include "analysissource.h"
#include "diagnostics.h"
#include "log.h"
#include "pluginicons.h"
#include "pluginmanager.h"
#include "pluginmenus.h"
#include "plugintablemodel.h"
#include <QAbstractButton>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <cmath>
#include <utility>

namespace {
constexpr int DefaultRenderIntervalMs = 100; // at most 10 renders per second
constexpr int MaxPendingPatchEntries = 4096;
constexpr int HardPendingPatchEntries = 65536;

QString keyOf( const QString& pluginId, const QString& id )
{
    return pluginId + QLatin1Char( '\n' ) + id;
}

Qt::DockWidgetArea dockArea( const QString& area )
{
    return area == "left"     ? Qt::LeftDockWidgetArea
           : area == "top"    ? Qt::TopDockWidgetArea
           : area == "bottom" ? Qt::BottomDockWidgetArea
                              : Qt::RightDockWidgetArea;
}

QString areaName( Qt::DockWidgetArea area )
{
    return area == Qt::LeftDockWidgetArea     ? QStringLiteral( "left" )
           : area == Qt::TopDockWidgetArea    ? QStringLiteral( "top" )
           : area == Qt::BottomDockWidgetArea ? QStringLiteral( "bottom" )
                                              : QStringLiteral( "right" );
}

QMessageBox::StandardButton standardButton( const QString& name )
{
    static const QMap<QString, QMessageBox::StandardButton> buttons{
        { "ok", QMessageBox::Ok },         { "cancel", QMessageBox::Cancel },
        { "yes", QMessageBox::Yes },       { "no", QMessageBox::No },
        { "retry", QMessageBox::Retry },   { "abort", QMessageBox::Abort },
        { "ignore", QMessageBox::Ignore }, { "close", QMessageBox::Close }
    };
    return buttons.value( name, QMessageBox::Ok );
}

QString safeName( QString text )
{
    static const QRegularExpression unsafe( "[^A-Za-z0-9._-]" );
    return text.replace( unsafe, "_" );
}

// E17f: a dialog view whose Esc and title bar close button go through the host, which presses
// `cancel` or `close` and may ask "Discard changes?" (docs/plugin-sdk-advanced.md section 29.3).
class PluginViewDialog final : public QDialog {
public:
    using QDialog::QDialog;
    std::function<void()> onReject;
    // E17f: the declared buttons and the one Enter presses (none when no `default` is declared).
    QPointer<QDialogButtonBox> buttonBox;
    QPointer<QPushButton> declaredDefault;
    QPointer<PluginView> view;

protected:
    void keyPressEvent( QKeyEvent* event ) override
    {
        // Enter that no element button took (the view presses an element `default` first) presses
        // the declared `default`, or nothing: never a button Qt made the default by itself, and
        // not where the focused control uses Enter itself (section 29.3).
        if ( ( event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter )
             && ( event->modifiers() & ~Qt::KeypadModifier ) == Qt::NoModifier ) {
            event->accept();
            if ( declaredDefault && declaredDefault->isEnabled() && declaredDefault->isVisible()
                 && ( !view || view->enterPressesDefault() ) ) {
                declaredDefault->click();
            }
            return;
        }
        QDialog::keyPressEvent( event );
    }

    void showEvent( QShowEvent* event ) override
    {
        QDialog::showEvent( event );
        // A shown QDialogButtonBox makes its first accept button the default when no button is;
        // Enter presses only the declared `default` (section 29.3).
        if ( buttonBox ) {
            for ( auto* button : buttonBox->buttons() ) {
                if ( auto* push = qobject_cast<QPushButton*>( button ) ) {
                    push->setDefault( push == declaredDefault.data() );
                }
            }
        }
    }

public:
    void reject() override
    {
        if ( onReject ) {
            onReject();
        }
        else {
            QDialog::reject();
        }
    }
};

QDialogButtonBox::StandardButton dialogBoxButton( const QString& name )
{
    static const QMap<QString, QDialogButtonBox::StandardButton> buttons{
        { "ok", QDialogButtonBox::Ok },       { "cancel", QDialogButtonBox::Cancel },
        { "apply", QDialogButtonBox::Apply }, { "close", QDialogButtonBox::Close },
        { "yes", QDialogButtonBox::Yes },     { "no", QDialogButtonBox::No }
    };
    return buttons.value( name, QDialogButtonBox::NoButton );
}

constexpr char DialogButtonProperty[] = "pluginDialogButton";

// Every opened view gets a serial of its own, so the answer to a question asked about one never
// acts on a view opened again under the same key while the question was open.
quint64 nextOpenSerial = 0;

// E17f: whether a queued patch holds a step a root cannot restate (section 29.2).
bool hasSteps( const QJsonArray& patch )
{
    for ( const auto& entry : patch ) {
        const auto object = entry.toObject();
        if ( object.contains( "append" ) || object.contains( "append_points" )
             || object.contains( "clear" ) || object.contains( "focus" ) ) {
            return true;
        }
    }
    return false;
}

// E17f: the "Discard changes?" question of a view that goes away while it is open.
void dismissDiscardQuestion( QWidget* window, const QString& pluginId, const QString& viewId )
{
    if ( !window ) {
        return;
    }
    const auto name = PluginView::objectNameFor( pluginId, viewId ) + "_discard";
    for ( auto* dialog : window->findChildren<QDialog*>( name, Qt::FindDirectChildrenOnly ) ) {
        if ( dialog->isVisible() ) {
            dialog->reject();
        }
    }
}
} // namespace

struct PluginViewHost::Open {
    QString pluginId;
    QString viewId;
    QString kind;
    QPointer<QWidget> container;
    QPointer<PluginView> view;
    QPointer<QLabel> note;
    // U15b: the source the view follows (focused, tab:<id> or file:<path>) and its picker.
    QString source = QStringLiteral( "focused" );
    QPointer<AnalysisSourcePicker> picker;
    std::optional<QJsonObject> pendingRoot;
    QJsonArray pendingPatch;
    QElapsedTimer lastRender;
    bool closing = false;
    bool stopped = false;
    // Disabled until the plugin sends a root, after opening and after every restart.
    bool awaitingRoot = true;
    bool editDropLogged = false; // an undelivered edit is logged once per open view
    // E17f: the title without the " *" mark, a dialog's declared buttons and its result.
    QString title;
    QStringList dialogButtons;
    bool confirmByPlugin = false;
    QPointer<QDialogButtonBox> buttonBox;
    QJsonValue result = QJsonValue( QJsonValue::Null );
    bool closeConfirmed = false; // the user chose Discard: the next close goes through
    bool confirming = false;     // the question is open
    quint64 serial = ++nextOpenSerial;
};

struct PluginViewHost::Message {
    QString pluginId;
    QString messageId;
    QPointer<QMessageBox> box;
    QMap<QAbstractButton*, QString> names;
    QPointer<QAbstractButton> defaultButton;
    QString defaultName;
    QString defaultText;
    QTimer timer;
    qint64 remainingMs = 0;
    bool finished = false;
    bool expiring = false;
};

PluginViewHost::PluginViewHost( QMainWindow* window, QString windowId, ContextProvider context,
                                cilogg::plugins::PluginManager* manager, Executor executor,
                                PluginViewDialogs dialogs )
    : QObject( window )
    , window_( window )
    , windowId_( std::move( windowId ) )
    , context_( std::move( context ) )
    , manager_( manager )
    , executor_( std::move( executor ) )
    , dialogs_( std::move( dialogs ) )
{
    renderTimer_.setSingleShot( true );
    renderTimer_.setInterval( DefaultRenderIntervalMs );
    connect( &renderTimer_, &QTimer::timeout, this, &PluginViewHost::flush );
    if ( !manager_ ) {
        return;
    }
    connect( manager_, &cilogg::plugins::PluginManager::viewRequest, this,
             &PluginViewHost::handleRequest );
    connect( manager_, &cilogg::plugins::PluginManager::pluginStopped, this,
             [ this ]( const QString& pluginId ) {
                 dropMessages( pluginId );
                 setStopped( pluginId, true );
             } );
    // ui/icons and the end of a plugin's images, once for every window.
    PluginIconRegistry::instance().attach( manager_ );
    // E17f: image elements show a plugin's new pictures.
    connect( &PluginIconRegistry::instance(), &PluginIconRegistry::changed, this,
             [ this ]( const QString& pluginId ) {
                 for ( auto& [ key, open ] : open_ ) {
                     Q_UNUSED( key );
                     if ( open->pluginId == pluginId && open->view ) {
                         open->view->refreshImages();
                     }
                 }
             } );
    connect( manager_, &cilogg::plugins::PluginManager::pluginReady, this,
             [ this ]( const QString& pluginId ) { setStopped( pluginId, false ); } );
    connect( manager_, &cilogg::plugins::PluginManager::viewsRevoked, this,
             [ this ]( const QString& pluginId ) {
                 dropMessages( pluginId );
                 for ( auto it = open_.begin(); it != open_.end(); ) {
                     if ( it->second->pluginId == pluginId ) {
                         it->second->closing = true;
                         if ( it->second->view ) {
                             it->second->view->cancelDialogs();
                         }
                         saveState( *it->second );
                         if ( it->second->container ) {
                             it->second->container->hide();
                             it->second->container->deleteLater();
                         }
                         it = open_.erase( it );
                     }
                     else {
                         ++it;
                     }
                 }
             } );
    manager_->noteActiveWindow( windowId_ );
    if ( window_ ) {
        window_->installEventFilter( this );
    }
}

PluginViewHost::~PluginViewHost()
{
    if ( !closing_ ) {
        windowClosing();
    }
}

void PluginViewHost::setRenderIntervalMs( int milliseconds )
{
    renderTimer_.setInterval( qMax( 0, milliseconds ) );
}

void PluginViewHost::setDialogs( PluginViewDialogs dialogs )
{
    dialogs_ = std::move( dialogs );
    for ( auto& [ key, open ] : open_ ) {
        Q_UNUSED( key );
        if ( open->view ) {
            open->view->setDialogs( dialogs_ );
        }
    }
}

int PluginViewHost::scaled( int milliseconds ) const
{
    return qMax( 1, static_cast<int>( std::lround( milliseconds * timeScale_ ) ) );
}

QJsonObject PluginViewHost::definition( const QString& pluginId, const QString& viewId ) const
{
    if ( !manager_ ) {
        return {};
    }
    for ( const auto& value : manager_->viewDefinitions( pluginId ) ) {
        if ( value.toObject().value( "id" ).toString() == viewId ) {
            return value.toObject();
        }
    }
    return {};
}

QWidget* PluginViewHost::container( const QString& pluginId, const QString& viewId ) const
{
    const auto it = open_.find( keyOf( pluginId, viewId ) );
    return it == open_.end() ? nullptr : it->second->container.data();
}

PluginView* PluginViewHost::view( const QString& pluginId, const QString& viewId ) const
{
    const auto it = open_.find( keyOf( pluginId, viewId ) );
    return it == open_.end() ? nullptr : it->second->view.data();
}

PluginMenuContext PluginViewHost::elementMenuContext( const QString& viewId,
                                                      const QString& elementId,
                                                      const QString& menuId,
                                                      const QJsonObject& target ) const
{
    PluginMenuContext context;
    context.location = QStringLiteral( "element" );
    context.windowId = windowId_;
    context.viewId = viewId;
    context.elementId = elementId;
    context.elementMenuId = menuId;
    context.rowId = target.value( "row_id" ).toString();
    context.columnId = target.value( "column_id" ).toString();
    // E17e: trees name the clicked node or path instead of a row and column.
    context.tree = target.contains( "node_id" );
    context.nodeId = target.value( "node_id" ).toString();
    context.button = target.value( "button" ).toBool(); // E17f
    for ( const auto& id : target.value( "selected_ids" ).toArray() ) {
        context.selectedIds.append( id.toString() );
    }
    // Rows count as whole "lines" for the `when` selection conditions.
    context.selectedLines = target.value( "selected_count" ).toInt();
    return context;
}

QMessageBox* PluginViewHost::messageBox( const QString& pluginId, const QString& messageId ) const
{
    const auto it = messages_.find( keyOf( pluginId, messageId ) );
    return it == messages_.end() || it->second->finished ? nullptr : it->second->box.data();
}

QStringList PluginViewHost::openViews() const
{
    QStringList result;
    for ( const auto& [ key, open ] : open_ ) {
        Q_UNUSED( key );
        result.append( open->pluginId + ':' + open->viewId );
    }
    return result;
}

bool PluginViewHost::openView( const QString& pluginId, const QString& viewId,
                               const QJsonObject& overrides, QString* error )
{
    const auto key = keyOf( pluginId, viewId );
    const bool focus = overrides.value( "focus" ).toBool( true );
    if ( overrides.contains( "source" ) && !overrides.value( "source" ).isString() ) {
        if ( error ) {
            *error = tr( "The view source must be text: focused, tab:<id> or file:<path>." );
        }
        return false;
    }
    const auto source = overrides.value( "source" ).toString( QStringLiteral( "focused" ) );
    if ( const auto existing = open_.find( key ); existing != open_.end() ) {
        if ( overrides.contains( "source" ) && !setViewSource( pluginId, viewId, source, error ) ) {
            return false;
        }
        auto& open = *existing->second;
        if ( overrides.contains( "title" ) ) {
            open.title = PluginView::limitText( overrides.value( "title" ).toString() );
            updateTitle( open );
        }
        if ( auto* dock = qobject_cast<QDockWidget*>( open.container.data() ) ) {
            if ( overrides.contains( "area" ) && window_ ) {
                window_->addDockWidget( dockArea( overrides.value( "area" ).toString() ), dock );
            }
            if ( overrides.contains( "floating" ) ) {
                dock->setFloating( overrides.value( "floating" ).toBool() );
            }
        }
        open.container->show();
        if ( focus ) {
            open.container->raise();
            open.container->activateWindow();
        }
        return true;
    }
    const auto def = definition( pluginId, viewId );
    if ( def.isEmpty() || !window_ || !manager_ || closing_ ) {
        if ( error ) {
            *error = tr( "The plugin does not declare this view." );
        }
        return false;
    }
    QString sourceError;
    if ( const auto parsed = AnalysisSource::parse( source, &sourceError );
         !parsed || parsed->kind == AnalysisSource::Kind::AllOpen ) {
        if ( error ) {
            *error = parsed ? tr( "A view follows one source: the focused tab, a tab or a file." )
                            : sourceError;
        }
        return false;
    }
    const auto context = contextFor( source, &sourceError );
    if ( context.isEmpty() ) {
        if ( error ) {
            *error = sourceError;
        }
        return false;
    }
    const auto result = manager_->openView( pluginId, viewId, context );
    if ( !result.ok() ) {
        if ( error ) {
            *error = result.error;
        }
        LOG_WARNING << "Plugin view open failed plugin=" << pluginId.toStdString()
                    << " view=" << viewId.toStdString() << " reason=" << result.error.toStdString();
        return false;
    }
    auto open = std::make_unique<Open>();
    open->pluginId = pluginId;
    open->viewId = viewId;
    open->kind = def.value( "kind" ).toString( "dock" );
    const auto title = PluginView::limitText(
        overrides.value( "title" ).toString( def.value( "title" ).toString( viewId ) ) );
    const auto name = PluginView::objectNameFor( pluginId, viewId );

    auto* body = new QWidget;
    auto* layout = new QVBoxLayout( body );
    layout->setContentsMargins( 0, 0, 0, 0 );
    auto* note = new QLabel( body );
    note->setObjectName( QString( name ).replace( 0, 10, "pluginViewStatus" ) );
    note->setTextFormat( Qt::PlainText );
    note->setWordWrap( true );
    note->setStyleSheet( "QLabel { background: palette(alternate-base); padding: 4px; }" );
    note->hide();
    layout->addWidget( note );
    open->source = source;
    if ( sources_ ) {
        // U15b: the view's header row chooses the tab it follows.
        auto* row = new QWidget( body );
        row->setObjectName( QString( name ).replace( 0, 10, "pluginViewSourceRow" ) );
        auto* rowLayout = new QHBoxLayout( row );
        rowLayout->setContentsMargins( 4, 2, 4, 2 );
        auto* label = new QLabel( tr( "Source" ), row );
        auto* picker = new AnalysisSourcePicker( sources_, SourceRules::oneSource(), row );
        picker->setObjectName( QString( name ).replace( 0, 10, "pluginViewSource" ) );
        picker->setAccessibleName( tr( "View source" ) );
        picker->setToolTip( tr( "The tab or file this view works on" ) );
        label->setBuddy( picker );
        rowLayout->addWidget( label );
        rowLayout->addWidget( picker, 1 );
        layout->insertWidget( 0, row );
        picker->setSelection( source );
        open->picker = picker;
        connect( picker, &AnalysisSourcePicker::selectionChanged, this,
                 [ this, pluginId, viewId, key ]( const QString& chosen ) {
                     QString failure;
                     if ( setViewSource( pluginId, viewId, chosen, &failure ) ) {
                         return;
                     }
                     const auto it = open_.find( key );
                     if ( it == open_.end() ) {
                         return;
                     }
                     if ( it->second->picker ) {
                         it->second->picker->setSelection( it->second->source );
                     }
                     if ( it->second->note ) {
                         it->second->note->setText( failure );
                         it->second->note->show();
                     }
                 } );
    }
    auto* view = new PluginView( pluginId, viewId, dialogs_, body );
    // Dialogs outlive a view that is deleted while one of them is open.
    view->setDialogParent( window_ );
    // Advanced tooltips ask the plugin through host queries (docs/plugin-sdk-advanced.md 5, 9).
    QString directory;
    for ( const auto& plugin : manager_->plugins() ) {
        if ( plugin.toMap().value( "id" ) == pluginId ) {
            directory = plugin.toMap().value( "directory" ).toString();
        }
    }
    // Design principle 11: E17 keys on existing events only for plugins that declare `handles`.
    view->setExtendedEvents( manager_->declaresHandles( pluginId ) );
    PluginViewQueries queries;
    queries.resolveTooltip
        = [ host = QPointer<PluginViewHost>( this ), pluginId, viewId,
            key ]( const QJsonObject& params, int timeoutMs, bool hover,
                   std::function<void( bool, const QJsonObject& )> done ) {
              if ( !host ) {
                  return QString{};
              }
              const auto it = host->open_.find( key );
              if ( !host->manager_ || it == host->open_.end() || it->second->stopped ) {
                  return QString{};
              }
              auto full = params;
              full.insert( "view_id", viewId );
              full.insert( "window_id", host->windowId_ );
              full.insert( "context", QJsonObject::fromVariantMap(
                                          host->contextFor( it->second->source, nullptr ) ) );
              const auto scope
                  = cilogg::plugins::PluginManager::viewScope( host->windowId_, viewId );
              // One hover query per view; get_plugin_tooltip reads have their own key, so a hover
              // never cancels them (design 5.2).
              return host->manager_->query(
                  pluginId, QStringLiteral( "tooltip/resolve" ), full, timeoutMs,
                  ( hover ? QStringLiteral( "tooltip:" ) : QStringLiteral( "tooltip-read:" ) )
                      + scope,
                  scope,
                  [ answered = std::move( done ), pluginId, viewId,
                    params ]( const cilogg::plugins::PluginQueries::Outcome& outcome ) {
                      using Status = cilogg::plugins::PluginQueries::Status;
                      if ( outcome.status != Status::Cancelled ) {
                          // Method, target ids and latency only, never the contents (design 5.3).
                          static const QStringList statuses{ "answered", "nothing", "error",
                                                             "invalid", "timed out", "cancelled" };
                          cilogg::diagnostics::ActivityJournal::record(
                              QStringLiteral( "plugin" ),
                              QStringLiteral( "Plugin tooltip query %1" )
                                  .arg( statuses.value( static_cast<int>( outcome.status ) ) ),
                              { { "plugin", pluginId },
                                { "view", viewId },
                                { "element", params.value( "element_id" ).toString() },
                                { "row", params.value( "row_id" ).toString() },
                                { "column", params.value( "column_id" ).toString() },
                                { "latencyMs", outcome.latencyMs } } );
                      }
                      answered( outcome.status == Status::Answered
                                    || outcome.status == Status::Nothing,
                                outcome.result.toObject() );
                  } );
          };
    queries.cancel = [ manager = manager_ ]( const QString& id ) {
        if ( manager ) {
            manager->cancelQuery( id );
        }
    };
    queries.iconHtml = [ pluginId, directory ]( const QJsonValue& reference ) {
        return PluginIconRegistry::instance().iconHtml( pluginId, directory, reference );
    };
    queries.icon = [ pluginId, directory ]( const QJsonValue& reference ) {
        return PluginIconRegistry::instance().icon( pluginId, directory, reference );
    };
    // E17f: `image` elements and Markdown images. A registered picture shows at its own size.
    queries.picture = [ pluginId, directory ]( const QJsonValue& reference, const QSize& size ) {
        return PluginIconRegistry::instance().pictureFor( pluginId, directory, reference, size );
    };
    // E17c completion (design 8.2): one query per view; a newer one replaces it.
    queries.complete = [ host = QPointer<PluginViewHost>( this ), pluginId, viewId,
                         key ]( const QJsonObject& params, int timeoutMs,
                                std::function<void( const QJsonArray& )> done ) {
        if ( !host ) {
            return QString{};
        }
        const auto it = host->open_.find( key );
        if ( !host->manager_ || it == host->open_.end() || it->second->stopped ) {
            return QString{};
        }
        auto full = params;
        full.insert( "view_id", viewId );
        full.insert( "window_id", host->windowId_ );
        full.insert( "context",
                     QJsonObject::fromVariantMap( host->contextFor( it->second->source, nullptr ) ) );
        const auto scope = cilogg::plugins::PluginManager::viewScope( host->windowId_, viewId );
        return host->manager_->query(
            pluginId, QStringLiteral( "field/complete" ), full, timeoutMs,
            QStringLiteral( "complete:" ) + scope, scope,
            [ answered = std::move( done ) ]( const cilogg::plugins::PluginQueries::Outcome& outcome ) {
                answered( outcome.status == cilogg::plugins::PluginQueries::Status::Answered
                              ? outcome.result.toObject().value( "items" ).toArray()
                              : QJsonArray{} );
            } );
    };
    // E17e lazy tree children (section 28.1). The exclusive key is the node: expanding it again
    // replaces its query, other nodes' queries continue.
    queries.treeChildren = [ host = QPointer<PluginViewHost>( this ), pluginId, viewId, key ](
                               const QJsonObject& params,
                               std::function<void( bool, const QJsonArray& )> done ) {
        if ( !host ) {
            return QString{};
        }
        const auto it = host->open_.find( key );
        if ( !host->manager_ || it == host->open_.end() || it->second->stopped ) {
            return QString{};
        }
        auto full = params;
        full.insert( "view_id", viewId );
        full.insert( "window_id", host->windowId_ );
        full.insert( "context",
                     QJsonObject::fromVariantMap( host->contextFor( it->second->source, nullptr ) ) );
        const auto scope = cilogg::plugins::PluginManager::viewScope( host->windowId_, viewId );
        return host->manager_->query(
            pluginId, QStringLiteral( "tree/children" ), full, 0,
            // Length-prefixed ids: two nodes never share a key, whatever characters they hold.
            QStringLiteral( "tree:" ) + scope + QLatin1Char( '/' )
                + pluginview::joinedKey( { params.value( "element_id" ).toString(),
                                           params.value( "node_id" ).toString() } ),
            scope,
            [ answered = std::move( done ), pluginId, viewId,
              params ]( const cilogg::plugins::PluginQueries::Outcome& outcome ) {
                using Status = cilogg::plugins::PluginQueries::Status;
                if ( outcome.status == Status::Cancelled ) {
                    // A newer query of the node replaced it, the view closed or the plugin
                    // stopped (the host fails the loading nodes then): no answer to give.
                    return;
                }
                // Ids and latency only, never the nodes (design 5.3).
                cilogg::diagnostics::ActivityJournal::record(
                    QStringLiteral( "plugin" ),
                    QStringLiteral( "Plugin tree children query %1" )
                        .arg( outcome.status == Status::Answered    ? "answered"
                              : outcome.status == Status::Nothing   ? "nothing"
                              : outcome.status == Status::TimedOut  ? "timed out"
                                                                    : "failed" ),
                    { { "plugin", pluginId },
                      { "view", viewId },
                      { "element", params.value( "element_id" ).toString() },
                      { "latencyMs", outcome.latencyMs } } );
                const bool ok
                    = outcome.status == Status::Answered || outcome.status == Status::Nothing;
                answered( ok, outcome.result.toObject().value( "nodes" ).toArray() );
            } );
    };
    // E17d element context menus (section 10.4): the window's PluginMenus adds the entries.
    queries.elementMenu = [ host = QPointer<PluginViewHost>( this ), pluginId,
                            viewId ]( QMenu* menu, const QString& elementId,
                                      const QString& menuId, const QJsonObject& target ) {
        if ( host && host->elementMenuFiller_ ) {
            host->elementMenuFiller_( menu, pluginId, menuId,
                                      host->elementMenuContext( viewId, elementId, menuId, target ) );
        }
    };
    // E17d kept columns (section 10.5): per plugin, view and element, in the manager's settings.
    queries.loadState = [ manager = manager_, pluginId, viewId ]( const QString& elementId ) {
        return manager ? manager->keptViewState( pluginId, viewId )
                             .value( "elements" )
                             .toObject()
                             .value( elementId )
                             .toObject()
                       : QJsonObject{};
    };
    queries.saveState = [ manager = manager_, pluginId, viewId ]( const QString& elementId,
                                                                   const QJsonObject& state ) {
        if ( !manager ) {
            return;
        }
        auto stored = manager->keptViewState( pluginId, viewId );
        auto elements = stored.value( "elements" ).toObject();
        // Each entry remembers when it was saved: when the view's state would pass the limit,
        // the entries saved longest ago go first (tables of ids the plugin no longer uses), and
        // the one being saved always stays.
        auto entry = state;
        entry.insert( "saved", static_cast<double>( QDateTime::currentMSecsSinceEpoch() ) );
        elements.insert( elementId, entry );
        stored.insert( "elements", elements );
        while ( !manager->setKeptViewState( pluginId, viewId, stored ) ) {
            QString oldest;
            double oldestSaved = 0;
            for ( auto it = elements.begin(); it != elements.end(); ++it ) {
                const auto saved = it.value().toObject().value( "saved" ).toDouble();
                if ( it.key() != elementId && ( oldest.isEmpty() || saved < oldestSaved ) ) {
                    oldest = it.key();
                    oldestSaved = saved;
                }
            }
            if ( oldest.isEmpty() ) {
                LOG_WARNING << "Plugin view state not kept (over the limit) plugin="
                            << pluginId.toStdString() << " view=" << viewId.toStdString();
                return;
            }
            elements.remove( oldest );
            stored.insert( "elements", elements );
        }
    };
    view->setQueries( std::move( queries ) );
    view->setEnabled( false ); // until the plugin sends its root
    layout->addWidget( view, 1 );
    open->note = note;
    open->view = view;

    QWidget* container = nullptr;
    if ( open->kind == "window" ) {
        container = new QWidget( window_, Qt::Window );
        auto* frame = new QVBoxLayout( container );
        frame->setContentsMargins( 0, 0, 0, 0 );
        frame->addWidget( body );
    }
    else if ( open->kind == "dialog" ) {
        auto* dialog = new PluginViewDialog( window_ );
        dialog->view = view;
        // Modal to its CILogg window only; shown with show(), never a blocking exec().
        dialog->setWindowModality( def.value( "modal" ).toBool( false ) ? Qt::WindowModal
                                                                        : Qt::NonModal );
        auto* frame = new QVBoxLayout( dialog );
        frame->setContentsMargins( 0, 0, 0, 0 );
        frame->addWidget( body );
        // E17f: the declared buttons in a button box below the view (section 29.3).
        for ( const auto& value : def.value( "buttons" ).toArray() ) {
            open->dialogButtons.append( value.toString() );
        }
        open->confirmByPlugin = def.value( "confirm_by_plugin" ).toBool();
        if ( !open->dialogButtons.isEmpty() ) {
            auto* box = new QDialogButtonBox( dialog );
            box->setObjectName( name + "_buttons" );
            box->setContentsMargins( 6, 0, 6, 6 );
            for ( const auto& buttonName : std::as_const( open->dialogButtons ) ) {
                auto* button = box->addButton( dialogBoxButton( buttonName ) );
                if ( !button ) {
                    continue;
                }
                button->setObjectName( name + "_button_" + buttonName );
                button->setProperty( DialogButtonProperty, buttonName );
                button->setAutoDefault( false );
                if ( buttonName == def.value( "default" ).toString() ) {
                    button->setDefault( true );
                    dialog->declaredDefault = button;
                }
            }
            dialog->buttonBox = box;
            connect( box, &QDialogButtonBox::clicked, this,
                     [ this, key ]( QAbstractButton* button ) {
                         dialogButton( key, button->property( DialogButtonProperty ).toString() );
                     } );
            box->setEnabled( false ); // as the view: until the plugin's first root
            frame->addWidget( box );
            open->buttonBox = box;
        }
        dialog->onReject = [ host = QPointer<PluginViewHost>( this ), key ] {
            if ( host ) {
                host->dialogRejected( key );
            }
        };
        connect( dialog, &QDialog::finished, this, [ this, key ] { containerClosed( key ); } );
        container = dialog;
    }
    else {
        auto* dock = new QDockWidget( title, window_ );
        dock->setFeatures( QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
                           | QDockWidget::DockWidgetFloatable );
        dock->setWidget( body );
        container = dock;
    }
    container->setObjectName( name );
    container->setWindowTitle( title );
    container->setAccessibleName( title );
    container->installEventFilter( this );
    open->container = container;
    open->title = title;
    // E17f: `modified` marks the title (section 29.3).
    connect( view, &PluginView::modifiedChanged, this, [ this, key ] {
        const auto it = open_.find( key );
        if ( it != open_.end() ) {
            updateTitle( *it->second );
        }
    } );

    connect( view, &PluginView::eventRaised, this,
             [ this, pluginId, viewId, key, view ]( const QJsonObject& event ) {
                 if ( manager_ && manager_->sendViewEvent( pluginId, viewId, windowId_, event ) ) {
                     return;
                 }
                 // Not delivered (the plugin is stopping, not reading its input, or the event is
                 // too large): the plugin will never answer, so an edited cell must not stay
                 // pending and changed check boxes go back (E17d). Every event reports it.
                 view->markEventDropped();
                 const auto type = event.value( "type" ).toString();
                 const auto it = open_.find( key );
                 if ( ( type == QLatin1String( "edited" ) || type == QLatin1String( "checked" ) )
                      && it != open_.end() && !std::exchange( it->second->editDropLogged, true ) ) {
                     LOG_WARNING << "Plugin view " << type.toStdString()
                                 << " event not delivered, set back plugin="
                                 << pluginId.toStdString() << " view=" << viewId.toStdString();
                 }
             } );
    connect( view, &PluginView::actionRequested, this,
             [ this, pluginId, viewId ]( const QString& action, const QVariantMap& arguments ) {
                 invokeAction( pluginId, viewId, action, arguments );
             } );
    auto* raw = open.get();
    open_.emplace( key, std::move( open ) );
    place( *raw, def, overrides );
    if ( auto* dock = qobject_cast<QDockWidget*>( container ) ) {
        connect( dock, &QDockWidget::dockLocationChanged, this, [ this, key ] {
            const auto it = open_.find( key );
            if ( it != open_.end() && !it->second->closing ) {
                saveState( *it->second );
            }
        } );
        connect( dock, &QDockWidget::topLevelChanged, this, [ this, key ] {
            const auto it = open_.find( key );
            if ( it != open_.end() && !it->second->closing ) {
                saveState( *it->second );
            }
        } );
    }
    container->show();
    if ( focus ) {
        container->raise();
        container->activateWindow();
    }
    LOG_DEBUG << "Plugin view container opened plugin=" << pluginId.toStdString()
              << " view=" << viewId.toStdString() << " kind=" << raw->kind.toStdString()
              << " window=" << windowId_.toStdString();
    return true;
}

void PluginViewHost::setSourceProviders( SourcesProvider sources, SourceContext context )
{
    sources_ = std::move( sources );
    sourceContext_ = std::move( context );
}

QVariantMap PluginViewHost::contextFor( const QString& source, QString* error ) const
{
    if ( source.isEmpty() || source == QLatin1String( "focused" ) || !sourceContext_ ) {
        return context_ ? context_() : QVariantMap{};
    }
    return sourceContext_( source, error );
}

QString PluginViewHost::viewSource( const QString& pluginId, const QString& viewId ) const
{
    const auto it = open_.find( keyOf( pluginId, viewId ) );
    return it == open_.end() ? QString{} : it->second->source;
}

bool PluginViewHost::setViewSource( const QString& pluginId, const QString& viewId,
                                    const QString& source, QString* error )
{
    const auto fail = [ error, &pluginId, &viewId ]( const QString& message ) {
        LOG_DEBUG << "Plugin view source refused plugin=" << pluginId.toStdString()
                  << " view=" << viewId.toStdString() << " reason=" << message.toStdString();
        if ( error ) {
            *error = message;
        }
        return false;
    };
    const auto it = open_.find( keyOf( pluginId, viewId ) );
    if ( it == open_.end() || !manager_ ) {
        return fail( tr( "The view is not open." ) );
    }
    const auto text = source.isEmpty() ? QStringLiteral( "focused" ) : source;
    QString message;
    const auto parsed = AnalysisSource::parse( text, &message );
    if ( !parsed ) {
        return fail( message );
    }
    if ( parsed->kind == AnalysisSource::Kind::AllOpen ) {
        return fail( tr( "A view follows one source: the focused tab, a tab or a file." ) );
    }
    const auto context = contextFor( text, &message );
    if ( context.isEmpty() ) {
        return fail( message.isEmpty() ? tr( "The source is not open." ) : message );
    }
    // view/opened again, now with the chosen tab's context.
    const auto result = manager_->openView( pluginId, viewId, context );
    if ( !result.ok() ) {
        return fail( result.error );
    }
    auto& open = *it->second;
    open.source = text;
    if ( open.picker ) {
        open.picker->setSelection( text );
    }
    LOG_DEBUG << "Plugin view follows plugin=" << pluginId.toStdString()
              << " view=" << viewId.toStdString() << " source=" << text.toStdString()
              << " tab=" << context.value( "tabId" ).toString().toStdString();
    return true;
}

void PluginViewHost::place( Open& open, const QJsonObject& definition,
                            const QJsonObject& overrides )
{
    const auto state = manager_ ? manager_->viewState( open.pluginId, open.viewId ) : QVariantMap{};
    const auto width = state.value( "width", definition.value( "width" ).toInt() ).toInt();
    const auto height = state.value( "height", definition.value( "height" ).toInt() ).toInt();
    const auto geometry = QByteArray::fromBase64( state.value( "geometry" ).toByteArray() );
    if ( auto* dock = qobject_cast<QDockWidget*>( open.container.data() ) ) {
        const bool overridden = overrides.contains( "area" ) || overrides.contains( "floating" );
        bool restored = false;
        if ( !overridden && state.isEmpty() ) {
            // A placement saved with the main window's dock state (stable object name).
            restored = window_->restoreDockWidget( dock );
        }
        if ( restored ) {
            return;
        }
        const auto area = overrides.value( "area" ).toString(
            state.value( "area", definition.value( "area" ).toString( "right" ) ).toString() );
        const auto floating
            = overrides.value( "floating" )
                  .toBool( state.value( "floating", definition.value( "floating" ).toBool( false ) )
                               .toBool() );
        window_->addDockWidget( dockArea( area ), dock );
        dock->setFloating( floating );
        if ( floating ) {
            if ( geometry.isEmpty() || !dock->restoreGeometry( geometry ) ) {
                dock->resize( width > 0 ? width : 360, height > 0 ? height : 480 );
            }
        }
        else if ( area == "left" || area == "right" ) {
            if ( width > 0 ) {
                window_->resizeDocks( { dock }, { width }, Qt::Horizontal );
            }
        }
        else if ( height > 0 ) {
            window_->resizeDocks( { dock }, { height }, Qt::Vertical );
        }
        return;
    }
    if ( geometry.isEmpty() || !open.container->restoreGeometry( geometry ) ) {
        const auto hint = open.container->sizeHint();
        open.container->resize( width > 0 ? width : qMax( 360, hint.width() ),
                                height > 0 ? height : qMax( 240, hint.height() ) );
    }
}

void PluginViewHost::saveState( const Open& open ) const
{
    if ( !manager_ || !open.container ) {
        return;
    }
    QVariantMap state;
    if ( auto* dock = qobject_cast<QDockWidget*>( open.container.data() ) ) {
        if ( window_ && !dock->isFloating() ) {
            state.insert( "area", areaName( window_->dockWidgetArea( dock ) ) );
        }
        else {
            state.insert(
                "area",
                manager_->viewState( open.pluginId, open.viewId ).value( "area", "right" ) );
        }
        state.insert( "floating", dock->isFloating() );
    }
    state.insert( "width", open.container->width() );
    state.insert( "height", open.container->height() );
    state.insert( "geometry", QString::fromLatin1( open.container->saveGeometry().toBase64() ) );
    manager_->setViewState( open.pluginId, open.viewId, state );
}

void PluginViewHost::containerClosed( const QString& key )
{
    const auto it = open_.find( key );
    if ( it == open_.end() || it->second->closing ) {
        return;
    }
    auto open = std::move( it->second );
    open_.erase( it );
    open->closing = true;
    if ( open->view ) {
        open->view->cancelDialogs();
    }
    dismissDiscardQuestion( window_, open->pluginId, open->viewId );
    saveState( *open );
    if ( manager_ ) {
        manager_->closeView( open->pluginId, open->viewId, windowId_, closedExtra( *open ) );
    }
    if ( open->container ) {
        open->container->hide();
        open->container->deleteLater();
    }
}

QJsonObject PluginViewHost::closedExtra( const Open& open ) const
{
    // E17f: only a dialog with `buttons` adds its result and inputs (principle 11).
    if ( open.dialogButtons.isEmpty() ) {
        return {};
    }
    // Inputs over their budget are shortened: say so, as view events do.
    bool truncated = false;
    QJsonObject extra{ { "result", open.result },
                       { "inputs", open.view ? open.view->inputs( &truncated ) : QJsonObject{} } };
    if ( truncated ) {
        extra.insert( "truncated", true );
    }
    return extra;
}

void PluginViewHost::updateTitle( Open& open )
{
    if ( open.container ) {
        const bool modified = open.view && open.view->modified();
        open.container->setWindowTitle( modified ? open.title + QStringLiteral( " *" ) : open.title );
    }
}

void PluginViewHost::syncButtons( Open& open )
{
    if ( open.buttonBox ) {
        open.buttonBox->setEnabled( open.view && open.view->isEnabled() );
    }
}

bool PluginViewHost::confirmDiscard( const QString& key )
{
    const auto it = open_.find( key );
    if ( it == open_.end() || !it->second->view || it->second->confirming ) {
        return false;
    }
    if ( !it->second->view->modified() ) {
        return true;
    }
    auto& open = *it->second;
    auto text = open.view->closeConfirmText();
    if ( text.isEmpty() ) {
        text = tr( "This view has unsaved changes. Discard them?" );
    }
    const auto dialogName = PluginView::objectNameFor( open.pluginId, open.viewId ) + "_discard";
    const auto serial = open.serial;
    open.confirming = true;
    const QPointer<PluginViewHost> guard( this );
    const bool discard = dialogs_.confirm
                         && dialogs_.confirm( window_ ? window_.data() : nullptr, dialogName,
                                              tr( "Discard changes?" ), text, tr( "Discard" ) );
    if ( !guard ) {
        return false;
    }
    // The question's event loop may have closed the view, or closed it and opened it again: the
    // answer is only for the view it was asked about.
    const auto after = open_.find( key );
    if ( after == open_.end() || after->second->serial != serial ) {
        return false;
    }
    after->second->confirming = false;
    return discard;
}

void PluginViewHost::dialogButton( const QString& key, const QString& name )
{
    auto it = open_.find( key );
    if ( it == open_.end() || !it->second->view || !it->second->view->isEnabled() ) {
        return;
    }
    static const QStringList Closing{ "ok", "yes", "cancel", "no", "close" };
    const bool accepts = name == QLatin1String( "ok" ) || name == QLatin1String( "yes" );
    const bool closes = Closing.contains( name ) && !( accepts && it->second->confirmByPlugin );
    // Leaving without saving asks first; `ok` and `yes` never ask.
    if ( closes && !accepts && !confirmDiscard( key ) ) {
        return;
    }
    it = open_.find( key );
    if ( it == open_.end() || !it->second->view || !it->second->view->isEnabled() ) {
        return;
    }
    LOG_DEBUG << "Plugin dialog button plugin=" << it->second->pluginId.toStdString()
              << " view=" << it->second->viewId.toStdString() << " button=" << name.toStdString();
    it->second->view->emitDialogButton( name );
    it = open_.find( key );
    if ( closes && it != open_.end() ) {
        it->second->result = name;
        containerClosed( key );
    }
}

void PluginViewHost::dialogRejected( const QString& key )
{
    // Esc and the title bar's close button press `cancel`, else `close`, else close the dialog.
    const auto it = open_.find( key );
    if ( it == open_.end() ) {
        return;
    }
    const auto& buttons = it->second->dialogButtons;
    const auto name = buttons.contains( "cancel" )  ? QStringLiteral( "cancel" )
                      : buttons.contains( "close" ) ? QStringLiteral( "close" )
                                                    : QString{};
    if ( !name.isEmpty() && it->second->view && it->second->view->isEnabled() ) {
        dialogButton( key, name );
        return;
    }
    if ( confirmDiscard( key ) ) {
        containerClosed( key );
    }
}

void PluginViewHost::closeView( const QString& pluginId, const QString& viewId )
{
    containerClosed( keyOf( pluginId, viewId ) );
}

void PluginViewHost::handleRequest( const QString& pluginId, const QString& method,
                                    const QJsonObject& params )
{
    if ( closing_ ) {
        return;
    }
    const auto windowId = params.value( "window_id" ).toString();
    const bool forThisWindow = windowId.isEmpty() || windowId == windowId_;
    if ( method == "view/update" && forThisWindow ) {
        queueUpdate( pluginId, params );
    }
    else if ( method == "view/show" && windowId == windowId_ ) {
        QString error;
        if ( !openView( pluginId, params.value( "view_id" ).toString(), params, &error ) ) {
            LOG_WARNING << "Plugin view/show failed plugin=" << pluginId.toStdString()
                        << " reason=" << error.toStdString();
        }
    }
    else if ( method == "view/hide" && forThisWindow ) {
        closeView( pluginId, params.value( "view_id" ).toString() );
    }
    else if ( method == "ui/message" && windowId == windowId_ ) {
        showMessage( pluginId, params );
    }
    else if ( method == "ui/close_message" && windowId == windowId_ ) {
        const auto key = keyOf( pluginId, params.value( "message_id" ).toString() );
        if ( messages_.count( key ) ) {
            finishMessage( key, "closed", false );
        }
    }
    else if ( method == "tooltip/invalidate" && forThisWindow ) {
        const auto it = open_.find( keyOf( pluginId, params.value( "view_id" ).toString() ) );
        if ( it != open_.end() && it->second->view ) {
            // Bounded: a longer list drops every tooltip of the element, which is always safe
            // and never scans the cache once per id.
            constexpr int MaxInvalidatedIds = 1000;
            const auto rowList = params.value( "row_ids" ).toArray();
            const auto nodeList = params.value( "node_ids" ).toArray();
            const auto elementId = params.value( "element_id" ).toString();
            if ( rowList.size() > MaxInvalidatedIds || nodeList.size() > MaxInvalidatedIds ) {
                it->second->view->invalidateTooltips( elementId );
                return;
            }
            QStringList rows;
            for ( const auto& row : rowList ) {
                rows.append( row.toString() );
            }
            QStringList nodes;
            for ( const auto& node : nodeList ) {
                nodes.append( node.toString() );
            }
            it->second->view->invalidateTooltips( elementId, rows, nodes );
        }
    }
}

void PluginViewHost::queueUpdate( const QString& pluginId, const QJsonObject& params )
{
    const auto it = open_.find( keyOf( pluginId, params.value( "view_id" ).toString() ) );
    if ( it == open_.end() ) {
        return;
    }
    auto& open = *it->second;
    if ( open.stopped ) {
        return;
    }
    if ( params.value( "root" ).isObject() ) {
        // E17f: appended text and points, clears and focus are steps a root cannot restate;
        // queued ones go to the tree they were sent for before the new root replaces it.
        if ( hasSteps( open.pendingPatch ) ) {
            render( open );
        }
        open.pendingRoot = params.value( "root" ).toObject();
        open.pendingPatch = {};
    }
    else if ( open.awaitingRoot && !open.pendingRoot ) {
        return; // patches for a stale tree; the plugin answers view/opened with a new root
    }
    else {
        for ( const auto& entry : params.value( "patch" ).toArray() ) {
            open.pendingPatch.append( entry );
        }
    }
    const auto interval = renderTimer_.interval();
    if ( open.pendingPatch.size() > MaxPendingPatchEntries
         && ( !open.lastRender.isValid() || open.lastRender.elapsed() >= interval
              || open.pendingPatch.size() > HardPendingPatchEntries ) ) {
        // Bounded memory: render this view early, at most once per interval unless the queue
        // reaches its hard limit. Other views keep their coalesced schedule.
        render( open );
    }
    if ( !renderTimer_.isActive() ) {
        renderTimer_.start();
    }
}

void PluginViewHost::render( Open& open )
{
    if ( !open.view ) {
        return;
    }
    if ( open.pendingRoot ) {
        open.view->setRoot( *open.pendingRoot );
        open.pendingRoot.reset();
        if ( open.awaitingRoot && !open.stopped ) {
            open.awaitingRoot = false;
            open.view->setEnabled( true );
            syncButtons( open );
            open.note->clear();
            open.note->hide();
        }
    }
    if ( !open.pendingPatch.isEmpty() ) {
        open.view->applyPatch( open.pendingPatch );
        open.pendingPatch = {};
    }
    open.lastRender.restart();
}

void PluginViewHost::flush()
{
    renderTimer_.stop();
    for ( auto& [ key, open ] : open_ ) {
        Q_UNUSED( key );
        if ( open->pendingRoot || !open->pendingPatch.isEmpty() ) {
            render( *open );
        }
    }
}

void PluginViewHost::setStopped( const QString& pluginId, bool stopped )
{
    for ( auto& [ key, open ] : open_ ) {
        Q_UNUSED( key );
        if ( open->pluginId != pluginId || !open->view ) {
            continue;
        }
        open->stopped = stopped;
        // Disabled while stopped, and after a restart until the plugin sends a new root, so a
        // stale view never looks live.
        open->awaitingRoot = true;
        // Pending tooltip queries end; cached answers go with the stop and the restart.
        open->view->resetTooltips();
        if ( stopped ) {
            // An edit the plugin never confirmed must not look saved, and a stopped plugin
            // never answers tree/children (its queries end as cancelled).
            open->view->revertPendingEdits();
            open->view->failTreeLoads();
        }
        open->view->setEnabled( false );
        syncButtons( *open );
        open->pendingRoot.reset();
        open->pendingPatch = {};
        if ( stopped ) {
            open->view->cancelDialogs();
        }
        open->note->setText( stopped ? tr( "The plugin stopped. Restart it in Plugins → Manage "
                                           "Plugins… or reopen this view." )
                                     : tr( "Waiting for the plugin…" ) );
        open->note->show();
    }
}

bool PluginViewHost::live( const QString& pluginId, const QString& viewId ) const
{
    // Checked at call time: the plugin process and the view that asked must both be live.
    const auto state = manager_ ? manager_->pluginState( pluginId ) : QString{};
    if ( state == "failed" || state == "stopping" || state.isEmpty() ) {
        return false;
    }
    const auto it = open_.find( keyOf( pluginId, viewId ) );
    return it != open_.end() && !it->second->stopped && !it->second->awaitingRoot
           && !it->second->closing && it->second->view && it->second->view->isEnabled();
}

void PluginViewHost::invokeAction( const QString& pluginId, const QString& viewId,
                                   const QString& action, const QVariantMap& arguments )
{
    if ( !live( pluginId, viewId ) ) {
        LOG_WARNING << "Plugin view action ignored: plugin not running plugin="
                    << pluginId.toStdString() << " action=" << action.toStdString();
        return;
    }
    CommanderRequest request;
    request.action = CommanderAction::InvokePlugin;
    request.pluginId = pluginId;
    request.pluginAction = action;
    request.pluginArguments = arguments;
    // The view's source: its tab when one was chosen, else the window's current tab.
    const auto source = viewSource( pluginId, viewId );
    QString sourceError;
    const auto context = contextFor( source, &sourceError );
    if ( context.isEmpty() && !source.isEmpty() && source != QLatin1String( "focused" ) ) {
        const auto it = open_.find( keyOf( pluginId, viewId ) );
        if ( it != open_.end() && it->second->note ) {
            it->second->note->setText( sourceError );
            it->second->note->show();
        }
        LOG_WARNING << "Plugin view action ignored: its source is gone plugin="
                    << pluginId.toStdString() << " source=" << source.toStdString();
        return;
    }
    const bool focused = source.isEmpty() || source == QLatin1String( "focused" );
    // A chosen tab may belong to another window: its id alone finds it.
    if ( focused && context.contains( "windowIndex" ) ) {
        request.windowIndex = context.value( "windowIndex" ).toInt();
    }
    request.tabId = context.value( "tabId" ).toString();
    const auto result = executor_ ? executor_( request ) : CommanderResult{};
    if ( !result.ok() ) {
        for ( auto& [ key, open ] : open_ ) {
            Q_UNUSED( key );
            if ( open->pluginId == pluginId && open->note ) {
                open->note->setText( result.message );
                open->note->show();
            }
        }
    }
}

void PluginViewHost::showMessage( const QString& pluginId, const QJsonObject& params )
{
    const auto messageId = params.value( "message_id" ).toString();
    const auto key = keyOf( pluginId, messageId );
    if ( !window_ || messages_.count( key ) ) {
        return;
    }
    auto message = std::make_unique<Message>();
    message->pluginId = pluginId;
    message->messageId = messageId;
    auto* box = new QMessageBox( window_ );
    box->setAttribute( Qt::WA_DeleteOnClose );
    const auto name
        = QStringLiteral( "pluginMessage_" ) + safeName( pluginId ) + "_" + safeName( messageId );
    box->setObjectName( name );
    box->setWindowTitle( params.value( "title" ).toString() );
    box->setTextFormat( Qt::PlainText );
    box->setText( params.value( "text" ).toString() );
    const auto icon = params.value( "icon" ).toString( "info" );
    box->setIcon( icon == "warning"    ? QMessageBox::Warning
                  : icon == "error"    ? QMessageBox::Critical
                  : icon == "question" ? QMessageBox::Question
                                       : QMessageBox::Information );
    const auto defaultName = params.value( "default" ).toString();
    for ( const auto& value : params.value( "buttons" ).toArray() ) {
        const auto buttonName = value.toString();
        auto* button = box->addButton( standardButton( buttonName ) );
        if ( !button ) {
            LOG_WARNING << "Plugin message button could not be created plugin="
                        << pluginId.toStdString()
                        << " button=" << buttonName.left( 16 ).toStdString();
            continue;
        }
        button->setObjectName( name + "_" + buttonName );
        button->setAccessibleName( button->text() );
        button->installEventFilter( this );
        message->names.insert( button, buttonName );
        if ( buttonName == defaultName ) {
            box->setDefaultButton( button );
            message->defaultButton = button;
            message->defaultName = buttonName;
            message->defaultText = button->text();
        }
    }
    box->setWindowModality( params.value( "modal" ).toBool( true ) ? Qt::WindowModal
                                                                   : Qt::NonModal );
    box->installEventFilter( this );
    message->box = box;
    connect( box, &QMessageBox::buttonClicked, this, [ this, key ]( QAbstractButton* button ) {
        const auto it = messages_.find( key );
        if ( it != messages_.end() ) {
            finishMessage( key, it->second->names.value( button, "closed" ), false );
        }
    } );
    connect( box, &QDialog::finished, this, [ this, key ] {
        if ( messages_.count( key ) ) {
            finishMessage( key, "closed", false );
        }
    } );
    message->remainingMs = params.value( "timeout_ms" ).toVariant().toLongLong();
    message->timer.setSingleShot( true );
    connect( &message->timer, &QTimer::timeout, this, [ this, key ] { tickMessage( key ); } );
    auto* raw = message.get();
    messages_.emplace( key, std::move( message ) );
    if ( raw->remainingMs > 0 && raw->defaultButton ) {
        const auto seconds = ( raw->remainingMs + 999 ) / 1000;
        raw->defaultButton->setText(
            QStringLiteral( "%1 (%2)" ).arg( raw->defaultText ).arg( seconds ) );
        const auto first = raw->remainingMs % 1000 == 0 ? 1000 : raw->remainingMs % 1000;
        raw->timer.start( scaled( static_cast<int>( first ) ) );
        raw->remainingMs -= first;
    }
    box->show();
}

void PluginViewHost::tickMessage( const QString& key )
{
    const auto it = messages_.find( key );
    if ( it == messages_.end() || it->second->finished ) {
        return;
    }
    auto& message = *it->second;
    if ( message.remainingMs <= 0 ) {
        // Finishing destroys this timer: do it outside the timer's own timeout slot.
        message.expiring = true;
        const auto button = message.defaultName;
        QTimer::singleShot( 0, this, [ this, key, button ] {
            const auto pending = messages_.find( key );
            if ( pending != messages_.end() && pending->second->expiring ) {
                finishMessage( key, button, true );
            }
        } );
        return;
    }
    if ( message.defaultButton ) {
        message.defaultButton->setText( QStringLiteral( "%1 (%2)" )
                                            .arg( message.defaultText )
                                            .arg( ( message.remainingMs + 999 ) / 1000 ) );
    }
    message.remainingMs -= 1000;
    message.timer.start( scaled( 1000 ) );
}

void PluginViewHost::finishMessage( const QString& key, const QString& button, bool timedOut )
{
    const auto it = messages_.find( key );
    if ( it == messages_.end() ) {
        return;
    }
    auto message = std::move( it->second );
    messages_.erase( it );
    message->finished = true;
    message->timer.stop();
    if ( manager_ ) {
        manager_->finishMessage( message->pluginId, message->messageId, button, timedOut );
    }
    if ( message->box ) {
        disconnect( message->box, nullptr, this, nullptr );
        message->box->removeEventFilter( this );
        message->box->close();
    }
}

void PluginViewHost::dropMessages( const QString& pluginId )
{
    for ( auto it = messages_.begin(); it != messages_.end(); ) {
        if ( it->second->pluginId != pluginId ) {
            ++it;
            continue;
        }
        it->second->finished = true;
        it->second->timer.stop();
        if ( it->second->box ) {
            disconnect( it->second->box, nullptr, this, nullptr );
            it->second->box->close();
        }
        it = messages_.erase( it );
    }
}

bool PluginViewHost::eventFilter( QObject* watched, QEvent* event )
{
    if ( watched == window_ && event->type() == QEvent::WindowActivate && manager_ ) {
        manager_->noteActiveWindow( windowId_ );
    }
    if ( event->type() == QEvent::Close ) {
        for ( const auto& [ key, open ] : open_ ) {
            if ( open->container == watched && open->kind != "dialog" ) {
                const auto closedKey = key;
                // E17f: a modified view asks "Discard changes?" first (section 29.3), after the
                // close event, since the question runs its own event loop.
                if ( open->view && open->view->modified() && !open->closeConfirmed && !closing_ ) {
                    event->ignore();
                    const auto serial = open->serial;
                    QTimer::singleShot( 0, this, [ this, closedKey, serial ] {
                        // Asked later: only while the same view is still open.
                        const auto asked = open_.find( closedKey );
                        if ( asked == open_.end() || asked->second->serial != serial
                             || !confirmDiscard( closedKey ) ) {
                            return;
                        }
                        const auto it = open_.find( closedKey );
                        if ( it != open_.end() && it->second->container ) {
                            it->second->closeConfirmed = true;
                            it->second->container->close();
                        }
                    } );
                    return true;
                }
                QTimer::singleShot( 0, this,
                                    [ this, closedKey ] { containerClosed( closedKey ); } );
                break;
            }
        }
    }
    if ( event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress ) {
        for ( auto& [ key, message ] : messages_ ) {
            Q_UNUSED( key );
            auto* widget = qobject_cast<QWidget*>( watched );
            if ( message->box && message->timer.isActive()
                 && ( watched == message->box
                      || ( widget && message->box->isAncestorOf( widget ) ) ) ) {
                // Any user input on the box stops the countdown.
                message->timer.stop();
                if ( message->defaultButton ) {
                    message->defaultButton->setText( message->defaultText );
                }
            }
        }
    }
    return QObject::eventFilter( watched, event );
}

void PluginViewHost::windowClosing()
{
    if ( closing_ ) {
        return;
    }
    closing_ = true;
    renderTimer_.stop();
    for ( auto& [ key, open ] : open_ ) {
        Q_UNUSED( key );
        open->closing = true;
        if ( open->view ) {
            open->view->cancelDialogs();
        }
        saveState( *open );
        if ( manager_ ) {
            manager_->closeView( open->pluginId, open->viewId, windowId_, closedExtra( *open ) );
        }
        if ( open->container ) {
            open->container->hide();
            open->container->deleteLater();
        }
    }
    open_.clear();
    for ( auto& [ key, message ] : messages_ ) {
        Q_UNUSED( key );
        message->finished = true;
        message->timer.stop();
        if ( manager_ ) {
            manager_->finishMessage( message->pluginId, message->messageId, "closed", false );
        }
        if ( message->box ) {
            disconnect( message->box, nullptr, this, nullptr );
            message->box->close();
        }
    }
    messages_.clear();
    if ( manager_ ) {
        disconnect( manager_, nullptr, this, nullptr );
    }
    if ( window_ ) {
        window_->removeEventFilter( this );
    }
}
