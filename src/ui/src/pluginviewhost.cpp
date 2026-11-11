// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pluginviewhost.h"
#include "log.h"
#include "pluginmanager.h"
#include <QAbstractButton>
#include <QDialog>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <cmath>

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
} // namespace

struct PluginViewHost::Open {
    QString pluginId;
    QString viewId;
    QString kind;
    QPointer<QWidget> container;
    QPointer<PluginView> view;
    QPointer<QLabel> note;
    std::optional<QJsonObject> pendingRoot;
    QJsonArray pendingPatch;
    QElapsedTimer lastRender;
    bool closing = false;
    bool stopped = false;
    // Disabled until the plugin sends a root, after opening and after every restart.
    bool awaitingRoot = true;
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
    if ( const auto existing = open_.find( key ); existing != open_.end() ) {
        auto& open = *existing->second;
        if ( overrides.contains( "title" ) ) {
            open.container->setWindowTitle(
                PluginView::limitText( overrides.value( "title" ).toString() ) );
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
    const auto result
        = manager_->openView( pluginId, viewId, context_ ? context_() : QVariantMap{} );
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
    auto* view = new PluginView( pluginId, viewId, dialogs_, body );
    // Dialogs outlive a view that is deleted while one of them is open.
    view->setDialogParent( window_ );
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
        auto* dialog = new QDialog( window_ );
        // Modal to its CILogg window only; shown with show(), never a blocking exec().
        dialog->setWindowModality( def.value( "modal" ).toBool( false ) ? Qt::WindowModal
                                                                        : Qt::NonModal );
        auto* frame = new QVBoxLayout( dialog );
        frame->setContentsMargins( 0, 0, 0, 0 );
        frame->addWidget( body );
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

    connect( view, &PluginView::eventRaised, this,
             [ this, pluginId, viewId ]( const QJsonObject& event ) {
                 if ( manager_ ) {
                     manager_->sendViewEvent( pluginId, viewId, windowId_, event );
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
    saveState( *open );
    if ( manager_ ) {
        manager_->closeView( open->pluginId, open->viewId, windowId_ );
    }
    if ( open->container ) {
        open->container->hide();
        open->container->deleteLater();
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
        open->view->setEnabled( false );
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
    const auto context = context_ ? context_() : QVariantMap{};
    if ( context.contains( "windowIndex" ) ) {
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
            manager_->closeView( open->pluginId, open->viewId, windowId_ );
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
